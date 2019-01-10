#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h> 
#include <fcntl.h>
#include <time.h>
#include <limits.h>
#include <ctype.h>

#include "hardware.h"
#include "osd.h"
#include "user_io.h"
#include "debug.h"
#include "spi.h"
#include "cfg.h"
#include "input.h"
#include "fpga_io.h"
#include "file_io.h"
#include "menu.h"
#include "DiskImage.h"
#include "brightness.h"
#include "sxmlc.h"
#include "tzx2wav.h"

#include "support.h"

static char core_path[1024];

uint8_t vol_att = 0;
unsigned long vol_set_timeout = 0;

fileTYPE sd_image[4] = {};
static uint64_t buffer_lba[4] = { ULLONG_MAX,ULLONG_MAX,ULLONG_MAX,ULLONG_MAX };

// mouse and keyboard emulation state
static int emu_mode = EMU_NONE;

// keep state over core type and its capabilities
static unsigned char core_type = CORE_TYPE_UNKNOWN;

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
static char keyboard_leds = 0;
static bool caps_status = 0;
static bool num_status = 0;
static bool scrl_status = 0;

static char minimig_adjust = 0;

static uint32_t uart_mode;
uint32_t user_io_get_uart_mode()
{
	return uart_mode;
}

// set by OSD code to suppress forwarding of those keys to the core which
// may be in use by an active OSD
static char osd_is_visible = 0;

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

char is_sharpmz()
{
	return(core_type == CORE_TYPE_SHARPMZ);
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

static char core_name[16 + 1];  // max 16 bytes for core name

char *user_io_get_core_name()
{
	return core_name;
}

const char *user_io_get_core_name_ex()
{
	switch (user_io_core_type())
	{
	case CORE_TYPE_MINIMIG2:
		return "MINIMIG";

	case CORE_TYPE_MIST:
		return "ST";

	case CORE_TYPE_ARCHIE:
		return "ARCHIE";

    case CORE_TYPE_SHARPMZ:
		return "SHARPMZ";

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

static int is_snes_type = 0;
char is_snes_core()
{
	if (!is_snes_type) is_snes_type = strcasecmp(core_name, "SNES") ? 2 : 1;
	return (is_snes_type == 1);
}

char is_cpc_core()
{
	return !strcasecmp(core_name, "amstrad");
}

char is_zx81_core()
{
	return !strcasecmp(core_name, "zx81");
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

	// get core name
	char *p = user_io_8bit_get_string(0);
	if (p && p[0]) strcpy(core_name, p);

	printf("Core name is \"%s\"\n", core_name);
}

static void set_kbd_led(int led, int state)
{
	if (led & HID_LED_CAPS_LOCK)
	{
		caps_status = state&HID_LED_CAPS_LOCK;
		if (!(keyboard_leds & KBD_LED_CAPS_CONTROL)) set_kbdled(led&HID_LED_CAPS_LOCK, caps_status);
	}

	if (led & HID_LED_NUM_LOCK)
	{
		num_status = state&HID_LED_NUM_LOCK;
		if (!(keyboard_leds & KBD_LED_NUM_CONTROL)) set_kbdled(led&HID_LED_NUM_LOCK, num_status);
	}

	if (led & HID_LED_SCROLL_LOCK)
	{
		scrl_status = state&HID_LED_SCROLL_LOCK;
		if (!(keyboard_leds & KBD_LED_SCRL_CONTROL)) set_kbdled(led&HID_LED_SCROLL_LOCK, scrl_status);
	}
}

static void set_emu_mode(int mode)
{
	uint8_t emu_led;
	emu_mode = mode;

	switch (emu_mode)
	{
	case EMU_JOY0:
		emu_led = 0x20;
		set_kbd_led(HID_LED_NUM_LOCK | HID_LED_SCROLL_LOCK, HID_LED_NUM_LOCK);
		Info("Kbd mode: Joystick 1", 1000);
		break;

	case EMU_JOY1:
		emu_led = 0x40;
		set_kbd_led(HID_LED_NUM_LOCK | HID_LED_SCROLL_LOCK, HID_LED_SCROLL_LOCK);
		Info("Kbd mode: Joystick 2", 1000);
		break;

	case EMU_MOUSE:
		emu_led = 0x60;
		set_kbd_led(HID_LED_NUM_LOCK | HID_LED_SCROLL_LOCK, HID_LED_NUM_LOCK | HID_LED_SCROLL_LOCK);
		Info("Kbd mode: Mouse", 1000);
		break;

	default:
		emu_led = 0;
		set_kbd_led(HID_LED_NUM_LOCK | HID_LED_SCROLL_LOCK, 0);
		Info("Kbd mode: Normal", 1000);
	}

	spi_uio_cmd16(UIO_LEDS, 0x6000 | emu_led);
	input_notify_mode();
}

int user_io_get_kbdemu()
{
	return emu_mode;
}

static int joy_force = 0;

// Analog/Digital Joystick translation
// 0 - translate Analog to Digital (default)
// 1 - translate Digital to Analog
// 2 - do not translate
static int joy_transl = 0;

int user_io_get_joy_transl()
{
	return joy_transl;
}

static void parse_config()
{
	int i = 0;
	char *p;

	joy_force = 0;

	do {
		p = user_io_8bit_get_string(i);
		printf("get cfgstring %d = %s\n", i, p);
		if (!i && p && p[0])
		{
			OsdCoreNameSet(p);
		}
		if (i>=2 && p && p[0])
		{
			if (p[0] == 'J')
			{
				int n = 1;
				if (p[1] == 'D') { joy_transl = 0; n++;	}
				if (p[1] == 'A') { joy_transl = 1; n++; }
				if (p[1] == 'N') { joy_transl = 2; n++; }

				if (p[n] == '1')
				{
					joy_force = 1;
					set_emu_mode(EMU_JOY0);
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
				uint32_t status = user_io_8bit_set_status(0, 0);
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
	} while (p || i<3);
}

//MSM6242B layout
void send_rtc(int type)
{
	//printf("Update RTC\n");

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

const char* get_rbf_dir()
{
	static char str[1024];

	const char *root = getRootDir();
	int len = strlen(root);
	if (!strlen(core_path) || strncmp(root, core_path, len)) return "";

	strcpy(str, core_path + len + 1);
	char *p = strrchr(str, '/');
	if (!p) return "";
	*p = 0;
	return str;
}

const char* get_rbf_name()
{
	if (!strlen(core_path)) return "";
	char *p = strrchr(core_path, '/');
	if (!p) return core_path;
	return p+1;
}

void user_io_init(const char *path)
{
	char *name;
	static char mainpath[512];
	core_name[0] = 0;
	disable_osd = 0;

	memset(sd_image, 0, sizeof(sd_image));
	ikbd_init();
	tos_config_init();

	strcpy(core_path, path);
	core_type = (fpga_core_id() & 0xFF);
	fio_size = fpga_get_fio_size();
	io_ver = fpga_get_io_version();

	if ((core_type != CORE_TYPE_DUMB) &&
		(core_type != CORE_TYPE_MINIMIG2) &&
		(core_type != CORE_TYPE_MIST) &&
		(core_type != CORE_TYPE_ARCHIE) &&
		(core_type != CORE_TYPE_8BIT) &&
		(core_type != CORE_TYPE_SHARPMZ))
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

		// set core name. This currently only sets a name for the 8 bit cores
		user_io_read_core_name();

		// send a reset
		user_io_8bit_set_status(UIO_STATUS_RESET, UIO_STATUS_RESET);
	}

	MiSTer_ini_parse();
	parse_video_mode();
	FileLoadConfig("Volume.dat", &vol_att, 1);
	vol_att &= 0x1F;
	if (!cfg.volumectl) vol_att = 0;
	spi_uio_cmd8(UIO_AUDVOL, vol_att);
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
		user_io_read_core_name();
		parse_config();
		break;

    case CORE_TYPE_SHARPMZ:
		puts("Identified Sharp MZ Series core");
        sharpmz_init();
		user_io_read_core_name();
		parse_config();
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
				status &= ~UIO_STATUS_RESET;
				user_io_8bit_set_status(status, 0xffffffff & ~UIO_STATUS_RESET);
			}
			parse_config();

			if (is_x86_core())
			{
				x86_config_load();
				x86_init();
			}
			else
			{
				if (!strlen(path) || !user_io_file_tx(path, 0, 0, 0, 1))
				{
					if (!is_cpc_core())
					{
						// check for multipart rom
						for (char i = 0; i < 4; i++)
						{
							sprintf(mainpath, "%s/boot%i.rom", user_io_get_core_name(), i);
							user_io_file_tx(mainpath, i<<6);
						}
					}

					// legacy style of rom
					sprintf(mainpath, "%s/boot.rom", user_io_get_core_name());
					if (!user_io_file_tx(mainpath))
					{
						strcpy(name + strlen(name) - 3, "ROM");
						sprintf(mainpath, "%s/%s", get_rbf_dir(), name);
						if (!get_rbf_dir()[0] || !user_io_file_tx(mainpath))
						{
							if (!user_io_file_tx(name))
							{
								sprintf(mainpath, "bootrom/%s", name);
								user_io_file_tx(mainpath);
							}
						}
					}
				}

				if (is_cpc_core())
				{
					for (int m = 0; m < 3; m++)
					{
						const char *model = !m ? "" : (m == 1) ? "0" : "1";
						sprintf(mainpath, "%s/boot%s.eZZ", user_io_get_core_name(), model);
						user_io_file_tx(mainpath, 0x40 * (m + 1),0,1);
						sprintf(mainpath, "%s/boot%s.eZ0", user_io_get_core_name(), model);
						user_io_file_tx(mainpath, 0x40 * (m + 1),0,1);
						for (int i = 0; i < 256; i++)
						{
							sprintf(mainpath, "%s/boot%s.e%02X", user_io_get_core_name(), model, i);
							user_io_file_tx(mainpath, 0x40 * (m + 1),0,1);
						}
					}
				}

				// check if vhd present
				sprintf(mainpath, "%s/boot.vhd", user_io_get_core_name());
				user_io_set_index(0);
				if (!user_io_file_mount(mainpath))
				{
					strcpy(name + strlen(name) - 3, "VHD");
					sprintf(mainpath, "%s/%s", get_rbf_dir(), name);
					if (!get_rbf_dir()[0] || !user_io_file_mount(mainpath))
					{
						user_io_file_mount(name);
					}
				}
			}
		}

		send_rtc(3);

		// release reset
		user_io_8bit_set_status(0, UIO_STATUS_RESET);
		break;
	}

	spi_uio_cmd_cont(UIO_GETUARTFLG);
	uart_mode = spi_w(0);
	DisableIO();

	uint32_t mode = 0;
	if (uart_mode)
	{
		sprintf(mainpath, "uartmode.%s", user_io_get_core_name_ex());
		FileLoadConfig(mainpath, &mode, 4);
		if (mode > 5) mode = 0;
	}

	char cmd[32];
	sprintf(cmd, "uartmode %d", mode);
	system(cmd);
}

static int joyswap = 0;
void user_io_set_joyswap(int swap)
{
	joyswap = swap;
}

int user_io_get_joyswap()
{
	return joyswap;
}

void user_io_analog_joystick(unsigned char joystick, char valueX, char valueY)
{
	uint8_t joy = (joystick>1 || !joyswap) ? joystick : joystick^1;

	if (core_type == CORE_TYPE_8BIT)
	{
		spi_uio_cmd8_cont(UIO_ASTICK, joy);
		if(io_ver) spi_w((valueY<<8) | (uint8_t)(valueX));
		else
		{
			spi8(valueX);
			spi8(valueY);
		}
		DisableIO();
	}
}

void user_io_digital_joystick(unsigned char joystick, uint16_t map, int newdir)
{
	uint8_t joy = (joystick>1 || !joyswap) ? joystick : joystick ^ 1;

	// atari ST handles joystick 0 and 1 through the ikbd emulated by the io controller
	// but only for joystick 1 and 2
	if (core_type == CORE_TYPE_MIST)
	{
		ikbd_joystick(joy, (uint8_t)map);
		return;
	}

	spi_uio_cmd16((joy < 2) ? (UIO_JOYSTICK0 + joy) : (UIO_JOYSTICK2 + joy - 2), map);

	if (!is_minimig() && joy_transl == 1 && newdir)
	{
		user_io_analog_joystick(joystick, (map & 2) ? 128 : (map & 1) ? 127 : 0, (map & 8) ? 128 : (map & 4) ? 127 : 0);
	}
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
/*
	printf("SD CID\n");
	hexdump(CID, sizeof(CID), 0);
	printf("SD CSD\n");
	hexdump(CSD, sizeof(CSD), 0);
*/
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

static void kbd_fifo_minimig_send(uint32_t code)
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

int user_io_file_mount(char *name, unsigned char index, char pre)
{
	int writable = 0;
	int ret = 0;
	if (x2trd_ext_supp(name))
	{
		ret = x2trd(name, sd_image+ index);
	}
	else
	{
		writable = FileCanWrite(name);
		ret = FileOpenEx(&sd_image[index], name, writable ? (O_RDWR | O_SYNC) : O_RDONLY);
	}

	buffer_lba[index] = ULLONG_MAX;

	if (!ret)
	{
		sd_image[index].size = 0;
		printf("Failed to open file %s\n", name);
		if (pre)
		{
			writable = 1;
			printf("Will be created upon write\n");
		}
		else
		{
			writable = 0;
			printf("Eject image from %d slot\n", index);
		}
	}
	else
	{
		printf("Mount %s as %s on %d slot\n", name, writable ? "read-write" : "read-only", index);
	}

	user_io_sd_set_config();

	// send mounted image size first then notify about mounting
	EnableIO();
	spi8(UIO_SET_SDINFO);

	__off64_t size = sd_image[index].size;
	if (!ret && pre)
	{
		size = -1;
		sd_image[index].type = 2;
		strcpy(sd_image[index].path, name);
	}

	if (io_ver)
	{
		spi_w((uint16_t)(size));
		spi_w((uint16_t)(size>>16));
		spi_w((uint16_t)(size>>32));
		spi_w((uint16_t)(size>>48));
	}
	else
	{
		spi32le(size);
		spi32le(size>>32);
	}
	DisableIO();

	// notify core of possible sd image change
	spi_uio_cmd8(UIO_SET_SDSTAT, (1<< index) | (writable ? 0 : 0x80));
	return ret ? 1 : 0;
}

static unsigned char col_attr[1025];
static int col_parse(XMLEvent evt, const XMLNode* node, SXML_CHAR* text, const int n, SAX_Data* sd)
{
	(void)sd;

	static int in_border = 0;
	static int in_color = 0;
	static int in_bright = 0;
	static int in_entry = 0;
	static int in_line = 0;
	static int in_paper = 0;
	static int in_ink = 0;
	static int end = 0;
	static int start = 0;
	static int line = 0;

	static char tmp[8];

	switch (evt)
	{
	case XML_EVENT_START_NODE:
		if (!strcasecmp(node->tag, "colourisation"))
		{
			in_border = 0;
			in_color = 0;
			in_bright = 0;
			in_entry = 0;
			in_line = 0;
			in_paper = 0;
			in_ink = 0;
		}

		if (!strcasecmp(node->tag, "border")) in_border = 1;
		if (!strcasecmp(node->tag, "colour")) in_color = 1;
		if (!strcasecmp(node->tag, "bright")) in_bright = 1;

		if (!strcasecmp(node->tag, "entry"))
		{
			int ncode = -1;
			int ncnt = -1;
			for (int i = 0; i < node->n_attributes; i++)
			{
				if (!strcasecmp(node->attributes[i].name, "code")) ncode = atoi(node->attributes[i].value);
				if (!strcasecmp(node->attributes[i].name, "quantity")) ncnt = atoi(node->attributes[i].value);
			}

			in_entry = 0;
			if (ncode >= 0 && ncode <= 127)
			{
				start = ncode;
				if (ncnt < 1) ncnt = 1;
				end = start + ncnt;
				if (end > 128) end = 128;
				memset(tmp, 0, sizeof(tmp));
				in_entry = 1;
			}
		}

		if (!strcasecmp(node->tag, "line"))
		{
			int nline = -1;
			for (int i = 0; i < node->n_attributes; i++)
			{
				if (!strcasecmp(node->attributes[i].name, "index")) nline = atoi(node->attributes[i].value);
			}

			in_line = 0;
			if (nline >= 0 && nline <= 7)
			{
				line = nline;
				if (in_entry) tmp[line] = 0;
				in_line = 1;
			}
		}

		if (!strcasecmp(node->tag, "paper")) in_paper = 1;
		if (!strcasecmp(node->tag, "ink"))   in_ink = 1;
		break;

	case XML_EVENT_END_NODE:
		if (!strcasecmp(node->tag, "border")) in_border = 0;
		if (!strcasecmp(node->tag, "colour")) in_color = 0;
		if (!strcasecmp(node->tag, "bright")) in_bright = 0;
		if (!strcasecmp(node->tag, "line"))   in_line = 0;
		if (!strcasecmp(node->tag, "paper"))  in_paper = 0;
		if (!strcasecmp(node->tag, "ink"))    in_ink = 0;
		if (!strcasecmp(node->tag, "entry"))
		{
			if (in_entry)
			{
				for (int i = start; i < end; i++) memcpy(&col_attr[i * 8], tmp, 8);
			}
			in_entry = 0;
		}
		break;

	case XML_EVENT_TEXT:
		if (in_border && in_color)  col_attr[1024] = (char)((col_attr[1024] & 8) | (atoi(text) & 7));
		if (in_border && in_bright) col_attr[1024] = (char)((col_attr[1024] & 7) | ((atoi(text) & 1) << 3));

		if (in_entry && in_line && in_ink   && in_color)  tmp[line] = (char)((tmp[line] & 0xF8) | (atoi(text) & 7));
		if (in_entry && in_line && in_ink   && in_bright) tmp[line] = (char)((tmp[line] & 0xF7) | ((atoi(text) & 1) << 3));
		if (in_entry && in_line && in_paper && in_color)  tmp[line] = (char)((tmp[line] & 0x8F) | ((atoi(text) & 7) << 4));
		if (in_entry && in_line && in_paper && in_bright) tmp[line] = (char)((tmp[line] & 0x7F) | ((atoi(text) & 1) << 7));
		break;

	case XML_EVENT_ERROR:
		printf("XML parse: %s: ERROR %d\n", text, n);
		break;
	default:
		break;
	}

	return true;
}

static const unsigned char defchars[512] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0xF0, 0xF0, 0xF0, 0x00, 0x00, 0x00, 0x00,
	0x0F, 0x0F, 0x0F, 0x0F, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
	0x0F, 0x0F, 0x0F, 0x0F, 0xF0, 0xF0, 0xF0, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xF0, 0xF0, 0xF0,
	0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0x00, 0x00, 0x00, 0x00, 0xAA, 0x55, 0xAA, 0x55,
	0xAA, 0x55, 0xAA, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x1C, 0x22, 0x78, 0x20, 0x20, 0x7E, 0x00, 0x00, 0x08, 0x3E, 0x28, 0x3E, 0x0A, 0x3E, 0x08,
	0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x10, 0x00, 0x00, 0x3C, 0x42, 0x04, 0x08, 0x00, 0x08, 0x00,
	0x00, 0x04, 0x08, 0x08, 0x08, 0x08, 0x04, 0x00, 0x00, 0x20, 0x10, 0x10, 0x10, 0x10, 0x20, 0x00,
	0x00, 0x00, 0x10, 0x08, 0x04, 0x08, 0x10, 0x00, 0x00, 0x00, 0x04, 0x08, 0x10, 0x08, 0x04, 0x00,
	0x00, 0x00, 0x00, 0x3E, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x3E, 0x08, 0x08, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x08, 0x3E, 0x08, 0x14, 0x00,
	0x00, 0x00, 0x02, 0x04, 0x08, 0x10, 0x20, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x10, 0x10, 0x20,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00,
	0x00, 0x3C, 0x46, 0x4A, 0x52, 0x62, 0x3C, 0x00, 0x00, 0x18, 0x28, 0x08, 0x08, 0x08, 0x3E, 0x00,
	0x00, 0x3C, 0x42, 0x02, 0x3C, 0x40, 0x7E, 0x00, 0x00, 0x3C, 0x42, 0x0C, 0x02, 0x42, 0x3C, 0x00,
	0x00, 0x08, 0x18, 0x28, 0x48, 0x7E, 0x08, 0x00, 0x00, 0x7E, 0x40, 0x7C, 0x02, 0x42, 0x3C, 0x00,
	0x00, 0x3C, 0x40, 0x7C, 0x42, 0x42, 0x3C, 0x00, 0x00, 0x7E, 0x02, 0x04, 0x08, 0x10, 0x10, 0x00,
	0x00, 0x3C, 0x42, 0x3C, 0x42, 0x42, 0x3C, 0x00, 0x00, 0x3C, 0x42, 0x42, 0x3E, 0x02, 0x3C, 0x00,
	0x00, 0x3C, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x00, 0x00, 0x7C, 0x42, 0x7C, 0x42, 0x42, 0x7C, 0x00,
	0x00, 0x3C, 0x42, 0x40, 0x40, 0x42, 0x3C, 0x00, 0x00, 0x78, 0x44, 0x42, 0x42, 0x44, 0x78, 0x00,
	0x00, 0x7E, 0x40, 0x7C, 0x40, 0x40, 0x7E, 0x00, 0x00, 0x7E, 0x40, 0x7C, 0x40, 0x40, 0x40, 0x00,
	0x00, 0x3C, 0x42, 0x40, 0x4E, 0x42, 0x3C, 0x00, 0x00, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x42, 0x00,
	0x00, 0x3E, 0x08, 0x08, 0x08, 0x08, 0x3E, 0x00, 0x00, 0x02, 0x02, 0x02, 0x42, 0x42, 0x3C, 0x00,
	0x00, 0x44, 0x48, 0x70, 0x48, 0x44, 0x42, 0x00, 0x00, 0x40, 0x40, 0x40, 0x40, 0x40, 0x7E, 0x00,
	0x00, 0x42, 0x66, 0x5A, 0x42, 0x42, 0x42, 0x00, 0x00, 0x42, 0x62, 0x52, 0x4A, 0x46, 0x42, 0x00,
	0x00, 0x3C, 0x42, 0x42, 0x42, 0x42, 0x3C, 0x00, 0x00, 0x7C, 0x42, 0x42, 0x7C, 0x40, 0x40, 0x00,
	0x00, 0x3C, 0x42, 0x42, 0x52, 0x4A, 0x3C, 0x00, 0x00, 0x7C, 0x42, 0x42, 0x7C, 0x44, 0x42, 0x00,
	0x00, 0x3C, 0x40, 0x3C, 0x02, 0x42, 0x3C, 0x00, 0x00, 0xFE, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00,
	0x00, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3C, 0x00, 0x00, 0x42, 0x42, 0x42, 0x42, 0x24, 0x18, 0x00,
	0x00, 0x42, 0x42, 0x42, 0x42, 0x5A, 0x24, 0x00, 0x00, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x00,
	0x00, 0x82, 0x44, 0x28, 0x10, 0x10, 0x10, 0x00, 0x00, 0x7E, 0x04, 0x08, 0x10, 0x20, 0x7E, 0x00
};


static int chr_parse(XMLEvent evt, const XMLNode* node, SXML_CHAR* text, const int n, SAX_Data* sd)
{
	(void)sd;

	static int in_entry = 0;
	static int in_line = 0;
	static int code = 0;
	static int line = 0;

	switch (evt)
	{
	case XML_EVENT_START_NODE:
		if (!strcasecmp(node->tag, "definition"))
		{
			in_entry = 0;
			in_line = 0;
			code = 0;
			line = 0;
		}

		if (!strcasecmp(node->tag, "entry"))
		{
			int ncode = -1;
			for (int i = 0; i < node->n_attributes; i++)
			{
				if (!strcasecmp(node->attributes[i].name, "code")) ncode = atoi(node->attributes[i].value);
			}

			in_entry = 0;
			if (ncode >= 0 && ncode <= 63)
			{
				code = ncode;
				in_entry = 1;
			}

			if (ncode >= 128 && ncode <= 191)
			{
				code = ncode - 64;
				in_entry = 1;
			}
		}

		if (!strcasecmp(node->tag, "line"))
		{
			int nline = -1;
			for (int i = 0; i < node->n_attributes; i++)
			{
				if (!strcasecmp(node->attributes[i].name, "index")) nline = atoi(node->attributes[i].value);
			}

			in_line = 0;
			if (nline >= 0 && nline <= 7)
			{
				line = nline;
				in_line = 1;
			}
		}
		break;

	case XML_EVENT_END_NODE:
		if (!strcasecmp(node->tag, "line"))  in_line = 0;
		if (!strcasecmp(node->tag, "entry")) in_entry = 0;
		break;

	case XML_EVENT_TEXT:
		if (in_entry && in_line)
		{
			unsigned char tmp = 0;
			if (strlen(text) >= 8)
			{
				for (int i = 0; i < 8; i++) tmp = (tmp << 1) | ((text[i] == '1') ? 1 : 0);
				if (code >= 64) tmp = ~tmp;
			}
			col_attr[code * 8 + line] = tmp;
			in_line = 0;
		}
		break;

	case XML_EVENT_ERROR:
		printf("XML parse: %s: ERROR %d\n", text, n);
		break;
	default:
		break;
	}

	return true;
}

static void send_pcolchr(const char* name, unsigned char index, int type)
{
	static char full_path[1024];

	sprintf(full_path, "%s/%s", getRootDir(), name);

	char *p = strrchr(full_path, '.');
	if (!p) p = full_path + strlen(full_path);
	strcpy(p, type ? ".chr" : ".col");
	
	if (type)
	{
		memcpy(col_attr, defchars, sizeof(defchars));
		memcpy(col_attr+sizeof(defchars), defchars, sizeof(defchars));
	}
	else memset(col_attr, 0, sizeof(col_attr));

	SAX_Callbacks sax;
	SAX_Callbacks_init(&sax);
	sax.all_event = type ? chr_parse : col_parse;
	if (XMLDoc_parse_file_SAX(full_path, &sax, 0))
	{
		printf("Send additional file %s\n", full_path);

		//hexdump(col_attr, sizeof(col_attr));

		user_io_set_index(index);

		EnableFpga();
		spi8(UIO_FILE_TX);
		spi8(0xff);
		DisableFpga();

		EnableFpga();
		spi8(UIO_FILE_TX_DAT);
		spi_write(col_attr, type ? 1024 : 1025, fio_size);
		DisableFpga();

		// signal end of transmission
		EnableFpga();
		spi8(UIO_FILE_TX);
		spi8(0x00);
		DisableFpga();
	}
}

int user_io_file_tx(const char* name, unsigned char index, char opensave, char mute, char composite)
{
	fileTYPE f = {};
	static uint8_t buf[4096];

	if (!FileOpen(&f, name, mute)) return 0;

	unsigned long bytes2send = f.size;

	if (composite)
	{
		if (!FileReadSec(&f, buf)) return 0;
		if (memcmp(buf, "MiSTer", 6)) return 0;

		uint32_t off = 16 + *(uint32_t*)(((uint8_t*)buf) + 12);
		bytes2send -= off;

		FileSeek(&f, off, SEEK_SET);
	}

	/* transmit the entire file using one transfer */
	printf("Selected file %s with %lu bytes to send for index %d.%d\n", name, bytes2send, index & 0x3F, index >> 6);

	// set index byte (0=bios rom, 1-n=OSD entry index)
	user_io_set_index(index);

	int len = strlen(f.name);
	char *p = f.name + len - 4;
	EnableFpga();
	spi8(UIO_FILE_INFO);
	spi_w(toupper(p[0]) << 8 | toupper(p[1]));
	spi_w(toupper(p[2]) << 8 | toupper(p[3]));
	DisableFpga();

	// prepare transmission of new file
	EnableFpga();
	spi8(UIO_FILE_TX);
	spi8(0xff);
	DisableFpga();

	if (strlen(f.name) > 4 && (!strcasecmp(f.name + strlen(f.name) - 4, ".tzx") || !strcasecmp(f.name + strlen(f.name) - 4, ".cdt")))
	{
		printf("Processing TZX...\n");

		EnableFpga();
		spi8(UIO_FILE_TX_DAT);
		tzx2csw(&f);
		DisableFpga();
	}
	else
	{
		if (is_snes_core() && bytes2send)
		{
			printf("Load SNES ROM.\n");
			uint8_t* buf = snes_get_header(&f);
			hexdump(buf, 16, 0);
			EnableFpga();
			spi8(UIO_FILE_TX_DAT);
			spi_write(buf, 512, fio_size);
			DisableFpga();

			if (bytes2send & 512)
			{
				bytes2send -= 512;
				FileReadSec(&f, buf);
			}
		}

		while (bytes2send)
		{
			printf(".");

			uint16_t chunk = (bytes2send > sizeof(buf)) ? sizeof(buf) : bytes2send;

			FileReadAdv(&f, buf, chunk);

			EnableFpga();
			spi8(UIO_FILE_TX_DAT);
			spi_write(buf, chunk, fio_size);
			DisableFpga();

			bytes2send -= chunk;
		}
		printf("\n");
	}

	FileClose(&f);

	if (opensave)
	{
		FileGenerateSavePath(name, "sav", (char*)buf, sizeof(buf));
		user_io_file_mount((char*)buf, 0, 1);
	}

	// signal end of transmission
	EnableFpga();
	spi8(UIO_FILE_TX);
	spi8(0x00);
	DisableFpga();
	printf("\n");

	if (is_zx81_core() && index)
	{
		send_pcolchr(name, (index & 0x1F) | 0x20, 0);
		send_pcolchr(name, (index & 0x1F) | 0x60, 1);
	}
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

uint32_t user_io_8bit_set_status(uint32_t new_status, uint32_t mask)
{
	static uint32_t status = 0;

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

	map = cfg.video_mode;
	map = (map << CONF_RES_SHIFT) & CONF_RES_MASK;

	int btn = fpga_get_buttons();

	if (btn & BUTTON_OSD) map |= BUTTON1;
	else if(btn & BUTTON_USR) map |= BUTTON2;
	if (kbd_reset) map |= BUTTON2;

	if (cfg.vga_scaler) map |= CONF_VGA_SCALER;
	if (cfg.csync) map |= CONF_CSYNC;
	if (cfg.ypbpr) map |= CONF_YPBPR;
	if (cfg.forced_scandoubler) map |= CONF_FORCED_SCANDOUBLER;
	if (cfg.hdmi_audio_96k) map |= CONF_AUDIO_96K;
	if (cfg.dvi) map |= CONF_DVI;

	if ((map != key_map) || force)
	{
		if (is_archie())
		{
			if ((key_map & BUTTON2) && !(map & BUTTON2))
			{
				const char *name = get_rbf_name();
				fpga_load_rbf(name[0] ? name : "Archie.rbf");
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

static int coldreset_req = 0;

static int adjust_video_mode(uint32_t vtime);
static uint32_t show_video_info(int force);
static uint32_t res_timer = 0;

void user_io_poll()
{
	if ((core_type != CORE_TYPE_MINIMIG2) &&
		(core_type != CORE_TYPE_MIST) &&
		(core_type != CORE_TYPE_ARCHIE) &&
		(core_type != CORE_TYPE_SHARPMZ) &&
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

		static u_int8_t last_status_change = 0;
		char stchg = spi_uio_cmd_cont(UIO_GET_STATUS);
		if ((stchg & 0xF0) == 0xA0 && last_status_change != (stchg & 0xF))
		{
			last_status_change = (stchg & 0xF);
			uint32_t st = spi32w(0);
			DisableIO();
			user_io_8bit_set_status(st, ~UIO_STATUS_RESET);
			//printf("** new status from core: %08X\n", st);
		}
		else
		{
			DisableIO();
		}

		// sd card emulation
		if (is_x86_core())
		{
			x86_poll();
		}
		else
		{
			static uint8_t buffer[4][512];
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

						int done = 0;
						buffer_lba[disk] = lba;

						// Fetch sector data from FPGA ...
						spi_uio_cmd_cont(UIO_SECTOR_WR);
						spi_block_read(buffer[disk], fio_size);
						DisableIO();


						if (sd_image[disk].type == 2 && !lba)
						{
							//Create the file
							if (FileOpenEx(&sd_image[disk], sd_image[disk].path, O_CREAT | O_RDWR | O_SYNC))
							{
								diskled_on();
								if (FileWriteSec(&sd_image[disk], buffer[disk]))
								{
									sd_image[disk].size = 512;
									done = 1;
								}
							}
							else
							{
								printf("Error in creating file: %s\n", sd_image[disk].path);
							}
						}
						else
						{
							// ... and write it to disk
							__off64_t size = sd_image[disk].size>>9;
							if (size && size>=lba)
							{
								diskled_on();
								if (FileSeekLBA(&sd_image[disk], lba))
								{
									if (FileWriteSec(&sd_image[disk], buffer[disk]))
									{
										done = 1;
										if (size == lba)
										{
											size++;
											sd_image[disk].size = size << 9;
										}
									}
								}
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

					if (sd_image[disk].type == 2)
					{
						buffer_lba[disk] = -1;
					}
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
	if (core_type == CORE_TYPE_SHARPMZ) sharpmz_poll();

	static uint8_t leds = 0;
	if(use_ps2ctl && core_type != CORE_TYPE_MINIMIG2)
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

	if (!res_timer)
	{
		res_timer = GetTimer(1000);
	}
	else if(CheckTimer(res_timer))
	{
		res_timer = GetTimer(500);
		if (!minimig_adjust)
		{
			uint32_t vtime = show_video_info(0);
			if (vtime && cfg.vsync_adjust && !is_menu_core())
			{
				adjust_video_mode(vtime);
				usleep(100000);
				show_video_info(1);
			}
		}
	}

	static int prev_coldreset_req = 0;
	static uint32_t reset_timer = 0;
	if (!prev_coldreset_req && coldreset_req)
	{
		reset_timer = GetTimer(1000);
	}

	if (!coldreset_req && prev_coldreset_req)
	{
		fpga_load_rbf("menu.rbf");
	}

	prev_coldreset_req = coldreset_req;
	if (reset_timer && CheckTimer(reset_timer))
	{
		reboot(1);
	}

	if (vol_set_timeout && CheckTimer(vol_set_timeout))
	{
		vol_set_timeout = 0;
		FileSaveConfig("Volume.dat", &vol_att, 1);
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

static void adjust_vsize(char force);
static void store_vsize();
void user_io_minimig_set_adjust(char n)
{
	if (minimig_adjust & !n) store_vsize();
	minimig_adjust = n;
}

char user_io_minimig_get_adjust()
{
	return minimig_adjust;
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
			if (press)
			{
				// send alternating make and break codes for caps lock
				if(caps_lock_toggle) code |= 0x80;
				caps_lock_toggle ^= HID_LED_CAPS_LOCK;
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
		if (minimig_adjust)
		{
			if (code == 0x44)
			{
				store_vsize();
				res_timer = 0;
				return;
			}

			if (code == 0x45)
			{
				Info("Canceled");
				res_timer = 0;
				minimig_adjust = 0;
				adjust_vsize(1);
				return;
			}
			code |= OSD;
		}

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

	if (core_type == CORE_TYPE_SHARPMZ)
	{
		uint32_t code = get_ps2_code(key);
		if (code == NONE) return;

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
}

void user_io_mouse(unsigned char b, int16_t x, int16_t y)
{
	switch (core_type)
	{
	case CORE_TYPE_MINIMIG2:
		mouse_pos[X] += x;
		mouse_pos[Y] += y;
		mouse_flags |= 0x80 | (b & 7);
		return;

	case CORE_TYPE_8BIT:
		mouse_pos[X] += x;
		mouse_pos[Y] -= y;  // ps2 y axis is reversed over usb
		mouse_flags |= 0x08 | (b & 7);
		return;

	case CORE_TYPE_MIST:
		ikbd_mouse(b, x, y);
		return;

	case CORE_TYPE_ARCHIE:
		archie_mouse(b, x, y);
		return;
	}
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

	if ((modifiers & ~2) == combo[(uint)useKeys])
	{
		if (modifiers & 2) // with lshift - cold reset
		{
			coldreset_req = 1;
		}
		else
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
		coldreset_req = 0;
		kbd_reset = 0;
	}
}

void user_io_osd_key_enable(char on)
{
	printf("OSD is now %s\n", on ? "visible" : "invisible");
	osd_is_visible = on;
}

static void set_volume(int cmd)
{
	if (!cfg.volumectl) return;

	vol_set_timeout = GetTimer(1000);

	vol_att &= 0x17;
	if(!cmd) vol_att ^= 0x10;
	else if (vol_att & 0x10) vol_att &= 0xF;
	else if (cmd < 0 && vol_att < 7) vol_att += 1;
	else if (cmd > 0 && vol_att > 0) vol_att -= 1;

	spi_uio_cmd8(UIO_AUDVOL, vol_att);

	if (vol_att & 0x10)
	{
		Info("\x8d Mute", 1000);
	}
	else
	{
		char str[32];
		memset(str, 0, sizeof(str));

		sprintf(str, "\x8d ");
		char *bar = str + strlen(str);
		memset(bar, 0x8C, 8);
		memset(bar, 0x7f, 8 - vol_att);
		Info(str, 1000);
	}
}

void user_io_kbd(uint16_t key, int press)
{
	if (key == KEY_MUTE)
	{
		if (press == 1 && hasAPI1_5() && !osd_is_visible && !is_menu_core()) set_volume(0);
	}
	else
	if (key == KEY_VOLUMEDOWN)
	{
		if (press && hasAPI1_5() && !osd_is_visible && !is_menu_core()) set_volume(-1);
	}
	else
	if (key == KEY_VOLUMEUP)
	{
		if (press && hasAPI1_5() && !osd_is_visible && !is_menu_core()) set_volume(1);
	}
	else
	if (key == 0xBE)
	{
		if (press) setBrightness(BRIGHTNESS_DOWN, 0);
	}
	else
	if (key == 0xBF)
	{
		if (press) setBrightness(BRIGHTNESS_UP, 0);
	}
	else
	if (key == KEY_F2 && osd_is_visible)
	{
		if (press == 1) cfg.rbf_hide_datecode = !cfg.rbf_hide_datecode;
		PrintDirectory();
	}
	else
	if ((core_type == CORE_TYPE_MINIMIG2) ||
		(core_type == CORE_TYPE_MIST) ||
		(core_type == CORE_TYPE_ARCHIE) ||
		(core_type == CORE_TYPE_SHARPMZ) ||
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
				if (!osd_is_visible && !is_menu_core() && key == KEY_MENU && press == 3) open_joystick_setup();
				else if ((has_menu() || osd_is_visible || (get_key_mod() & (LALT | RALT | RGUI | LGUI))) && (((key == KEY_F12) && ((!is_x86_core() && !is_archie()) || (get_key_mod() & (RGUI | LGUI)))) || key == KEY_MENU)) menu_key_set(KEY_F12);
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
							int mode = emu_mode;

							// all off: normal
							// num lock on, scroll lock on: mouse emu
							// num lock on, scroll lock off: joy0 emu
							// num lock off, scroll lock on: joy1 emu

							switch (code & 0xff)
							{
							case 1:
								if (!joy_force) mode = EMU_MOUSE;
								break;

							case 2:
								mode = EMU_JOY0;
								break;

							case 3:
								mode = EMU_JOY1;
								break;

							case 4:
								if (!joy_force) mode = EMU_NONE;
								break;

							default:
								if (joy_force) mode = (mode == EMU_JOY0) ? EMU_JOY1 : EMU_JOY0;
								else
								{
									mode = (mode + 1) & 3;
									if(cfg.kbd_nomouse && mode == EMU_MOUSE) mode = (mode + 1) & 3;
								}
								break;
							}
							set_emu_mode(mode);
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
	printf("Subindex of \"%s\" in \"%s\": ", name, ext);

	char *p = strrchr(name, '.');
	if (p)
	{
		p++;
		char e[4] = "   ";
		for (int i = 0; i < 3; i++)
		{
			if (!*p) break;
			e[i] = *p++;
		}

		while (*ext)
		{
			int found = 1;
			for (int i = 0; i < 3; i++)
			{
				if (ext[i] != '?' && (toupper(ext[i]) != toupper(e[i]))) found = 0;
			}

			if (found)
			{
				printf("%d\n", idx);
				return idx;
			}

			if (strlen(ext) <= 3) break;
			idx++;
			ext += 3;
		}
	}

	printf("not found! use 0\n");
	return 0;
}

struct vmode_t
{
	uint32_t vpar[8];
	double Fpix;
};

vmode_t vmodes[] =
{
	{ { 1280, 110,  40, 220,  720,  5,  5, 20 },  74.25  }, //0
	{ { 1024,  24, 136, 160,  768,  3,  6, 29 },  65     }, //1
	{ {  720,  16,  62,  60,  480,  9,  6, 30 },  27     }, //2
	{ {  720,  12,  64,  68,  576,  5,  5, 39 },  27     }, //3
	{ { 1280,  48, 112, 248, 1024,  1,  3, 38 }, 108     }, //4
	{ {  800,  40, 128,  88,  600,  1,  4, 23 },  40     }, //5
	{ {  640,  16,  96,  48,  480, 10,  2, 33 },  25.175 }, //6
	{ { 1280, 440,  40, 220,  720,  5,  5, 20 },  74.25  }, //7
	{ { 1920,  88,  44, 148, 1080,  4,  5, 36 }, 148.5   }, //8
	{ { 1920, 528,  44, 148, 1080,  4,  5, 36 }, 148.5   }, //9
	{ { 1366,  70, 143, 213,  768,  3,  3, 24 },  85.5   }, //10
	{ { 1024,  40, 104, 144,  600,  1,  3, 18 },  48.96  }, //11
};
#define VMODES_NUM (sizeof(vmodes) / sizeof(vmodes[0]))

static uint32_t vitems[32];
double Fpix = 0;

static uint32_t getPLLdiv(uint32_t div)
{
	if (div & 1) return 0x20000 | (((div / 2) + 1) << 8) | (div / 2);
	return ((div / 2) << 8) | (div / 2);
}

static int findPLLpar(double Fout, uint32_t *pc, uint32_t *pm, double *pko)
{
	uint32_t c = 1;
	while ((Fout*c) < 400) c++;

	while (1)
	{
		double fvco = Fout*c;
		uint32_t m = (uint32_t)(fvco / 50);
		double ko = ((fvco / 50) - m);

		fvco = ko + m;
		fvco *= 50.f;

		if (ko && (ko <= 0.05f || ko >= 0.95f))
		{
			printf("Fvco=%f, C=%d, M=%d, K=%f ", fvco, c, m, ko);
			if (fvco > 1500.f)
			{
				printf("-> No exact parameters found\n");
				return 0;
			}
			printf("-> K is outside allowed range\n");
			c++;
		}
		else
		{
			*pc = c;
			*pm = m;
			*pko = ko;
			return 1;
		}
	}

	//will never reach here
	return 0;
}

static void setPLL(double Fout)
{
	double fvco, ko;
	uint32_t m, c;

	printf("Calculate PLL for %.4f MHz:\n", Fout);

	if (!findPLLpar(Fout, &c, &m, &ko))
	{
		c = 1;
		while ((Fout*c) < 400) c++;

		fvco = Fout*c;
		m = (uint32_t)(fvco / 50);
		ko = ((fvco / 50) - m);

		//Make sure K is in allowed range.
		if (ko <= 0.05f)
		{
			ko = 0;
		}
		else if (ko >= 0.95f)
		{
			m++;
			ko = 0;
		}
	}

	uint32_t k = ko ? (uint32_t)(ko * 4294967296) : 1;

	fvco = ko + m;
	fvco *= 50.f;
	Fpix = fvco / c;

	printf("Fvco=%f, C=%d, M=%d, K=%f(%u) -> Fpix=%f\n", fvco, c, m, ko, k, Fpix);

	vitems[9]  = 4;
	vitems[10] = getPLLdiv(m);
	vitems[11] = 3;
	vitems[12] = 0x10000;
	vitems[13] = 5;
	vitems[14] = getPLLdiv(c);
	vitems[15] = 9;
	vitems[16] = 2;
	vitems[17] = 8;
	vitems[18] = 7;
	vitems[19] = 7;
	vitems[20] = k;
}

static char scaler_flt_cfg[1024] = { 0 };
static char new_scaler = 0;

static void setScaler()
{
	fileTYPE f = {};
	static char filename[1024];

	if (!spi_uio_cmd_cont(UIO_SET_FLTNUM))
	{
		DisableIO();
		return;
	}

	new_scaler = 1;
	spi8(scaler_flt_cfg[0]);
	DisableIO();
	sprintf(filename, COEFF_DIR"/%s", scaler_flt_cfg + 1);

	if (FileOpen(&f, filename))
	{
		printf("Read scaler coefficients\n");
		char *buf = (char*)malloc(f.size+1);
		if (buf)
		{
			memset(buf, 0, f.size + 1);
			int size;
			if ((size = FileReadAdv(&f, buf, f.size)))
			{
				spi_uio_cmd_cont(UIO_SET_FLTCOEF);

				char *end = buf + size;
				char *pos = buf;
				int phase = 0;
				while (pos < end)
				{
					char *st = pos;
					while ((pos < end) && *pos && (*pos != 10)) pos++;
					*pos = 0;
					while (*st == ' ' || *st == '\t' || *st == 13) st++;
					if (*st == '#' || *st == ';' || !*st) pos++;
					else
					{
						int c0, c1, c2, c3;
						int n = sscanf(st, "%d,%d,%d,%d", &c0, &c1, &c2, &c3);
						if (n == 4)
						{
							printf("   phase %c-%02d: %4d,%4d,%4d,%4d\n", (phase >= 16) ? 'V' : 'H', phase % 16, c0, c1, c2, c3);
							//printf("%03X: %03X %03X %03X %03X;\n",phase*4, c0 & 0x1FF, c1 & 0x1FF, c2 & 0x1FF, c3 & 0x1FF);

							spi_w((c0 & 0x1FF) | (((phase * 4) + 0) << 9));
							spi_w((c1 & 0x1FF) | (((phase * 4) + 1) << 9));
							spi_w((c2 & 0x1FF) | (((phase * 4) + 2) << 9));
							spi_w((c3 & 0x1FF) | (((phase * 4) + 3) << 9));

							phase++;
							if (phase >= 32) break;
						}
					}
				}
				DisableIO();
			}

			free(buf);
		}
	}
}

int user_io_get_scaler_flt()
{
	return new_scaler ? scaler_flt_cfg[0] : -1;
}

char* user_io_get_scaler_coeff()
{
	return scaler_flt_cfg + 1;
}

static char scaler_cfg[128] = { 0 };

void user_io_set_scaler_flt(int n)
{
	scaler_flt_cfg[0] = (char)n;
	FileSaveConfig(scaler_cfg, &scaler_flt_cfg, sizeof(scaler_flt_cfg));
	spi_uio_cmd8(UIO_SET_FLTNUM, scaler_flt_cfg[0]);
	spi_uio_cmd(UIO_SET_FLTCOEF);
}

void user_io_set_scaler_coeff(char *name)
{
	strcpy(scaler_flt_cfg + 1, name);
	FileSaveConfig(scaler_cfg, &scaler_flt_cfg, sizeof(scaler_flt_cfg));
	setScaler();
	user_io_send_buttons(1);
}

static void loadScalerCfg()
{
	sprintf(scaler_cfg, "%s_scaler.cfg", user_io_get_core_name_ex());
	if (!FileLoadConfig(scaler_cfg, &scaler_flt_cfg, sizeof(scaler_flt_cfg) - 1) || scaler_flt_cfg[0]>4)
	{
		memset(scaler_flt_cfg, 0, sizeof(scaler_flt_cfg));
	}
}

static void setVideo()
{
	loadScalerCfg();
	setScaler();

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
		if (i & 1) spi_w(vitems[i] | ((i==9 && cfg.vsync_adjust==2) ? 0x8000 : 0));
		else
		{
			spi_w(vitems[i]);
			spi_w(vitems[i] >> 16);
		}
	}

	printf("\n");
	DisableIO();
}

static int parse_custom_video_mode()
{
	char *vcfg = cfg.video_conf;

	int khz = 0;
	int cnt = 0;
	while (*vcfg)
	{
		char *next;
		if (cnt == 9 && vitems[0] == 1)
		{
			double Fpix = khz ? strtoul(vcfg, &next, 0)/1000.f : strtod(vcfg, &next);
			if (vcfg == next || (Fpix < 20.f || Fpix > 200.f))
			{
				printf("Error parsing video_mode parameter: ""%s""\n", cfg.video_conf);
				return 0;
			}

			setPLL(Fpix);
			break;
		}

		uint32_t val = strtoul(vcfg, &next, 0);
		if (vcfg == next || (*next != ',' && *next))
		{
			printf("Error parsing video_mode parameter: ""%s""\n", cfg.video_conf);
			return 0;
		}

		if (!cnt && val >= 100)
		{
			vitems[cnt++] = 1;
			khz = 1;
		}
		if (cnt < 32) vitems[cnt] = val;
		if (*next == ',') next++;
		vcfg = next;
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
	cfg.video_mode = 0;

	int mode = parse_custom_video_mode();
	if (mode >= 0)
	{
		if ((uint)mode >= VMODES_NUM) mode = 0;
		for (int i = 0; i < 8; i++)
		{
			vitems[i + 1] = vmodes[mode].vpar[i];
		}

		setPLL(vmodes[mode].Fpix);
	}
	setVideo();
}

static int adjust_video_mode(uint32_t vtime)
{
	printf("Adjust VSync(%d).\n", cfg.vsync_adjust);

	double Fpix = 100 * (vitems[1] + vitems[2] + vitems[3] + vitems[4]) * (vitems[5] + vitems[6] + vitems[7] + vitems[8]);
	Fpix /= vtime;
	if (Fpix < 20.f || Fpix > 200.f)
	{
		printf("Estimated Fpix(%.4f MHz) is outside supported range. Canceling auto-adjust.\n", Fpix);
		return 0;
	}

	setPLL(Fpix);
	setVideo();
	user_io_send_buttons(1);
	return 1;
}

typedef struct
{
	uint32_t mode;
	uint32_t hpos;
	uint32_t vpos;
	uint32_t reserved;
} vmode_adjust_t;

vmode_adjust_t vmodes_adj[64] = {};

static void adjust_vsize(char force)
{
	static uint16_t nres = 0;
	spi_uio_cmd_cont(UIO_GET_VMODE);
	uint16_t res = spi_w(0);
	if ((res & 0x8000) && (nres != res || force))
	{
		nres = res;
		uint16_t scr_hsize = spi_w(0);
		uint16_t scr_vsize = spi_w(0);
		DisableIO();

		printf("\033[1;37mVMODE: resolution: %u x %u, mode: %u\033[0m\n", scr_hsize, scr_vsize, res & 255);

		static int loaded = 0;
		if (~loaded)
		{
			FileLoadConfig("minimig_vadjust.dat", vmodes_adj, sizeof(vmodes_adj));
			loaded = 1;
		}

		uint32_t mode = scr_hsize | (scr_vsize << 12) | ((res & 0xFF) << 24);
		if (mode)
		{
			for (uint i = 0; i < sizeof(vmodes_adj) / sizeof(vmodes_adj[0]); i++)
			{
				if (vmodes_adj[i].mode == mode)
				{
					spi_uio_cmd_cont(UIO_SET_VPOS);
					spi_w(vmodes_adj[i].hpos >> 16);
					spi_w(vmodes_adj[i].hpos);
					spi_w(vmodes_adj[i].vpos >> 16);
					spi_w(vmodes_adj[i].vpos);
					printf("\033[1;37mVMODE: set positions: [%u-%u, %u-%u]\033[0m\n", vmodes_adj[i].hpos >> 16, (uint16_t)vmodes_adj[i].hpos, vmodes_adj[i].vpos >> 16, (uint16_t)vmodes_adj[i].vpos);
					DisableIO();
					return;
				}
			}
			printf("\033[1;37mVMODE: preset not found.\033[0m\n");
			spi_uio_cmd_cont(UIO_SET_VPOS); spi_w(0); spi_w(0); spi_w(0); spi_w(0);
			DisableIO();
		}
	}
	else
	{
		DisableIO();
	}
}

static void store_vsize()
{
	Info("Stored");
	minimig_adjust = 0;

	spi_uio_cmd_cont(UIO_GET_VMODE);
	uint16_t res = spi_w(0);
	uint16_t scr_hsize = spi_w(0);
	uint16_t scr_vsize = spi_w(0);
	uint16_t scr_hbl_l = spi_w(0);
	uint16_t scr_hbl_r = spi_w(0);
	uint16_t scr_vbl_t = spi_w(0);
	uint16_t scr_vbl_b = spi_w(0);
	DisableIO();

	printf("\033[1;37mVMODE: store position: [%u-%u, %u-%u]\033[0m\n", scr_hbl_l, scr_hbl_r, scr_vbl_t, scr_vbl_b);

	uint32_t mode = scr_hsize | (scr_vsize << 12) | ((res & 0xFF) << 24);
	if (mode)
	{
		int applied = 0;
		int empty = -1;
		for (int i = 0; (uint)i < sizeof(vmodes_adj) / sizeof(vmodes_adj[0]); i++)
		{
			if (vmodes_adj[i].mode == mode)
			{
				vmodes_adj[i].hpos = (scr_hbl_l << 16) | scr_hbl_r;
				vmodes_adj[i].vpos = (scr_vbl_t << 16) | scr_vbl_b;
				applied = 1;
			}
			if (empty < 0 && !vmodes_adj[i].mode) empty = i;
		}

		if (!applied && empty >= 0)
		{
			vmodes_adj[empty].mode = mode;
			vmodes_adj[empty].hpos = (scr_hbl_l << 16) | scr_hbl_r;
			vmodes_adj[empty].vpos = (scr_vbl_t << 16) | scr_vbl_b;
			applied = 1;
		}

		if (applied)
		{
			FileSaveConfig("minimig_vadjust.dat", vmodes_adj, sizeof(vmodes_adj));
		}
	}
}

static int api1_5 = 0;
static uint32_t show_video_info(int force)
{
	uint32_t ret = 0;
	static uint8_t nres = 0;
	spi_uio_cmd_cont(UIO_GET_VRES);
	uint8_t res = spi_in();
	if ((nres != res) || force)
	{
		nres = res;
		uint32_t width = spi_w(0) | (spi_w(0) << 16);
		uint32_t height = spi_w(0) | (spi_w(0) << 16);
		uint32_t htime = spi_w(0) | (spi_w(0) << 16);
		uint32_t vtime = spi_w(0) | (spi_w(0) << 16);
		uint32_t ptime = spi_w(0) | (spi_w(0) << 16);
		uint32_t vtimeh = spi_w(0) | (spi_w(0) << 16);
		DisableIO();

		float vrate = 100000000;
		if (vtime) vrate /= vtime; else vrate = 0;
		float hrate = 100000;
		if (htime) hrate /= htime; else hrate = 0;

		float prate = width * 100;
		prate /= ptime;

		printf("\033[1;33mINFO: Video resolution: %u x %u, fHorz = %.1fKHz, fVert = %.1fHz, fPix = %.2fMHz\033[0m\n", width, height, hrate, vrate, prate);
		printf("\033[1;33mINFO: Frame time (100MHz counter): VGA = %d, HDMI = %d\033[0m\n", vtime, vtimeh);

		if (vtimeh) api1_5 = 1;
		if (hasAPI1_5() && cfg.video_info)
		{
			static char str[128];
			float vrateh = 100000000;
			if (vtimeh) vrateh /= vtimeh; else vrateh = 0;
			sprintf(str, "%4dx%-4d %6.2fKHz %4.1fHz\n" \
				         "\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\n" \
				         "%4dx%-4d %6.2fMHz %4.1fHz",
				width, height, hrate, vrate, vitems[1], vitems[5], Fpix, vrateh);
			Info(str, cfg.video_info * 1000);
		}

		uint32_t scrh = vitems[5];
		if (height && scrh)
		{
			if (cfg.vscale_border)
			{
				uint32_t border = cfg.vscale_border * 2;
				if ((border + 100) > scrh) border = scrh - 100;
				scrh -= border;
			}

			if (cfg.vscale_mode)
			{
				uint32_t div = 1 << (cfg.vscale_mode - 1);
				uint32_t mag = (scrh*div) / height;
				scrh = (height * mag) / div;
			}

			if(cfg.vscale_border || cfg.vscale_mode)
			{
				printf("*** Set vertical scaling to : %d\n", scrh);
				spi_uio_cmd16(UIO_SETHEIGHT, scrh);
			}
			else
			{
				spi_uio_cmd16(UIO_SETHEIGHT, 0);
			}
		}

		if (vtime && vtimeh) ret = vtime;
	}
	else
	{
		DisableIO();
	}

	adjust_vsize(0);
	return ret;
}

int hasAPI1_5()
{
	return api1_5;
}
