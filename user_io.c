#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h> 
#include <fcntl.h>

#include "hardware.h"
#include "osd.h"
#include "state.h"
#include "state.h"
#include "user_io.h"
#include "archie.h"
#include "debug.h"
#include "keycodes.h"
#include "ikbd.h"
#include "spi.h"
#include "mist_cfg.h"
#include "tos.h"
#include "errors.h"
#include "input.h"
#include "fpga_io.h"
#include "file_io.h"
#include "config.h"

// up to 16 key can be remapped
#define MAX_REMAP  16
unsigned char key_remap_table[MAX_REMAP][2];

#define BREAK  0x8000

fileTYPE sd_image;

// mouse and keyboard emulation state
typedef enum { EMU_NONE, EMU_MOUSE, EMU_JOY0, EMU_JOY1 } emu_mode_t;
static emu_mode_t emu_mode = EMU_NONE;
static unsigned char emu_state = 0;
static unsigned long emu_timer = 0;
#define EMU_MOUSE_FREQ 5

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

void user_io_init()
{
	// no sd card image selected, SD card accesses will go directly
	// to the card
	sd_image.size = 0;

	// mark remap table as unused
	memset(key_remap_table, 0, sizeof(key_remap_table));

	ikbd_init();
}

unsigned char user_io_core_type()
{
	return core_type;
}

char is_minimig()
{
	return(core_type == CORE_TYPE_MINIMIG2);
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

char is_menu_core()
{
	return !strcasecmp(core_name, "MENU");
}

static void user_io_read_core_name()
{
	core_name[0] = 0;

	if (user_io_is_8bit_with_config_string())
	{
		char *p = user_io_8bit_get_string(0);  // get core name
		if (p && p[0]) strcpy(core_name, p);
	}

	iprintf("Core name is \"%s\"\n", core_name);
}

void user_io_detect_core_type()
{
	char *name;
	char mainpath[32];
	core_name[0] = 0;

	core_type = (fpga_core_id() & 0xFF);
	fio_size = fpga_get_fio_size();
	io_ver = fpga_get_io_version();

	if ((core_type != CORE_TYPE_DUMB) &&
		(core_type != CORE_TYPE_MINIMIG2) &&
		(core_type != CORE_TYPE_PACE) &&
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

	switch (core_type)
	{
	case CORE_TYPE_UNKNOWN:
		iprintf("Unable to identify core (%x)!\n", core_type);
		break;

	case CORE_TYPE_DUMB:
		puts("Identified core without user interface");
		break;

	case CORE_TYPE_MINIMIG2:
		puts("Identified Minimig V2 core");
		break;

	case CORE_TYPE_PACE:
		puts("Identified PACE core");
		break;

	case CORE_TYPE_MIST:
		puts("Identified MiST core");
		break;

	case CORE_TYPE_ARCHIE:
		puts("Identified Archimedes core");
		archie_init();
		break;

	case CORE_TYPE_8BIT:
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

		// try to load config
		name = user_io_create_config_name();
		if(strlen(name) > 0)
		{
			iprintf("Loading config %s\n", name);
			unsigned long status = 0;
			if (FileLoadConfig(name, &status, 4))
			{
				iprintf("Found config\n");
				user_io_8bit_set_status(status, 0xffffffff);
			}

			// check if there's a <core>.rom present
			sprintf(mainpath, "%s/boot.rom", user_io_get_core_name());
			if (!user_io_file_tx(mainpath, 0))
			{
				strcpy(name + strlen(name) - 3, "ROM");
				user_io_file_tx(name, 0);
			}

			// check if there's a <core>.vhd present
			sprintf(mainpath, "%s/boot.vhd", user_io_get_core_name());
			if (!user_io_file_mount(mainpath))
			{
				strcpy(name + strlen(name) - 3, "VHD");
				user_io_file_mount(name);
			}
		}

		// release reset
		user_io_8bit_set_status(0, UIO_STATUS_RESET);
		break;
	}
}

unsigned short usb2amiga(unsigned  char k)
{
	//  replace MENU key by RGUI to allow using Right Amiga on reduced keyboards
	// (it also disables the use of Menu for OSD)
	if (mist_cfg.key_menu_as_rgui && k == 0x65)
	{
		return 0x67;
	}
	return usb2ami[k];
}

unsigned short usb2ps2code(unsigned char k)
{
	//  replace MENU key by RGUI e.g. to allow using RGUI on reduced keyboards without physical key
	// (it also disables the use of Menu for OSD)
	if (mist_cfg.key_menu_as_rgui && k == 0x65)
	{
		return EXT | 0x27;
	}
	return usb2ps2[k];
}

void user_io_analog_joystick(unsigned char joystick, char valueX, char valueY)
{
	if (core_type == CORE_TYPE_8BIT)
	{
		uint16_t pos = valueX;
		spi_uio_cmd8_cont(UIO_ASTICK, joystick);
		if(io_ver) spi_w((pos<<8) | (uint8_t)(valueY));
		else
		{
			spi8(valueX);
			spi8(valueY);
		}
		DisableIO();
	}
}

void user_io_digital_joystick(unsigned char joystick, unsigned char map)
{
	uint8_t state = map;
	// "only" 6 joysticks are supported
	if (joystick >= 6) return;

	// the physical joysticks (db9 ports at the right device side)
	// as well as the joystick emulation are renumbered if usb joysticks
	// are present in the system. The USB joystick(s) replace joystick 1
	// and 0 and the physical joysticks are "shifted up". 
	// Since the primary joystick is in port 1 the first usb joystick 
	// becomes joystick 1 and only the second one becomes joystick 0
	// (mouse port)

	StateJoySet(state, joystick == 0 ? 1 : 0);
	if (joystick == 1)
	{
		//StateJoyUpdateTurboStructure(0);
		//map = (unsigned char) StateJoyStructureState(0) & 0xFF;
	}
	else if (joystick == 0)
	{// WARNING: 0 is the second joystick, either USB or DB9
	 //StateJoyUpdateTurboStructure(1);
	 //map = (unsigned char) StateJoyStructureState(1) & 0xFF;
	}

	// if osd is open control it via joystick
	if (osd_is_visible)
	{
		static const uint8_t joy2kbd[] = {
			OSDCTRLMENU, OSDCTRLMENU, OSDCTRLMENU, OSDCTRLSELECT,
			OSDCTRLUP, OSDCTRLDOWN, OSDCTRLLEFT, OSDCTRLRIGHT };

		// iprintf("joy to osd\n");

		//    OsdKeySet(0x80 | usb2ami[pressed[i]]);

		return;
	}

	//  iprintf("j%d: %x\n", joystick, map);


	// atari ST handles joystick 0 and 1 through the ikbd emulated by the io controller
	// but only for joystick 1 and 2
	if ((core_type == CORE_TYPE_MIST) && (joystick < 2))
	{
		ikbd_joystick(joystick, map);
		return;
	}

	// every other core else uses this
	// (even MIST, joystick 3 and 4 were introduced later)
	spi_uio_cmd8((joystick < 2) ? (UIO_JOYSTICK0 + joystick) : ((UIO_JOYSTICK2 + joystick - 2)), map);
}

static char dig2ana(char min, char max)
{
	if (min && !max) return -128;
	if (max && !min) return  127;
	return 0;
}

void user_io_joystick(unsigned char joystick, unsigned char map)
{
	// digital joysticks also send analog signals
	user_io_digital_joystick(joystick, map);
	user_io_analog_joystick(joystick,
		dig2ana(map&JOY_LEFT, map&JOY_RIGHT),
		dig2ana(map&JOY_UP, map&JOY_DOWN));
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
	CSD[6] = (uint8_t)(sd_image.size >> 9);
	CSD[7] = (uint8_t)(sd_image.size >> 17);
	CSD[8] = (uint8_t)(sd_image.size >> 25);

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
uint8_t user_io_kbdled_get_status(void)
{
	uint8_t c;

	spi_uio_cmd_cont(UIO_GET_KBD_LED);
	c = spi_in();
	DisableIO();

	return c;
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

// the physical joysticks (db9 ports at the right device side)
// as well as the joystick emulation are renumbered if usb joysticks
// are present in the system. The USB joystick(s) replace joystick 1
// and 0 and the physical joysticks are "shifted up". 
//
// Since the primary joystick is in port 1 the first usb joystick 
// becomes joystick 1 and only the second one becomes joystick 0
// (mouse port)

static uint8_t joystick_renumber(uint8_t j)
{
	uint8_t usb_sticks = 0; //hid_get_joysticks();

							// no usb sticks present: no changes are being made
	if (!usb_sticks) return j;

	if (j == 0) {
		// if usb joysticks are present, then physical joystick 0 (mouse port)
		// becomes becomes 2,3,...
		j = usb_sticks + 1;
	}
	else {
		// if one usb joystick is present, then physical joystick 1 (joystick port)
		// becomes physical joystick 0 (mouse) port. If more than 1 usb joystick
		// is present it becomes 2,3,...
		if (usb_sticks == 1) j = 0;
		else                j = usb_sticks;
	}

	return j;
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

int user_io_file_mount(char *name)
{
	int writable = FileCanWrite(name);

	int ret = FileOpenEx(&sd_image, name, writable ? (O_RDWR | O_SYNC) : O_RDONLY);
	if (!ret)
	{
		sd_image.size = 0;
		printf("Failed to open file %s\n", name);
		return 0;
	}

	printf("Mount %s as %s\n", name, writable ? "read-write" : "read-only");

	// send mounted image size first then notify about mounting
	EnableIO();
	spi8(UIO_SET_SDINFO);
	if (io_ver)
	{
		spi_w((uint16_t)(sd_image.size));
		spi_w((uint16_t)(sd_image.size>>16));
		spi_w((uint16_t)(sd_image.size>>32));
		spi_w((uint16_t)(sd_image.size>>48));
	}
	else
	{
		spi32le(sd_image.size);
		spi32le(sd_image.size>>32);
	}
	DisableIO();

	// notify core of possible sd image change
	spi_uio_cmd8(UIO_SET_SDSTAT, 0);
	return 1;
}

int user_io_file_tx(char* name, unsigned char index)
{
	fileTYPE f;
	static uint8_t buf[512];

	if (!FileOpen(&f, name)) return 0;

	unsigned long bytes2send = f.size;

	/* transmit the entire file using one transfer */
	iprintf("Selected file %s with %lu bytes to send for index %d.%d\n", name, bytes2send, index&0x3F, index>>6);

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

	while (bytes2send)
	{
		iprintf(".");

		uint16_t chunk = (bytes2send>512) ? 512 : bytes2send;

		FileReadSec(&f, buf);

		EnableFpga();
		spi8(UIO_FILE_TX_DAT);
		spi_write(buf, chunk, fio_size);
		DisableFpga();

		bytes2send -= chunk;
	}

	FileClose(&f);

	// signal end of transmission
	EnableFpga();
	spi8(UIO_FILE_TX);
	spi8(0x00);
	DisableFpga();

	iprintf("\n");
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

	//  iprintf("String: ");
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

		//    iprintf("%c", i);
		i = spi_in();
	}

	DisableIO();
	//  iprintf("\n");

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
	static unsigned char key_map = 0;
	unsigned char map = 0;

	int btn = fpga_get_buttons();

	if (btn & BUTTON_OSD) map |= BUTTON1;
	else if(btn & BUTTON_USR) map |= BUTTON2;
	if (kbd_reset) map |= BUTTON2;

	if (mist_cfg.vga_scaler) map |= CONF_VGA_SCALER;
	if (mist_cfg.csync) map |= CONF_CSYNC;
	if (mist_cfg.ypbpr) map |= CONF_YPBPR;
	if (mist_cfg.forced_scandoubler) map |= CONF_FORCED_SCANDOUBLER;

	if ((map != key_map) || force)
	{
		key_map = map;
		spi_uio_cmd8(UIO_BUT_SW, map);
		printf("sending keymap: %X\n", map);
	}

	if (old_video_mode != mist_cfg.video_mode)
	{
		old_video_mode = mist_cfg.video_mode;
		spi_uio_cmd8(UIO_SET_VIDEO, old_video_mode);
	}
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

uint32_t diskled_timer = 0;
uint32_t diskled_is_on = 0;
void __inline diskled_on()
{
	DISKLED_ON;
	diskled_timer = GetTimer(50);
	diskled_is_on = 1;
}

void user_io_poll()
{
	if ((core_type != CORE_TYPE_MINIMIG2) &&
		(core_type != CORE_TYPE_PACE) &&
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

	// mouse movement emulation is continous 
	if (emu_mode == EMU_MOUSE)
	{
		if (CheckTimer(emu_timer))
		{
			emu_timer = GetTimer(EMU_MOUSE_FREQ);

			if (emu_state & JOY_MOVE)
			{
				unsigned char b = 0;
				int16_t x = 0, y = 0;
				if ((emu_state & (JOY_LEFT | JOY_RIGHT)) == JOY_LEFT)  x = -1;
				if ((emu_state & (JOY_LEFT | JOY_RIGHT)) == JOY_RIGHT) x = +1;
				if ((emu_state & (JOY_UP | JOY_DOWN)) == JOY_UP)    y = -1;
				if ((emu_state & (JOY_UP | JOY_DOWN)) == JOY_DOWN)  y = +1;

				if (emu_state & JOY_BTN1) b |= 1;
				if (emu_state & JOY_BTN2) b |= 2;

				user_io_mouse(b, x, y);
			}
		}
	}

	if (core_type == CORE_TYPE_MINIMIG2)
	{
		kbd_fifo_poll();

		// frequently check mouse for events
		if (CheckTimer(mouse_timer))
		{
			mouse_timer = GetTimer(MOUSE_FREQ);

			// has ps2 mouse data been updated in the meantime
			if (mouse_flags & 0x80)
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

				spi8(mouse_flags & 0x03);
				DisableIO();

				// reset flags
				mouse_flags = 0;
			}
		}
	}

	if (core_type == CORE_TYPE_MIST)
	{
		// do some tos specific monitoring here
		tos_poll();
	}

	if (core_type == CORE_TYPE_8BIT)
	{
		unsigned char c = 1, f, p = 0;

		// check for serial data to be sent

		// check for incoming serial data. this is directly forwarded to the
		// arm rs232 and mixes with debug output.
		spi_uio_cmd_cont(UIO_SIO_IN);
		// status byte is 1000000A with A=1 if data is available
		if ((f = spi_in(0)) == 0x81)
		{
			iprintf("\033[1;36m");

			// character 0xff is returned if FPGA isn't configured
			while ((f == 0x81) && (c != 0xff) && (c != 0x00) && (p < 8))
			{
				c = spi_in();
				if (c != 0xff && c != 0x00) iprintf("%c", c);

				f = spi_in();
				p++;
			}
			iprintf("\033[0m");
		}
		DisableIO();

		// sd card emulation
		{
			static char buffer[512];
			static uint32_t buffer_lba = 0xffffffff;
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
					iprintf("core requests SD config\n");
					user_io_sd_set_config();
				}

				// check if system is trying to access a sdhc card from 
				// a sd/mmc setup

				// check if an SDHC card is inserted
				static char using_sdhc = 1;

				// SD request and 
				if ((c & 0x03) && !(c & 0x04))
				{
					if (using_sdhc)
					{
						// we have not been using sdhc so far? 
						// -> complain!
						ErrorMessage(" This core does not support\n"
							" SDHC cards. Using them may\n"
							" lead to data corruption.\n\n"
							" Please use an SD card <2GB!", 0);
						using_sdhc = 0;
					}
				}
				else
				{
					// SDHC request from core is always ok
					using_sdhc = 1;
				}

				if ((c & 0x03) == 0x02)
				{
					// only write if the inserted card is not sdhc or
					// if the core uses sdhc
					if(c & 0x04)
					{
						uint8_t wr_buf[512];
						//iprintf("SD WR %d\n", lba);

						// if we write the sector stored in the read buffer, then
						// update the read buffer with the new contents
						if (buffer_lba == lba) memcpy(buffer, wr_buf, 512);

						buffer_lba = 0xffffffff;

						// Fetch sector data from FPGA ...
						spi_uio_cmd_cont(UIO_SECTOR_WR);
						spi_block_read(wr_buf, fio_size);
						DisableIO();

						// ... and write it to disk
						diskled_on();

						if (sd_image.size)
						{
							if (FileSeekLBA(&sd_image, lba))
							{
								FileWriteSec(&sd_image, wr_buf);
							}
						}
					}
				}

				if ((c & 0x03) == 0x01)
				{
					//iprintf("SD RD %d\n", lba);

					// are we using a file as the sd card image?
					// (C64 floppy does that ...)
					if (buffer_lba != lba)
					{
						diskled_on();

						if (sd_image.size)
						{
							if (FileSeekLBA(&sd_image, lba))
							{
								FileReadSec(&sd_image, buffer);
							}
						}
						buffer_lba = lba;
					}

					if(buffer_lba == lba)
					{
						//hexdump(buffer, 32, 0);

						// data is now stored in buffer. send it to fpga
						spi_uio_cmd_cont(UIO_SECTOR_RD);
						spi_block_write(buffer, fio_size);
						DisableIO();

						// the end of this transfer acknowledges the FPGA internal
						// sd card emulation
					}

					// just load the next sector now, so it may be prefetched
					// for the next request already
					diskled_on();

					if (sd_image.size)
					{
						if (FileSeekLBA(&sd_image, lba + 1))
						{
							FileReadSec(&sd_image, buffer);
						}
					}
					buffer_lba = lba + 1;
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
					ps2_mouse[1] = -128;
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
					ps2_mouse[2] = -128;
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
				if (!(ps2_mouse[0] == 0x08 && ps2_mouse[1] == 0 && ps2_mouse[2] == 0))
					iprintf("PS2 MOUSE: %x %d %d\n", ps2_mouse[0], ps2_mouse[1], ps2_mouse[2]);

				spi_uio_cmd_cont(UIO_MOUSE);
				spi8(ps2_mouse[0]);
				spi8(ps2_mouse[1]);
				spi8(ps2_mouse[2]);
				DisableIO();

				// reset counters
				mouse_flags = 0;
				mouse_pos[X] = mouse_pos[Y] = 0;
			}
		}

		// --------------- THE FOLLOWING IS DEPRECATED AND WILL BE REMOVED ------------
		// ------------------------ USE SD CARD EMULATION INSTEAD ---------------------

		// raw sector io for the atari800 core which include a full
		// file system driver usually implemented using a second cpu
		static unsigned long bit8_status = 0;
		unsigned long status;

		/* read status byte */
		EnableFpga();
		spi8(UIO_GET_STATUS);
		status = spi_in();
		status = (status << 8) | spi_in();
		status = (status << 8) | spi_in();
		status = (status << 8) | spi_in();
		DisableFpga();
		/*
		if (status != bit8_status)
		{
			unsigned long sector = (status >> 8) & 0xffffff;
			char buffer[512];

			bit8_status = status;

			// sector read testing 
			DISKLED_ON;

			// sector read
			if (((status & 0xff) == 0xa5) || ((status & 0x3f) == 0x29))
			{

				// extended command with 26 bits (for 32GB SDHC)
				if ((status & 0x3f) == 0x29) sector = (status >> 6) & 0x3ffffff;

				bit8_debugf("SECIO rd %ld", sector);

				if (MMC_Read(sector, buffer))
				{
					// data is now stored in buffer. send it to fpga
					EnableFpga();
					spi8(UIO_SECTOR_SND);     // send sector data IO->FPGA
					spi_block_write(buffer);
					DisableFpga();
				}
				else
				{
					bit8_debugf("rd %ld fail", sector);
				}
			}

			// sector write
			if (((status & 0xff) == 0xa6) || ((status & 0x3f) == 0x2a))
			{
				// extended command with 26 bits (for 32GB SDHC)
				if ((status & 0x3f) == 0x2a) sector = (status >> 6) & 0x3ffffff;

				bit8_debugf("SECIO wr %ld", sector);

				// read sector from FPGA
				EnableFpga();
				spi8(UIO_SECTOR_RCV);     // receive sector data FPGA->IO
				spi_block_read(buffer);
				DisableFpga();

				if (!MMC_Write(sector, buffer)) bit8_debugf("wr %ld fail", sector);
			}

			DISKLED_OFF;
		}
		*/
	}

	if (core_type == CORE_TYPE_ARCHIE) archie_poll();

	if (CheckTimer(led_timer))
	{
		led_timer = GetTimer(LED_FREQ);
		uint8_t leds = user_io_kbdled_get_status();
		if ((leds & KBD_LED_FLAG_MASK) != KBD_LED_FLAG_STATUS) leds = 0;

		if ((keyboard_leds & KBD_LED_CAPS_MASK) != (leds & KBD_LED_CAPS_MASK))
			set_kbdled(HID_LED_CAPS_LOCK, (leds & KBD_LED_CAPS_CONTROL) ? leds & KBD_LED_CAPS_STATUS : caps_status);

		if ((keyboard_leds & KBD_LED_NUM_MASK) != (leds & KBD_LED_NUM_MASK))
			set_kbdled(HID_LED_NUM_LOCK, (leds & KBD_LED_NUM_CONTROL) ? leds & KBD_LED_NUM_STATUS : num_status);

		if ((keyboard_leds & KBD_LED_SCRL_MASK) != (leds & KBD_LED_SCRL_MASK))
			set_kbdled(HID_LED_SCROLL_LOCK, (leds & KBD_LED_SCRL_CONTROL) ? leds & KBD_LED_SCRL_STATUS : scrl_status);
		keyboard_leds = leds;
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

static void send_keycode(unsigned short code)
{
	if (core_type == CORE_TYPE_MINIMIG2)
	{
		// amiga has "break" marker in msb
		if (code & BREAK) code = (code & 0xff) | 0x80;

		// send immediately if possible
		if (CheckTimer(kbd_timer) && (kbd_fifo_w == kbd_fifo_r))
		{
			kbd_fifo_minimig_send(code);
		}
		else
		{
			kbd_fifo_enqueue(code);
		}
	}

	if (core_type == CORE_TYPE_MIST)
	{
		// atari has "break" marker in msb
		if (code & BREAK) code = (code & 0xff) | 0x80;
		ikbd_keyboard(code);
	}

	if (core_type == CORE_TYPE_8BIT)
	{
		// send ps2 keycodes for those cores that prefer ps2
		spi_uio_cmd_cont(UIO_KEYBOARD);

		// "pause" has a complex code 
		if ((code & 0xff) == 0x77)
		{
			// pause does not have a break code
			if (!(code & BREAK))
			{
				// Pause key sends E11477E1F014E077
				static const unsigned char c[] = {
					0xe1, 0x14, 0x77, 0xe1, 0xf0, 0x14, 0xf0, 0x77, 0x00 };
				const unsigned char *p = c;

				iprintf("PS2 KBD ");
				while (*p)
				{
					iprintf("%x ", *p);
					spi8(*p++);
				}
				iprintf("\n");
			}
		}
		else
		{
			/*
			iprintf("PS2 KBD ");
			if (code & EXT)   iprintf("e0 ");
			if (code & BREAK) iprintf("f0 ");
			iprintf("%x\n", code & 0xff);
			*/

			// prepend extended code flag if required
			if (code & EXT) spi8(0xe0);

			// prepend break code if required
			if (code & BREAK) spi8(0xf0);

			// send code itself
			spi8(code & 0xff);
		}

		DisableIO();
	}

	if (core_type == CORE_TYPE_ARCHIE) archie_kbd(code);
}

void user_io_mouse(unsigned char b, int16_t x, int16_t y)
{
	// send mouse data as minimig expects it
	if (core_type == CORE_TYPE_MINIMIG2)
	{
		mouse_pos[X] += x;
		mouse_pos[Y] += y;
		mouse_flags |= 0x80 | (b & 3);
	}

	// 8 bit core expects ps2 like data
	if (core_type == CORE_TYPE_8BIT)
	{
		mouse_pos[X] += x;
		mouse_pos[Y] -= y;  // ps2 y axis is reversed over usb
		mouse_flags |= 0x08 | (b & 3);
	}

	// send mouse data as mist expects it
	if (core_type == CORE_TYPE_MIST) ikbd_mouse(b, x, y);
	if (core_type == CORE_TYPE_ARCHIE) archie_mouse(b, x, y);
}

// check if this is a key that's supposed to be suppressed
// when emulation is active
static unsigned char is_emu_key(unsigned char c, unsigned alt) {
	static const unsigned char m[] = { JOY_RIGHT, JOY_LEFT, JOY_DOWN, JOY_UP };
	static const unsigned char m2[] =
	{
		0x5A, JOY_DOWN,
		0x5C, JOY_LEFT,
		0x5D, JOY_DOWN,
		0x5E, JOY_RIGHT,
		0x60, JOY_UP,
		0x5F, JOY_BTN1,
		0x61, JOY_BTN2
	};

	if (emu_mode == EMU_NONE) return 0;

	if (alt)
	{
		for (int i = 0; i<(sizeof(m2) / sizeof(m2[0])); i += 2) if (c == m2[i]) return m2[i + 1];
	}
	else
	{
		// direction keys R/L/D/U
		if (c >= 0x4f && c <= 0x52) return m[c - 0x4f];
	}

	return 0;
}

/* usb modifer bits:
0     1     2    3    4     5     6    7
LCTRL LSHIFT LALT LGUI RCTRL RSHIFT RALT RGUI
*/
#define EMU_BTN1  (0+(keyrah*4))  // left control
#define EMU_BTN2  (1+(keyrah*4))  // left shift
#define EMU_BTN3  (2+(keyrah*4))  // left alt
#define EMU_BTN4  (3+(keyrah*4))  // left gui (usually windows key)

unsigned short keycode(unsigned char in)
{
	if (core_type == CORE_TYPE_MINIMIG2) return usb2amiga(in);

	// atari st and the 8 bit core (currently only used for atari 800)
	// use the same key codes
	if (core_type == CORE_TYPE_MIST) return usb2atari[in];
	if (core_type == CORE_TYPE_ARCHIE) return usb2archie[in];
	if (core_type == CORE_TYPE_8BIT) return usb2ps2code(in);

	return MISS;
}

extern configTYPE config;

void check_reset(unsigned short modifiers, char useKeys)
{
	unsigned short combo[] =
	{
		0x45,  // lctrl+lalt+ralt
		0x89,  // lctrl+lgui+rgui
		0x105, // lctrl+lalt+del
	};

	if ((modifiers & ~2) == combo[useKeys])
	{
		if (modifiers & 2) // with lshift - MiST reset
		{
			reboot(1);
		}

		switch (core_type)
		{
		case CORE_TYPE_MINIMIG2:
			ConfigIDE(config.enable_ide, config.hardfile[0].present && config.hardfile[0].enabled, config.hardfile[1].present && config.hardfile[1].enabled);
			OsdReset(RESET_NORMAL);
			break;

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

unsigned short modifier_keycode(unsigned char index)
{
	/* usb modifer bits:
	0     1     2    3    4     5     6    7
	LCTRL LSHIFT LALT LGUI RCTRL RSHIFT RALT RGUI
	*/

	if (core_type == CORE_TYPE_MINIMIG2)
	{
		static const unsigned short amiga_modifier[] = { 0x63, 0x60, 0x64, 0x66, 0x63, 0x61, 0x65, 0x67 };
		return amiga_modifier[index];
	}

	if (core_type == CORE_TYPE_MIST)
	{
		static const unsigned short atari_modifier[] = { 0x1d, 0x2a, 0x38, MISS, 0x1d, 0x36, 0x38, MISS };
		return atari_modifier[index];
	}

	if (core_type == CORE_TYPE_8BIT)
	{
		static const unsigned short ps2_modifier[] = { 0x14, 0x12, 0x11, EXT | 0x1f, EXT | 0x14, 0x59, EXT | 0x11, EXT | 0x27 };
		return ps2_modifier[index];
	}

	if (core_type == CORE_TYPE_ARCHIE)
	{
		static const unsigned short archie_modifier[] = { 0x36, 0x4c, 0x5e, MISS, 0x61, 0x58, 0x60, MISS };
		return archie_modifier[index];
	}

	return MISS;
}

void user_io_osd_key_enable(char on)
{
	iprintf("OSD is now %s\n", on ? "visible" : "invisible");
	osd_is_visible = on;
}

static char key_used_by_osd(unsigned short s)
{
	// this key is only used to open the OSD and has no keycode
	if ((s & OSD_OPEN) && !(s & 0xff))  return true;

	// no keys are suppressed if the OSD is inactive
	if (!osd_is_visible) return false;

	// in atari mode eat all keys if the OSD is online,
	// else none as it's up to the core to forward keys
	// to the OSD
	return((core_type == CORE_TYPE_MIST) || (core_type == CORE_TYPE_ARCHIE) || (core_type == CORE_TYPE_8BIT));
}

static char kr_fn_table[] =
{
	0x54, 0x48, // pause/break
	0x55, 0x46, // prnscr
	0x50, 0x4a, // home
	0x4f, 0x4d, // end
	0x52, 0x4b, // pgup
	0x51, 0x4e, // pgdown
	0x3a, 0x44, // f11
	0x3b, 0x45, // f12

	0x3c, 0x6c, // EMU_MOUSE
	0x3d, 0x6d, // EMU_JOY0
	0x3e, 0x6e, // EMU_JOY1
	0x3f, 0x6f, // EMU_NONE

				//Emulate keypad for A600 
	0x1E, 0x59, //KP1
	0x1F, 0x5A, //KP2
	0x20, 0x5B, //KP3
	0x21, 0x5C, //KP4
	0x22, 0x5D, //KP5
	0x23, 0x5E, //KP6
	0x24, 0x5F, //KP7
	0x25, 0x60, //KP8
	0x26, 0x61, //KP9
	0x27, 0x62, //KP0
	0x2D, 0x56, //KP-
	0x2E, 0x57, //KP+
	0x31, 0x55, //KP*
	0x2F, 0x68, //KP(
	0x30, 0x69, //KP)
	0x37, 0x63, //KP.
	0x28, 0x58  //KP Enter
};

static void keyrah_trans(unsigned char *m, unsigned char *k)
{
	static char keyrah_fn_state = 0;
	char fn = 0;
	char empty = 1;
	char rctrl = 0;
	int i = 0;
	while (i<6)
	{
		if ((k[i] == 0x64) || (k[i] == 0x32))
		{
			if (k[i] == 0x64) fn = 1;
			if (k[i] == 0x32) rctrl = 1;
			for (int n = i; n<5; n++) k[n] = k[n + 1];
			k[5] = 0;
		}
		else
		{
			if (k[i]) empty = 0;
			i++;
		}
	}

	if (fn)
	{
		for (i = 0; i<6; i++)
		{
			for (int n = 0; n<(sizeof(kr_fn_table) / (2 * sizeof(kr_fn_table[0]))); n++)
			{
				if (k[i] == kr_fn_table[n * 2]) k[i] = kr_fn_table[(n * 2) + 1];
			}
		}
	}
	else
	{
		// free these keys for core usage
		for (i = 0; i<6; i++)
		{
			if (k[i] == 0x53) k[i] = 0x68;
			if (k[i] == 0x47) k[i] = 0x69;
			if (k[i] == 0x49) k[i] = 0x6b; // workaround!
		}
	}

	*m = rctrl ? (*m) | 0x10 : (*m) & ~0x10;
	if (fn)
	{
		keyrah_fn_state |= 1;
		if (*m || !empty) keyrah_fn_state |= 2;
	}
	else
	{
		if (keyrah_fn_state == 1)
		{
			if (core_type == CORE_TYPE_MINIMIG2)
			{
				send_keycode(KEY_MENU);
				send_keycode(BREAK | KEY_MENU);
			}
			else
			{
				OsdKeySet(KEY_MENU);
			}
		}
		keyrah_fn_state = 0;
	}
}

//Keyrah v2: USB\VID_18D8&PID_0002\A600/A1200_MULTIMEDIA_EXTENSION_VERSION
#define KEYRAH_ID (mist_cfg.keyrah_mode && (((((uint32_t)vid)<<16) | pid) == mist_cfg.keyrah_mode))

void user_io_kbd(unsigned char m, unsigned char *k, unsigned short vid, unsigned short pid)
{
	char keyrah = KEYRAH_ID ? 1 : 0;
	if (emu_mode == EMU_MOUSE) keyrah <<= 1;

	if (keyrah) keyrah_trans(&m, k);

	unsigned short reset_m = m;
	for (char i = 0; i<6; i++) if (k[i] == 0x4c) reset_m |= 0x100;
	check_reset(reset_m, KEYRAH_ID ? 1 : mist_cfg.reset_combo);

	if ((core_type == CORE_TYPE_MINIMIG2) ||
		(core_type == CORE_TYPE_MIST) ||
		(core_type == CORE_TYPE_ARCHIE) ||
		(core_type == CORE_TYPE_8BIT))
	{
		//iprintf("KBD: %d\n", m);
		//hexdump(k, 6, 0);

		static unsigned char modifier = 0, pressed[6] = { 0,0,0,0,0,0 };
		char keycodes[6] = { 0,0,0,0,0,0 };
		uint16_t keycodes_ps2[6] = { 0,0,0,0,0,0 };
		char i, j;

		// remap keycodes if requested
		for (i = 0; (i<6) && k[i]; i++)
		{
			for (j = 0; j<MAX_REMAP; j++)
			{
				if (key_remap_table[j][0] == k[i])
				{
					k[i] = key_remap_table[j][1];
					break;
				}
			}
		}

		// remap modifiers to each other if requested
		//  bit  0     1      2    3    4     5      6    7
		//  key  LCTRL LSHIFT LALT LGUI RCTRL RSHIFT RALT RGUI
		if (false)
		{ // (disabled until we configure it via INI)
			uint8_t default_mod_mapping[8] =
			{
				0x1,
				0x2,
				0x4,
				0x8,
				0x10,
				0x20,
				0x40,
				0x80
			};
			uint8_t modifiers = 0;
			for (i = 0; i<8; i++) if (m & (0x01 << i))  modifiers |= default_mod_mapping[i];
			m = modifiers;
		}

		// modifier keys are used as buttons in emu mode
		if (emu_mode != EMU_NONE && !osd_is_visible)
		{
			char last_btn = emu_state & (JOY_BTN1 | JOY_BTN2 | JOY_BTN3 | JOY_BTN4);
			if (keyrah != 2)
			{
				if (m & (1 << EMU_BTN1)) emu_state |= JOY_BTN1;
				else                  emu_state &= ~JOY_BTN1;
				if (m & (1 << EMU_BTN2)) emu_state |= JOY_BTN2;
				else                  emu_state &= ~JOY_BTN2;
			}
			if (m & (1 << EMU_BTN3)) emu_state |= JOY_BTN3;
			else                  emu_state &= ~JOY_BTN3;
			if (m & (1 << EMU_BTN4)) emu_state |= JOY_BTN4;
			else                  emu_state &= ~JOY_BTN4;

			// check if state of mouse buttons has changed
			// (on a mouse only two buttons are supported)
			if ((last_btn  & (JOY_BTN1 | JOY_BTN2)) != (emu_state & (JOY_BTN1 | JOY_BTN2)))
			{
				if (emu_mode == EMU_MOUSE)
				{
					unsigned char b;
					if (emu_state & JOY_BTN1) b |= 1;
					if (emu_state & JOY_BTN2) b |= 2;
					user_io_mouse(b, 0, 0);
				}
			}

			// check if state of joystick buttons has changed
			if (last_btn != (emu_state & (JOY_BTN1 | JOY_BTN2 | JOY_BTN3 | JOY_BTN4)))
			{
				if (emu_mode == EMU_JOY0) user_io_joystick(joystick_renumber(0), emu_state);
				if (emu_mode == EMU_JOY1) user_io_joystick(joystick_renumber(1), emu_state);
			}
		}

		// handle modifier keys
		if (m != modifier && !osd_is_visible)
		{
			for (i = 0; i<8; i++)
			{
				// Do we have a downstroke on a modifier key?
				if ((m & (1 << i)) && !(modifier & (1 << i)))
				{
					// shift keys are used for mouse joystick emulation in emu mode
					if (((i != EMU_BTN1) && (i != EMU_BTN2) && (i != EMU_BTN3) && (i != EMU_BTN4)) || (emu_mode == EMU_NONE))
					{
						if (modifier_keycode(i) != MISS) send_keycode(modifier_keycode(i));
					}
				}

				if (!(m & (1 << i)) && (modifier & (1 << i)))
				{
					if (((i != EMU_BTN1) && (i != EMU_BTN2) && (i != EMU_BTN3) && (i != EMU_BTN4)) || (emu_mode == EMU_NONE))
					{
						if (modifier_keycode(i) != MISS) send_keycode(BREAK | modifier_keycode(i));
					}
				}
			}

			modifier = m;
		}

		// check if there are keys in the pressed list which aren't 
		// reported anymore
		for (i = 0; i<6; i++)
		{
			unsigned short code = keycode(pressed[i]);

			if (pressed[i] && code != MISS)
			{
				//iprintf("key 0x%X break: 0x%X\n", pressed[i], code);

				for (j = 0; j<6 && pressed[i] != k[j]; j++);

				// don't send break for caps lock
				if (j == 6)
				{
					// If OSD is visible, then all keys are sent into the OSD
					// using Amiga key codes since the OSD itself uses Amiga key codes
					// for historical reasons. If the OSD is invisble then only
					// those keys marked for OSD in the core specific table are
					// sent for OSD handling.
					if (code & OSD_OPEN)
					{
						OsdKeySet(0x80 | KEY_MENU);
					}
					else
					{
						// special OSD key handled internally 
						if (osd_is_visible) OsdKeySet(0x80 | usb2amiga(pressed[i]));
					}

					if (!key_used_by_osd(code))
					{
						// iprintf("Key is not used by OSD\n");
						if (is_emu_key(pressed[i], keyrah) && !osd_is_visible)
						{
							emu_state &= ~is_emu_key(pressed[i], keyrah);
							if (emu_mode == EMU_JOY0) user_io_joystick(joystick_renumber(0), emu_state);
							if (emu_mode == EMU_JOY1) user_io_joystick(joystick_renumber(1), emu_state);
							if (keyrah == 2)
							{
								unsigned char b;
								if (emu_state & JOY_BTN1) b |= 1;
								if (emu_state & JOY_BTN2) b |= 2;
								user_io_mouse(b, 0, 0);
							}
						}
						else if (!(code & CAPS_LOCK_TOGGLE) && !(code & NUM_LOCK_TOGGLE))
						{
							send_keycode(BREAK | code);
						}
					}
				}
			}
		}

		for (i = 0; i<6; i++)
		{
			unsigned short code = keycode(k[i]);

			if (k[i] && (k[i] <= KEYCODE_MAX) && code != MISS)
			{
				// check if this key is already in the list of pressed keys
				for (j = 0; j<6 && k[i] != pressed[j]; j++);

				if (j == 6)
				{
					//iprintf("key 0x%X make: 0x%X\n", k[i], code);

					// If OSD is visible, then all keys are sent into the OSD
					// using Amiga key codes since the OSD itself uses Amiga key codes
					// for historical reasons. If the OSD is invisble then only
					// those keys marked for OSD in the core specific table are
					// sent for OSD handling.
					if (code & OSD_OPEN)
					{
						OsdKeySet(KEY_MENU);
					}
					else
					{
						// special OSD key handled internally 
						if (osd_is_visible) OsdKeySet(usb2amiga(k[i]));
					}

					// no further processing of any key that is currently 
					// redirected to the OSD
					if (!key_used_by_osd(code))
					{
						// iprintf("Key is not used by OSD\n");
						if (is_emu_key(k[i], keyrah) && !osd_is_visible)
						{
							emu_state |= is_emu_key(k[i], keyrah);

							// joystick emulation is also affected by the presence of
							// usb joysticks
							if (emu_mode == EMU_JOY0) user_io_joystick(joystick_renumber(0), emu_state);
							if (emu_mode == EMU_JOY1) user_io_joystick(joystick_renumber(1), emu_state);
							if (keyrah == 2)
							{
								unsigned char b;
								if (emu_state & JOY_BTN1) b |= 1;
								if (emu_state & JOY_BTN2) b |= 2;
								user_io_mouse(b, 0, 0);
							}
						}
						else if (!(code & CAPS_LOCK_TOGGLE) && !(code & NUM_LOCK_TOGGLE))
						{
							send_keycode(code);
						}
						else
						{
							if (code & CAPS_LOCK_TOGGLE)
							{
								// send alternating make and break codes for caps lock
								send_keycode((code & 0xff) | (caps_lock_toggle ? BREAK : 0));
								caps_lock_toggle = !caps_lock_toggle;

								set_kbd_led(HID_LED_CAPS_LOCK, caps_lock_toggle);
							}

							if (code & NUM_LOCK_TOGGLE)
							{
								// num lock has four states indicated by leds:
								// all off: normal
								// num lock on, scroll lock on: mouse emu
								// num lock on, scroll lock off: joy0 emu
								// num lock off, scroll lock on: joy1 emu

								if (emu_mode == EMU_MOUSE) emu_timer = GetTimer(EMU_MOUSE_FREQ);

								switch (code ^ NUM_LOCK_TOGGLE)
								{
								case 1:
									emu_mode = EMU_MOUSE;
									break;

								case 2:
									emu_mode = EMU_JOY0;
									break;

								case 3:
									emu_mode = EMU_JOY1;
									break;

								case 4:
									emu_mode = EMU_NONE;
									break;

								default:
									emu_mode = (emu_mode + 1) & 3;
									break;
								}
								if(emu_mode == EMU_MOUSE || emu_mode == EMU_JOY0) set_kbd_led(HID_LED_NUM_LOCK, true);
								else set_kbd_led(HID_LED_NUM_LOCK, false);

								if(emu_mode == EMU_MOUSE || emu_mode == EMU_JOY1) set_kbd_led(HID_LED_SCROLL_LOCK, true);
								else set_kbd_led(HID_LED_SCROLL_LOCK, false);
							}
						}
					}
				}
			}
		}

		for (i = 0; i<6; i++)
		{
			pressed[i] = k[i];
			keycodes[i] = pressed[i]; // send raw USB code, not amiga - keycode(pressed[i]);
			keycodes_ps2[i] = keycode(pressed[i]);
		}
		StateKeyboardSet(m, keycodes, keycodes_ps2);
	}
}

/* translates a USB modifiers into scancodes */
void add_modifiers(uint8_t mod, uint16_t* keys_ps2)
{
	uint8_t i;
	uint8_t offset = 1;
	uint8_t index = 0;
	while (offset)
	{
		if (mod&offset)
		{
			uint16_t ps2_value = modifier_keycode(index);
			if (ps2_value != MISS)
			{
				if (ps2_value & EXT) ps2_value = (0xE000 | (ps2_value & 0xFF));
				for (i = 0; i<4; i++)
				{
					if (keys_ps2[i] == 0)
					{
						keys_ps2[i] = ps2_value;
						break;
					}
				}
			}
		}
		offset <<= 1;
		index++;
	}
}

void user_io_key_remap(char *s)
{
	// s is a string containing two comma separated hex numbers
	if ((strlen(s) != 5) && (s[2] != ','))
	{
		ini_parser_debugf("malformed entry %s", s);
		return;
	}

	char i;
	for (i = 0; i<MAX_REMAP; i++)
	{
		if (!key_remap_table[i][0])
		{
			key_remap_table[i][0] = strtol(s, NULL, 16);
			key_remap_table[i][1] = strtol(s + 3, NULL, 16);

			ini_parser_debugf("key remap entry %d = %02x,%02x", i, key_remap_table[i][0], key_remap_table[i][1]);
			return;
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
