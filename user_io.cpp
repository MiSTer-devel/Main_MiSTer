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

#include "lib/imlib2/Imlib2.h"

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
#include "audio.h"
#include "shmem.h"
#include "ide.h"

#include "support.h"

static char core_path[1024] = {};
static char rbf_path[1024] = {};

static fileTYPE sd_image[16] = {};
static int      sd_type[16] = {};
static int      sd_image_cangrow[16] = {};
static uint64_t buffer_lba[16] = { ULLONG_MAX,ULLONG_MAX,ULLONG_MAX,ULLONG_MAX,
								   ULLONG_MAX,ULLONG_MAX,ULLONG_MAX,ULLONG_MAX,
								   ULLONG_MAX,ULLONG_MAX,ULLONG_MAX,ULLONG_MAX,
								   ULLONG_MAX,ULLONG_MAX,ULLONG_MAX,ULLONG_MAX };

static int use_save = 0;

// mouse and keyboard emulation state
static int emu_mode = EMU_NONE;

// keep state over core type and its capabilities
static unsigned char core_type = CORE_TYPE_UNKNOWN;
static unsigned char dual_sdr = 0;

static int fio_size = 0;
static int io_ver = 0;

// keep state of caps lock
static char caps_lock_toggle = 0;

#define LED_FREQ 100   // 100 ms
static unsigned long led_timer;
static char keyboard_leds = 0;
static bool caps_status = 0;
static bool num_status = 0;
static bool scrl_status = 0;

static uint16_t sdram_cfg = 0;

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

static char core_name[32] = {};
static char ovr_name[32] = {};
static char orig_name[32] = {};

static char filepath_store[1024];

char *user_io_make_filepath(const char *path, const char *filename)
{
	snprintf(filepath_store, 1024, "%s/%s", path, filename);

	return filepath_store;
}

void user_io_name_override(const char* name)
{
	snprintf(ovr_name, sizeof(ovr_name), "%s", name);
}

void user_io_set_core_name(const char *name)
{
	snprintf(core_name, sizeof(core_name), name);
	printf("Core name set to \"%s\"\n", core_name);
}

char *user_io_get_core_name(int orig)
{
	return orig ? orig_name : core_name;
}

char *user_io_get_core_path(const char *suffix, int recheck)
{
	static char old_name[256] = {};
	static char tmp[1024] = {};

	if (!suffix) suffix = (!strcasecmp(core_name, "minimig")) ? "Amiga" : core_name;
	if (recheck || strcmp(old_name, suffix) || !tmp[0])
	{
		strcpy(old_name, suffix);
		strcpy(tmp, suffix);
		prefixGameDir(tmp, sizeof(tmp));
	}

	return tmp;
}

static char is_arcade_type = 0;
char is_arcade()
{
	return is_arcade_type;
}

static int is_menu_type = 0;
char is_menu()
{
	if (!is_menu_type) is_menu_type = strcasecmp(core_name, "MENU") ? 2 : 1;
	return (is_menu_type == 1);
}

static int is_x86_type = 0;
char is_x86()
{
	if (!is_x86_type) is_x86_type = strcasecmp(core_name, "AO486") ? 2 : 1;
	return (is_x86_type == 1);
}

static int is_snes_type = 0;
char is_snes()
{
	if (!is_snes_type) is_snes_type = strcasecmp(core_name, "SNES") ? 2 : 1;
	return (is_snes_type == 1);
}

static int is_cpc_type = 0;
char is_cpc()
{
	if (!is_cpc_type) is_cpc_type = strcasecmp(core_name, "amstrad") ? 2 : 1;
	return (is_cpc_type == 1);
}

static int is_zx81_type = 0;
char is_zx81()
{
	if (!is_zx81_type) is_zx81_type = strcasecmp(core_name, "zx81") ? 2 : 1;
	return (is_zx81_type == 1);
}

static int is_neogeo_type = 0;
char is_neogeo()
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
char is_megacd()
{
	if (!is_megacd_type) is_megacd_type = strcasecmp(core_name, "MEGACD") ? 2 : 1;
	return (is_megacd_type == 1);
}

static int is_pce_type = 0;
char is_pce()
{
	if (!is_pce_type) is_pce_type = strcasecmp(core_name, "TGFX16") ? 2 : 1;
	return (is_pce_type == 1);
}

static int is_archie_type = 0;
char is_archie()
{
	if (!is_archie_type) is_archie_type = strcasecmp(core_name, "ARCHIE") ? 2 : 1;
	return (is_archie_type == 1);
}

static int is_gba_type = 0;
char is_gba()
{
	if (!is_gba_type) is_gba_type = strcasecmp(core_name, "GBA") ? 2 : 1;
	return (is_gba_type == 1);
}

static int is_c64_type = 0;
char is_c64()
{
	if (!is_c64_type) is_c64_type = strcasecmp(core_name, "C64") ? 2 : 1;
	return (is_c64_type == 1);
}

static int is_psx_type = 0;
char is_psx()
{
	if (!is_psx_type) is_psx_type = strcasecmp(core_name, "PSX") ? 2 : 1;
	return (is_psx_type == 1);
}

static int is_st_type = 0;
char is_st()
{
	if (!is_st_type) is_st_type = strcasecmp(core_name, "AtariST") ? 2 : 1;
	return (is_st_type == 1);
}

char is_sharpmz()
{
	return(core_type == CORE_TYPE_SHARPMZ);
}

static int is_electron_type = 0;
char is_electron()
{
	if (!is_electron_type) is_electron_type = strcasecmp(core_name, "AcornElectron") ? 2 : 1;
	return (is_electron_type == 1);
}

static int is_no_type = 0;
static int disable_osd = 0;
char has_menu()
{
	if (disable_osd) return 0;

	if (!is_no_type) is_no_type = user_io_get_core_name()[0] ? 1 : 2;
	return (is_no_type == 1);
}

void user_io_read_core_name()
{
	is_menu_type = 0;
	is_x86_type  = 0;
	is_no_type   = 0;
	is_snes_type = 0;
	is_cpc_type = 0;
	is_zx81_type = 0;
	is_neogeo_type = 0;
	is_minimig_type = 0;
	is_megacd_type = 0;
	is_pce_type = 0;
	is_archie_type = 0;
	is_gba_type = 0;
	is_c64_type = 0;
	is_st_type = 0;
	core_name[0] = 0;

	char *p = user_io_get_confstr(0);
	if (p && p[0]) snprintf(orig_name, sizeof(orig_name), "%s", p);

	// get core name
	if (ovr_name[0]) strcpy(core_name, ovr_name);
	else if (orig_name[0]) strcpy(core_name, p);

	printf("Core name is \"%s\"\n", core_name);
}

int substrcpy(char *d, const char *s, char idx)
{
	char p = 0;
	char *b = d;

	while (*s)
	{
		if ((p == idx) && *s && (*s != ',')) *d++ = *s;

		if (*s == ',')
		{
			if (p == idx) break;
			p++;
		}

		s++;
	}

	*d = 0;
	return (int)(d - b);
}

static char cur_status[16] = {};

int user_io_status_bits(const char *opt, int *s, int *e, int ex, int single)
{
	uint32_t start = 0, end = 0;
	if (opt[0] == '[')
	{
		if (!single && sscanf(opt, "[%u:%u]", &end, &start) == 2)
		{
			if (start > 127 || end > 127 || end <= start) return 0;
		}
		else if (sscanf(opt, "[%u]", &start) == 1)
		{
			if (start > 127) return 0;
			end = start;
		}
		else return 0;
	}
	else
	{
		if ((opt[0] >= '0') && (opt[0] <= '9')) start = opt[0] - '0';
		else if ((opt[0] >= 'A') && (opt[0] <= 'V')) start = opt[0] - 'A' + 10;
		else return 0;

		if (!single && (opt[1] >= '0') && (opt[1] <= '9')) end = opt[1] - '0';
		else if (!single && (opt[1] >= 'A') && (opt[1] <= 'V')) end = opt[1] - 'A' + 10;
		else
		{
			single = 1;
			end = start;
		}

		if (ex)
		{
			start += 32;
			end += 32;
		}

		if (start > 127 || end > 127 || (!single && end <= start)) return 0;
	}

	//max 8 bits per option
	if (end - start > 8) return 0;

	if (s) *s = (int)start;
	if (e) *e = (int)end;
	return 1 + end - start;
}

uint32_t user_io_status_get(const char *opt, int ex)
{
	int start, end;
	int size = user_io_status_bits(opt, &start, &end, ex);
	if (!size) return 0;

	uint32_t x = (cur_status[end / 8] << 8) | cur_status[start / 8];
	x >>= start % 8;
	return x & ~(0xffffffff << size);
}

uint32_t user_io_status_mask(const char *opt)
{
	return ~(0xffffffff << user_io_status_bits(opt, 0, 0, 0));
}

uint32_t user_io_hd_mask(const char *opt)
{
	int start;
	int size = user_io_status_bits(opt, &start, 0, 0, 1);
	if (!size) return 0;
	return start;
}

void user_io_status_set(const char *opt, uint32_t value, int ex)
{
	int start, end;
	int size = user_io_status_bits(opt, &start, &end, ex);
	if (!size) return;

	int s = start / 8;
	int e = end / 8;

	uint32_t mask = ~(0xffffffff << size);
	mask <<= start % 8;
	uint32_t x = (cur_status[e] << 8) | cur_status[s];
	x = (x & ~mask) | ((value << (start % 8)) & mask);

	cur_status[s] = (char)x;
	if (e != s) cur_status[e] = (char)(x >> 8);

	if (!is_st())
	{
		spi_uio_cmd_cont(UIO_SET_STATUS2);
		for (uint32_t i = 0; i < sizeof(cur_status); i += 2) spi_w((cur_status[i + 1] << 8) | cur_status[i]);
		DisableIO();
	}
}

int user_io_status_save(const char *filename)
{
	return FileSaveConfig(filename, cur_status, sizeof(cur_status));
}

void user_io_status_reset()
{
	memset(cur_status, 0, sizeof(cur_status));
	user_io_status_set("[0]", 0);
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
static uint32_t ss_base = 0;
static uint32_t ss_size = 0;
static uint32_t uart_speeds[13] = {};
static char uart_speed_labels[13][32] = {};
static uint32_t midi_speeds[13] = {};
static char midi_speed_labels[13][32] = {};
static const uint32_t mlink_speeds[13] = { 110, 300, 600, 1200, 2400, 4800, 9600, 14400, 19200, 31250, 38400, 57600, 115200 };
static const char mlink_speed_labels[13][32] = { "110", "300", "600", "1200", "2400", "4800", "9600", "14400", "19200", "31250/MIDI", "38400", "57600", "115200" };
static char defmra[1024] = {};

static void parse_config()
{
	char mask[sizeof(cur_status) * 8] = {};
	char overlap[sizeof(cur_status) * 8] = {};
	int start, end, sz;

	int i = 0;
	char *p;

	joy_force = 0;
	joy_bcount = 0;

	do {
		p = user_io_get_confstr(i);
		printf("get cfgstring %d = %s\n", i, p ? p : "NULL");
		if (!i)
		{
			OsdCoreNameSet((p && p[0]) ? p : "CORE");
		}

		if (i == 1 && p)
		{
			while (p && *p)
			{
				if (!strncasecmp(p, "SS", 2))
				{
					char *end = 0;
					ss_base = strtoul(p+2, &end, 16);
					p = end;
					if (p && *p == ':')
					{
						p++;
						ss_size = strtoul(p, &end, 16);
					}

					printf("Got save state parameters: base=0x%X, size=0x%X\n", ss_base, ss_size);

					if (!ss_size || ss_size > (128 * 1024 * 1024))
					{
						ss_size = 0;
						ss_base = 0;
						printf("Invalid size!\n");
					}
					else if (ss_base < 0x20000000 || ss_base >= 0x40000000 || (ss_base + ss_size) >= 0x40000000)
					{
						ss_size = 0;
						ss_base = 0;
						printf("Invalid base!\n");
					}
				}

				if (!strncasecmp(p, "UART", 4))
				{
					p += 4;
					for (int i = 0; i < 10 && p && *p; i++)
					{
						char *end = 0;
						uart_speeds[i] = strtoul(p, &end, 10);
						p = end;
						if (p && *p == '(')
						{
							p++;
							int n = 0;
							while (*p != ';' && *p != ':' && *p != ')' && *p != ',')
							{
								if (n < 16) uart_speed_labels[i][n] = *p;
								p++;
								n++;
							}
							if (*p == ')') p++;
						}
						else
						{
							sprintf(uart_speed_labels[i], "%d", uart_speeds[i]);
						}
						if (p && *p == ':') p++;
					}

					printf("Got UART speeds:");
					for(int i=0; i<10; i++) printf(" %d", uart_speeds[i]);
					printf("\n");
				}

				if (!strncasecmp(p, "MIDI", 4))
				{
					p += 4;
					for (int i = 0; i < 10 && p && *p; i++)
					{
						char *end = 0;
						midi_speeds[i] = strtoul(p, &end, 10);
						p = end;
						if (p && *p == '(')
						{
							p++;
							int n = 0;
							while (*p != ';' && *p != ':' && *p != ')' && *p != ',')
							{
								if (n < 16) midi_speed_labels[i][n] = *p;
								p++;
								n++;
							}
							if (*p == ')') p++;
						}
						else
						{
							sprintf(midi_speed_labels[i], "%d", midi_speeds[i]);
						}
						if (p && *p == ':') p++;
					}

					if (!midi_speeds[0])
					{
						midi_speeds[0] = 31250;
						strcpy(midi_speed_labels[0], "31250");
					}

					printf("Got MIDI speeds:");
					for (int i = 0; i < 10; i++) printf(" %d", midi_speeds[i]);
					printf("\n");
				}

				p = strchr(p, ',');
				if (p) p++;
			}
		}

		if (i >= 2 && p && p[0])
		{
			if (!strncmp(p, "DEFMRA,", 7))
			{
				snprintf(defmra, sizeof(defmra), (p[7] == '/') ? "%s%s" : "%s/_Arcades/%s", getRootDir(), p + 7);
			}
			else if (!strncmp(p, "DIP", 3))
			{

			}
			else
			{
				//skip Disable/Hide masks
				while ((p[0] == 'H' || p[0] == 'D' || p[0] == 'h' || p[0] == 'd') && strlen(p) >= 2) p += 2;
			}
			if (p[0] == 'P') p += 2;

			if (p[0] == 'R' || p[0] == 'T' || p[0] == 'r' || p[0] == 't')
			{
				sz = user_io_status_bits(p + 1, &start, &end, p[0] == 'r' || p[0] == 't');
				if (sz == 1)
				{
					overlap[start] |= mask[start];
					mask[start] |= 1;
				}
				else
				{
					printf("Invalid OSD option: %s\n", p);
				}
			}
			else if (p[0] == 'O' || p[0] == 'o')
			{
				char *opt = (p[1] == 'X') ? (p + 2) : (p + 1);
				sz = user_io_status_bits(opt, &start, &end, p[0] == 'o');
				if (sz)
				{
					while (sz)
					{
						overlap[start] |= mask[start];
						mask[start] |= 1;
						sz--;
						start++;
					}
				}
				else
				{
					printf("Invalid OSD option: %s\n", p);
				}
			}

			if (p[0] == 'J')
			{
				int n = 1;
				if (p[1] == 'D') { joy_transl = 0; n++; }
				if (p[1] == 'A') { joy_transl = 1; n++; }
				if (p[1] == 'N') { joy_transl = 2; n++; }

				if (p[n] == '1')
				{
					joy_force = 1;
					set_emu_mode(EMU_JOY0);
				}
			}

			if (p[0] == 'O' && p[1] == 'X')
			{
				int x = user_io_status_get(p + 2);
				printf("found OX option: %s: %d\n", p, x);

				if (is_x86())
				{
					if (p[2] == '2') x86_set_fdd_boot(!(x & 1));
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
				strcpy(s, OsdCoreNameGet());
				strcat(s, " ");
				substrcpy(s + strlen(s), p, 1);
				OsdCoreNameSet(s);
			}

			if (p[0] == 'C')
			{
				use_cheats = 1;
			}

			if (p[0] == 'F')
			{
				int opensave = 0;
				int idx = 1;
				if (p[idx] == 'S')
				{
					opensave = 1;
					idx++;
				}

				if (p[idx] == 'C')
				{
					idx++;
					static char str[1024];
					uint32_t load_addr = 0;
					if (substrcpy(str, p, 3))
					{
						load_addr = strtoul(str, NULL, 16);
						if (load_addr < 0x20000000 || load_addr >= 0x40000000)
						{
							printf("Loading address 0x%X is outside the supported range! Using normal load.\n", load_addr);
							load_addr = 0;
						}
					}

					sprintf(str, "%s.f%c", user_io_get_core_name(), p[idx]);
					if (FileLoadConfig(str, str, sizeof(str)) && str[0])
					{

						idx = p[idx] - '0';
						StoreIdx_F(idx, str);
						user_io_file_tx(str, idx, opensave, 0, 0, load_addr);
					}
				}
			}

			if (p[0] == 'S' && p[1] == 'C')
			{
				static char str[1024];
				sprintf(str, "%s.s%c", user_io_get_core_name(), p[2]);

				static char ext[256];
				substrcpy(ext, p, 1);
				while (strlen(ext) % 3) strcat(ext, " ");

				if (FileLoadConfig(str, str, sizeof(str)) && str[0])
				{
					int idx = p[2] - '0';
					StoreIdx_S(idx, str);
					if (is_x86())
					{
						x86_set_image(idx, str);
					}
					else if (is_megacd())
					{
						mcd_set_image(idx, str);
					}
					else if (is_pce())
					{
						pcecd_set_image(idx, str);
						cheats_init(str, 0);
					}
					else
					{
						user_io_set_index(user_io_ext_idx(str, ext) << 6 | idx);
						user_io_file_mount(str, idx);
					}
				}
			}
		}
		i++;
	} while (p || i<3);

	mask[0] = 1; // reset is always on bit 0
	printf("\n// Status Bit Map:\n");
	printf("//              Upper                          Lower\n");
	printf("// 0         1         2         3          4         5         6   \n");
	printf("// 01234567890123456789012345678901 23456789012345678901234567890123\n");
	printf("// 0123456789ABCDEFGHIJKLMNOPQRSTUV 0123456789ABCDEFGHIJKLMNOPQRSTUV\n");
	char str[128];
	strcpy(str, "// ");
	for (int i = 0; i < 32; i++) strcat(str, mask[i] ? "X" : " ");
	strcat(str, " ");
	for (int i = 32; i < 64; i++) strcat(str, mask[i] ? "X" : " ");
	strcat(str, "\n");
	printf(str);

	int ovr = 0;
	for (int i = 0; i < 64; i++) ovr |= overlap[i];

	if (ovr)
	{
		strcpy(str, "// ");
		for (int i = 0; i < 32; i++) strcat(str, overlap[i] ? "^" : " ");
		strcat(str, " ");
		for (int i = 32; i < 64; i++) strcat(str, overlap[i] ? "^" : " ");
		strcat(str, "\n");
		printf(str);
		printf("// *Overlapped bits!* (can be intentional)\n");
	}
	printf("\n");

	ovr = 0;
	for (int i = 64; i < 128; i++) ovr |= mask[i];

	if (ovr)
	{
		printf("// 0     0         0         0          1         1         1       \n");
		printf("// 6     7         8         9          0         1         2       \n");
		printf("// 45678901234567890123456789012345 67890123456789012345678901234567\n");
		strcpy(str, "// ");
		for (int i = 64; i < 96; i++) strcat(str, mask[i] ? "X" : " ");
		strcat(str, " ");
		for (int i = 96; i < 128; i++) strcat(str, mask[i] ? "X" : " ");
		strcat(str, "\n");
		printf(str);

		ovr = 0;
		for (int i = 64; i < 128; i++) ovr |= overlap[i];

		if (ovr)
		{
			strcpy(str, "// ");
			for (int i = 64; i < 96; i++) strcat(str, overlap[i] ? "^" : " ");
			strcat(str, " ");
			for (int i = 96; i < 128; i++) strcat(str, overlap[i] ? "^" : " ");
			strcat(str, "\n");
			printf(str);
			printf("// *Overlapped bits!* (can be intentional)\n");
		}
		printf("\n");
	}


	// legacy GBA versions
	if (is_gba() && !ss_base)
	{
		ss_base = 0x3E000000;
		ss_size = 0x100000;
	}
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
	if (!stat("/tmp/uartmode4", &filestat)) return 4;
	if (!stat("/tmp/uartmode5", &filestat)) return 5;
	return 0;
}

void SetUARTMode(int mode)
{
	mode &= 0xF;
	uint32_t baud = GetUARTbaud(mode);

	spi_uio_cmd_cont(UIO_SET_UART);
	spi_w((mode == 4 || mode == 5) ? 1 : mode);
	spi_w(baud);
	spi_w(baud >> 16);
	DisableIO();

	MakeFile("/tmp/CORENAME", user_io_get_core_name());

	char data[20];
	sprintf(data, "%d", baud);
	MakeFile("/tmp/UART_SPEED", data);

	char cmd[32];
	sprintf(cmd, "uartmode %d", mode);
	system(cmd);
}

static int uart_speed_idx = 0;
static int midi_speed_idx = 0;
static int mlink_speed_idx = 0;

const uint32_t* GetUARTbauds(int mode)
{
	return (mode == 3) ? midi_speeds : (mode > 3) ? mlink_speeds : uart_speeds;
}

uint32_t GetUARTbaud(int mode)
{
	return (mode == 3) ? midi_speeds[midi_speed_idx] : (mode > 3) ? mlink_speeds[mlink_speed_idx] : uart_speeds[uart_speed_idx];
}

const char* GetUARTbaud_label(int mode)
{
	return (mode == 3) ? midi_speed_labels[midi_speed_idx] : (mode > 3) ? mlink_speed_labels[mlink_speed_idx] : uart_speed_labels[uart_speed_idx];
}

const char* GetUARTbaud_label(int mode, int idx)
{
	return (mode == 3) ? midi_speed_labels[idx] : (mode > 3) ? mlink_speed_labels[idx] : uart_speed_labels[idx];
}

int GetUARTbaud_idx(int mode)
{
	return (mode == 3) ? midi_speed_idx : (mode > 3) ? mlink_speed_idx : uart_speed_idx;
}

char * GetMidiLinkSoundfont()
{
    FILE * file;
    static char mLinkSoundfont[255];
    char fileName[] = "/tmp/ML_SOUNDFONT";
    char strip[] = "/media/fat/";
    file = fopen(fileName, "r");
    if (file)
    {
        fgets((char *) &mLinkSoundfont, sizeof(mLinkSoundfont), file);
        fclose(file);
        if(0 == strncmp(strip, mLinkSoundfont, sizeof(strip)-1))
		return &mLinkSoundfont[sizeof(strip)-1];
    }
    else
    {
        printf("ERROR: GetMidiLinkSoundfont : Unable to open --> '%s'\n", fileName);
        sprintf(mLinkSoundfont, "linux/soundfonts");
    }
    return mLinkSoundfont;
}

uint32_t ValidateUARTbaud(int mode, uint32_t baud)
{
	const uint32_t *bauds = GetUARTbauds(mode);
	int idx = 0;
	for (int i = 0; i < 13; i++)
	{
		if (!bauds[i]) break;
		if (bauds[i] == baud)
		{
			idx = i;
			break;
		}
	}

	if (mode == 3) midi_speed_idx = idx;
	else if (mode > 3) mlink_speed_idx = idx;
	else uart_speed_idx = idx;

	return bauds[idx];
}

int GetMidiLinkMode()
{
	struct stat filestat;
	if (!stat("/tmp/ML_FSYNTH", &filestat))  return 0;
	if (!stat("/tmp/ML_MUNT", &filestat))    return 1;
	if (!stat("/tmp/ML_USBMIDI", &filestat)) return 2;
	if (!stat("/tmp/ML_UDP", &filestat))     return 3;
	if (!stat("/tmp/ML_TCP", &filestat))     return 4;
	if (!stat("/tmp/ML_UDP_ALT", &filestat)) return 5;
	if (!stat("/tmp/ML_USBSER", &filestat))  return 6;
	return 0;
}

void SetMidiLinkMode(int mode)
{
	remove("/tmp/ML_FSYNTH");
	remove("/tmp/ML_MUNT");
	remove("/tmp/ML_UDP");
	remove("/tmp/ML_USBMIDI");
	remove("/tmp/ML_UDP_ALT");
	remove("/tmp/ML_TCP_ALT");
	remove("/tmp/ML_SERMIDI");
	remove("/tmp/ML_USBSER");
	remove("/tmp/ML_TCP");

	struct stat filestat;
	if (mode == 6 && stat("/dev/ttyUSB0", &filestat)) mode = 4;

	switch (mode)
	{
		case 0: MakeFile("/tmp/ML_FSYNTH", "");  break;
		case 1: MakeFile("/tmp/ML_MUNT", "");    break;
		case 2: MakeFile("/tmp/ML_USBMIDI", ""); break;
		case 3: MakeFile("/tmp/ML_UDP", "");     break;
		case 4: MakeFile("/tmp/ML_TCP", "");     break;
		case 5: MakeFile("/tmp/ML_UDP_ALT", ""); break;
		case 6: MakeFile("/tmp/ML_USBSER", "");  break;
	}
}

void ResetUART()
{
	if (uart_mode)
	{
		int mode = GetUARTMode();
		if (mode != 0)
		{
			SetUARTMode(0);
			SetUARTMode(mode);
		}
	}
}

uint16_t sdram_sz(int sz)
{
	int res = 0;

	void* buf = shmem_map(0x1FFFF000, 0x1000);
	if (!buf) return 0;

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
		else if(!is_menu())
		{
			printf("*** SDRAM config not found\n");
		}
	}

	shmem_unmap(buf, 0x1000);
	return res;
}

uint16_t altcfg(int alt)
{
	int res = 0;

	void* buf = shmem_map(0x1FFFF000, 0x1000);
	if (!buf) return 0;

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

	shmem_unmap(buf, 0x1000);
	return res;
}

int user_io_is_dualsdr()
{
	return dual_sdr;
}

int user_io_get_width()
{
	return fio_size;
}

void user_io_init(const char *path, const char *xml)
{
	char *name;
	static char mainpath[512];
	core_name[0] = 0;
	disable_osd = 0;

	// we need to set the directory to where the XML file (MRA) is
	// not the RBF. The RBF will be in arcade, which the user shouldn't
	// browse
	strcpy(core_path, xml ? xml : path);
	strcpy(rbf_path, path);

	memset(sd_image, 0, sizeof(sd_image));

	core_type = (fpga_core_id() & 0xFF);
	fio_size = fpga_get_fio_size();
	io_ver = fpga_get_io_version();

	if (core_type == CORE_TYPE_8BIT2)
	{
		dual_sdr = 1;
		core_type = CORE_TYPE_8BIT;
	}

	if ((core_type != CORE_TYPE_8BIT) &&
		(core_type != CORE_TYPE_SHARPMZ))
	{
		core_type = CORE_TYPE_UNKNOWN;
		fio_size = 0;
		io_ver = 0;
	}

	OsdSetSize(8);

	if (xml && isXmlName(xml) == 1)
	{
		is_arcade_type = 1;
		arcade_override_name(xml);
	}

	if (core_type == CORE_TYPE_8BIT)
	{
		printf("Identified 8BIT core");
		spi_uio_cmd16(UIO_SET_MEMSZ, sdram_sz(-1));

		// send a reset
		user_io_status_set("[0]", 1);
	}
	else if (core_type == CORE_TYPE_SHARPMZ)
	{
		user_io_set_core_name("sharpmz");
	}

	user_io_read_confstr();
	user_io_read_core_name();

	cfg_parse();
	while (cfg.waitmount[0] && !is_menu())
	{
		printf("> > > wait for %s mount < < <\n", cfg.waitmount);
		static char str[256];
		snprintf(str, sizeof(str), "exit $(mount | grep \"%s\" | wc -c)", cfg.waitmount);
		int ret = system(str);
		if (!(ret & 0xFF) && ret) break;
		sleep(1);
	}

	parse_config();
	if (!xml && defmra[0] && FileExists(defmra))
	{
		// attn: FC option won't use name from defmra!
		// attn: cfg is parsed before defmra, no defmra-name specifics possible in INI!
		xml = (const char*)defmra;
		strcpy(core_path, xml);
		is_arcade_type = 1;
		arcade_override_name(xml);
		user_io_read_core_name();
		printf("Using default MRA: %s\n", xml);
	}

	if (cfg.log_file_entry) MakeFile("/tmp/STARTPATH", core_path);

	if (cfg.bootcore[0] != '\0')
	{
		bootcore_init(xml ? xml : path);
	}

	video_mode_load();
	if (strlen(cfg.font)) LoadFont(cfg.font);
	load_volume();

	user_io_send_buttons(1);
	if (xml && isXmlName(xml) == 2) mgl_parse(xml);

	switch (core_type)
	{
	case CORE_TYPE_UNKNOWN:
		printf("Unable to identify core (%x)!\n", core_type);
		break;

	case CORE_TYPE_SHARPMZ:
		printf("Identified Sharp MZ Series core");
		user_io_set_core_name("sharpmz");
		sharpmz_init();
		parse_buttons();
		break;

	case CORE_TYPE_8BIT:
		// try to load config
		name = user_io_create_config_name();
		if (strlen(name) > 0)
		{
			if (!is_st() && !is_minimig())
			{
				printf("Loading config %s\n", name);
				memset(cur_status, 0, sizeof(cur_status));
				if (FileLoadConfig(name, cur_status, sizeof(cur_status)))
				{
					printf("Found config:\n");
					hexdump(cur_status, sizeof(cur_status));
				}
				else
				{
					memset(cur_status, 0, sizeof(cur_status));
				}

				user_io_status_set("[0]", 1);
			}

			if (is_st())
			{
				tos_config_load(0);
				tos_upload(NULL);
			}
			else if (is_menu())
			{
				user_io_status_set("[4]", (cfg.menu_pal) ? 1 : 0);
				if (cfg.fb_terminal) video_menu_bg(user_io_status_get("[3:1]"));
				else user_io_status_set("[3:1]", 0);
			}
			else
			{
				if (xml && isXmlName(xml) == 1)
				{
					arcade_send_rom(xml);
				}
				else if (is_minimig())
				{
					printf("Identified Minimig V2 core");
					BootInit();
				}
				else if (is_x86())
				{
					x86_config_load();
					x86_init();
				}
				else if (is_archie())
				{
					printf("Identified Archimedes core");
					archie_init();
				}
				else
				{
					const char *home = HomeDir();

					if (!strlen(path) || !user_io_file_tx(path, 0, 0, 0, 1))
					{
						if (!is_cpc())
						{
							// check for multipart rom
							for (char i = 0; i < 4; i++)
							{
								sprintf(mainpath, "%s/boot%d.rom", home, i);
								user_io_file_tx(mainpath, i << 6);
							}
						}

						// legacy style of rom
						sprintf(mainpath, "%s/boot.rom", home);
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

					if (is_cpc())
					{
						for (int m = 0; m < 3; m++)
						{
							const char *model = !m ? "" : (m == 1) ? "0" : "1";
							sprintf(mainpath, "%s/boot%s.eZZ", home, model);
							user_io_file_tx(mainpath, 0x40 * (m + 1), 0, 1);
							sprintf(mainpath, "%s/boot%s.eZ0", home, model);
							user_io_file_tx(mainpath, 0x40 * (m + 1), 0, 1);
							for (int i = 0; i < 256; i++)
							{
								sprintf(mainpath, "%s/boot%s.e%02X", home, model, i);
								user_io_file_tx(mainpath, 0x40 * (m + 1), 0, 1);
							}
						}
					}

					// check if vhd present
					for (char i = 0; i < 4; i++)
					{
						sprintf(mainpath, "%s/boot%d.vhd", home, i);
						if (FileExists(mainpath))
						{
							user_io_set_index(i << 6);
							user_io_file_mount(mainpath, i);
						}
					}

					sprintf(mainpath, "%s/boot.vhd", home);
					if (FileExists(mainpath))
					{
						user_io_set_index(0);
						user_io_file_mount(mainpath, 0);
					}
					else
					{
						strcpy(name + strlen(name) - 3, "VHD");
						sprintf(mainpath, "%s/%s", get_rbf_dir(), name);
						if (FileExists(mainpath))
						{
							user_io_set_index(0);
							user_io_file_mount(mainpath, 0);
						}
						else if (FileExists(name))
						{
							user_io_set_index(0);
							user_io_file_mount(name, 0);
						}
					}
				}
			}

			parse_buttons();
		}

		send_rtc(3);

		// release reset
		if (!is_minimig() && !is_st()) user_io_status_set("[0]", 0);
		if (xml && isXmlName(xml) == 1) arcade_check_error();
		break;
	}

	OsdRotation((cfg.osd_rotate == 1) ? 3 : (cfg.osd_rotate == 2) ? 1 : 0);

	uart_mode = spi_uio_cmd16(UIO_GETUARTFLG, 0) || uart_speeds[0];
	uint32_t mode = 0;
	if (uart_mode)
	{
		if (!uart_speeds[0])
		{
			uart_speeds[0] = is_st() ? 19200 : 115200;
			sprintf(uart_speed_labels[0], "%d", uart_speeds[0]);

			if (!midi_speeds[0])
			{
				midi_speeds[0] = 31250;
				sprintf(midi_speed_labels[0], "%d", midi_speeds[0]);
			}
		}

		sprintf(mainpath, "uartmode.%s", user_io_get_core_name());
		FileLoadConfig(mainpath, &mode, 4);

		uint32_t speeds[3] = {};
		sprintf(mainpath, "uartspeed.%s", user_io_get_core_name());
		FileLoadConfig(mainpath, speeds, sizeof(speeds));

		ValidateUARTbaud(1, speeds[0]);
		ValidateUARTbaud(3, speeds[1]);
		ValidateUARTbaud(4, speeds[2] ? speeds[2] : uart_speeds[0]);

		printf("UART bauds: %d/%d/%d\n", GetUARTbaud(1), GetUARTbaud(3), GetUARTbaud(4));
	}

	SetUARTMode(0);
	int midilink = (mode >> 8) & 0xFF;
	int uartmode = mode & 0xFF;
	if (uartmode == 4 && (midilink < 4 || midilink>6)) midilink = 4;
	if (uartmode == 3 && midilink > 3) midilink = 0;
	if (uartmode < 3 || uartmode > 4) midilink = 0;
	SetMidiLinkMode(midilink);
	SetUARTMode(uartmode);

	if (!mgl_get()->count || is_menu() || is_st() || is_archie() || user_io_core_type() == CORE_TYPE_SHARPMZ)
	{
		mgl_get()->done = 1;
	}
	else
	{
		mgl_get()->timer = GetTimer(mgl_get()->item[0].delay * 1000);
	}
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

void user_io_l_analog_joystick(unsigned char joystick, char valueX, char valueY)
{
	uint8_t joy = (joystick > 1 || !joyswap) ? joystick : (joystick >= 15) ? (joystick ^ 16) : (joystick ^ 1);

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

void user_io_r_analog_joystick(unsigned char joystick, char valueX, char valueY)
{
	uint8_t joy = (joystick > 1 || !joyswap) ? joystick : (joystick ^ 1);

	if (core_type == CORE_TYPE_8BIT)
	{
		spi_uio_cmd8_cont(UIO_ASTICK_2, joy);
		if (io_ver) spi_w((valueY << 8) | (uint8_t)(valueX));
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

	static int use32 = 0;
	use32 |= map >> 16;

	spi_uio_cmd_cont((joy < 2) ? (UIO_JOYSTICK0 + joy) : (UIO_JOYSTICK2 + joy - 2));
	spi_w(map);
	if(use32) spi_w(map>>16);
	DisableIO();

	if (!is_minimig() && joy_transl == 1 && newdir)
	{
		user_io_l_analog_joystick(joystick, (map & 2) ? 128 : (map & 1) ? 127 : 0, (map & 8) ? 128 : (map & 4) ? 127 : 0);
	}
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

int process_ss(const char *rom_name, int enable)
{
	static char ss_name[1024] = {};
	static char *ss_sufx = 0;
	static uint32_t ss_cnt[4] = {};
	static void *base[4] = {};
	static int enabled = 0;

	if (!ss_base) return 0;

	if (rom_name)
	{
		enabled = enable;
		if (!enabled) return 0;

		uint32_t len = ss_size;
		uint32_t map_addr = ss_base;
		fileTYPE f = {};

		for (int i = 0; i < 4; i++)
		{
			if (!base[i]) base[i] = shmem_map(map_addr, len);
			if (!base[i])
			{
				printf("Unable to mmap (0x%X, %d)!\n", map_addr, len);
			}
			else
			{
				ss_cnt[i] = 0;
				memset(base[i], 0, len);

				if (!i)
				{
					FileGenerateSavestatePath(rom_name, ss_name, 1);
					printf("Base SavestatePath=%s\n", ss_name);
					if (!FileExists(ss_name)) FileGenerateSavestatePath(rom_name, ss_name, 0);
				}
				else
				{
					FileGenerateSavestatePath(rom_name, ss_name, i + 1);
				}

				if (FileExists(ss_name))
				{
					if (!FileOpen(&f, ss_name))
					{
						printf("Unable to open file: %s\n", ss_name);
					}
					else
					{
						int ret = FileReadAdv(&f, base[i], len);
						FileClose(&f);
						*(uint32_t*)(base[i]) = 1;
						ss_cnt[i] = 1;
						printf("process_ss: read %d bytes from file: %s\n", ret, ss_name);
					}
				}
			}

			map_addr += len;
		}

		FileGenerateSavestatePath(rom_name, ss_name, 1);
		ss_sufx = ss_name + strlen(ss_name) - 4;
		return 1;
	}

	if (!enabled) return 0;

	static unsigned long ss_timer = 0;
	if (ss_timer && !CheckTimer(ss_timer)) return 0;
	ss_timer = GetTimer(1000);

	fileTYPE f = {};
	for (int i = 0; i < 4; i++)
	{
		if (base[i])
		{
			uint32_t curcnt = ((uint32_t*)(base[i]))[0];
			uint32_t size = ((uint32_t*)(base[i]))[1];

			if (curcnt != ss_cnt[i])
			{
				ss_cnt[i] = curcnt;
				if (size) size = (size + 2) * 4;
				if (size > 0 && size <= ss_size)
				{
					MenuHide();
					Info("Saving the state", 500);

					*ss_sufx = i + '1';
					if (FileOpenEx(&f, ss_name, O_CREAT | O_TRUNC | O_RDWR | O_SYNC))
					{
						int ret = FileWriteAdv(&f, base[i], size);
						FileClose(&f);
						printf("Wrote %d bytes to file: %s\n", ret, ss_name);
					}
					else
					{
						printf("Unable to create file: %s\n", ss_name);
					}
				}
			}
		}
	}

	return 1;
}

void user_io_set_index(unsigned char index)
{
	EnableFpga();
	spi8(FIO_FILE_INDEX);
	spi8(index);
	DisableFpga();
}

void user_io_set_aindex(uint16_t index)
{
	EnableFpga();
	spi8(FIO_FILE_INDEX);
	spi_w(index);
	DisableFpga();
}

void user_io_set_download(unsigned char enable, int addr)
{
	EnableFpga();
	spi8(FIO_FILE_TX);
	spi8(enable ? 0xff : 0);
	if (enable && addr)
	{
		spi_w(addr);
		spi_w(addr >> 16);
	}
	DisableFpga();
}

void user_io_file_tx_data(const uint8_t *addr, uint32_t len)
{
	EnableFpga();
	spi8(FIO_FILE_TX_DAT);
	spi_write(addr, len, fio_size);
	DisableFpga();
}

void user_io_set_upload(unsigned char enable, int addr)
{
	EnableFpga();
	spi8(FIO_FILE_TX);
	spi8(enable ? 0xaa : 0);
	if (enable && addr)
	{
		spi_w(addr);
		spi_w(addr >> 16);
	}
	DisableFpga();
}

void user_io_file_rx_data(uint8_t *addr, uint32_t len)
{
	EnableFpga();
	spi8(FIO_FILE_TX_DAT);
	spi_read(addr, len, fio_size);
	DisableFpga();
}

void user_io_file_info(const char *ext)
{
	EnableFpga();
	spi8(FIO_FILE_INFO);
	spi_w(toupper(ext[0]) << 8 | toupper(ext[1]));
	spi_w(toupper(ext[2]) << 8 | toupper(ext[3]));
	DisableFpga();
}

int user_io_file_mount(const char *name, unsigned char index, char pre, int pre_size)
{
	int writable = 0;
	int ret = 0;
	int len = strlen(name);

	sd_image_cangrow[index] = (pre != 0);
	sd_type[index] = 0;

	if (len)
	{
		if (!strcasecmp(user_io_get_core_name(), "apple-ii"))
		{
			ret = dsk2nib(name, sd_image + index);
		}

		if (!ret)
		{
			if (x2trd_ext_supp(name))
			{
				ret = x2trd(name, sd_image + index);
			}
			else if (len > 4 && !strcasecmp(name + len - 4, ".t64"))
			{
				writable = 0;
				ret = c64_openT64(name, sd_image + index);
				if (ret)
				{
					ret = c64_openGCR(name, sd_image + index, index);
					sd_type[index] = 1;
					if (!ret) FileClose(&sd_image[index]);
				}
			}
			else
			{
				writable = FileCanWrite(name);
				ret = FileOpenEx(&sd_image[index], name, writable ? (O_RDWR | O_SYNC) : O_RDONLY);
				if (ret && len > 4 && (!strcasecmp(name + len - 4, ".d64") || !strcasecmp(name + len - 4, ".g64")))
				{
					ret = c64_openGCR(name, sd_image + index, index);
					sd_type[index] = 1;
					if(!ret) FileClose(&sd_image[index]);
				}
			}
		}

		if (!ret)
		{
			printf("Failed to open file %s\n", name);
		}
	}
	else
	{
		FileClose(&sd_image[index]);
		c64_closeGCR(index);
	}

	buffer_lba[index] = -1;
	if (!index) use_save = pre;

	if (!ret)
	{
		sd_image[index].size = 0;
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
		size = pre_size;
	}

	if (io_ver)
	{
		spi32_w(size);
		spi32_w(size >> 32);
	}
	else
	{
		spi32_b(size);
		spi32_b(size>>32);
	}
	DisableIO();

	// notify core of possible sd image change
	spi_uio_cmd8(UIO_SET_SDSTAT, (1<< index) | (writable ? 0 : 0x80));
	return ret ? 1 : 0;
}

void user_io_bufferinvalidate(unsigned char index)
{
	buffer_lba[index] = -1;
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
		user_io_file_tx_data(col_attr, type ? 1024 : 1025);
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
		for (uint i = 0; i < sizeof(cur_status); i += 2)
		{
			uint16_t x = spi_w(0);
			cur_status[i] = (char)x;
			cur_status[i + 1] = (char)(x >> 8);
		}
		DisableIO();
		user_io_status_set("[0]", 0);
	}
	else
	{
		DisableIO();
	}
}

static void show_core_info(int info_n)
{
	int i = 2;
	user_io_read_confstr();

	while (1)
	{
		char *p = user_io_get_confstr(i++);
		if (!p) break;

		if (p[0] == 'I')
		{
			static char str[256];
			substrcpy(str, p, info_n);
			if (strlen(str)) Info(str);
			break;
		}
	}
}

int user_io_file_tx_a(const char* name, uint16_t index)
{
	fileTYPE f = {};
	static uint8_t buf[4096];

	if (!FileOpen(&f, name, 1)) return 0;

	unsigned long bytes2send = f.size;

	/* transmit the entire file using one transfer */
	printf("Addon file %s with %lu bytes to send for index %04X\n", name, bytes2send, index);

	// set index byte (0=bios rom, 1-n=OSD entry index)
	user_io_set_aindex(index);

	// prepare transmission of new file
	user_io_set_download(1);

	int use_progress = 1;
	int size = bytes2send;
	if (use_progress) ProgressMessage(0, 0, 0, 0);
	while (bytes2send)
	{
		uint32_t chunk = (bytes2send > sizeof(buf)) ? sizeof(buf) : bytes2send;

		FileReadAdv(&f, buf, chunk);
		user_io_file_tx_data(buf, chunk);

		if (use_progress) ProgressMessage("Loading", f.name, size - bytes2send, size);
		bytes2send -= chunk;
	}

	// check if core requests some change while downloading
	check_status_change();

	printf("Done.\n");
	FileClose(&f);

	// signal end of transmission
	user_io_set_download(0);
	ProgressMessage(0, 0, 0, 0);
	return 1;
}

int user_io_file_tx(const char* name, unsigned char index, char opensave, char mute, char composite, uint32_t load_addr)
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
	if(load_addr) printf("Load to address 0x%X\n", load_addr);

	// set index byte (0=bios rom, 1-n=OSD entry index)
	user_io_set_index(index);

	int len = strlen(f.name);
	char *p = f.name + len - 4;
	user_io_file_info(p);

	// prepare transmission of new file
	user_io_set_download(1, load_addr ? bytes2send : 0);

	int dosend = 1;



	int is_snes_bs = 0;
	if (is_snes() && bytes2send)
	{
		const char *ext = strrchr(f.name, '.');
		if (ext && !strcasecmp(ext, ".BS")) {
			is_snes_bs = 1;
		}

		if (is_snes_bs) {
			char *rom_path = (char*)buf;
			strcpy(rom_path, name);
			char *offs = strrchr(rom_path, '/');
			if (offs) *offs = 0;
			else *rom_path = 0;

			fileTYPE fb = {};
			if (FileOpen(&fb, user_io_make_filepath(rom_path, "bsx_bios.rom")) ||
				FileOpen(&fb, user_io_make_filepath(HomeDir(), "bsx_bios.rom")))
			{
				printf("Load BSX bios ROM.\n");
				uint8_t* buf = snes_get_header(&fb);
				hexdump(buf, 16, 0);
				user_io_file_tx_data(buf, 512);

				//strip original SNES ROM header if present (not used)
				if ((bytes2send % 1024) == 512)
				{
					bytes2send -= 512;
					FileReadSec(&f, buf);
				}

				uint32_t sz = fb.size;
				while (sz)
				{
					uint32_t chunk = (sz > sizeof(buf)) ? sizeof(buf) : sz;
					FileReadAdv(&fb, buf, chunk);
					user_io_file_tx_data(buf, chunk);
					sz -= chunk;
				}
				FileClose(&fb);
			}
			else
			{
				dosend = 0;
				Info("Cannot open bsx_bios.rom!");
				sleep(1);
			}
		}
		else if ((index & 0x3F) == 1) {
			printf("Load SPC ROM.\n");
			FileReadSec(&f, buf);
			user_io_file_tx_data(buf, 256);

			FileSeek(&f, (64*1024)+256, SEEK_SET);
			FileReadSec(&f, buf);
			user_io_file_tx_data(buf, 256);

			FileSeek(&f, 256, SEEK_SET);
			bytes2send = 64 * 1024;
		}
		else {
			printf("Load SNES ROM.\n");
			uint8_t* buf = snes_get_header(&f);
			hexdump(buf, 16, 0);
			user_io_file_tx_data(buf, 512);

			//strip original SNES ROM header if present (not used)
			if ((bytes2send % 1024) == 512)
			{
				bytes2send -= 512;
				FileReadSec(&f, buf);
			}
		}
	}

	file_crc = 0;
	uint32_t skip = bytes2send & 0x3FF; // skip possible header up to 1023 bytes

	int use_progress = 1; // (bytes2send > (1024 * 1024)) ? 1 : 0;
	int size = bytes2send;
	if (use_progress) ProgressMessage(0, 0, 0, 0);

	if(ss_base && opensave) process_ss(name);

	if (is_gba())
	{
		if ((index >> 6) == 1 || (index >> 6) == 2)
		{
			fileTYPE fg = {};
			if (!FileOpen(&fg, user_io_make_filepath(HomeDir(), "goomba.rom")))
			{
				dosend = 0;
				Info("Cannot open goomba.rom!");
				sleep(1);
			}
			else
			{
				uint32_t sz = fg.size;
				while (sz)
				{
					uint32_t chunk = (sz > sizeof(buf)) ? sizeof(buf) : sz;
					FileReadAdv(&fg, buf, chunk);
					user_io_file_tx_data(buf, chunk);
					sz -= chunk;
				}
				FileClose(&fg);
			}
		}
	}

	if (is_electron() && bytes2send)
	{
		const char *ext = strrchr(f.name, '.');
		if (ext && !strcasecmp(ext, ".UEF")) {
			UEF_FileSend(&f,use_progress);
			dosend=0;
		}
	}

	if (dosend && load_addr >= 0x20000000 && (load_addr + bytes2send) <= 0x40000000)
	{
		uint8_t *mem = (uint8_t *)shmem_map(fpga_mem(load_addr), bytes2send);
		if (mem)
		{
			while (bytes2send)
			{
				uint32_t chunk = (bytes2send > (256 * 1024)) ? (256 * 1024) : bytes2send;
				FileReadAdv(&f, mem + size - bytes2send, chunk);

				file_crc = crc32(file_crc, mem + skip + size - bytes2send, chunk - skip);
				skip = 0;

				if (use_progress) ProgressMessage("Loading", f.name, size - bytes2send, size);
				bytes2send -= chunk;
			}

			shmem_unmap(mem, bytes2send);
		}
	}
	else
	{
		while (dosend && bytes2send)
		{
			uint32_t chunk = (bytes2send > sizeof(buf)) ? sizeof(buf) : bytes2send;

			FileReadAdv(&f, buf, chunk);
			if (is_snes() && is_snes_bs) snes_patch_bs_header(&f, buf);
			user_io_file_tx_data(buf, chunk);

			if (use_progress) ProgressMessage("Loading", f.name, size - bytes2send, size);
			bytes2send -= chunk;

			if (skip >= chunk) skip -= chunk;
			else
			{
				file_crc = crc32(file_crc, buf + skip, chunk - skip);
				skip = 0;
			}
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

	if (is_zx81() && index)
	{
		send_pcolchr(name, (index & 0x1F) | 0x20, 0);
		send_pcolchr(name, (index & 0x1F) | 0x60, 1);
	}

	ProgressMessage(0, 0, 0, 0);

    if (is_snes())
    {
        // Setup MSU
        snes_msu_init(name);
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
	int lidx = 0;
	static char buffer[(1024*2) + 1];  // max bytes per config item

	char *start = cfgstr;
	while (lidx < index)
	{
		start = strchr(start, ';');
		if (!start) return NULL;
		start++;
		lidx++;
	}

	char *end = strchr(start, ';');
	int len = end ? end - start : strlen(start);
	if (!len) return NULL;

	if ((uint32_t)len > sizeof(buffer) - 1) len = sizeof(buffer) - 1;
	memcpy(buffer, start, len);
	buffer[len] = 0;
	return buffer;
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

static int vga_fb = 0;
void set_vga_fb(int enable)
{
	vga_fb = enable;
	user_io_send_buttons(1);
}

int get_vga_fb()
{
	return vga_fb;
}

static char kbd_reset = 0;
static char kbd_reset_ovr = 0;

void user_io_send_buttons(char force)
{
	static unsigned short key_map = 0;
	unsigned short map = 0;

	cur_btn = fpga_get_buttons();

	if (user_io_menu_button()) map |= BUTTON1;
	if (user_io_user_button()) map |= BUTTON2;
	if (kbd_reset || kbd_reset_ovr) map |= BUTTON2;

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
	if (vga_fb) map |= CONF_VGA_FB;

	if ((map != key_map) || force)
	{
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
		if (!user_io_osd_is_visible() && (key_map & BUTTON2) && !(map & BUTTON2))
		{
			if (is_minimig()) minimig_reset();
			if (is_megacd()) mcd_reset();
			if (is_pce()) pcecd_reset();
			if (is_x86()) x86_init();
			ResetUART();
		}

		key_map = map;
		if (user_io_osd_is_visible()) map &= ~BUTTON2;
		spi_uio_cmd16(UIO_BUT_SW, map);
		printf("sending keymap: %X\n", map);
	}
}

int user_io_get_kbd_reset()
{
	return kbd_reset || kbd_reset_ovr;
}

void user_io_set_kbd_reset(int reset)
{
	kbd_reset_ovr = reset;
}

void user_io_set_ini(int ini_num)
{
	const char *name = rbf_path;
	const char *xml = strcasecmp(rbf_path, core_path) ? core_path : NULL;

	if (!name[0])
	{
		name = "menu.rbf";
		xml = NULL;
	}

	if (FileExists(cfg_get_name(ini_num)))
	{
		altcfg(ini_num);
		fpga_load_rbf(name, NULL, xml);
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

void user_io_poll()
{
	if ((core_type != CORE_TYPE_SHARPMZ) &&
		(core_type != CORE_TYPE_8BIT))
	{
		return;  // no user io for the installed core
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
		sysled_enable(0);
		HandleFDD(c1, c2);
		sysled_enable(1);

		uint16_t sd_req = ide_check();
		if (sd_req & 0x8000)
		{
			ide_io(0, sd_req & 7);
			ide_io(1, (sd_req >> 3) & 7);
		}
		else
		{
			HandleHDD(c1, c2);
		}
		UpdateDriveStatus();

		kbd_fifo_poll();

		if (!rtc_timer || CheckTimer(rtc_timer))
		{
			// Update once per minute should be enough
			rtc_timer = GetTimer(60000);
			send_rtc(1);
		}

		minimig_share_poll();
	}

	if (core_type == CORE_TYPE_8BIT && !is_menu())
	{
		check_status_change();
	}

	// sd card emulation
	if (is_x86())
	{
		x86_poll();
	}
	else if ((core_type == CORE_TYPE_8BIT) && !is_menu() && !is_minimig())
	{
		if (is_st()) tos_poll();
		if (is_snes()) snes_poll();

		for (int i = 0; i < 4; i++)
		{
			int disk = -1;
			int ack = 0;
			int op = 0;
			static uint8_t buffer[16][16384];
			uint64_t lba;
			uint32_t blksz, blks, sz;

			uint16_t c = spi_uio_cmd_cont(UIO_GET_SDSTAT);
			if (c & 0x8000)
			{
				disk = (c >> 2) & 0xF;
				op = c & 3;
				ack = disk << 8;
				spi_w(0);
				lba = spi_w(0);
				lba = (lba & 0xFFFF) | (((uint32_t)spi_w(0)) << 16);
				blks = ((c >> 9) & 0x3F) + 1;
				blksz = (disk == 1 && is_psx()) ? 2352 : (128 << ((c >> 6) & 7));

				sz = blksz * blks;
				if (sz > sizeof(buffer[0]))
				{
					blks = sizeof(buffer[0]) / blksz;
					sz = blksz * blks;
				}

				//if (op) printf("c=%X, op=%d, blkpow=%d, sz=%d, lba=%llu, disk=%d\n", c, op, blkpow, sz, lba, disk);
			}
			else
			{
				c = spi_w(0);
				if ((c & 0xf0) == 0x50 && (c & 0x3F03))
				{
					lba = spi_w(0);
					lba = (lba & 0xFFFF) | (((uint32_t)spi_w(0)) << 16);

					// check if core requests configuration
					if ((c & 0xC) == 0xC)
					{
						printf("core requests SD config\n");
						user_io_sd_set_config();
					}

					if (c & 0x0003)
					{
						disk = 0;
						op = (c & 1) ? 1 : 2;
					}
					else if (c & 0x0900)
					{
						disk = 1;
						op = (c & 0x100) ? 1 : 2;
					}
					else if (c & 0x1200)
					{
						disk = 2;
						op = (c & 0x200) ? 1 : 2;
					}
					else if (c & 0x2400)
					{
						disk = 3;
						op = (c & 0x400) ? 1 : 2;
					}

					ack = ((c & 4) ? 0 : ((disk + 1) << 8));
				}

				sz = 512;
				blksz = 512;
				blks = 1;
			}
			DisableIO();

			if ((blks == 32) && sd_type[disk])
			{
				if (op == 2) c64_writeGCR(disk, lba);
				else if (op & 1) c64_readGCR(disk, lba);
				else break;
			}
			else if (op == 2)
			{
				//printf("SD WR %d on %d\n", lba, disk);

				if (use_save) menu_process_save();

				buffer_lba[disk] = -1;

				// Fetch sector data from FPGA ...
				EnableIO();
				spi_w(UIO_SECTOR_WR | ack);
				spi_block_read(buffer[disk], fio_size, sz);
				DisableIO();

				if (sd_image[disk].type == 2 && !lba)
				{
					//Create the file
					if (FileOpenEx(&sd_image[disk], sd_image[disk].path, O_CREAT | O_RDWR | O_SYNC))
					{
						diskled_on();
						if (FileWriteAdv(&sd_image[disk], buffer[disk], sz))
						{
							sd_image[disk].size = sz;
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
					uint64_t size = sd_image[disk].size / blksz;
					if (sz && lba <= size)
					{
						diskled_on();
						if (FileSeek(&sd_image[disk], lba * blksz, SEEK_SET))
						{
							if (!sd_image_cangrow[disk])
							{
								__off64_t rem = sd_image[disk].size - sd_image[disk].offset;
								sz = (rem >= sz) ? sz : (int)rem;
							}

							if (sz) FileWriteAdv(&sd_image[disk], buffer[disk], sz);
						}
					}
				}
			}
			else if (op & 1)
			{
				uint32_t buf_n = sizeof(buffer[0]) / blksz;
				//printf("SD RD (%llu,%d) on %d, WIDE=%d\n", lba, blksz, disk, fio_size);

				int done = 0;
				uint32_t offset;

				if ((buffer_lba[disk] == (uint64_t)-1) || lba < buffer_lba[disk] || (lba + blks - buffer_lba[disk]) > buf_n)
				{
					buffer_lba[disk] = -1;
					if (blksz == 2352 && is_psx())
					{
						diskled_on();
						psx_read_cd(buffer[disk], lba, buf_n);
						done = 1;
						buffer_lba[disk] = lba;
					}
					else if (sd_image[disk].size)
					{
						diskled_on();
						if (FileSeek(&sd_image[disk], lba * blksz, SEEK_SET))
						{
							if (FileReadAdv(&sd_image[disk], buffer[disk], sizeof(buffer[disk])))
							{
								done = 1;
								buffer_lba[disk] = lba;
							}
						}
					}

					//Even after error we have to provide the block to the core
					//Give an empty block.
					if (!done)
					{
						if (sd_image[disk].type == 2)
						{
							if (is_megacd())
							{
								mcd_fill_blanksave(buffer[disk], lba);
							}
							else if (is_pce())
							{
								memset(buffer[disk], 0, sizeof(buffer[disk]));
								if (!lba)
								{
									memcpy(buffer[disk], "HUBM\x00\x88\x10\x80", 8);
								}
							}
							else if (is_psx())
							{
								psx_fill_blanksave(buffer[disk], lba, blks);
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

					offset = 0;
				}
				else
				{
					offset = (lba - buffer_lba[disk]) * blksz;
					done = 1;
				}

				// data is now stored in buffer. send it to fpga
				EnableIO();
				spi_w(UIO_SECTOR_RD | ack);
				spi_block_write(buffer[disk] + offset, fio_size, sz);
				DisableIO();

				if (sd_image[disk].type == 2)
				{
					buffer_lba[disk] = -1;
				}
				else if (done && (lba + blks - buffer_lba[disk]) == buf_n)
				{
					diskled_on();
					lba += blks;
					if (blksz == 2352 && is_psx())
					{
						psx_read_cd(buffer[disk], lba, buf_n);
						buffer_lba[disk] = lba;
					}
					else if (FileSeek(&sd_image[disk], lba * blksz, SEEK_SET) &&
						FileReadAdv(&sd_image[disk], buffer[disk], sizeof(buffer[disk])))
					{
						buffer_lba[disk] = lba;
					}
					else
					{
						memset(buffer[disk], 0, sizeof(buffer[disk]));
						buffer_lba[disk] = -1;
					}
				}
			}
			else break;
		}
	}

	if (is_neogeo() && (!rtc_timer || CheckTimer(rtc_timer)))
	{
		// Update once per minute should be enough
		rtc_timer = GetTimer(60000);
		send_rtc(1);
	}

	if (is_archie()) archie_poll();
	if (core_type == CORE_TYPE_SHARPMZ) sharpmz_poll();

	static uint8_t leds = 0;

	if (use_ps2ctl && !is_minimig() && !is_archie())
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
					ps2_kbd_scan_set = 2;
					kbd_reply(0xFA);
					kbd_reply(0xAA);
					break;

				case 0xf2:
					kbd_reply(0xFA);
					kbd_reply(0xAB);
					kbd_reply(0x83);
					break;

				case 0xf0: // scan get/set
					kbd_reply(0xFA);
					byte++;
					break;

				case 0xf6: // set default parameters
					kbd_reply(0xFA);
					ps2_kbd_scan_set = 2;
					break;

				case 0xf3: // set type rate
					kbd_reply(0xFA);
					byte++;
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

				case 0xee:
					kbd_reply(0xEE);
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

				case 0xf0:
					byte = 0;
					if (kbd_ctl <= 3)
					{
						kbd_reply(0xFA);
						if (!kbd_ctl) kbd_reply(ps2_kbd_scan_set); // get
						else ps2_kbd_scan_set = kbd_ctl; // set
					}
					else
					{
						kbd_reply(0xFE); // RESEND
					}
					break;

				case 0xf3: // set type rate
					kbd_reply(0xFA);
					byte = 0;
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

	if (CheckTimer(led_timer) && !is_menu())
	{
		led_timer = GetTimer(LED_FREQ);
		if (!use_ps2ctl)
		{
			uint16_t s = user_io_kbdled_get_status();
			if (s & 0x100) use_ps2ctl = 1;
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

		uint8_t info_n = spi_uio_cmd(UIO_INFO_GET);
		if (info_n) show_core_info(info_n);
	}

	if (!res_timer)
	{
		res_timer = GetTimer(1000);
	}
	else if (CheckTimer(res_timer))
	{
		if (is_menu())
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
			if (is_minimig()) minimig_adjust_vsize(0);
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

	save_volume();

	if (diskled_is_on && CheckTimer(diskled_timer))
	{
		fpga_set_led(0);
		diskled_is_on = 0;
	}

	if (is_megacd()) mcd_poll();
	if (is_pce()) pcecd_poll();
	process_ss(0);
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

	if (is_archie())
	{
		if (press > 1) return;

		uint32_t code = get_archie_code(key);
		if (code == NONE) return;

		//WIN+...
		if (get_key_mod() & (RGUI | LGUI))
		{
			switch (code)
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
				static const unsigned char c_set1[] = { 0xe1, 0x1d, 0x45, 0xe1, 0x9d, 0xc5, 0x00 };
				const unsigned char *p = (ps2_kbd_scan_set == 1) ? c_set1 : c;

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
				static const unsigned char c_set1[2][8] = {
					{ 0xE0, 0xB7, 0xE0, 0xAA, 0x00, 0x00, 0x00, 0x00 },
					{ 0xE0, 0x2A, 0xE0, 0x37, 0x00, 0x00, 0x00, 0x00 }
				};

				const unsigned char *p = (ps2_kbd_scan_set == 1) ? c_set1[press] : c[press];

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
            if (!press)
            {
                if (ps2_kbd_scan_set == 1)
                        code |= 0x80;
                    else
                        spi8(0xf0);
            }
			// send code itself
			spi8(code & 0xff);

			DisableIO();
		}
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
	if (osd_is_visible && !is_menu()) return;

	switch (core_type)
	{
	case CORE_TYPE_8BIT:
		if (is_minimig())
		{
			spi_uio_cmd_cont(UIO_MOUSE);
			spi8((x < -127) ? -127 : (x > 127) ? 127 : x);
			spi8((y < -127) ? -127 : (y > 127) ? 127 : y);
			spi8(b & 0x07);
			spi8((w < -127) ? -127 : (w > 127) ? 127 : w);
			DisableIO();
		}
		else if (is_archie())
		{
			archie_mouse(b, x, y);
		}
		else
		{
			unsigned char ps2_mouse[3];

			// PS2 format:
			// YOvfl, XOvfl, dy8, dx8, 1, mbtn, rbtn, lbtn
			// dx[7:0]
			// dy[7:0]
			ps2_mouse[0] = (b & 7) | 8;

			// ------ X axis -----------
			// store sign bit in first byte
			ps2_mouse[0] |= (x < 0) ? 0x10 : 0x00;
			if (x < -255)
			{
				// min possible value + overflow flag
				ps2_mouse[0] |= 0x40;
				ps2_mouse[1] = 1; // -255
			}
			else if (x > 255)
			{
				// max possible value + overflow flag
				ps2_mouse[0] |= 0x40;
				ps2_mouse[1] = 255;
			}
			else
			{
				ps2_mouse[1] = (char)x;
			}

			// ------ Y axis -----------
			// store sign bit in first byte
			y = -y;
			ps2_mouse[0] |= (y < 0) ? 0x20 : 0x00;
			if (y < -255)
			{
				// min possible value + overflow flag
				ps2_mouse[0] |= 0x80;
				ps2_mouse[2] = 1; // -255;
			}
			else if (y > 255)
			{
				// max possible value + overflow flag
				ps2_mouse[0] |= 0x80;
				ps2_mouse[2] = 255;
			}
			else
			{
				ps2_mouse[2] = (char)y;
			}

			if (w > 63) w = 63;
			else if (w < -63) w = -63;

			// collect movement info and send at predefined rate
			if (is_menu() && !video_fb_state()) printf("PS2 MOUSE: %x %d %d %d\n", ps2_mouse[0], ps2_mouse[1], ps2_mouse[2], w);

			if (!osd_is_visible)
			{
				spi_uio_cmd_cont(UIO_MOUSE);
				spi_w(ps2_mouse[0] | ((w & 0x7f) << 8));
				spi_w(ps2_mouse[1] | ((((uint16_t)b) << 5) & 0xF00));
				spi_w(ps2_mouse[2] | ((((uint16_t)b) << 1) & 0x100));
				DisableIO();
			}
		}
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

void user_io_kbd(uint16_t key, int press)
{
	if(is_menu()) spi_uio_cmd(UIO_KEYBOARD); //ping the Menu core to wakeup

	// Win+PrnScr or Alt/Win+ScrLk - screen shot
	bool key_WinPrnScr = (key == KEY_SYSRQ && (get_key_mod() & (RGUI | LGUI)));
	// Excluding scroll lock for PS/2 so Win+ScrLk can be used to change the emu mode.
	bool key_AltWinScrLk = (key == KEY_SCROLLLOCK && (get_key_mod() & (LALT | RALT | RGUI | LGUI))) && !use_ps2ctl;
	if (key_WinPrnScr || key_AltWinScrLk)
	{
		int shift = (get_key_mod() & LSHIFT);
		if (press == 1)
		{
			printf("print key pressed - do screen shot\n");
			user_io_screenshot(nullptr,!shift);
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
				if (is_menu() && !video_fb_state()) printf("PS2 code(break)%s for core: %d(0x%X)\n", (code & EXT) ? "(ext)" : "", code & 255, code & 255);

				if (key == KEY_MENU) key = KEY_F12;
				if (osd_is_visible) menu_key_set(UPSTROKE | key);

				//don't block depress so keys won't stick in core if pressed before OSD.
				send_keycode(key, press);
			}
			else
			{
				if (is_menu() && !video_fb_state()) printf("PS2 code(make)%s for core: %d(0x%X)\n", (code & EXT) ? "(ext)" : "", code & 255, code & 255);
				if (!osd_is_visible && !is_menu() && key == KEY_MENU && press == 3) open_joystick_setup();
				else if ((has_menu() || osd_is_visible || (get_key_mod() & (LALT | RALT | RGUI | LGUI))) && (((key == KEY_F12) && ((!is_x86() && !is_archie()) || (get_key_mod() & (RGUI | LGUI)))) || key == KEY_MENU))
				{
					if (press == 1) menu_key_set(KEY_F12);
				}
				else if (osd_is_visible)
				{
					if (press == 1) menu_key_set(key);
				}
				else
				{
					// When ps2ctl is set then the RGUI or LGUI key must be held in addition
					// to the EMU_SWITCH_1 or EMU_SWITCH_2. This allows for cores such as AO486
					// to pass through the Scroll Lock and Num Lock keys.
					bool ps2ctl_modifier = (get_key_mod() & (RGUI | LGUI)) || !use_ps2ctl;
					bool key_EMU_SWITCH_1 = (code & EMU_SWITCH_1) && ps2ctl_modifier;
					bool key_EMU_SWITCH_2 = (code & EMU_SWITCH_2) && ps2ctl_modifier && !is_archie();

					if (( key_EMU_SWITCH_1 || key_EMU_SWITCH_2 ) && !is_menu())
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

static struct { const char *fmtstr; Imlib_Load_Error errno; } err_strings[] = {
  {"file '%s' does not exist", IMLIB_LOAD_ERROR_FILE_DOES_NOT_EXIST},
  {"file '%s' is a directory", IMLIB_LOAD_ERROR_FILE_IS_DIRECTORY},
  {"permission denied to read file '%s'", IMLIB_LOAD_ERROR_PERMISSION_DENIED_TO_READ},
  {"no loader for the file format used in file '%s'", IMLIB_LOAD_ERROR_NO_LOADER_FOR_FILE_FORMAT},
  {"path for file '%s' is too long", IMLIB_LOAD_ERROR_PATH_TOO_LONG},
  {"a component of path '%s' does not exist", IMLIB_LOAD_ERROR_PATH_COMPONENT_NON_EXISTANT},
  {"a component of path '%s' is not a directory", IMLIB_LOAD_ERROR_PATH_COMPONENT_NOT_DIRECTORY},
  {"path '%s' has too many symbolic links", IMLIB_LOAD_ERROR_TOO_MANY_SYMBOLIC_LINKS},
  {"ran out of file descriptors trying to access file '%s'", IMLIB_LOAD_ERROR_OUT_OF_FILE_DESCRIPTORS},
  {"denied write permission for file '%s'", IMLIB_LOAD_ERROR_PERMISSION_DENIED_TO_WRITE},
  {"out of disk space writing to file '%s'", IMLIB_LOAD_ERROR_OUT_OF_DISK_SPACE},
  {(const char *)NULL, (Imlib_Load_Error) 0}
};

static void print_imlib_load_error (Imlib_Load_Error err, const char *filepath) {
  int i;
  for (i = 0; err_strings[i].fmtstr != NULL; i++) {
    if (err == err_strings[i].errno) {
	printf("Screenshot Error (%d): ",err);
	printf(err_strings[i].fmtstr,filepath);
	printf("\n");
      return ;
    }
  }
  /* Unrecognised error */
    printf("Screenshot Error (%d): unrecognized error accessing file '%s'\n",err,filepath);
  return ;
}

bool user_io_screenshot(const char *pngname, int rescale)
{
	mister_scaler *ms = mister_scaler_init();
	if (ms == NULL)
	{
		printf("problem with scaler, maybe not a new enough version\n");
		Info("Scaler not compatible");
		return false;
	}
	else
	{
		const char *basename = last_filename;
		if( pngname && *pngname )
			basename = pngname;
		unsigned char *outputbuf = (unsigned char *)calloc(ms->width*ms->height * 4, 1);
		// read the image into the outpubuf - RGBA format
		mister_scaler_read_32(ms,outputbuf);
		// using_data will keep a pointer and dispose of the outbuf
		Imlib_Image im = imlib_create_image_using_data(ms->width,ms->height,(unsigned int *)outputbuf);
		imlib_context_set_image(im);

		static char filename[1024];
		FileGenerateScreenshotName(basename, filename, 1024);

		/* do we want to save a rescaled image? */
		if (rescale)
		{
			Imlib_Image im_scaled=imlib_create_cropped_scaled_image(0,0,ms->width,ms->height,ms->output_width,ms->output_height);
			imlib_free_image_and_decache();
			imlib_context_set_image(im_scaled);
		}
		Imlib_Load_Error error;
		imlib_save_image_with_error_return(getFullPath(filename),&error);
		if (error != IMLIB_LOAD_ERROR_NONE)
		{
			print_imlib_load_error (error, filename);
			Info("error in saving png");
			return false;
		}
		imlib_free_image_and_decache();
		mister_scaler_free(ms);
		free(outputbuf);
		char msg[1024];
		snprintf(msg, 1024, "Screen saved to\n%s", filename + strlen(SCREENSHOT_DIR"/"));
		Info(msg);
	}
	return true;
}

void user_io_screenshot_cmd(const char *cmd)
{
	if( strncmp( cmd, "screenshot", 10 ))
	{
		return;
	}

	cmd += 10;
	while( *cmd != '\0' && ( *cmd == '\t' || *cmd == ' ' || *cmd == '\n' ) )
		cmd++;

	user_io_screenshot(cmd,0);
}
