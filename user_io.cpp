#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <fcntl.h>
#include <time.h>
#include <limits.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/mman.h>

#include "lib/lodepng/lodepng.h"
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
#include "bootcore.h"
#include "charrom.h"
#include "scaler.h"
#include "miniz.h"
#include "cheats.h"
#include "video.h"

#include "support.h"

static char core_path[1024];

static uint8_t vol_att = 0;
unsigned long vol_set_timeout = 0;

static fileTYPE sd_image[4] = {};
static uint64_t buffer_lba[4] = { ULLONG_MAX,ULLONG_MAX,ULLONG_MAX,ULLONG_MAX };

// mouse and keyboard emulation state
static int emu_mode = EMU_NONE;

// keep state over core type and its capabilities
static unsigned char core_type = CORE_TYPE_UNKNOWN;
static unsigned char dual_sdr = 0;

static int fio_size = 0;
static int io_ver = 0;

// keep state of caps lock
static char caps_lock_toggle = 0;

// mouse position storage for ps2 and minimig rate limitation
#define X 0
#define Y 1
#define MOUSE_FREQ 20   // 20 ms -> 50hz
static int16_t mouse_pos[2] = { 0, 0 };
static int16_t mouse_wheel = 0;
static uint8_t mouse_flags = 0;
static unsigned long mouse_timer;

#define LED_FREQ 100   // 100 ms
static unsigned long led_timer;
static char keyboard_leds = 0;
static bool caps_status = 0;
static bool num_status = 0;
static bool scrl_status = 0;

static uint16_t sdram_cfg = 0;

typedef struct
{
	bool track_active;
	bool pregap_present;
	uint8_t pre_m;		// Actual "PREGAP".
	uint8_t pre_s;
	uint8_t pre_f;
	bool ind0_present;
	uint8_t ind0_m;		// "Pregap" INDEX 00
	uint8_t ind0_s;
	uint8_t ind0_f;
	uint8_t ind1_m;		// "Track Start" INDEX 01
	uint8_t ind1_s;
	uint8_t ind1_f;
	uint8_t type;		// 0==AUDIO. 4==DATA.
	int bytes_per_sec;
} cd_track_t;

// Track 1-99, so entry zero is unused / ignored.
cd_track_t cd_trackinfo[100];

uint8_t cd_first_track;
uint8_t cd_last_track;

static inline uint32_t msf_to_lba(uint8_t m, uint8_t s, uint8_t f)
{
	return (m*60*75) + (s*75) + f;
}

uint32_t dec_2_bcd(uint32_t a)
{
	uint32_t result = 0;
	int shift = 0;

	while (a != 0)
	{
		result |= (a % 10) << shift;
		a /= 10;
		shift += 4;
	}
	return result;
}

uint32_t bcd_2_dec(uint32_t a)
{
	uint32_t result = 0;
	uint32_t scale = 1;

	while (a != 0)
	{
		result += (a & 0x0f) * scale;
		a >>= 4;
		scale *= 10;
	}
	return result;
}

static char last_filename[1024] = {};
void user_io_store_filename(char *filename)
{
	char *p = strrchr(filename, '/');
	if (p) strcpy(last_filename, p + 1);
	else strcpy(last_filename, filename);

	p = strrchr(last_filename, '.');
	if (p) *p = 0;
}

const char *get_image_name(int i)
{
	if (!sd_image[i].size)  return NULL;

	char *p = strrchr(sd_image[i].name, '/');
	if (!p) p = sd_image[i].name; else p++;

	return p;
}

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
static char core_dir[1024];

static char filepath_store[1024];

char *user_io_make_filepath(const char *path, const char *filename)
{
	snprintf(filepath_store, 1024, "%s/%s", path, filename);

	return filepath_store;
}

void user_io_set_core_name(const char *name)
{
	strncpy(core_name, name, 17);
	strncpy(core_dir, name, 1024);
	prefixGameDir(core_dir, 1024);

	printf("Core name set to \"%s\"\n", core_name);
}

char *user_io_get_core_name()
{
	return core_name;
}

char *user_io_get_core_path(void)
{
	return core_dir;
}

const char *user_io_get_core_name_ex()
{
	switch (user_io_core_type())
	{
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

static int is_cpc_type = 0;
char is_cpc_core()
{
	if (!is_cpc_type) is_cpc_type = strcasecmp(core_name, "amstrad") ? 2 : 1;
	return (is_cpc_type == 1);
}

static int is_zx81_type = 0;
char is_zx81_core()
{
	if (!is_zx81_type) is_zx81_type = strcasecmp(core_name, "zx81") ? 2 : 1;
	return (is_zx81_type == 1);
}

static int is_neogeo_type = 0;
char is_neogeo_core()
{
	if (!is_neogeo_type) is_neogeo_type = strcasecmp(core_name, "neogeo") ? 2 : 1;
	return (is_neogeo_type == 1);
}

static int is_minimig_type = 0;
char is_minimig()
{
	if (!is_minimig_type) is_minimig_type = strcasecmp(core_name, "minimig") ? 2 : 1;
	return (is_minimig_type == 1);
}

static int is_megacd_type = 0;
char is_megacd_core()
{
	if (!is_megacd_type) is_megacd_type = strcasecmp(core_name, "MEGACD") ? 2 : 1;
	return (is_megacd_type == 1);
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
	is_snes_type = 0;
	is_cpc_type = 0;
	is_zx81_type = 0;
	is_neogeo_type = 0;
	is_minimig_type = 0;
	core_name[0] = 0;

	// get core name
	char *p = user_io_get_confstr(0);
	if (p && p[0]) strcpy(core_name, p);

	strncpy(core_dir, !strcasecmp(p, "minimig") ? "Amiga" : core_name, 1024);
	prefixGameDir(core_dir, 1024);

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

static int use_cheats = 0;

static void parse_config()
{
	int i = 0;
	char *p;

	joy_force = 0;

	do {
		p = user_io_get_confstr(i);
		printf("get cfgstring %d = %s\n", i, p);
		if (!i && p && p[0])
		{
			OsdCoreNameSet(p);
		}

		if (i>=2 && p && p[0])
		{
			//skip Disable/Hide masks
			while((p[0] == 'H' || p[0] == 'D') && strlen(p)>=2) p += 2;

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
				for (int n = 0; n < 28; n++)
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
				char s[128];
				strcpy(s, OsdCoreName());
				strcat(s, " ");
				substrcpy(s + strlen(s), p, 1);
				OsdCoreNameSet(s);
			}

			if (p[0] == 'C')
			{
				use_cheats = 1;
			}
		}
		i++;
	} while (p || i<3);
}

//MSM6242B layout
static void send_rtc(int type)
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

const char* get_rbf_path()
{
	return core_path;
}

void MakeFile(const char * filename, const char * data)
{
        FILE * file;
        file = fopen(filename, "w");
        fwrite(data, strlen(data), 1, file);
        fclose(file);
}

int GetUARTMode()
{
	struct stat filestat;
	if (!stat("/tmp/uartmode1", &filestat)) return 1;
	if (!stat("/tmp/uartmode2", &filestat)) return 2;
	if (!stat("/tmp/uartmode3", &filestat)) return 3;
	return 0;
}

int GetMidiLinkMode()
{
	struct stat filestat;
	if (!stat("/tmp/ML_FSYNTH", &filestat)) return 0;
	if (!stat("/tmp/ML_MUNT", &filestat)) return 1;
	if (!stat("/tmp/ML_TCP", &filestat)) return 2;
	if (!stat("/tmp/ML_UDP", &filestat)) return 3;
	return 0;
}

void SetMidiLinkMode(int mode)
{
        MakeFile("/tmp/CORENAME", user_io_get_core_name_ex());
        remove("/tmp/ML_FSYNTH");
        remove("/tmp/ML_MUNT");
        remove("/tmp/ML_UDP");
        remove("/tmp/ML_TCP");
        switch (mode)
        {
        case 0: MakeFile("/tmp/ML_FSYNTH", ""); break;
        case 1: MakeFile("/tmp/ML_MUNT", ""); break;
        case 2: MakeFile("/tmp/ML_TCP", ""); break;
        case 3: MakeFile("/tmp/ML_UDP", ""); break;
        }
}

uint16_t sdram_sz(int sz)
{
	int res = 0;

	int fd;
	if ((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1) return 0;

	void* buf = mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x1FFFF000);
	if (buf == (void *)-1)
	{
		printf("Unable to mmap(/dev/mem)\n");
		close(fd);
		return 0;
	}

	volatile uint8_t* par = (volatile uint8_t*)buf;
	par += 0xF00;
	if (sz >= 0)
	{
		*par++ = 0x12;
		*par++ = 0x57;
		*par++ = (uint8_t)(sz>>8);
		*par++ = (uint8_t)sz;
	}
	else
	{
		if ((par[0] == 0x12) && (par[1] == 0x57))
		{
			res = 0x8000 | (par[2]<<8) | par[3];
			if(res & 0x4000) printf("*** Debug phase: %d\n", (res & 0x100) ? (res & 0xFF) : -(res & 0xFF));
			else printf("*** Found SDRAM config: %d\n", res & 7);
		}
		else if(!is_menu_core())
		{
			printf("*** SDRAM config not found\n");
		}
	}

	munmap(buf, 0x1000);
	close(fd);
	return res;
}

uint16_t altcfg(int alt)
{
	int res = 0;

	int fd;
	if ((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1) return 0;

	void* buf = mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x1FFFF000);
	if (buf == (void *)-1)
	{
		printf("Unable to mmap(/dev/mem)\n");
		close(fd);
		return 0;
	}

	volatile uint8_t* par = (volatile uint8_t*)buf;
	par += 0xF04;
	if (alt >= 0)
	{
		*par++ = 0x34;
		*par++ = 0x99;
		*par++ = 0xBA;
		*par++ = (uint8_t)alt;
		printf("** altcfg(%d)\n", alt);
	}
	else
	{
		if ((par[0] == 0x34) && (par[1] == 0x99) && (par[2] == 0xBA))
		{
			res = par[3];
			printf("** altcfg: got cfg %d\n", res);
		}
		else
		{
			printf("** altcfg: no cfg\n");
		}
	}

	munmap(buf, 0x1000);
	close(fd);
	return res;
}

int user_io_is_dualsdr()
{
	return dual_sdr;
}

void user_io_init(const char *path)
{
	char *name;
	static char mainpath[512];
	core_name[0] = 0;
	disable_osd = 0;

	memset(sd_image, 0, sizeof(sd_image));

	strcpy(core_path, path);
	core_type = (fpga_core_id() & 0xFF);
	fio_size = fpga_get_fio_size();
	io_ver = fpga_get_io_version();

	if (core_type == CORE_TYPE_8BIT2)
	{
		dual_sdr = 1;
		core_type = CORE_TYPE_8BIT;
	}

	if ((core_type != CORE_TYPE_DUMB) &&
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

	user_io_read_confstr();
	if (core_type == CORE_TYPE_8BIT)
	{
		puts("Identified 8BIT core");

		// set core name. This currently only sets a name for the 8 bit cores
		user_io_read_core_name();
		spi_uio_cmd16(UIO_SET_MEMSZ, sdram_sz(-1));

		// send a reset
		user_io_8bit_set_status(UIO_STATUS_RESET, UIO_STATUS_RESET);
	}

	MiSTer_ini_parse();
	if (cfg.bootcore[0] != '\0')
	{
		bootcore_init(path);
	}

	video_mode_load();
	if(strlen(cfg.font)) LoadFont(cfg.font);
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

	case CORE_TYPE_MIST:
		puts("Identified MiST core");
		ikbd_init();
		tos_config_init();
		tos_upload(NULL);
		break;

	case CORE_TYPE_ARCHIE:
		puts("Identified Archimedes core");
		spi_uio_cmd16(UIO_SET_MEMSZ, sdram_sz(-1));
		send_rtc(1);
		user_io_set_core_name("Archie");
		archie_init();
		user_io_read_core_name();
		parse_config();
		break;

    case CORE_TYPE_SHARPMZ:
		puts("Identified Sharp MZ Series core");
		user_io_set_core_name("sharpmz");
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
			uint32_t status[2] = { 0, 0 };
			if (FileLoadConfig(name, status, 8))
			{
				printf("Found config: %08X-%08X\n", status[0], status[1]);
				status[0] &= ~UIO_STATUS_RESET;
				user_io_8bit_set_status(status[0], ~UIO_STATUS_RESET, 0);
				user_io_8bit_set_status(status[1], 0xffffffff, 1);
			}
			parse_config();

			if (is_menu_core())
			{
				user_io_8bit_set_status((cfg.menu_pal) ? 0x10 : 0, 0x10);
				if (cfg.fb_terminal) video_menu_bg((status[0] >> 1) & 7);
				else user_io_8bit_set_status(0, 0xE);
			}
			else
			{
				if (is_minimig())
				{
					puts("Identified Minimig V2 core");
					BootInit();
				}
				else if (is_x86_core())
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
								sprintf(mainpath, "%s/boot%i.rom", user_io_get_core_path(), i);
								user_io_file_tx(mainpath, i << 6);
							}
						}

						// legacy style of rom
						sprintf(mainpath, "%s/boot.rom", user_io_get_core_path());
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

						// cheats for boot file
						if (user_io_use_cheats()) cheats_init("", user_io_get_file_crc());
					}

					if (is_cpc_core())
					{
						for (int m = 0; m < 3; m++)
						{
							const char *model = !m ? "" : (m == 1) ? "0" : "1";
							sprintf(mainpath, "%s/boot%s.eZZ", user_io_get_core_path(), model);
							user_io_file_tx(mainpath, 0x40 * (m + 1), 0, 1);
							sprintf(mainpath, "%s/boot%s.eZ0", user_io_get_core_path(), model);
							user_io_file_tx(mainpath, 0x40 * (m + 1), 0, 1);
							for (int i = 0; i < 256; i++)
							{
								sprintf(mainpath, "%s/boot%s.e%02X", user_io_get_core_path(), model, i);
								user_io_file_tx(mainpath, 0x40 * (m + 1), 0, 1);
							}
						}
					}

					// check if vhd present
					sprintf(mainpath, "%s/boot.vhd", user_io_get_core_path());
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
		}

		send_rtc(3);

		// release reset
		user_io_8bit_set_status(0, UIO_STATUS_RESET);
		break;
	}

	OsdRotation((cfg.osd_rotate == 1) ? 3 : (cfg.osd_rotate == 2) ? 1 : 0);

	spi_uio_cmd_cont(UIO_GETUARTFLG);
	uart_mode = spi_w(0);
	DisableIO();

	uint32_t mode = 0;
	if (uart_mode)
	{
		sprintf(mainpath, "uartmode.%s", user_io_get_core_name_ex());
		FileLoadConfig(mainpath, &mode, 4);
	}

	char cmd[32];
	system("uartmode 0");

	SetMidiLinkMode((mode >> 8) & 0xFF);
	sprintf(cmd, "uartmode %d", mode & 0xFF);
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

void user_io_digital_joystick(unsigned char joystick, uint32_t map, int newdir)
{
	uint8_t joy = (joystick>1 || !joyswap) ? joystick : joystick ^ 1;

	// atari ST handles joystick 0 and 1 through the ikbd emulated by the io controller
	// but only for joystick 1 and 2
	if (core_type == CORE_TYPE_MIST)
	{
		ikbd_joystick(joy, (uint8_t)map);
		return;
	}

	spi_uio_cmd_cont((joy < 2) ? (UIO_JOYSTICK0 + joy) : (UIO_JOYSTICK2 + joy - 2));
	spi_w(map);
	if(joy_bcount>12) spi_w(map>>16);
	DisableIO();

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
uint16_t user_io_sd_get_status(uint32_t *lba, uint16_t *req_type)
{
	uint32_t s;
	uint16_t c;
	uint16_t req = 0;

	spi_uio_cmd_cont(UIO_GET_SDSTAT);
	if (io_ver)
	{
		c = spi_w(0);
		s = spi_w(0);
		s = (s & 0xFFFF) | (((uint32_t)spi_w(0))<<16);
		req = spi_w(0);
	}
	else
	{
		//note: using 32bit big-endian transfer!
		c = spi_in();
		s = spi_in();
		s = (s << 8) | spi_in();
		s = (s << 8) | spi_in();
		s = (s << 8) | spi_in();
		req = spi_in();
	}
	DisableIO();

	if (lba)
		*lba = s;

	if (req)
		*req_type = req;

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

void user_io_set_download(unsigned char enable)
{
	EnableFpga();
	spi8(UIO_FILE_TX);
	spi8(enable ? 0xff : 0x00);
	DisableFpga();
}

int user_io_file_mount(char *name, unsigned char index, char pre)
{
	int writable = 0;
	int ret = 0;

	if (!strcasecmp(user_io_get_core_name_ex(), "apple-ii"))
	{
		ret = dsk2nib(name, sd_image + index);
	}

	if (!ret)
	{
		if (x2trd_ext_supp(name))
		{
			ret = x2trd(name, sd_image + index);
		}
		else
		{
			writable = FileCanWrite(name);
			ret = FileOpenEx(&sd_image[index], name, writable ? (O_RDWR | O_SYNC) : O_RDONLY);
		}
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
		user_io_set_download(1);

		EnableFpga();
		spi8(UIO_FILE_TX_DAT);
		spi_write(col_attr, type ? 1024 : 1025, fio_size);
		DisableFpga();

		// signal end of transmission
		user_io_set_download(0);
	}
}

static uint32_t file_crc;
uint32_t user_io_get_file_crc()
{
	return file_crc;
}

int user_io_use_cheats()
{
	return use_cheats;
}

static void check_status_change()
{
	static u_int8_t last_status_change = 0;
	char stchg = spi_uio_cmd_cont(UIO_GET_STATUS);
	if ((stchg & 0xF0) == 0xA0 && last_status_change != (stchg & 0xF))
	{
		last_status_change = (stchg & 0xF);
		uint32_t st0 = spi32w(0);
		uint32_t st1 = spi32w(0);
		DisableIO();
		user_io_8bit_set_status(st0, ~UIO_STATUS_RESET, 0);
		user_io_8bit_set_status(st1, 0xFFFFFFFF, 1);
	}
	else
	{
		DisableIO();
	}
}

static char pchar[] = { 0x8C, 0x8F, 0x7F };

#define PROGRESS_CNT    28
#define PROGRESS_CHARS  (sizeof(pchar)/sizeof(pchar[0]))
#define PROGRESS_MAX    ((PROGRESS_CHARS*PROGRESS_CNT)-1)

static void tx_progress(const char* name, unsigned int progress)
{
	static char progress_buf[128];
	memset(progress_buf, 0, sizeof(progress_buf));

	if (progress > PROGRESS_MAX) progress = PROGRESS_MAX;
	char c = pchar[progress % PROGRESS_CHARS];
	progress /= PROGRESS_CHARS;

	char *buf = progress_buf;
	sprintf(buf, "\n\n %.27s\n ", name);
	buf += strlen(buf);

	for (unsigned int i = 0; i <= progress; i++) buf[i] = (i < progress) ? 0x7F : c;
	buf[PROGRESS_CNT] = 0;

	InfoMessage(progress_buf, 2000, "Loading");
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
	user_io_set_download(1);

	if (is_snes_core() && bytes2send)
	{
		printf("Load SNES ROM.\n");
		uint8_t* buf = snes_get_header(&f);
		hexdump(buf, 16, 0);
		EnableFpga();
		spi8(UIO_FILE_TX_DAT);
		spi_write(buf, 512, fio_size);
		DisableFpga();

		//strip original SNES ROM header if present (not used)
		if (bytes2send & 512)
		{
			bytes2send -= 512;
			FileReadSec(&f, buf);
		}
	}

	file_crc = 0;
	uint32_t skip = bytes2send & 0x3FF; // skip possible header up to 1023 bytes

	int use_progress = 1; // (bytes2send > (1024 * 1024)) ? 1 : 0;
	int size = bytes2send;
	int progress = -1;
	if (use_progress) MenuHide();

	while (bytes2send)
	{
		uint16_t chunk = (bytes2send > sizeof(buf)) ? sizeof(buf) : bytes2send;

		FileReadAdv(&f, buf, chunk);

		EnableFpga();
		spi8(UIO_FILE_TX_DAT);
		spi_write(buf, chunk, fio_size);
		DisableFpga();

		if (use_progress)
		{
			int new_progress = PROGRESS_MAX - ((((uint64_t)bytes2send)*PROGRESS_MAX) / size);
			if (progress != new_progress)
			{
				progress = new_progress;
				tx_progress(f.name, progress);
			}
		}
		bytes2send -= chunk;

		if (skip >= chunk) skip -= chunk;
		else
		{
			file_crc = crc32(file_crc, buf + skip, chunk - skip);
			skip = 0;
		}
	}

	// check if core requests some change while downloading
	check_status_change();

	printf("Done.\n");
	printf("CRC32: %08X\n", file_crc);

	FileClose(&f);

	if (opensave)
	{
		FileGenerateSavePath(name, (char*)buf);
		user_io_file_mount((char*)buf, 0, 1);
	}

	// signal end of transmission
	user_io_set_download(0);
	printf("\n");

	if (is_zx81_core() && index)
	{
		send_pcolchr(name, (index & 0x1F) | 0x20, 0);
		send_pcolchr(name, (index & 0x1F) | 0x60, 1);
	}
	return 1;
}

static char cfgstr[1024 * 10] = {};
void user_io_read_confstr()
{
	spi_uio_cmd_cont(UIO_GET_STRING);

	uint32_t j = 0;
	while (j < sizeof(cfgstr) - 1)
	{
		char i = spi_in();
		if (!i) break;
		cfgstr[j++] = i;
	}

	cfgstr[j++] = 0;
	DisableIO();
}

char *user_io_get_confstr(int index)
{
	unsigned char i, lidx = 0, j = 0;
	static char buffer[128 + 1];  // max 128 bytes per config item
								  // clear buffer
	buffer[0] = 0;
	int pos = 0;

	i = cfgstr[pos++];
	while (i && (j < sizeof(buffer)))
	{
		if (i == ';')
		{
			if (lidx == index) buffer[j++] = 0;
			lidx++;
		}
		else
		{
			if (lidx == index) buffer[j++] = i;
		}

		i = cfgstr[pos++];
	}

	// if this was the last string in the config string list, then it still
	// needs to be terminated
	if (lidx == index) buffer[j] = 0;

	// also return NULL for empty strings
	return buffer[0] ? buffer : NULL;
}

uint32_t user_io_8bit_set_status(uint32_t new_status, uint32_t mask, int ex)
{
	static uint32_t status[2] = { 0, 0 };
	if (ex) ex = 1;

	// if mask is 0 just return the current status
	if (mask) {
		// keep everything not masked
		status[ex] &= ~mask;
		// updated masked bits
		status[ex] |= new_status & mask;

		if (!io_ver)
		{
			spi_uio_cmd8(UIO_SET_STATUS, status[0]);
			spi_uio_cmd32(UIO_SET_STATUS2, status[0], 0);
		}
		else
		{
			spi_uio_cmd_cont(UIO_SET_STATUS2);
			spi32w(status[0]);
			spi32w(status[1]);
			DisableIO();
		}
	}

	return status[ex];
}

static char cur_btn = 0;
char user_io_menu_button()
{
	return (cur_btn & BUTTON_OSD) ? 1 : 0;
}

char user_io_user_button()
{
	return (cur_btn & BUTTON_USR) ? 1 : 0;
}

static char kbd_reset = 0;
void user_io_send_buttons(char force)
{
	static unsigned short key_map = 0;
	unsigned short map = 0;

	cur_btn = fpga_get_buttons();

	if (user_io_menu_button()) map |= BUTTON1;
	if (user_io_user_button()) map |= BUTTON2;
	if (kbd_reset) map |= BUTTON2;

	if (cfg.vga_scaler) map |= CONF_VGA_SCALER;
	if (cfg.vga_sog) map |= CONF_VGA_SOG;
	if (cfg.csync) map |= CONF_CSYNC;
	if (cfg.ypbpr) map |= CONF_YPBPR;
	if (cfg.forced_scandoubler) map |= CONF_FORCED_SCANDOUBLER;
	if (cfg.hdmi_audio_96k) map |= CONF_AUDIO_96K;
	if (cfg.dvi) map |= CONF_DVI;
	if (cfg.hdmi_limited & 1) map |= CONF_HDMI_LIMITED1;
	if (cfg.hdmi_limited & 2) map |= CONF_HDMI_LIMITED2;
	if (cfg.direct_video) map |= CONF_DIRECT_VIDEO;

	if ((map != key_map) || force)
	{
		if ((key_map & (BUTTON1 | BUTTON2)) == BUTTON2 && (map & (BUTTON1 | BUTTON2)) == (BUTTON1 | BUTTON2) && is_menu_core())
		{
			if (FileExists(ini_cfg.filename_alt))
			{
				altcfg(altcfg() ? 0 : 1);
				fpga_load_rbf("menu.rbf");
			}
		}

		const char *name = get_rbf_path();
		if (name[0] && (get_key_mod() & (LGUI | LSHIFT)) == (LGUI | LSHIFT) && (key_map & BUTTON2) && !(map & BUTTON2))
		{
			uint16_t sz = sdram_sz(-1);
			if (sz & 0x4000) sz++;
			else sz = 0x4000;
			sdram_sz(sz);
			fpga_load_rbf(name);
		}

		//special reset for some cores
		if ((key_map & BUTTON2) && !(map & BUTTON2))
		{
			if (is_archie()) fpga_load_rbf(name[0] ? name : "Archie.rbf");
			if (is_minimig()) minimig_reset();
			if (is_megacd_core()) mcd_reset();
		}

		key_map = map;
		spi_uio_cmd16(UIO_BUT_SW, map);
		printf("sending keymap: %X\n", map);
		if ((key_map & BUTTON2) && is_x86_core()) x86_init();
	}
}

static uint32_t diskled_timer = 0;
static uint32_t diskled_is_on = 0;
void diskled_on()
{
	fpga_set_led(1);
	diskled_timer = GetTimer(50);
	diskled_is_on = 1;
}

static void kbd_reply(char code)
{
	printf("kbd_reply = 0x%02X\n", code);
	spi_uio_cmd16(UIO_KEYBOARD, 0xFF00 | code);
}

static void mouse_reply(char code)
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

static uint32_t res_timer = 0;

uint8_t cd_lba_to_track(uint32_t req_lba) {
	uint8_t track=1;
	for (track=1; track<=cd_last_track; track++) {
		uint32_t toc_lba = msf_to_lba(cd_trackinfo[track].ind1_m, cd_trackinfo[track].ind1_s, cd_trackinfo[track].ind1_f);	// Convert track MSF to LBA.
		//printf("TOC Track LBA: %08d\n", toc_lba);
		if (req_lba > toc_lba) continue;	// See if the TOC LBA is > the requested LBA.
		else break;
	}
	return track-1;	// The start LBA of the PREVIOUS track checked was lower than our requested LBA.
}

int cue_pt = 0;
char cue_getch()
{
	static uint8_t buf[512];
	if (!(cue_pt & 0x1ff)) FileReadSec(&sd_image[2], buf);
	if (cue_pt >= sd_image[2].size) return 0;
	return buf[(cue_pt++) & 0x1ff];
}

char cue_readline(char *buffer)
{
	char my_char = 0;
	bool ret = 0;
	int char_count = 0;

	for (int i=0; i<1024; i++) {
		ret = ( my_char = cue_getch() );
		if (my_char!=0x20) {	// Ditch the spaces.
			buffer[char_count] = my_char;
			if (ret==0 || my_char==0x0A) {
				buffer[char_count+1] = 0x00;	// Null terminator.
				break;
			}
			else char_count++;
		}
	}
	return ret;
}

void parse_cue_file(void)
{
	 int i_num, i_min, i_sec, i_frame, bytes_per_sec = 0;

	// Clear the trackinfo before starting.
	for (int i=0;i<=99;i++)
	{
		cd_trackinfo[i].track_active = 0;
		cd_trackinfo[i].pregap_present = 0;
		cd_trackinfo[i].pre_m = 0;
		cd_trackinfo[i].pre_s = 0;
		cd_trackinfo[i].pre_f = 0;
		cd_trackinfo[i].ind0_present = 0;
		cd_trackinfo[i].ind0_m = 0;
		cd_trackinfo[i].ind0_s = 0;
		cd_trackinfo[i].ind0_f = 0;
		cd_trackinfo[i].ind1_m = 0;
		cd_trackinfo[i].ind1_s = 0;
		cd_trackinfo[i].ind1_f = 0;
		cd_trackinfo[i].type = 0;
		cd_trackinfo[i].bytes_per_sec = 0;
	}

	size_t i_tracks = 0;
	char str[1024];
	char type[5];

	cue_pt = 0;	// Set cue file index to zero.

	int track_num = 0;
	bool first_track_done = 0;

	// Note: strncmp==0 means a MATCH! Because reasons.

    while( i_tracks < 99 )
    {
		if ( !cue_readline(str) ) break;	// Read in a whole line from the CUE file (until the end of the file).

		if ( strncmp(str, "TRACK", 5)==0 ) {	// Is this a track?
			sscanf( str, "%*5s%2u%5s%*1s%4u", &track_num, type, &bytes_per_sec);
			if (!first_track_done) {
				first_track_done = 1;
				cd_first_track = track_num;
			}
		}

		if ( strncmp(str, "PREGAP", 6)==0 )
		{
			sscanf( str, "%*6s%2u:%2u:%2u", &i_min, &i_sec, &i_frame );
			cd_trackinfo[track_num].pregap_present = 1;
			cd_trackinfo[track_num].pre_m = i_min;
			cd_trackinfo[track_num].pre_s = i_sec;
			cd_trackinfo[track_num].pre_f = i_frame;
		}

		if ( strncmp(str, "INDEX", 5)==0 )		// Is this an Index?
        {
			sscanf( str, "%*5s%2u%2u:%2u:%2u", &i_num, &i_min, &i_sec, &i_frame );

			cd_trackinfo[track_num].track_active = 1;
			if ( strcmp(type, "AUDIO")==0 ) {
				cd_trackinfo[track_num].type = 0;
				bytes_per_sec = 2352;		// Audio tracks assume 2352 bytes per sector, so it's not listed in the CUE file.
			}
			else if ( strcmp(type, "MODE1")==0 ) {
				cd_trackinfo[track_num].type = 4;
			}
			cd_trackinfo[track_num].bytes_per_sec = bytes_per_sec;

			/*
			if (i_num==0) {	// "Pregap" index, sort of.
				printf("Track:%02d  Pregap:%d  M:%02d  S:%02d  F:%02d  Type:%s  TOCtype:%d  BPS:%04d\n", track_num, cd_trackinfo[track_num].pregap_present, i_min, i_sec, i_frame, type, cd_trackinfo[track_num].type, bytes_per_sec);
				cd_trackinfo[track_num].ind0_m = i_min;
				cd_trackinfo[track_num].ind0_s = i_sec;
				cd_trackinfo[track_num].ind0_f = i_frame;
			}
			*/

			if (i_num==1) {	// "Track Start" index.
				printf("Track:%02d  Pregap:%d  M:%02d  S:%02d  F:%02d  Type:%s  TOCtype:%d  BPS:%04d\n", track_num, cd_trackinfo[track_num].pregap_present, i_min, i_sec, i_frame, type, cd_trackinfo[track_num].type, bytes_per_sec);
				cd_trackinfo[track_num].ind1_m = i_min;
				cd_trackinfo[track_num].ind1_s = i_sec;
				cd_trackinfo[track_num].ind1_f = i_frame;
			}
        }
		i_tracks++;
    }
	cd_last_track = track_num;
}

void cd_generate_toc(uint16_t req_type, uint8_t *buffer)
{
	uint8_t m,s,f;
	uint32_t lba;

	switch ( (req_type&0xFF00)>>8 ) {
		case 0xD0: {	// Request First Track and Last Track (BCD).
			//buffer[0] = 0x01;	// Rondo - First track (BCD).
			//buffer[1] = 0x22;	// Rondo - Last track (BCD).
			buffer[0] = dec_2_bcd( cd_first_track );
			buffer[1] = dec_2_bcd( cd_last_track );
			buffer[2] = 0x00;	// Padding.
			buffer[3] = 0x00;	// Padding.
			printf("Core requesting CD TOC0. First Track:%02X. Last Track:%02X (BCD)\n", buffer[0], buffer[1]);
		}; break;

		case 0xD1: {	// Request Total Disk Size (MSF, in BCD).
			//buffer[0] = 0x49;	// Rondo - Minutes = 0x49 (73).
			//buffer[1] = 0x09;	// Rondo - Seconds = 0x09 (9).
			//buffer[2] = 0x12;	// Rondo - Frames = 0x12 (18).

			// ADD the PREGAP (if present).
			/*
			if (buffer[3]==4 && cd_trackinfo[track].pregap_present) {
				m = cd_trackinfo[cd_last_track].ind1_m + cd_trackinfo[cd_last_track].pre_m;
				s = cd_trackinfo[cd_last_track].ind1_s + cd_trackinfo[cd_last_track].pre_s;
				f = cd_trackinfo[cd_last_track].ind1_f + cd_trackinfo[cd_last_track].pre_f;
				// Not sure if audio tracks need the 2-second lead-in offset added? ElectronAsh.
				uint32_t lba = msf_to_lba(m, s, f);	// Convert to LBA, so we can add the 2-second lead-in.
				//lba += 2*75;						// Standard lead-in is 2 seconds (75 sectors per second, so 150).
				// Convert back from LBA to MSF...
				m = lba / (60 * 75);
				lba -= m * (60 * 75);
				s = lba / 75;
				f = lba % 75;

				buffer[0] = dec_2_bcd( m );
				buffer[1] = dec_2_bcd( s );
				buffer[2] = dec_2_bcd( f );
			}
			else
			{*/
				buffer[0] = dec_2_bcd( cd_trackinfo[cd_last_track].ind1_m );
				buffer[1] = dec_2_bcd( cd_trackinfo[cd_last_track].ind1_s );
				buffer[2] = dec_2_bcd( cd_trackinfo[cd_last_track].ind1_f );
			//}
			buffer[3] = 0x00;	// Padding.

			printf("Core requesting CD TOC1. Total Disk Size:M:%02X S:%02X F:%02X (BCD)\n", buffer[0], buffer[1], buffer[2]);
		}; break;

		case 0xD2: {	// Request Track Info (Start MSF in BCD, and track type).
			uint8_t track = bcd_2_dec(req_type&0xFF);	// Track number from req_type upper byte is in BCD!

			//  If a DATA track, check for a pregap, and ADD it (if present).
			if (cd_trackinfo[track].type==4 && cd_trackinfo[track].pregap_present) {
				m = cd_trackinfo[track].ind1_m + cd_trackinfo[track].pre_m;
				s = cd_trackinfo[track].ind1_s + cd_trackinfo[track].pre_s;
				f = cd_trackinfo[track].ind1_f + cd_trackinfo[track].pre_f;

				lba = msf_to_lba(m, s, f);	// Convert to LBA, so we can add the 2-second lead-in.
				lba += 2*75;				// Standard lead-in is 2 seconds (75 sectors per second, so 150).

				// Convert back from LBA to MSF...
				m = lba / (60 * 75);
				lba -= m * (60 * 75);
				s = lba / 75;
				f = lba % 75;

				buffer[0] = dec_2_bcd( m );
				buffer[1] = dec_2_bcd( s );
				buffer[2] = dec_2_bcd( f );
			}
			else
			{
				buffer[0] = dec_2_bcd( cd_trackinfo[track].ind1_m );
				buffer[1] = dec_2_bcd( cd_trackinfo[track].ind1_s );
				buffer[2] = dec_2_bcd( cd_trackinfo[track].ind1_f );
			}
			buffer[3] = cd_trackinfo[track].type;

			printf("Core requesting CD TOC2. Track:%02d. M:%02X S:%02X F:%02X (BCD). Type:", track, buffer[0], buffer[1], buffer[2]);
			if (buffer[3]==0x00) printf("AUDIO\n");
			else if (buffer[3]==0x04) printf("DATA\n");
			else printf("UNKNOWN!\n");
		}; break;
	}
}

void user_io_poll()
{
	if ((core_type != CORE_TYPE_MIST) &&
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

	if (is_minimig())
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

	if (core_type == CORE_TYPE_8BIT && !is_menu_core())
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

		check_status_change();
	}

	// sd card emulation
	if (is_x86_core())
	{
		x86_poll();
	}
	else if ((core_type == CORE_TYPE_8BIT || core_type == CORE_TYPE_ARCHIE) && !is_menu_core() && !is_minimig())
	{
		static uint8_t buffer[4][512];
		uint32_t lba;
		uint16_t req_type = 0;
		uint16_t c = user_io_sd_get_status(&lba, &req_type);
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

				//printf("SD RD %d on %d, WIDE=%d\n", lba, disk, fio_size);

				int done = 0;
				if (disk && is_neogeo_core())
				{
					uint32_t offset = 0;

					if (sd_image[disk].size)
					{
						diskled_on();
						printf("req_type: 0x%04X  ", req_type);
						switch ((req_type & 0xFF00) >> 8)
						{
						case 0xD0:case 0xD1:case 0xD2:
						{
							cd_generate_toc(req_type, buffer[disk]);
							done = 1;
						};
						break;

						case 0x48:
						{
							// Added this, Neo CD always requests by MSF (furrtek)
							if ((req_type & 0xFF) == 0x01)
							{
								printf("Neo CD requested raw lba value (MSF): 0x%08X\n", lba);
								uint8_t m = bcd_2_dec((lba & 0xFF0000) >> 16);
								uint8_t s = bcd_2_dec((lba & 0xFF00) >> 8);
								uint8_t f = bcd_2_dec((lba & 0xFF) >> 0);
								lba = msf_to_lba(m, s, f);
								lba -= (2 * 75); // Remove 2 second pregap
							}

							uint8_t track = cd_lba_to_track(lba);
							uint16_t bps = cd_trackinfo[track].bytes_per_sec;
							uint32_t pregap = 0;

							if (cd_trackinfo[track].pregap_present)
							{
								pregap = msf_to_lba(cd_trackinfo[track].pre_m, cd_trackinfo[track].pre_s, cd_trackinfo[track].pre_f);
							}

							if (bps == 2352) offset = 16 + ((lba - pregap) * 2352);		// Rondo etc.
							else if (bps == 2048) offset = ((lba - pregap) * 2048);	// Homebrew, etc.
							else printf("Data track %02d has unhandled bytes-per-sec of %d !\n", track, bps);

							if (FileSeek(&sd_image[disk], offset, SEEK_SET))
							{
								if (FileReadAdv(&sd_image[disk], buffer[disk], 2048)) done = 1;
							}
							printf("Core requesting 2048-byte CD sector, from LBA: 0x%08X  TRACK: %02d  BPS: %04d  OFFSET: 0x%08X \n", lba, track, bps, offset);
						};
						break;

						case 0x52:
						{
							switch (req_type & 0xFF)
							{
								// "lba" holds the LBA. Dun do nothing. (no conversion needed).
							case 0x00:
								break;

								// "lba" holds the MSF (BCD). Convert to LBA.
							case 0x01:
							{
								uint8_t m = bcd_2_dec((lba & 0xFF0000) >> 16);
								uint8_t s = bcd_2_dec((lba & 0xFF00) >> 8);
								uint8_t f = bcd_2_dec((lba & 0xFF) >> 0);
								lba = msf_to_lba(m, s, f);
							};
							break;

							// "lba" holds the TRACK number (BCD?). Grab the track start MSF from the TOC, then convert to LBA.
							case 0x02:
							{
								uint8_t track = bcd_2_dec(lba);
								lba = msf_to_lba(cd_trackinfo[track].ind1_m, cd_trackinfo[track].ind1_s, cd_trackinfo[track].ind1_f);
							};
							break;
							}

							uint8_t track = cd_lba_to_track(lba);

							if (cd_trackinfo[track].type != 0x00)
							{
								printf("Error: Core is trying to play back non-audio track as CDDA!\n");
								memset(buffer[disk], 0, sizeof(buffer[disk]));
							}
							else
							{
								if (FileSeek(&sd_image[disk], (lba - 525) * 2352, SEEK_SET))
								{
									if (FileReadAdv(&sd_image[disk], buffer[disk], 2352)) done = 1;
								}
							}
							printf("Core requesting a raw 2352-byte CD sector, from LBA: 0x%08X  TRACK: %02d\n", lba, track);
						};
						break;

						default:
						{
							if (FileSeekLBA(&sd_image[disk], lba))
							{
								if (FileReadSec(&sd_image[disk], buffer[disk])) done = 1;
							}
							printf("Core requesting a 512-byte SD / VHD sector, from LBA: 0x%08X\n", lba);
						};
						break;
						}
					}

					//Even after error we have to provide the block to the core
					//Give an empty block.
					if (!done) memset(buffer[disk], 0, sizeof(buffer[disk]));
					buffer_lba[disk] = lba;

					spi_uio_cmd_cont(UIO_SECTOR_RD);
					if ((req_type & 0xF000) == 0xD000) spi_write(buffer[disk], 4, fio_size);			// TOC. (4 bytes, including padding).
					else if ((req_type & 0xFF00) == 0x4800) spi_write(buffer[disk], 2048, fio_size);	// 2048-byte CD sector.
					else if ((req_type & 0xFF00) == 0x5200) spi_write(buffer[disk], 2352, fio_size);	// 2352-byte CD sector.
					else spi_write(buffer[disk], 512, fio_size);									// Standard 512-byte SD / VHD sector.
					DisableIO();
				}
				else
				{
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
						if (!done)
						{
							if (sd_image[disk].type == 2)
							{
								if (is_megacd_core())
								{
									mcd_fill_blanksave(buffer[disk], lba);
								}
								else
								{
									memset(buffer[disk], -1, sizeof(buffer[disk]));
								}
							}
							else
							{
								memset(buffer[disk], 0, sizeof(buffer[disk]));
							}
						}
						buffer_lba[disk] = lba;
					}

					if (buffer_lba[disk] == lba)
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
					if (done) buffer_lba[disk] = lba + 1;

					if (sd_image[disk].type == 2)
					{
						buffer_lba[disk] = -1;
					}
				}
			}
		}
	}

	if (core_type == CORE_TYPE_8BIT && !is_menu_core() && !is_minimig())
	{
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

				int16_t ps2_wheel = mouse_wheel;
				if (ps2_wheel > 63) ps2_wheel = 63;
				else if (ps2_wheel < -63) ps2_wheel = -63;

				// collect movement info and send at predefined rate
				if (is_menu_core() && !video_fb_state()) printf("PS2 MOUSE: %x %d %d %d\n", ps2_mouse[0], ps2_mouse[1], ps2_mouse[2], ps2_wheel);

				if (!osd_is_visible)
				{
					spi_uio_cmd_cont(UIO_MOUSE);
					spi_w(ps2_mouse[0] | ((ps2_wheel&127)<<8));
					spi_w(ps2_mouse[1]);
					spi_w(ps2_mouse[2]);
					DisableIO();
				}

				// reset counters
				mouse_flags = 0;
				mouse_pos[X] = mouse_pos[Y] = mouse_wheel = 0;
			}
		}
	}

	if (is_neogeo_core() && (!rtc_timer || CheckTimer(rtc_timer)))
	{
		// Update once per minute should be enough
		rtc_timer = GetTimer(60000);
		send_rtc(1);
	}

	if (core_type == CORE_TYPE_ARCHIE) archie_poll();
	if (core_type == CORE_TYPE_SHARPMZ) sharpmz_poll();

	static uint8_t leds = 0;
	if(use_ps2ctl && !is_minimig())
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

	if (CheckTimer(led_timer) && !is_menu_core())
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
		if (is_menu_core())
		{
			static int got_cfg = 0;
			if (!got_cfg)
			{
				spi_uio_cmd_cont(UIO_GET_OSDMASK);
				sdram_cfg = spi_w(0);
				DisableIO();

				if (sdram_cfg & 0x8000)
				{
					got_cfg = 1;
					printf("*** Got SDRAM module type: %d\n", sdram_cfg & 7);
					switch (user_io_get_sdram_cfg() & 7)
					{
					case 7:
						sdram_sz(3);
						break;
					case 3:
						sdram_sz(2);
						break;
					case 1:
						sdram_sz(1);
						break;
					default:
						sdram_sz(0);
					}
				}
			}
		}

		res_timer = GetTimer(500);
		if (!minimig_get_adjust())
		{
			video_mode_adjust();
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

	if (diskled_is_on && CheckTimer(diskled_timer))
	{
		fpga_set_led(0);
		diskled_is_on = 0;
	}

	if (is_megacd_core()) mcd_poll();
}

static void send_keycode(unsigned short key, int press)
{
	if (is_minimig())
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
		if (minimig_get_adjust())
		{
			if (code == 0x44)
			{
				minimig_set_adjust(0);
				res_timer = 0;
				return;
			}

			if (code == 0x45)
			{
				Info("Canceled");
				res_timer = 0;
				minimig_set_adjust(2);
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

void user_io_mouse(unsigned char b, int16_t x, int16_t y, int16_t w)
{
	switch (core_type)
	{
	case CORE_TYPE_8BIT:
		if (is_minimig())
		{
			mouse_pos[X] += x;
			mouse_pos[Y] += y;
			mouse_flags |= 0x80 | (b & 7);
		}
		else
		{
			mouse_pos[X] += x;
			mouse_pos[Y] -= y;  // ps2 y axis is reversed over usb
			mouse_wheel += w;
			mouse_flags |= 0x08 | (b & 7);
		}
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
		case CORE_TYPE_ARCHIE:
		case CORE_TYPE_8BIT:
			if(is_minimig()) minimig_reset();
			else kbd_reset = 1;
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
	//printf("OSD is now %s\n", on ? "visible" : "invisible");
	osd_is_visible = on;
	input_switch(-1);
}

int get_volume()
{
	return vol_att & 0x17;
}

void set_volume(int cmd)
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
	if(is_menu_core()) spi_uio_cmd(UIO_KEYBOARD); //ping the Menu core to wakeup

	// Win+PrnScr or Alt/Win+ScrLk - screen shot
	if ((key == KEY_SYSRQ && (get_key_mod() & (RGUI | LGUI))) || (key == KEY_SCROLLLOCK && (get_key_mod() & (LALT | RALT | RGUI | LGUI))))
	{
		if (press == 1)
		{
			printf("print key pressed - do screen shot\n");
			mister_scaler *ms = mister_scaler_init();
			if (ms == NULL)
			{
				printf("problem with scaler, maybe not a new enough version\n");
				Info("Scaler not compatible");
			}
			else
			{
				unsigned char *outputbuf = (unsigned char *)calloc(ms->width*ms->height * 3, 1);
				mister_scaler_read(ms, outputbuf);
				static char filename[1024];
				FileGenerateScreenshotName(last_filename, filename, 1024);
				unsigned error = lodepng_encode24_file(getFullPath(filename), outputbuf, ms->width, ms->height);
				if (error) {
					printf("error %u: %s\n", error, lodepng_error_text(error));
					printf("%s", filename);
					Info("error in saving png");
				}
				free(outputbuf);
				mister_scaler_free(ms);
				char msg[1024];
				snprintf(msg, 1024, "Screen saved to\n%s", filename + strlen(SCREENSHOT_DIR"/"));
				Info(msg);
			}
		}

	}
	else
	if (key == KEY_MUTE)
	{
		if (press == 1 && hasAPI1_5()) set_volume(0);
	}
	else
	if (key == KEY_VOLUMEDOWN)
	{
		if (press && hasAPI1_5()) set_volume(-1);
	}
	else
	if (key == KEY_VOLUMEUP)
	{
		if (press && hasAPI1_5()) set_volume(1);
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
	{
		if (key)
		{
			uint32_t code = get_ps2_code(key);
			if (!press)
			{
				if (is_menu_core() && !video_fb_state()) printf("PS2 code(break)%s for core: %d(0x%X)\n", (code & EXT) ? "(ext)" : "", code & 255, code & 255);

				if (key == KEY_MENU) key = KEY_F12;
				if (osd_is_visible) menu_key_set(UPSTROKE | key);

				//don't block depress so keys won't stick in core if pressed before OSD.
				send_keycode(key, press);
			}
			else
			{
				if (is_menu_core() && !video_fb_state()) printf("PS2 code(make)%s for core: %d(0x%X)\n", (code & EXT) ? "(ext)" : "", code & 255, code & 255);
				if (!osd_is_visible && !is_menu_core() && key == KEY_MENU && press == 3) open_joystick_setup();
				else if ((has_menu() || osd_is_visible || (get_key_mod() & (LALT | RALT | RGUI | LGUI))) && (((key == KEY_F12) && ((!is_x86_core() && !is_archie()) || (get_key_mod() & (RGUI | LGUI)))) || key == KEY_MENU)) menu_key_set(KEY_F12);
				else if (osd_is_visible)
				{
					if (press == 1) menu_key_set(key);
				}
				else
				{
					if (((code & EMU_SWITCH_1) || ((code & EMU_SWITCH_2) && !use_ps2ctl && !is_archie())) && !is_menu_core())
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
						if(input_state()) send_keycode(key, press);
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
				if (ext[i] == '*') break;
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

uint16_t user_io_get_sdram_cfg()
{
	return sdram_cfg;
}
