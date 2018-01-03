#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h> 
#include <fcntl.h>
#include <time.h>

#include "hardware.h"
#include "osd.h"
#include "user_io.h"
#include "archie.h"
#include "debug.h"
#include "ikbd.h"
#include "spi.h"
#include "mist_cfg.h"
#include "tos.h"
#include "input.h"
#include "fpga_io.h"
#include "file_io.h"
#include "config.h"
#include "menu.h"
#include "x86.h"
#include "tzx2wav.h"
#include "DiskImage.h"
#include "boot.h"
#include "fdd.h"
#include "hdd.h"

#define BREAK  0x8000

fileTYPE sd_image[4] = { 0 };

// mouse and keyboard emulation state
static emu_mode_t emu_mode = EMU_NONE;

// keep state over core type and its capabilities
static unsigned char core_type = CORE_TYPE_UNKNOWN;
static char core_type_8bit_with_config_string = 0;

static int fio_size = 0;
static int io_ver = 0;

// keep state of caps lock
static char caps_lock_toggle = 0;

// mouse position storage for ps2 and minimig rate limitation
#define X 0
#define Y 1
#define MOUSE_FREQ 20   // 20 ms -> 50hz
static int16_t mouse_pos[2] = { 0, 0 };
static uint8_t mouse_flags = 0;
static unsigned long mouse_timer;

#define LED_FREQ 100   // 100 ms
static unsigned long led_timer;
char keyboard_leds = 0;
bool caps_status = 0;
bool num_status = 0;
bool scrl_status = 0;

// set by OSD code to suppress forwarding of those keys to the core which
// may be in use by an active OSD
static char osd_is_visible = false;

char user_io_osd_is_visible()
{
	return osd_is_visible;
}

unsigned char user_io_core_type()
{
	return core_type;
}

char is_minimig()
{
	return(core_type == CORE_TYPE_MINIMIG2);
}

char is_archie()
{
	return(core_type == CORE_TYPE_ARCHIE);
}

char* user_io_create_config_name()
{
	static char str[40];
	str[0] = 0;
	char *p = user_io_get_core_name();
	if (p[0])
	{
		strcpy(str, p);
		strcat(str, ".CFG");
	}
	return str;
}

char user_io_is_8bit_with_config_string()
{
	return core_type_8bit_with_config_string;
}

static char core_name[16 + 1];  // max 16 bytes for core name

char *user_io_get_core_name()
{
	return core_name;
}

char *user_io_get_core_name_ex()
{
	switch (user_io_core_type())
	{
	case CORE_TYPE_MINIMIG2:
		return "MINIMIG";

	case CORE_TYPE_MIST:
		return "ST";

	case CORE_TYPE_ARCHIE:
		return "ARCHIE";

	case CORE_TYPE_8BIT:
		return core_name;
	}

	return "";
}

static int is_menu_type = 0;
char is_menu_core()
{
	if (!is_menu_type) is_menu_type = strcasecmp(core_name, "MENU") ? 2 : 1;
	return (is_menu_type == 1);
}

static int is_x86_type = 0;
char is_x86_core()
{
	if (!is_x86_type) is_x86_type = strcasecmp(core_name, "AO486") ? 2 : 1;
	return (is_x86_type == 1);
}

static int is_no_type = 0;
static int disable_osd = 0;
char has_menu()
{
	if (disable_osd) return 0;

	if (!is_no_type) is_no_type = user_io_get_core_name_ex()[0] ? 1 : 2;
	return (is_no_type == 1);
}

static void user_io_read_core_name()
{
	is_menu_type = 0;
	is_x86_type  = 0;
	is_no_type   = 0;
	core_name[0] = 0;

	if (user_io_is_8bit_with_config_string())
	{
		char *p = user_io_8bit_get_string(0);  // get core name
		if (p && p[0]) strcpy(core_name, p);
	}

	printf("Core name is \"%s\"\n", core_name);
}

static void set_kbd_led(unsigned char led, bool on)
{
	if (led & HID_LED_CAPS_LOCK)
	{
		if (!(keyboard_leds & KBD_LED_CAPS_CONTROL)) set_kbdled(led, on);
		caps_status = on;
	}

	if (led & HID_LED_NUM_LOCK)
	{
		if (!(keyboard_leds & KBD_LED_NUM_CONTROL)) set_kbdled(led, on);
		num_status = on;
	}

	if (led & HID_LED_SCROLL_LOCK)
	{
		if (!(keyboard_leds & KBD_LED_SCRL_CONTROL)) set_kbdled(led, on);
		scrl_status = on;
	}
}

static int joy_force = 0;

static void parse_config()
{
	joy_force = 0;
	if (core_type_8bit_with_config_string)
	{
		int i = 2;
		char *p;
		do {
			p = user_io_8bit_get_string(i);
			if (i && p && p[0])
			{
				if (p[0] == 'J')
				{
					if (p[1] == '1')
					{
						joy_force = 1;
						emu_mode = EMU_JOY0;
						input_notify_mode();
						set_kbd_led(HID_LED_NUM_LOCK, true);
					}

					joy_bcount = 0;
					for (int n = 0; n < 12; n++)
					{
						substrcpy(joy_bnames[n], p, n + 1);
						if (!joy_bnames[n][0]) break;
						joy_bcount++;
					}
				}

				if (p[0] == 'O' && p[1] == 'X')
				{
					unsigned long status = user_io_8bit_set_status(0, 0);
					printf("found OX option: %s, 0x%08X\n", p, status);

					unsigned long x = getStatus(p+1, status);

					if (is_x86_core())
					{
						if (p[2] == '2') x86_set_fdd_boot(!(x&1));
					}
				}

				if (p[0] == 'X')
				{
					disable_osd = 1;
				}

				if (p[0] == 'V')
				{
					// get version string
					char s[40];
					strcpy(s, OsdCoreName());
					strcat(s, " ");
					substrcpy(s + strlen(s), p, 1);
					OsdCoreNameSet(s);
				}
			}
			i++;
		} while (p);
	}
}

//MSM6242B layout
void send_rtc(int type)
{
	printf("Update RTC\n");

	time_t t = time(NULL);

	if (type & 1)
	{
		struct tm tm = *localtime(&t);

		uint8_t rtc[8];
		rtc[0] = (tm.tm_sec % 10) | ((tm.tm_sec / 10) << 4);
		rtc[1] = (tm.tm_min % 10) | ((tm.tm_min / 10) << 4);
		rtc[2] = (tm.tm_hour % 10) | ((tm.tm_hour / 10) << 4);
		rtc[3] = (tm.tm_mday % 10) | ((tm.tm_mday / 10) << 4);

		rtc[4] = ((tm.tm_mon + 1) % 10) | (((tm.tm_mon + 1) / 10) << 4);
		rtc[5] = (tm.tm_year % 10) | (((tm.tm_year / 10) % 10) << 4);
		rtc[6] = tm.tm_wday;
		rtc[7] = 0x40;

		spi_uio_cmd_cont(UIO_RTC);
		spi_w((rtc[1] << 8) | rtc[0]);
		spi_w((rtc[3] << 8) | rtc[2]);
		spi_w((rtc[5] << 8) | rtc[4]);
		spi_w((rtc[7] << 8) | rtc[6]);
		DisableIO();
	}

	if (type & 2)
	{
		t += t - mktime(gmtime(&t));

		spi_uio_cmd_cont(UIO_TIMESTAMP);
		spi_w(t);
		spi_w(t >> 16);
		DisableIO();
	}
}

void user_io_init()
{
	char *name;
	char mainpath[32];
	core_name[0] = 0;
	disable_osd = 0;

	memset(sd_image, 0, sizeof(sd_image));
	ikbd_init();
	tos_config_init();

	core_type = (fpga_core_id() & 0xFF);
	fio_size = fpga_get_fio_size();
	io_ver = fpga_get_io_version();

	if ((core_type != CORE_TYPE_DUMB) &&
		(core_type != CORE_TYPE_MINIMIG2) &&
		(core_type != CORE_TYPE_MIST) &&
		(core_type != CORE_TYPE_ARCHIE) &&
		(core_type != CORE_TYPE_8BIT))
	{
		core_type = CORE_TYPE_UNKNOWN;
		fio_size = 0;
		io_ver = 0;
	}

	spi_init(core_type != CORE_TYPE_UNKNOWN);
	OsdSetSize(8);

	if (core_type == CORE_TYPE_8BIT)
	{
		puts("Identified 8BIT core");

		// forward SD card config to core in case it uses the local
		// SD card implementation
		user_io_sd_set_config();
		// check if core has a config string
		core_type_8bit_with_config_string = (user_io_8bit_get_string(0) != NULL);

		// set core name. This currently only sets a name for the 8 bit cores
		user_io_read_core_name();

		// send a reset
		user_io_8bit_set_status(UIO_STATUS_RESET, UIO_STATUS_RESET);
	}

	mist_ini_parse();
	parse_video_mode();
	user_io_send_buttons(1);

	switch (core_type)
	{
	case CORE_TYPE_UNKNOWN:
		printf("Unable to identify core (%x)!\n", core_type);
		break;

	case CORE_TYPE_DUMB:
		puts("Identified core without user interface");
		break;

	case CORE_TYPE_MINIMIG2:
		puts("Identified Minimig V2 core");
		BootInit();
		break;

	case CORE_TYPE_MIST:
		puts("Identified MiST core");
		tos_upload(NULL);
		break;

	case CORE_TYPE_ARCHIE:
		puts("Identified Archimedes core");
		archie_init();
		break;

	case CORE_TYPE_8BIT:
		// try to load config
		name = user_io_create_config_name();
		if(strlen(name) > 0)
		{
			OsdCoreNameSet(user_io_get_core_name());

			printf("Loading config %s\n", name);
			unsigned long status = 0;
			if (FileLoadConfig(name, &status, 4))
			{
				printf("Found config\n");
				user_io_8bit_set_status(status, 0xffffffff);
			}
			parse_config();

			if (is_x86_core())
			{
				x86_config_load();
				x86_init();
			}
			else
			{
				// check for multipart rom
				sprintf(mainpath, "%s/boot0.rom", user_io_get_core_name());
				if (user_io_file_tx(mainpath, 0))
				{
					sprintf(mainpath, "%s/boot1.rom", user_io_get_core_name());
					if (user_io_file_tx(mainpath, 0x40))
					{
						sprintf(mainpath, "%s/boot2.rom", user_io_get_core_name());
						if (user_io_file_tx(mainpath, 0x80))
						{
							sprintf(mainpath, "%s/boot3.rom", user_io_get_core_name());
							user_io_file_tx(mainpath, 0xC0);
						}
					}
				}
				else
				{
					// legacy style of rom
					sprintf(mainpath, "%s/boot.rom", user_io_get_core_name());
					if (!user_io_file_tx(mainpath, 0))
					{
						strcpy(name + strlen(name) - 3, "ROM");
						user_io_file_tx(name, 0);
					}
				}

				// check if there's a <core>.vhd present
				sprintf(mainpath, "%s/boot.vhd", user_io_get_core_name());
				user_io_set_index(0);
				if (!user_io_file_mount(0, mainpath))
				{
					strcpy(name + strlen(name) - 3, "VHD");
					user_io_file_mount(0, name);
				}
			}
		}

		send_rtc(3);

		// release reset
		user_io_8bit_set_status(0, UIO_STATUS_RESET);
		break;
	}
}

void user_io_analog_joystick(unsigned char joystick, char valueX, char valueY)
{
	if (core_type == CORE_TYPE_8BIT)
	{
		uint16_t pos = valueY;
		spi_uio_cmd8_cont(UIO_ASTICK, joystick);
		if(io_ver) spi_w((pos<<8) | (uint8_t)(valueX));
		else
		{
			spi8(valueX);
			spi8(valueY);
		}
		DisableIO();
	}
}

void user_io_digital_joystick(unsigned char joystick, uint16_t map)
{
	if (joystick >= 6) return;

	if (is_minimig())
	{
		if (joystick < 2) spi_uio_cmd16(UIO_JOYSTICK0 + joystick, map);
		return;
	}

	// atari ST handles joystick 0 and 1 through the ikbd emulated by the io controller
	// but only for joystick 1 and 2
	if ((core_type == CORE_TYPE_MIST) && (joystick < 2))
	{
		ikbd_joystick(joystick, (uint8_t)map);
		return;
	}

	spi_uio_cmd16((joystick < 2) ? (UIO_JOYSTICK0 + joystick) : (UIO_JOYSTICK2 + joystick - 2), map);
}

// transmit serial/rs232 data into core
void user_io_serial_tx(char *chr, uint16_t cnt)
{
	spi_uio_cmd_cont(UIO_SERIAL_OUT);
	while (cnt--) spi8(*chr++);
	DisableIO();
}

char user_io_serial_status(serial_status_t *status_in, uint8_t status_out)
{
	uint8_t i, *p = (uint8_t*)status_in;

	spi_uio_cmd_cont(UIO_SERIAL_STAT);

	// first byte returned by core must be "magic". otherwise the
	// core doesn't support this request
	if (spi_b(status_out) != 0xa5)
	{
		DisableIO();
		return 0;
	}

	// read the whole structure
	for (i = 0; i<sizeof(serial_status_t); i++) *p++ = spi_in();

	DisableIO();
	return 1;
}

// transmit midi data into core
void user_io_midi_tx(char chr)
{
	spi_uio_cmd8(UIO_MIDI_OUT, chr);
}

// send ethernet mac address into FPGA
void user_io_eth_send_mac(uint8_t *mac)
{
	uint8_t i;

	spi_uio_cmd_cont(UIO_ETH_MAC);
	for (i = 0; i<6; i++) spi8(*mac++);
	DisableIO();
}

static uint8_t CSD[16] = { 0xf1, 0x40, 0x40, 0x0a, 0x80, 0x7f, 0xe5, 0xe9, 0x00, 0x00, 0x59, 0x5b, 0x32, 0x00, 0x0e, 0x40 };
static uint8_t CID[16] = { 0x3e, 0x00, 0x00, 0x34, 0x38, 0x32, 0x44, 0x00, 0x00, 0x73, 0x2f, 0x6f, 0x93, 0x00, 0xc7, 0xcd };

// set SD card info in FPGA (CSD, CID)
void user_io_sd_set_config(void)
{
	CSD[6] = (uint8_t)(sd_image[0].size >> 9);
	CSD[7] = (uint8_t)(sd_image[0].size >> 17);
	CSD[8] = (uint8_t)(sd_image[0].size >> 25);

	// forward it to the FPGA
	spi_uio_cmd_cont(UIO_SET_SDCONF);
	spi_write(CID, sizeof(CID), fio_size);
	spi_write(CSD, sizeof(CSD), fio_size);
	spi8(1); //SDHC permanently

	DisableIO();

	//  hexdump(data, sizeof(data), 0);
}

// read 8+32 bit sd card status word from FPGA
uint16_t user_io_sd_get_status(uint32_t *lba)
{
	uint32_t s;
	uint16_t c;

	spi_uio_cmd_cont(UIO_GET_SDSTAT);
	if (io_ver)
	{
		c = spi_w(0);
		s = spi_w(0);
		s = (s & 0xFFFF) | (((uint32_t)spi_w(0))<<16);
	}
	else
	{
		//note: using 32bit big-endian transfer!
		c = spi_in();
		s = spi_in();
		s = (s << 8) | spi_in();
		s = (s << 8) | spi_in();
		s = (s << 8) | spi_in();
	}
	DisableIO();

	if (lba)
		*lba = s;

	return c;
}

// read 8 bit keyboard LEDs status from FPGA
uint16_t user_io_kbdled_get_status(void)
{
	uint16_t c;

	spi_uio_cmd_cont(UIO_GET_KBD_LED);
	c = spi_w(0);
	DisableIO();

	return c;
}

uint8_t user_io_ps2_ctl(uint8_t *kbd_ctl, uint8_t *mouse_ctl)
{
	uint16_t c;
	uint8_t res = 0;

	spi_uio_cmd_cont(UIO_PS2_CTL);

	c = spi_w(0);
	if (kbd_ctl) *kbd_ctl = (uint8_t)c;
	res |= ((c >> 8) & 1);

	c = spi_w(0);
	if (mouse_ctl) *mouse_ctl = (uint8_t)c;
	res |= ((c >> 7) & 2);

	DisableIO();
	return res;
}

// read 32 bit ethernet status word from FPGA
uint32_t user_io_eth_get_status(void)
{
	uint32_t s;

	spi_uio_cmd_cont(UIO_ETH_STATUS);
	s = spi_in();
	s = (s << 8) | spi_in();
	s = (s << 8) | spi_in();
	s = (s << 8) | spi_in();
	DisableIO();

	return s;
}

// read ethernet frame from FPGAs ethernet tx buffer
void user_io_eth_receive_tx_frame(uint8_t *d, uint16_t len)
{
	spi_uio_cmd_cont(UIO_ETH_FRM_IN);
	while (len--) *d++ = spi_in();
	DisableIO();
}

// write ethernet frame to FPGAs rx buffer
void user_io_eth_send_rx_frame(uint8_t *s, uint16_t len)
{
	spi_uio_cmd_cont(UIO_ETH_FRM_OUT);
	spi_write(s, len, 0);
	spi8(0);     // one additional byte to allow fpga to store the previous one
	DisableIO();
}

// 16 byte fifo for amiga key codes to limit max key rate sent into the core
#define KBD_FIFO_SIZE  16   // must be power of 2
static unsigned short kbd_fifo[KBD_FIFO_SIZE];
static unsigned char kbd_fifo_r = 0, kbd_fifo_w = 0;
static long kbd_timer = 0;

static void kbd_fifo_minimig_send(unsigned short code)
{
	spi_uio_cmd8((code&OSD) ? UIO_KBD_OSD : UIO_KEYBOARD, code & 0xff);
	kbd_timer = GetTimer(10);  // next key after 10ms earliest
}

static void kbd_fifo_enqueue(unsigned short code)
{
	// if fifo full just drop the value. This should never happen
	if (((kbd_fifo_w + 1)&(KBD_FIFO_SIZE - 1)) == kbd_fifo_r)
		return;

	// store in queue
	kbd_fifo[kbd_fifo_w] = code;
	kbd_fifo_w = (kbd_fifo_w + 1)&(KBD_FIFO_SIZE - 1);
}

// send pending bytes if timer has run up
static void kbd_fifo_poll()
{
	// timer enabled and runnig?
	if (kbd_timer && !CheckTimer(kbd_timer))
		return;

	kbd_timer = 0;  // timer == 0 means timer is not running anymore

	if (kbd_fifo_w == kbd_fifo_r)
		return;

	kbd_fifo_minimig_send(kbd_fifo[kbd_fifo_r]);
	kbd_fifo_r = (kbd_fifo_r + 1)&(KBD_FIFO_SIZE - 1);
}

void user_io_set_index(unsigned char index)
{
	EnableFpga();
	spi8(UIO_FILE_INDEX);
	spi8(index);
	DisableFpga();
}

int user_io_file_mount(int num, char *name)
{
	int writable = 0;
	int ret = 0;
	if (x2trd_ext_supp(name))
	{
		ret = x2trd(name, sd_image+num);
	}
	else
	{
		writable = FileCanWrite(name);
		ret = FileOpenEx(&sd_image[num], name, writable ? (O_RDWR | O_SYNC) : O_RDONLY);
	}

	if (!ret)
	{
		writable = 0;
		sd_image[num].size = 0;
		printf("Failed to open file %s\n", name);
		printf("Eject image from %d slot\n", num);
	}
	else
	{
		printf("Mount %s as %s on %d slot\n", name, writable ? "read-write" : "read-only", num);
	}

	// send mounted image size first then notify about mounting
	EnableIO();
	spi8(UIO_SET_SDINFO);
	if (io_ver)
	{
		spi_w((uint16_t)(sd_image[num].size));
		spi_w((uint16_t)(sd_image[num].size>>16));
		spi_w((uint16_t)(sd_image[num].size>>32));
		spi_w((uint16_t)(sd_image[num].size>>48));
	}
	else
	{
		spi32le(sd_image[num].size);
		spi32le(sd_image[num].size>>32);
	}
	DisableIO();

	// notify core of possible sd image change
	spi_uio_cmd8(UIO_SET_SDSTAT, (1<<num) | (writable ? 0 : 0x80));
	return ret ? 1 : 0;
}

int user_io_file_tx(char* name, unsigned char index)
{
	fileTYPE f = { 0 };
	static uint8_t buf[512];

	if (!FileOpen(&f, name)) return 0;

	unsigned long bytes2send = f.size;

	/* transmit the entire file using one transfer */
	printf("Selected file %s with %lu bytes to send for index %d.%d\n", name, bytes2send, index&0x3F, index>>6);

	// set index byte (0=bios rom, 1-n=OSD entry index)
	user_io_set_index(index);

	// send directory entry (for alpha amstrad core)
	//EnableFpga();
	//spi8(UIO_FILE_INFO);
	//spi_write((void*)(DirEntry + sort_table[iSelectedEntry]), sizeof(DIRENTRY));
	//DisableFpga();

	//  hexdump(DirEntry+sort_table[iSelectedEntry], sizeof(DIRENTRY), 0);

	// prepare transmission of new file
	EnableFpga();
	spi8(UIO_FILE_TX);
	spi8(0xff);
	DisableFpga();

	if (strlen(f.name) > 4 && !strcasecmp(f.name + strlen(f.name) - 4, ".tzx"))
	{
		printf("Processing TZX...\n");
		
		EnableFpga();
		spi8(UIO_FILE_TX_DAT);
		tzx2csw(&f);
		DisableFpga();
	}
	else
	{
		while (bytes2send)
		{
			printf(".");

			uint16_t chunk = (bytes2send > 512) ? 512 : bytes2send;

			FileReadSec(&f, buf);

			EnableFpga();
			spi8(UIO_FILE_TX_DAT);
			spi_write(buf, chunk, fio_size);
			DisableFpga();

			bytes2send -= chunk;
		}
	}

	FileClose(&f);

	// signal end of transmission
	EnableFpga();
	spi8(UIO_FILE_TX);
	spi8(0x00);
	DisableFpga();

	printf("\n");
	return 1;
}

// 8 bit cores have a config string telling the firmware how
// to treat it
char *user_io_8bit_get_string(char index)
{
	unsigned char i, lidx = 0, j = 0;
	static char buffer[128 + 1];  // max 128 bytes per config item

								  // clear buffer
	buffer[0] = 0;

	spi_uio_cmd_cont(UIO_GET_STRING);
	i = spi_in();
	// the first char returned will be 0xff if the core doesn't support
	// config strings. atari 800 returns 0xa4 which is the status byte
	if ((i == 0xff) || (i == 0xa4))
	{
		DisableIO();
		return NULL;
	}

	//  printf("String: ");
	while ((i != 0) && (i != 0xff) && (j<sizeof(buffer)))
	{
		if (i == ';') {
			if (lidx == index) buffer[j++] = 0;
			lidx++;
		}
		else {
			if (lidx == index)
				buffer[j++] = i;
		}

		//  printf("%c", i);
		i = spi_in();
	}

	DisableIO();
	//  printf("\n");

	// if this was the last string in the config string list, then it still
	// needs to be terminated
	if (lidx == index) buffer[j] = 0;

	// also return NULL for empty strings
	if (!buffer[0]) return NULL;

	return buffer;
}

unsigned long user_io_8bit_set_status(unsigned long new_status, unsigned long mask)
{
	static unsigned long status = 0;

	// if mask is 0 just return the current status 
	if (mask) {
		// keep everything not masked
		status &= ~mask;
		// updated masked bits
		status |= new_status & mask;

		if(!io_ver)	spi_uio_cmd8(UIO_SET_STATUS, status);
		spi_uio_cmd32(UIO_SET_STATUS2, status, io_ver);
	}

	return status;
}

char kbd_reset = 0;
char old_video_mode = -1;

void user_io_send_buttons(char force)
{
	static unsigned short key_map = 0;
	unsigned short map = 0;

	map = mist_cfg.video_mode;
	map = (map << CONF_RES_SHIFT) & CONF_RES_MASK;

	int btn = fpga_get_buttons();

	if (btn & BUTTON_OSD) map |= BUTTON1;
	else if(btn & BUTTON_USR) map |= BUTTON2;
	if (kbd_reset) map |= BUTTON2;

	if (mist_cfg.vga_scaler) map |= CONF_VGA_SCALER;
	if (mist_cfg.csync) map |= CONF_CSYNC;
	if (mist_cfg.ypbpr) map |= CONF_YPBPR;
	if (mist_cfg.forced_scandoubler) map |= CONF_FORCED_SCANDOUBLER;
	if (mist_cfg.hdmi_audio_96k) map |= CONF_AUDIO_96K;
	if (mist_cfg.dvi) map |= CONF_DVI;

	if ((map != key_map) || force)
	{
		if (is_archie())
		{
			if ((key_map & BUTTON2) && !(map & BUTTON2))
			{
				fpga_load_rbf("Archie.rbf");
			}
		}
		key_map = map;
		spi_uio_cmd16(UIO_BUT_SW, map);
		printf("sending keymap: %X\n", map);
		if ((key_map & BUTTON2) && is_x86_core()) x86_init();
	}
}

uint32_t diskled_timer = 0;
uint32_t diskled_is_on = 0;
void __inline diskled_on()
{
	DISKLED_ON;
	diskled_timer = GetTimer(50);
	diskled_is_on = 1;
}

void kbd_reply(char code)
{
	printf("kbd_reply = 0x%02X\n", code);
	spi_uio_cmd16(UIO_KEYBOARD, 0xFF00 | code);
}

void mouse_reply(char code)
{
	printf("mouse_reply = 0x%02X\n", code);
	spi_uio_cmd16(UIO_MOUSE, 0xFF00 | code);
}

static uint8_t use_ps2ctl = 0;
static unsigned long rtc_timer = 0;

void user_io_rtc_reset()
{
	rtc_timer = 0;
}

void user_io_poll()
{
	if ((core_type != CORE_TYPE_MINIMIG2) &&
		(core_type != CORE_TYPE_MIST) &&
		(core_type != CORE_TYPE_ARCHIE) &&
		(core_type != CORE_TYPE_8BIT))
	{
		return;  // no user io for the installed core
	}

	if (core_type == CORE_TYPE_MIST)
	{
		ikbd_poll();

		unsigned char c = 0;

		// check for incoming serial data. this is directly forwarded to the
		// arm rs232 and mixes with debug output. Useful for debugging only of
		// e.g. the diagnostic cartridge    
		spi_uio_cmd_cont(UIO_SERIAL_IN);
		while (spi_in())
		{
			c = spi_in();
			if (c != 0xff) putchar(c);
		}
		DisableIO();
	}

	user_io_send_buttons(0);

	if (core_type == CORE_TYPE_MINIMIG2)
	{
		//HDD & FDD query
		unsigned char  c1, c2;
		EnableFpga();
		uint16_t tmp = spi_w(0);
		c1 = (uint8_t)(tmp >> 8); // cmd request and drive number
		c2 = (uint8_t)tmp;      // track number
		spi_w(0);
		spi_w(0);
		DisableFpga();
		HandleFDD(c1, c2);
		HandleHDD(c1, c2);
		UpdateDriveStatus();

		kbd_fifo_poll();

		// frequently check mouse for events
		if (CheckTimer(mouse_timer))
		{
			mouse_timer = GetTimer(MOUSE_FREQ);

			// has ps2 mouse data been updated in the meantime
			if (mouse_flags & 0x80)
			{
				if (!osd_is_visible)
				{
					spi_uio_cmd_cont(UIO_MOUSE);

					// ----- X axis -------
					if (mouse_pos[X] < -128)
					{
						spi8(-128);
						mouse_pos[X] += 128;
					}
					else if (mouse_pos[X] > 127)
					{
						spi8(127);
						mouse_pos[X] -= 127;
					}
					else
					{
						spi8(mouse_pos[X]);
						mouse_pos[X] = 0;
					}

					// ----- Y axis -------
					if (mouse_pos[Y] < -128)
					{
						spi8(-128);
						mouse_pos[Y] += 128;
					}
					else if (mouse_pos[Y] > 127)
					{
						spi8(127);
						mouse_pos[Y] -= 127;
					}
					else
					{
						spi8(mouse_pos[Y]);
						mouse_pos[Y] = 0;
					}

					spi8(mouse_flags & 0x07);
					DisableIO();
				}
				else
				{
					mouse_pos[X] = 0;
					mouse_pos[Y] = 0;
				}

				// reset flags
				mouse_flags = 0;
			}
		}


		if (!rtc_timer || CheckTimer(rtc_timer))
		{
			// Update once per minute should be enough
			rtc_timer = GetTimer(60000);
			send_rtc(1);
		}
	}

	if (core_type == CORE_TYPE_MIST)
	{
		// do some tos specific monitoring here
		tos_poll();
	}

	if (core_type == CORE_TYPE_8BIT)
	{
		/*
		unsigned char c = 1, f, p = 0;

		// check for serial data to be sent
		// check for incoming serial data. this is directly forwarded to the
		// arm rs232 and mixes with debug output.
		spi_uio_cmd_cont(UIO_SIO_IN);
		// status byte is 1000000A with A=1 if data is available
		if ((f = spi_in(0)) == 0x81)
		{
			printf("\033[1;36m");

			// character 0xff is returned if FPGA isn't configured
			while ((f == 0x81) && (c != 0xff) && (c != 0x00) && (p < 8))
			{
				c = spi_in();
				if (c != 0xff && c != 0x00) printf("%c", c);

				f = spi_in();
				p++;
			}
			printf("\033[0m");
		}
		DisableIO();
		*/

		// sd card emulation
		if (is_x86_core())
		{
			x86_poll();
		}
		else
		{
			static char buffer[4][512];
			static uint64_t buffer_lba[4] = { -1,-1,-1,-1 };
			uint32_t lba;
			uint16_t c = user_io_sd_get_status(&lba);
			//if(c&3) printf("user_io_sd_get_status: cmd=%02x, lba=%08x\n", c, lba);

			// valid sd commands start with "5x" to avoid problems with
			// cores that don't implement this command
			if ((c & 0xf0) == 0x50)
			{
				// check if core requests configuration
				if (c & 0x08)
				{
					printf("core requests SD config\n");
					user_io_sd_set_config();
				}

				if(c & 0x3802)
				{
					int disk = 3;
					if (c & 0x0002) disk = 0;
					else if (c & 0x0800) disk = 1;
					else if (c & 0x1000) disk = 2;

					// only write if the inserted card is not sdhc or
					// if the core uses sdhc
					if(c & 0x04)
					{
						//printf("SD WR %d on %d\n", lba, disk);

						buffer_lba[disk] = lba;

						// Fetch sector data from FPGA ...
						spi_uio_cmd_cont(UIO_SECTOR_WR);
						spi_block_read(buffer[disk], fio_size);
						DisableIO();

						// ... and write it to disk
						int done = 0;

						if (sd_image[disk].size)
						{
							diskled_on();
							if (FileSeekLBA(&sd_image[disk], lba))
							{
								if (FileWriteSec(&sd_image[disk], buffer[disk])) done = 1;
							}
						}

						if (!done) buffer_lba[disk] = -1;
					}
				}
				else
				if (c & 0x0701)
				{
					int disk = 3;
					if (c & 0x0001) disk = 0;
					else if (c & 0x0100) disk = 1;
					else if (c & 0x0200) disk = 2;

					//printf("SD RD %d on %d\n", lba, disk);

					int done = 0;

					if (buffer_lba[disk] != lba)
					{
						if (sd_image[disk].size)
						{
							diskled_on();
							if (FileSeekLBA(&sd_image[disk], lba))
							{
								if (FileReadSec(&sd_image[disk], buffer[disk]))
								{
									done = 1;
								}
							}
						}

						//Even after error we have to provide the block to the core
						//Give an empty block.
						if (!done) memset(buffer[disk], 0, sizeof(buffer[disk]));

						buffer_lba[disk] = lba;
					}

					if(buffer_lba[disk] == lba)
					{
						//hexdump(buffer, 32, 0);

						// data is now stored in buffer. send it to fpga
						spi_uio_cmd_cont(UIO_SECTOR_RD);
						spi_block_write(buffer[disk], fio_size);
						DisableIO();
					}

					// just load the next sector now, so it may be prefetched
					// for the next request already
					done = 0;
					if (sd_image[disk].size)
					{
						diskled_on();
						if (FileSeekLBA(&sd_image[disk], lba + 1))
						{
							if (FileReadSec(&sd_image[disk], buffer[disk]))
							{
								done = 1;
							}
						}
					}
					if(done) buffer_lba[disk] = lba + 1;
				}
			}

			if(diskled_is_on && CheckTimer(diskled_timer))
			{
				DISKLED_OFF;
				diskled_is_on = 0;
			}
		}

		// frequently check ps2 mouse for events
		if (CheckTimer(mouse_timer))
		{
			mouse_timer = GetTimer(MOUSE_FREQ);

			// has ps2 mouse data been updated in the meantime
			if (mouse_flags & 0x08)
			{
				unsigned char ps2_mouse[3];

				// PS2 format: 
				// YOvfl, XOvfl, dy8, dx8, 1, mbtn, rbtn, lbtn
				// dx[7:0]
				// dy[7:0]
				ps2_mouse[0] = mouse_flags;

				// ------ X axis -----------
				// store sign bit in first byte
				ps2_mouse[0] |= (mouse_pos[X] < 0) ? 0x10 : 0x00;
				if (mouse_pos[X] < -255)
				{
					// min possible value + overflow flag
					ps2_mouse[0] |= 0x40;
					ps2_mouse[1] = 1; // -255
				}
				else if (mouse_pos[X] > 255)
				{
					// max possible value + overflow flag
					ps2_mouse[0] |= 0x40;
					ps2_mouse[1] = 255;
				}
				else
				{
					ps2_mouse[1] = mouse_pos[X];
				}

				// ------ Y axis -----------
				// store sign bit in first byte
				ps2_mouse[0] |= (mouse_pos[Y] < 0) ? 0x20 : 0x00;
				if (mouse_pos[Y] < -255)
				{
					// min possible value + overflow flag
					ps2_mouse[0] |= 0x80;
					ps2_mouse[2] = 1; // -255;
				}
				else if (mouse_pos[Y] > 255)
				{
					// max possible value + overflow flag
					ps2_mouse[0] |= 0x80;
					ps2_mouse[2] = 255;
				}
				else
				{
					ps2_mouse[2] = mouse_pos[Y];
				}

				// collect movement info and send at predefined rate
				if (is_menu_core() && !(ps2_mouse[0] == 0x08 && ps2_mouse[1] == 0 && ps2_mouse[2] == 0))
					printf("PS2 MOUSE: %x %d %d\n", ps2_mouse[0], ps2_mouse[1], ps2_mouse[2]);

				if (!osd_is_visible)
				{
					spi_uio_cmd_cont(UIO_MOUSE);
					spi8(ps2_mouse[0]);
					spi8(ps2_mouse[1]);
					spi8(ps2_mouse[2]);
					DisableIO();
				}

				// reset counters
				mouse_flags = 0;
				mouse_pos[X] = mouse_pos[Y] = 0;
			}
		}
	}

	if (core_type == CORE_TYPE_ARCHIE) archie_poll();

	static uint8_t leds = 0;
	if(use_ps2ctl)
	{
		leds |= (KBD_LED_FLAG_STATUS | KBD_LED_CAPS_CONTROL);

		uint8_t kbd_ctl, mouse_ctl;
		uint8_t ps2ctl = user_io_ps2_ctl(&kbd_ctl, &mouse_ctl);

		if (ps2ctl & 1)
		{
			static uint8_t cmd = 0;
			static uint8_t byte = 0;

			printf("kbd_ctl = 0x%02X\n", kbd_ctl);
			if (!byte)
			{
				cmd = kbd_ctl;
				switch (cmd)
				{
				case 0xff:
					kbd_reply(0xFA);
					kbd_reply(0xAA);
					break;

				case 0xf2:
					kbd_reply(0xFA);
					kbd_reply(0xAB);
					kbd_reply(0x83);
					break;

				case 0xf4:
				case 0xf5:
				case 0xfa:
					kbd_reply(0xFA);
					break;

				case 0xed:
					kbd_reply(0xFA);
					byte++;
					break;

				default:
					kbd_reply(0xFE);
					break;
				}
			}
			else
			{
				switch (cmd)
				{
				case 0xed:
					kbd_reply(0xFA);
					byte = 0;

					if (kbd_ctl & 4) leds |= KBD_LED_CAPS_STATUS;
						else leds &= ~KBD_LED_CAPS_STATUS;

					break;

				default:
					byte = 0;
					break;
				}
			}
		}

		if (ps2ctl & 2)
		{
			static uint8_t cmd = 0;
			static uint8_t byte = 0;

			printf("mouse_ctl = 0x%02X\n", mouse_ctl);
			if (!byte)
			{
				cmd = mouse_ctl;
				switch (cmd)
				{
				case 0xe8:
				case 0xf3:
					mouse_reply(0xFA);
					byte++;
					break;

				case 0xf2:
					mouse_reply(0xFA);
					mouse_reply(0x00);
					break;

				case 0xe6:
				case 0xea:
				case 0xf0:
				case 0xf4:
				case 0xf5:
				case 0xf6:
					mouse_reply(0xFA);
					break;

				case 0xe9:
					mouse_reply(0xFA);
					mouse_reply(0x00);
					mouse_reply(0x00);
					mouse_reply(0x00);
					break;

				case 0xff:
					mouse_reply(0xFA);
					mouse_reply(0xAA);
					mouse_reply(0x00);
					break;

				default:
					mouse_reply(0xFE);
					break;
				}
			}
			else
			{
				switch (cmd)
				{
				case 0xf3:
				case 0xe8:
					mouse_reply(0xFA);
					byte = 0;
					break;

				default:
					byte = 0;
					break;
				}
			}
		}
	}

	if (CheckTimer(led_timer))
	{
		led_timer = GetTimer(LED_FREQ);
		if (!use_ps2ctl)
		{
			uint16_t s = user_io_kbdled_get_status();
			if(s & 0x100) use_ps2ctl = 1;
			if (!use_ps2ctl) leds = (uint8_t)s;
		}

		if ((leds & KBD_LED_FLAG_MASK) != KBD_LED_FLAG_STATUS) leds = 0;

		if ((keyboard_leds & KBD_LED_CAPS_MASK) != (leds & KBD_LED_CAPS_MASK))
			set_kbdled(HID_LED_CAPS_LOCK, (leds & KBD_LED_CAPS_CONTROL) ? leds & KBD_LED_CAPS_STATUS : caps_status);

		if ((keyboard_leds & KBD_LED_NUM_MASK) != (leds & KBD_LED_NUM_MASK))
			set_kbdled(HID_LED_NUM_LOCK, (leds & KBD_LED_NUM_CONTROL) ? leds & KBD_LED_NUM_STATUS : num_status);

		if ((keyboard_leds & KBD_LED_SCRL_MASK) != (leds & KBD_LED_SCRL_MASK))
			set_kbdled(HID_LED_SCROLL_LOCK, (leds & KBD_LED_SCRL_CONTROL) ? leds & KBD_LED_SCRL_STATUS : scrl_status);
		keyboard_leds = leds;
	}

	static uint32_t res_timer = 0;
	if(!res_timer || CheckTimer(res_timer))
	{
		res_timer = GetTimer(2000); // every 2 sec

		static uint8_t nres = 0;
		spi_uio_cmd_cont(UIO_GET_VRES);
		uint8_t res = spi_in();
		if (nres != res)
		{
			nres = res;
			uint32_t width  = spi_w(0) | (spi_w(0) << 16);
			uint32_t height = spi_w(0) | (spi_w(0) << 16);
			uint32_t htime  = spi_w(0) | (spi_w(0) << 16);
			uint32_t vtime  = spi_w(0) | (spi_w(0) << 16);
			uint32_t ptime  = spi_w(0) | (spi_w(0) << 16);

			float vrate = 100000000;
			vrate /= vtime;
			float hrate = 100000;
			hrate /= htime;

			float prate = width*100;
			prate /= ptime;

			printf("\033[1;33mINFO: Video resolution: %u x %u, fHorz = %.1fKHz, fVert = %.1fHz, fPix = %.2fMHz\033[0m\n", width, height, hrate, vrate, prate);
		}
		DisableIO();
	}
}

char user_io_dip_switch1()
{
	return 0;
}

char user_io_menu_button()
{
	return((fpga_get_buttons() & BUTTON_OSD) ? 1 : 0);
}

char user_io_user_button()
{
	return((!user_io_menu_button() && (fpga_get_buttons() & BUTTON_USR)) ? 1 : 0);
}

static void send_keycode(unsigned short key, int press)
{
	if (core_type == CORE_TYPE_MINIMIG2)
	{
		if (press > 1) return;

		uint32_t code = get_amiga_code(key);
		if (code == NONE) return;

		if (code & CAPS_TOGGLE)
		{
			if (press = 1)
			{
				// send alternating make and break codes for caps lock
				if(caps_lock_toggle) code |= 0x80;
				caps_lock_toggle = !caps_lock_toggle;
				set_kbd_led(HID_LED_CAPS_LOCK, caps_lock_toggle);
			}
			else
			{
				return;
			}
		}
		else
		{
			// amiga has "break" marker in msb
			if (!press) code |= 0x80;
		}

		code &= 0xff;

		// send immediately if possible
		if (CheckTimer(kbd_timer) && (kbd_fifo_w == kbd_fifo_r))
		{
			kbd_fifo_minimig_send(code);
		}
		else
		{
			kbd_fifo_enqueue(code);
		}
		return;
	}

	if (core_type == CORE_TYPE_MIST)
	{
		if (press > 1) return;

		uint32_t code = get_atari_code(key);
		if (code == NONE) return;

		// atari has "break" marker in msb
		if (!press) code = (code & 0xff) | 0x80;
		ikbd_keyboard(code);
		return;
	}

	if (core_type == CORE_TYPE_8BIT)
	{
		uint32_t code = get_ps2_code(key);
		if (code == NONE) return;

		//pause
		if ((code & 0xff) == 0xE1)
		{
			// pause does not have a break code
			if (press != 1)
			{
				// Pause key sends E11477E1F014E077
				static const unsigned char c[] = { 0xe1, 0x14, 0x77, 0xe1, 0xf0, 0x14, 0xf0, 0x77, 0x00 };
				const unsigned char *p = c;

				spi_uio_cmd_cont(UIO_KEYBOARD);

				printf("PS2 PAUSE CODE: ");
				while (*p)
				{
					printf("%x ", *p);
					spi8(*p++);
				}
				printf("\n");

				DisableIO();
			}
		}
		// print screen
		else if ((code & 0xff) == 0xE2)
		{
			if (press <= 1)
			{
				static const unsigned char c[2][8] = {
					{ 0xE0, 0xF0, 0x7C, 0xE0, 0xF0, 0x12, 0x00, 0x00 },
					{ 0xE0, 0x12, 0xE0, 0x7C, 0x00, 0x00, 0x00, 0x00 }
				};

				const unsigned char *p = c[press];

				spi_uio_cmd_cont(UIO_KEYBOARD);

				printf("PS2 PRINT CODE: ");
				while (*p)
				{
					printf("%x ", *p);
					spi8(*p++);
				}
				printf("\n");

				DisableIO();
			}
		}
		else
		{
			if (press > 1 && !use_ps2ctl) return;

			spi_uio_cmd_cont(UIO_KEYBOARD);

			// prepend extended code flag if required
			if (code & EXT) spi8(0xe0);

			// prepend break code if required
			if (!press) spi8(0xf0);

			// send code itself
			spi8(code & 0xff);

			DisableIO();
		}
	}

	if (core_type == CORE_TYPE_ARCHIE)
	{
		if (press > 1) return;

		uint32_t code = get_archie_code(key);
		if (code == NONE) return;

		//WIN+...
		if (get_key_mod() & (RGUI | LGUI))
		{
			switch(code)
			{
				case 0x00: code = 0xf;  //ESC = BRAKE
					break;

				case 0x11: code = 0x73; // 1 = Mouse extra 1
					break;

				case 0x12: code = 0x74; // 2 = Mouse extra 2
					break;

				case 0x13: code = 0x25; // 3 = KP#
					break;
			}
		}

		if (code == 0 && (get_key_mod() & (RGUI | LGUI)))
		{
			code = 0xF;
		}
		if (!press) code |= 0x8000;
		archie_kbd(code);
	}
}

void user_io_mouse(unsigned char b, int16_t x, int16_t y)
{
	// send mouse data as minimig expects it
	if (core_type == CORE_TYPE_MINIMIG2)
	{
		mouse_pos[X] += x;
		mouse_pos[Y] += y;
		mouse_flags |= 0x80 | (b & 7);
	}

	// 8 bit core expects ps2 like data
	if (core_type == CORE_TYPE_8BIT)
	{
		mouse_pos[X] += x;
		mouse_pos[Y] -= y;  // ps2 y axis is reversed over usb
		mouse_flags |= 0x08 | (b & 7);
	}

	// send mouse data as mist expects it
	if (core_type == CORE_TYPE_MIST) ikbd_mouse(b, x, y);
	if (core_type == CORE_TYPE_ARCHIE) archie_mouse(b, x, y);
}

/* usb modifer bits:
0     1     2    3    4     5     6    7
LCTRL LSHIFT LALT LGUI RCTRL RSHIFT RALT RGUI
*/
#define EMU_BTN1  (0+(keyrah*4))  // left control
#define EMU_BTN2  (1+(keyrah*4))  // left shift
#define EMU_BTN3  (2+(keyrah*4))  // left alt
#define EMU_BTN4  (3+(keyrah*4))  // left gui (usually windows key)

extern configTYPE config;

void user_io_check_reset(unsigned short modifiers, char useKeys)
{
	unsigned short combo[] =
	{
		0x45,  // lctrl+lalt+ralt
		0x89,  // lctrl+lgui+rgui
		0x105, // lctrl+lalt+del
	};

	if (useKeys >= (sizeof(combo) / sizeof(combo[0]))) useKeys = 0;

	if ((modifiers & ~2) == combo[useKeys])
	{
		if (modifiers & 2) // with lshift - MiST reset
		{
			reboot(1);
		}

		switch (core_type)
		{
		case CORE_TYPE_MINIMIG2:
			MinimigReset();
			break;

		case CORE_TYPE_ARCHIE:
		case CORE_TYPE_8BIT:
			kbd_reset = 1;
			break;
		}
	}
	else
	{
		kbd_reset = 0;
	}
}

void user_io_osd_key_enable(char on)
{
	printf("OSD is now %s\n", on ? "visible" : "invisible");
	osd_is_visible = on;
}

static char key_used_by_osd(uint32_t s)
{
	// this key is only used to open the OSD and has no keycode
	if (s & OSD_OPEN) return 1;

	// no keys are suppressed if the OSD is inactive
	return osd_is_visible;
}

void user_io_kbd(uint16_t key, int press)
{
	if ((core_type == CORE_TYPE_MINIMIG2) ||
		(core_type == CORE_TYPE_MIST) ||
		(core_type == CORE_TYPE_ARCHIE) ||
		(core_type == CORE_TYPE_8BIT))
	{
		if (key)
		{
			uint32_t code = get_ps2_code(key);
			if (!press)
			{
				if (is_menu_core()) printf("PS2 code(break)%s for core: %d(0x%X)\n", (code & EXT) ? "(ext)" : "", code & 255, code & 255);

				if (key == KEY_MENU) key = KEY_F12;
				if (osd_is_visible) menu_key_set(UPSTROKE | key);

				//don't block depress so keys won't stick in core if pressed before OSD.
				send_keycode(key, press);
			}
			else
			{
				if (is_menu_core()) printf("PS2 code(make)%s for core: %d(0x%X)\n", (code & EXT) ? "(ext)" : "", code & 255, code & 255);

				if ((has_menu() || osd_is_visible || (get_key_mod() & (LALT | RALT | RGUI | LGUI)))  && (((key == KEY_F12) && ((!is_x86_core() && !is_archie()) || (get_key_mod() & (RGUI | LGUI)))) || key == KEY_MENU)) menu_key_set(KEY_F12);
				else if (osd_is_visible)
				{
					if (press == 1) menu_key_set(key);
				}
				else
				{
					if ((code & EMU_SWITCH_1) || ((code & EMU_SWITCH_2) && !use_ps2ctl && !is_archie()))
					{
						if (press == 1)
						{
							// num lock has four states indicated by leds:
							// all off: normal
							// num lock on, scroll lock on: mouse emu
							// num lock on, scroll lock off: joy0 emu
							// num lock off, scroll lock on: joy1 emu

							switch (code & 0xff)
							{
							case 1:
								if (!joy_force) emu_mode = EMU_MOUSE;
								break;

							case 2:
								emu_mode = EMU_JOY0;
								break;

							case 3:
								emu_mode = EMU_JOY1;
								break;

							case 4:
								if (!joy_force) emu_mode = EMU_NONE;
								break;

							default:
								if (joy_force) emu_mode = (emu_mode == EMU_JOY0) ? EMU_JOY1 : EMU_JOY0;
								else emu_mode = (emu_mode + 1) & 3;
								break;
							}
							input_notify_mode();
							if (emu_mode == EMU_MOUSE || emu_mode == EMU_JOY0) set_kbd_led(HID_LED_NUM_LOCK, true);
							else set_kbd_led(HID_LED_NUM_LOCK, false);

							if (emu_mode == EMU_MOUSE || emu_mode == EMU_JOY1) set_kbd_led(HID_LED_SCROLL_LOCK, true);
							else set_kbd_led(HID_LED_SCROLL_LOCK, false);
						}
					}
					else
					{
						if(key == KEY_MENU) key = KEY_F12;
						send_keycode(key, press);
					}
				}
			}
		}
	}
}

unsigned char user_io_ext_idx(char *name, char* ext)
{
	unsigned char idx = 0;
	int len = strlen(ext);
	printf("Subindex of \"%s\" in \"%s\": ", name, ext);

	while ((len>3) && *ext)
	{
		if (!strncasecmp(name + strlen(name) - 3, ext, 3))
		{
			printf("%d\n", idx);
			return idx;
		}
		if (strlen(ext) <= 3) break;
		idx++;
		ext += 3;
	}

	printf("0\n", name, ext, 0);
	return 0;
}

emu_mode_t user_io_get_kbdemu()
{
	return emu_mode;
}

int getPLL(double Fout, uint32_t *M, uint32_t *K, uint32_t *C)
{
	uint32_t c = 1;
	while ((Fout*c) < 400) c++;

	printf("Calculate PLL for %.3f MHz:\n",Fout);

	while (1)
	{
		printf("C=%d, ", c);
		*C = c;

		double fvco = Fout*c;
		printf("Fvco=%f, ", fvco);

		uint32_t m = (uint32_t)(fvco / 50);
		printf("M=%d, ", m);
		*M = m;

		double ko = ((fvco / 50) - m);
		printf("K_orig=%f, ", ko);

		uint32_t k = (uint32_t)(ko * 4294967296);
		if (!k) k = 1;
		printf("K=%u. ", k);
		*K = k;

		if (ko && (ko <= 0.05f || ko >= 0.95f))
		{
			if (fvco > 1500.f)
			{
				printf("Fvco > 1500MHz. Cannot calculate PLL parameters!");
				return 0;
			}
			printf("K_orig is outside desired range try next C0\n");
			c++;
		}
		else
		{
			printf("\n");
			return 1;
		}
	}
}

uint32_t getPLLdiv(uint32_t div)
{
	if (div & 1) return 0x20000 | (((div / 2)+1) << 8) | (div / 2);
	return ((div / 2) << 8) | (div / 2);
}

struct
{
	uint32_t vpar[8];
	double Fpix;
} vmodes[8] =
{
	{ {1280, 110, 40,  220, 720,  5,  5, 20}, 74.25  },
	{ {1024, 24,  136, 160, 768,  3,  6, 29}, 65     },
	{ {720,  16,  62,  60,  480,  9,  6, 30}, 27     },
	{ {720,  12,  64,  68,  576,  5,  5, 39}, 27     },
	{ {1280, 48,  112, 248, 1024, 1,  3, 38}, 108    },
	{ {800,  40,  128, 88,  600,  1,  4, 23}, 40     },
	{ {640,  16,  96,  48,  480,  10, 2, 33}, 25.175 },
	{ {1280, 440, 40,  220, 720,  5,  5, 20}, 74.25  }
};

uint32_t vitems[32];

static int parse_custom_video_mode()
{
	char *cfg = mist_cfg.video_conf;

	int cnt = 0;
	while (*cfg)
	{
		char *next;
		if (cnt == 9 && vitems[0] == 1)
		{
			double Fout = strtod(cfg, &next);
			if (cfg == next || (Fout < 20.f || Fout > 200.f))
			{
				printf("Error parsing video_mode parameter: ""%s""\n", mist_cfg.video_conf);
				return 0;
			}

			uint32_t M, K, C;
			if (!getPLL(Fout, &M, &K, &C)) return 0;

			vitems[9] = 4;
			vitems[10] = getPLLdiv(M);
			vitems[11] = 3;
			vitems[12] = 0x10000;
			vitems[13] = 5;
			vitems[14] = getPLLdiv(C);
			vitems[15] = 9;
			vitems[16] = 2;
			vitems[17] = 8;
			vitems[18] = 7;
			vitems[19] = 7;
			vitems[20] = K;
			break;
		}

		uint32_t val = strtoul(cfg, &next, 0);
		if (cfg == next || (*next != ',' && *next))
		{
			printf("Error parsing video_mode parameter: ""%s""\n", mist_cfg.video_conf);
			return 0;
		}

		if (cnt < 32) vitems[cnt] = val;
		if (*next == ',') next++;
		cfg = next;
		cnt++;
	}

	if (cnt == 1)
	{
		printf("Set predefined video_mode to %d\n", vitems[0]);
		return vitems[0];
	}

	if ((vitems[0] == 0 && cnt < 21) || (vitems[0] == 1 && cnt < 9))
	{
		printf("Incorrect amount of items in video_mode parameter: %d\n", cnt);
		return 0;
	}

	if (vitems[0] > 1)
	{
		printf("Incorrect video_mode parameter\n");
		return 0;
	}

	return -1;
}

void parse_video_mode()
{
	// always 0. Use custom parameters.
	mist_cfg.video_mode = 0;

	int mode = parse_custom_video_mode();
	if (mode >= 0)
	{
		if (mode >= 8) mode = 0;
		for (int i = 0; i < 8; i++)
		{
			vitems[i + 1] = vmodes[mode].vpar[i];
		}

		uint32_t M, K, C;
		getPLL(vmodes[mode].Fpix, &M, &K, &C);

		vitems[9] = 4;
		vitems[10] = getPLLdiv(M);
		vitems[11] = 3;
		vitems[12] = 0x10000;
		vitems[13] = 5;
		vitems[14] = getPLLdiv(C);
		vitems[15] = 9;
		vitems[16] = 2;
		vitems[17] = 8;
		vitems[18] = 7;
		vitems[19] = 7;
		vitems[20] = K;
	}

	printf("Send HDMI parameters:\n");
	spi_uio_cmd_cont(UIO_SET_VIDEO);
	printf("video: ");
	for (int i = 1; i <= 8; i++)
	{
		spi_w(vitems[i]);
		printf("%d, ", vitems[i]);
	}
	printf("\nPLL: ");
	for (int i = 9; i < 21; i++)
	{
		printf("0x%X, ", vitems[i]);
		if (i & 1) spi_w(vitems[i]);
		else
		{
			spi_w(vitems[i]);
			spi_w(vitems[i] >> 16);
		}
	}

	printf("\n");
	DisableIO();
}
