/*
Copyright 2005, 2006, 2007 Dennis van Weeren
Copyright 2008, 2009 Jakub Bednarski

This file is part of Minimig

Minimig is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

Minimig is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// 2009-11-14   - OSD labels changed
// 2009-12-15   - added display of directory name extensions
// 2010-01-09   - support for variable number of tracks
// 2016-06-01   - improvements to 8-bit menu

#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>
#include <fcntl.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <stdbool.h>
#include <stdio.h>
#include <sched.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <libgen.h>
#include <bluetooth.h>
#include <hci.h>
#include <hci_lib.h>

#include "file_io.h"
#include "osd.h"
#include "hardware.h"
#include "menu.h"
#include "user_io.h"
#include "debug.h"
#include "fpga_io.h"
#include "cfg.h"
#include "input.h"
#include "battery.h"
#include "cheats.h"
#include "video.h"
#include "audio.h"
#include "joymapping.h"
#include "recent.h"
#include "support.h"
#include "bootcore.h"
#include "ide.h"

/*menu states*/
enum MENU
{
	MENU_NONE1,
	MENU_NONE2,
	MENU_SYSTEM1,
	MENU_SYSTEM2,
	MENU_COMMON1,
	MENU_COMMON2,
	MENU_MISC1,
	MENU_MISC2,

	MENU_INFO,

	MENU_FILE_SELECT1,
	MENU_FILE_SELECT2,
	MENU_CORE_FILE_SELECTED1,
	MENU_CORE_FILE_SELECTED2,
	MENU_CORE_FILE_CANCELED,
	MENU_RECENT1,
	MENU_RECENT2,
	MENU_RECENT3,
	MENU_RECENT4,
	MENU_ABOUT1,
	MENU_ABOUT2,
	MENU_RESET1,
	MENU_RESET2,

	MENU_JOYSYSMAP,
	MENU_JOYDIGMAP,
	MENU_JOYDIGMAP1,
	MENU_JOYDIGMAP2,
	MENU_JOYDIGMAP3,
	MENU_JOYDIGMAP4,
	MENU_JOYRESET,
	MENU_JOYRESET1,
	MENU_JOYKBDMAP,
	MENU_JOYKBDMAP1,
	MENU_KBDMAP,
	MENU_KBDMAP1,
	MENU_BTPAIR,
	MENU_BTPAIR2,
	MENU_LGCAL,
	MENU_LGCAL1,
	MENU_LGCAL2,

	MENU_SCRIPTS_PRE,
	MENU_SCRIPTS_PRE1,
	MENU_SCRIPTS,
	MENU_SCRIPTS1,
	MENU_SCRIPTS_FB,
	MENU_SCRIPTS_FB2,

	MENU_CHEATS1,
	MENU_CHEATS2,

	MENU_UART1,
	MENU_UART2,
	MENU_UART3,
	MENU_UART4,
	MENU_BAUD1,
	MENU_BAUD2,

	MENU_SFONT_FILE_SELECTED,

	MENU_VIDEOPROC1,
	MENU_VIDEOPROC2,
	MENU_COEFF_FILE_SELECTED,
	MENU_GAMMA_FILE_SELECTED,
	MENU_SMASK_FILE_SELECTED,
	MENU_PRESET_FILE_SELECTED,

	MENU_AFILTER_FILE_SELECTED,

	// Generic
	MENU_GENERIC_MAIN1,
	MENU_GENERIC_MAIN2,
	MENU_GENERIC_FILE_SELECTED,
	MENU_GENERIC_IMAGE_SELECTED,
	MENU_GENERIC_SAVE_WAIT,

	// Arcade
	MENU_ARCADE_DIP1,
	MENU_ARCADE_DIP2,

	// Minimig
	MENU_MINIMIG_MAIN1,
	MENU_MINIMIG_MAIN2,
	MENU_MINIMIG_VIDEO1,
	MENU_MINIMIG_VIDEO2,
	MENU_MINIMIG_CHIPSET1,
	MENU_MINIMIG_CHIPSET2,
	MENU_MINIMIG_DISK1,
	MENU_MINIMIG_DISK2,
	MENU_MINIMIG_HDFFILE_SELECTED,
	MENU_MINIMIG_ADFFILE_SELECTED,
	MENU_MINIMIG_ROMFILE_SELECTED,
	MENU_MINIMIG_LOADCONFIG1,
	MENU_MINIMIG_LOADCONFIG2,
	MENU_MINIMIG_SAVECONFIG1,
	MENU_MINIMIG_SAVECONFIG2,

	// Atari ST
	MENU_ST_MAIN1,
	MENU_ST_MAIN2,
	MENU_ST_SYSTEM1,
	MENU_ST_SYSTEM2,
	MENU_ST_FDD_FILE_SELECTED,
	MENU_ST_HDD_FILE_SELECTED,
	MENU_ST_SYSTEM_FILE_SELECTED,
	MENU_ST_LOAD_CONFIG1,
	MENU_ST_LOAD_CONFIG2,
	MENU_ST_SAVE_CONFIG1,
	MENU_ST_SAVE_CONFIG2,

	// Archie
	MENU_ARCHIE_MAIN1,
	MENU_ARCHIE_MAIN2,
	MENU_ARCHIE_MAIN_FILE_SELECTED,

	// MT32-pi
	MENU_MT32PI_MAIN1,
	MENU_MT32PI_MAIN2,
};

static uint32_t menustate = MENU_NONE1;
static uint32_t parentstate;
static uint32_t menusub = 0;
static uint32_t menusub_last = 0; //for when we allocate it dynamically and need to know last row
static uint64_t menumask = 0; // Used to determine which rows are selectable...
static uint32_t menu_timer = 0;
static uint32_t menu_save_timer = 0;
static uint32_t load_addr = 0;
static int32_t  bt_timer = 0;

extern const char *version;

const char *config_tos_wrprot[] = { "None", "A:", "B:", "A: and B:" };

const char *config_scanlines_msg[] = { "Off", "HQ2x", "CRT 25%" , "CRT 50%" , "CRT 75%" };
const char *config_blank_msg[] = { "Blank", "Blank+" };
const char *config_dither_msg[] = { "off", "SPT", "RND", "S+R" };
const char *config_autofire_msg[] = { "        AUTOFIRE OFF", "        AUTOFIRE FAST", "        AUTOFIRE MEDIUM", "        AUTOFIRE SLOW" };
const char *config_cd32pad_msg[] = { "OFF", "ON" };
const char *config_button_turbo_msg[] = { "OFF", "FAST", "MEDIUM", "SLOW" };
const char *config_button_turbo_choice_msg[] = { "A only", "B only", "A & B" };
const char *joy_button_map[] = { "RIGHT", "LEFT", "DOWN", "UP", "BUTTON A", "BUTTON B", "BUTTON X", "BUTTON Y", "BUTTON L", "BUTTON R", "SELECT", "START", "KBD TOGGLE", "MENU", "    Stick 1: Tilt RIGHT", "    Stick 1: Tilt DOWN", "   Mouse emu X: Tilt RIGHT", "   Mouse emu Y: Tilt DOWN" };
const char *joy_ana_map[] = { "    DPAD test: Press RIGHT", "    DPAD test: Press DOWN", "   Stick 1 Test: Tilt RIGHT", "   Stick 1 Test: Tilt DOWN", "   Stick 2 Test: Tilt RIGHT", "   Stick 2 Test: Tilt DOWN" };
const char *config_stereo_msg[] = { "0%", "25%", "50%", "100%" };
const char *config_uart_msg[] = { "      None", "       PPP", "   Console", "      MIDI", "     Modem"};
const char *config_midilink_mode[] = {"Local", "Local", "  USB", "  UDP", "-----", "-----", "  USB" };
const char *config_afilter_msg[] = { "Internal","Custom" };
const char *config_smask_msg[] = { "None", "1x", "2x", "1x Rotated", "2x Rotated" };
const char *config_scale[] = { "Normal", "V-Integer", "HV-Integer-", "HV-Integer+", "HV-Integer", "???", "???", "???" };

#define DPAD_NAMES 4
#define DPAD_BUTTON_NAMES 12  //DPAD_NAMES + 6 buttons + start/select

#define script_line_length 1024
#define script_lines 50
static FILE *script_pipe;
static int script_file;
static char script_command[script_line_length];
static int script_line;
static char script_output[script_lines][script_line_length];
static char script_line_output[script_line_length];
static bool script_finished;

// one screen width
static const char* HELPTEXT_SPACER = "                                ";
static char helptext_custom[1024];

enum HelpText_Message
{
	HELPTEXT_NONE, HELPTEXT_CUSTOM, HELPTEXT_MAIN, HELPTEXT_HARDFILE, HELPTEXT_CHIPSET, HELPTEXT_MEMORY, HELPTEXT_EJECT, HELPTEXT_CLEAR
};

static const char *helptexts[] =
{
	0,
	helptext_custom,
	"                                Welcome to MiSTer! Use the cursor keys to navigate the menus. Use space bar or enter to select an item. Press Esc or F12 to exit the menus. Joystick emulation on the numeric keypad can be toggled with the numlock or scrlock key, while pressing Ctrl-Alt-0 (numeric keypad) toggles autofire mode.",
	"                                Minimig can emulate an A600/A1200 IDE harddisk interface. The emulation can make use of Minimig-style hardfiles (complete disk images) or UAE-style hardfiles (filesystem images with no partition table).",
	"                                Minimig's processor core can emulate a 68000 (cycle accuracy as A500/A600) or 68020 (maximum performance) processor with transparent cache.",
	"                                Minimig can make use of up to 2 megabytes of Chip RAM, up to 1.5 megabytes of Slow RAM (A500 Trapdoor RAM), and up to 384 megabytes of Fast RAM (8MB max for 68000 mode). To use the HRTmon feature you will need a file on the SD card named hrtmon.rom.",
	"                                Backspace key (or B-hold + A on gamepad) to unmount",
	"                                Backspace key (or B-hold + A on gamepad) to clear stored option. You have to reload the core to be able to use default value.",
};

static const uint32_t helptext_timeouts[] =
{
	10000,
	10000,
	10000,
	10000,
	10000,
	10000,
	2000,
	2000
};

static const char *info_top = "\x80\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x82";
static const char *info_bottom = "\x85\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x84";

// file selection menu variables
static char fs_pFileExt[13] = "xxx";
static uint32_t fs_ExtLen = 0;
static uint32_t fs_Options;
static uint32_t fs_MenuSelect;
static uint32_t fs_MenuCancel;

static char* GetExt(char *ext)
{
	static char extlist[32];
	char *p = extlist;

	while (*ext) {
		strcpy(p, ",");
		strncat(p, ext, 3);
		while (*(p + strlen(p) - 1) == ' ') *(p + strlen(p) - 1) = 0;
		if (strlen(ext) <= 3) break;
		ext += 3;
		p += strlen(p);
	}

	return extlist + 1;
}

static char SelectedDir[1024] = {};
static char SelectedLabel[1024] = {};

static char Selected_F[16][1024] = {};
static char Selected_S[16][1024] = {};
static char Selected_tmp[1024] = {};

void StoreIdx_F(int idx, const char *path)
{
	strcpy(Selected_F[idx], path);
}

void StoreIdx_S(int idx, const char *path)
{
	strcpy(Selected_S[idx], path);
}

static char selPath[1024] = {};

static int changeDir(char *dir)
{
	char curdir[128];
	memset(curdir, 0, sizeof(curdir));
	if(!dir || !strcmp(dir, ".."))
	{
		if (!strlen(selPath))
		{
			return 0;
		}

		char *p = strrchr(selPath, '/');
		if (p)
		{
			*p = 0;
			strncpy(curdir, p + 1, sizeof(curdir) - 1);
		}
		else
		{
			strncpy(curdir, selPath, sizeof(curdir) - 1);
			selPath[0] = 0;
		}
	}
	else
	{
		if (strlen(selPath) + strlen(dir) > sizeof(selPath) - 100)
		{
			return 0;
		}

		if (strlen(selPath)) strcat(selPath, "/");
		strcat(selPath, dir);
	}

	ScanDirectory(selPath, SCANF_INIT, fs_pFileExt, fs_Options);
	if(curdir[0])
	{
		ScanDirectory(selPath, SCANF_SET_ITEM, curdir, fs_Options);
	}
	return 1;
}

static const char *home_dir = NULL;
static char filter[256] = {};
static unsigned long filter_typing_timer = 0;

// this function displays file selection menu
void SelectFile(const char* path, const char* pFileExt, int Options, unsigned char MenuSelect, unsigned char MenuCancel)
{
	static char tmp[1024];
	printf("pFileExt = %s\n", pFileExt);
	filter_typing_timer = 0;
	filter[0] = 0;

	strncpy(selPath, path, sizeof(selPath) - 1);
	selPath[sizeof(selPath) - 1] = 0;

	if (Options & SCANO_CORES)
	{
		strcpy(selPath, get_rbf_dir());
		if (strlen(get_rbf_name()))
		{
			if(strlen(selPath)) strcat(selPath, "/");
			strcat(selPath, get_rbf_name());
		}
		pFileExt = "RBFMRA";
		home_dir = NULL;
	}
	else if (Options & SCANO_TXT)
	{
		if(pFileExt == 0) pFileExt = "TXT";
		home_dir = NULL;
	}
	else
	{
		const char *home = is_menu() ? "Scripts" : user_io_get_core_path((is_pce() && !strncasecmp(pFileExt, "CUE", 3)) ? PCECD_DIR : NULL, 1);
		home_dir = strrchr(home, '/');
		if (home_dir) home_dir++;
		else home_dir = home;

		if (Options & SCANO_SAVES)
		{
			snprintf(tmp, sizeof(tmp), "%s/%s", SAVE_DIR, CoreName);
			home = tmp;
		}

		if (strncasecmp(home, selPath, strlen(home)) || !strcasecmp(home, selPath) || (!FileExists(selPath) && !PathIsDir(selPath)))
		{
			Options &= ~SCANO_NOENTER;
			strcpy(selPath, home);
		}
	}

	ScanDirectory(selPath, SCANF_INIT, pFileExt, Options);
	AdjustDirectory(selPath);

	strcpy(fs_pFileExt, pFileExt);
	fs_ExtLen = strlen(fs_pFileExt);
	fs_Options = Options & ~SCANO_NOENTER;
	fs_MenuSelect = MenuSelect;
	fs_MenuCancel = MenuCancel;

	menustate = MENU_FILE_SELECT1;
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

#define STD_EXIT       "            exit"
#define STD_BACK       "            back"
#define STD_SPACE_EXIT "        SPACE to exit"
#define STD_COMBO_EXIT "      Ctrl+ESC to exit"

int getOptIdx(char *opt)
{
	if ((opt[1] >= '0') && (opt[1] <= '9')) return opt[1] - '0';
	if ((opt[1] >= 'A') && (opt[1] <= 'V')) return opt[1] - 'A' + 10;
	return 0; // basically 0 cannot be valid because used as a reset. Thus can be used as a error.
}

uint32_t getStatus(char *opt, uint32_t status)
{
	char idx1 = getOptIdx(opt);
	char idx2 = getOptIdx(opt + 1);
	uint32_t x = (status & (1 << idx1)) ? 1 : 0;

	if (idx2>idx1) {
		x = status >> idx1;
		x = x & ~(0xffffffff << (idx2 - idx1 + 1));
	}

	return x;
}

uint32_t setStatus(char *opt, uint32_t status, uint32_t value)
{
	unsigned char idx1 = getOptIdx(opt);
	unsigned char idx2 = getOptIdx(opt + 1);
	uint32_t x = 1;

	if (idx2>idx1) x = ~(0xffffffff << (idx2 - idx1 + 1));
	x = x << idx1;

	return (status & ~x) | ((value << idx1) & x);
}

uint32_t getStatusMask(char *opt)
{
	char idx1 = getOptIdx(opt);
	char idx2 = getOptIdx(opt + 1);
	uint32_t x = 1;

	if (idx2 > idx1) x = ~(0xffffffff << (idx2 - idx1 + 1));
	return x;
}

// conversion table of Amiga keyboard scan codes to ASCII codes
static const uint8_t keycode_table[128] =
{
	0,'1','2','3','4','5','6','7','8','9','0',  0,  0,  0,  0,  0,
	'Q','W','E','R','T','Y','U','I','O','P',  0,  0,  0,  0,  0,  0,
	'A','S','D','F','G','H','J','K','L',  0,  0,  0,  0,  0,  0,  0,
	0,'Z','X','C','V','B','N','M',  0,  0,  0,  0,  0,  0,  0,  0,
	0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
	0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  0,  0,
	0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0
};

static uint8_t GetASCIIKey(uint32_t keycode)
{
	if (keycode & UPSTROKE)
		return 0;

	return keycode_table[get_amiga_code(keycode & 0xFFFF) & 0x7F];
}

/* the Atari core handles OSD keys competely inside the core */
static uint32_t menu_key = 0;

void menu_key_set(unsigned int c)
{
	//printf("OSD enqueue: %x\n", c);
	menu_key = c;
}

// get key status
static int hold_cnt = 0;
static uint32_t menu_key_get(void)
{
	static uint32_t prev_key = 0;
	static unsigned long db_time = 0;
	if (prev_key != menu_key || !db_time)
	{
		prev_key = menu_key;
		db_time = GetTimer(20);
	}

	uint32_t c = 0;
	if (CheckTimer(db_time))
	{
		static uint32_t c2;
		static unsigned long repeat;
		uint32_t c1;

		c1 = menu_key;
		c = 0;
		if (c1 != c2)
		{
			c = c1;
			hold_cnt = 1;
		}
		c2 = c1;

		// generate repeat "key-pressed" events
		if ((c1 & UPSTROKE) || (!c1))
		{
			hold_cnt = 0;
			repeat = GetTimer(REPEATDELAY);
		}
		else if (CheckTimer(repeat))
		{
			repeat = GetTimer(REPEATRATE);
			if (GetASCIIKey(c1) || ((menustate == MENU_COMMON2) && (menusub == 15)) || ((menustate == MENU_SYSTEM2) && (menusub == 4)))
			{
				c = c1;
				hold_cnt++;
			}
		}
	}

	// currently no key pressed
	if (!c)
	{
		static unsigned long longpress = 0, longpress_consumed = 0;
		static unsigned char last_but = 0;
		unsigned char but = user_io_menu_button();

		if (but && !last_but) longpress = GetTimer(3000);
		if (but && CheckTimer(longpress) && !longpress_consumed)
		{
			longpress_consumed = 1;
			if (menustate == MENU_SCRIPTS1) c = KEY_BACKSPACE;
			else menustate = MENU_BTPAIR;
		}

		if (!but && last_but && !longpress_consumed) c = KEY_F12;

		if (!but) longpress_consumed = 0;
		last_but = but;
	}

	if (!c)
	{
		static unsigned long longpress = 0, longpress_consumed = 0;
		static unsigned char last_but = 0;
		unsigned char but = user_io_user_button();

		if (user_io_osd_is_visible())
		{
			if (but && !last_but) longpress = GetTimer(1500);
			if (but && CheckTimer(longpress) && !longpress_consumed)
			{
				longpress_consumed = 1;
				if (is_menu())
				{
					if (menustate == MENU_SYSTEM2 || menustate == MENU_FILE_SELECT2) menustate = MENU_JOYSYSMAP;
				}
				else if (get_map_vid() || get_map_pid())
				{
					menustate = MENU_JOYRESET;
				}
			}

			if (!but && last_but && !longpress_consumed)
			{
				if (get_map_vid() || get_map_pid())
				{
					send_map_cmd(KEY_ALTERASE);
				}
			}
		}

		if (!but) longpress_consumed = 0;
		last_but = but;
	}

	return(c);
}

static char* getNet(int spec)
{
	int netType = 0;
	struct ifaddrs *ifaddr, *ifa, *ifae = 0, *ifaw = 0;
	static char host[NI_MAXHOST];

	if (getifaddrs(&ifaddr) == -1)
	{
		printf("getifaddrs: error\n");
		return NULL;
	}

	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
	{
		if (ifa->ifa_addr == NULL) continue;
		if (!memcmp(ifa->ifa_addr->sa_data, "\x00\x00\xa9\xfe", 4)) continue; // 169.254.x.x

		if ((strcmp(ifa->ifa_name, "eth0") == 0)     && (ifa->ifa_addr->sa_family == AF_INET)) ifae = ifa;
		if ((strncmp(ifa->ifa_name, "wlan", 4) == 0) && (ifa->ifa_addr->sa_family == AF_INET)) ifaw = ifa;
	}

	ifa = 0;
	netType = 0;
	if (ifae && (!spec || spec == 1))
	{
		ifa = ifae;
		netType = 1;
	}

	if (ifaw && (!spec || spec == 2))
	{
		ifa = ifaw;
		netType = 2;
	}

	if (spec && ifa)
	{
		strcpy(host, "IP: ");
		getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host + strlen(host), NI_MAXHOST - strlen(host), NULL, 0, NI_NUMERICHOST);
	}

	freeifaddrs(ifaddr);
	return spec ? (ifa ? host : 0) : (char*)netType;
}

static long sysinfo_timer;
static void infowrite(int pos, const char* txt)
{
	char str[40];
	memset(str, 0x20, 29);
	int len = strlen(txt);
	if (len > 27) len = 27;
	if(len) strncpy(str + 1+ ((27-len)/2), txt, len);
	str[0] = 0x83;
	str[28] = 0x83;
	str[29] = 0;
	OsdWrite(pos, str, 0, 0);
}

static void printSysInfo()
{
	if (!sysinfo_timer || CheckTimer(sysinfo_timer))
	{
		sysinfo_timer = GetTimer(2000);
		struct battery_data_t bat;
		int hasbat = getBattery(0, &bat);
		int n = 3;

		char str[40];
		OsdWrite(n++, info_top, 0, 0);
		if (!hasbat)
		{
			infowrite(n++, "");
		}

		int j = 0;
		char *net;
		net = getNet(1);
		if (net)
		{
			sprintf(str, "\x1c %s", net);
			infowrite(n++, str);
			j++;
		}
		net = getNet(2);
		if (net)
		{
			sprintf(str, "\x1d %s", net);
			infowrite(n++, str);
			j++;
		}
		if (!j) infowrite(n++, "No network");
		if (j<2) infowrite(n++, "");

		if (hasbat)
		{
			sprintf(str, "\x1F ");
			if (bat.capacity == -1) strcat(str, "n/a");
			else sprintf(str + strlen(str), "%d%%", bat.capacity);
			if (bat.current != -1) sprintf(str + strlen(str), " %dmAh", bat.current);
			if (bat.voltage != -1) sprintf(str + strlen(str), " %d.%dV", bat.voltage / 1000, (bat.voltage / 100) % 10);

			infowrite(n++, str);

			str[0] = 0;
			if (bat.load_current > 0)
			{
				sprintf(str + strlen(str), " \x12 %dmA", bat.load_current);
				if (bat.time != -1)
				{
					if (bat.time < 90) sprintf(str + strlen(str), ", ETA: %dm", bat.time);
					else sprintf(str + strlen(str), ", ETA: %dh%02dm", bat.time / 60, bat.time % 60);
				}
			}
			else if (bat.load_current < -1)
			{
				sprintf(str + strlen(str), " \x13 %dmA", -bat.load_current);
				if (bat.time != -1)
				{
					if (bat.time < 90) sprintf(str + strlen(str), ", ETA: %dm", bat.time);
					else sprintf(str + strlen(str), ", ETA: %dh%02dm", bat.time / 60, bat.time % 60);
				}
			}
			else
			{
				strcat(str, "Not charging");
			}
			infowrite(n++, str);
		}
		else
		{
			infowrite(n++, "");
		}
		OsdWrite(n++, info_bottom, 0, 0);
	}
}

static int  firstmenu = 0;
static int  adjvisible;

static void MenuWrite(unsigned char n, const char *s = "", unsigned char invert = 0, unsigned char stipple = 0, int arrow = 0)
{
	int row = n - firstmenu;

	if (row < 0)
	{
		if (invert) adjvisible = row;
		return;
	}

	if (row >= OsdGetSize())
	{
		if (invert) adjvisible = row - OsdGetSize() + 1;
		return;
	}

	OsdSetArrow(arrow);
	OsdWriteOffset(row, s, invert, stipple, 0, (row == 0 && firstmenu) ? 17 : (row == (OsdGetSize()-1) && !arrow) ? 16 : 0, 0);
}

const char* get_rbf_name_bootcore(char *str)
{
	if (!strlen(cfg.bootcore)) return "";
	char *p = strrchr(str, '/');
	if (!p) return str;

	char *spl = strrchr(p + 1, '.');
	if (spl && (!strcmp(spl, ".rbf") || !strcmp(spl, ".mra")))
	{
		*spl = 0;
	}
	else
	{
		return NULL;
	}
	return p + 1;
}

static void vga_nag()
{
	if (video_fb_state())
	{
		EnableOsd_on(OSD_VGA);
		OsdSetSize(16);
		OsdSetTitle("Information");
		int n = 0;
		OsdWrite(n++);
		OsdWrite(n++);
		OsdWrite(n++);
		OsdWrite(n++);
		OsdWrite(n++, "  If you see this, then you");
		OsdWrite(n++, "  need to modify MiSTer.ini");
		OsdWrite(n++);
		OsdWrite(n++, " Either disable framebuffer:");
		OsdWrite(n++, "       fb_terminal=0");
		OsdWrite(n++);
		OsdWrite(n++, "  or enable scaler on VGA:");
		OsdWrite(n++, "       vga_scaler=1");
		for (; n < OsdGetSize(); n++) OsdWrite(n);
		OsdUpdate();
		OsdEnable(OSD_MSG);
		EnableOsd_on(OSD_HDMI);
	}

	OsdDisable();
	EnableOsd_on(OSD_ALL);
}

void process_addon(char *ext, uint8_t idx)
{
	static char name[1024];

	while (*ext && *ext != ',') ext++;
	if (*ext) ext++;
	if (!*ext) return;

	printf("addons: %s\n", ext);

	int i = 0;
	while (1)
	{
		char *fname = name;
		strcpy(name, selPath);
		char *p = strrchr(name, '.');
		if (!p) p = name + strlen(name);
		*p++ = '.';

		substrcpy(p, ext, i);
		if (!strlen(p)) return;
		if (*p == '!')
		{
			*p = 0;
			char *bs = strrchr(name, '/');
			if (!bs)
			{
				fname = p + 1;
			}
			else
			{
				strcpy(bs + 1, p + 1);
			}
		}

		printf("Trying: %s\n", fname);
		user_io_file_tx_a(fname, ((i+1) << 8) | idx);
		i++;
	}
}

static int get_arc(const char *str)
{
	int arc = 0;
	if (!strcmp(str, "[ARC1]")) arc = 1;
	else if(!strcmp(str, "[ARC2]")) arc = 2;
	else return 0;

	uint32_t x = 0, y = 0;
	if (sscanf(cfg.custom_aspect_ratio[arc - 1], "%u:%u", &x, &y) != 2 || x < 1 || x > 4095 || y < 1 || y > 4095) arc = -1;

	return arc;
}

static int get_ar_name(int ar, char *str)
{
	switch (ar)
	{
	case 0:
		strcat(str, "Original");
		break;

	case 1:
		strcat(str, "Full Screen");
		break;

	case 2:
		if (get_arc("[ARC1]") <= 0)
		{
			strcat(str, "Original");
			ar = 0;
		}
		else
		{
			strcat(str, cfg.custom_aspect_ratio[0]);
		}
		break;

	case 3:
		if (get_arc("[ARC2]") <= 0)
		{
			strcat(str, "Original");
			ar = 0;
		}
		else
		{
			strcat(str, cfg.custom_aspect_ratio[1]);
		}
		break;
	}

	return ar;
}

static int next_ar(int ar, int minus)
{
	if (minus)
	{
		ar = (ar - 1) & 3;
		while (1)
		{
			if (ar == 3 && get_arc("[ARC2]") > 0 && get_arc("[ARC1]") > 0) break;
			if (ar == 2 && get_arc("[ARC1]") > 0) break;
			if (ar < 2) break;
			ar--;
		}
	}
	else
	{
		ar = (ar + 1) & 3;
		if (ar == 3 && get_arc("[ARC2]") <= 0) ar = 0;
		if (ar == 2 && get_arc("[ARC1]") <= 0) ar = 0;
	}

	return ar;
}

static int joymap_first = 0;

static int gun_x = 0;
static int gun_y = 0;
static int gun_ok = 0;
static int gun_side = 0;
static int gun_idx = 0;
static uint16_t gun_pos[4] = {};
static int page = 0;

void HandleUI(void)
{
	if (bt_timer >= 0)
	{
		if (!bt_timer) bt_timer = (int32_t)GetTimer(6000);
		else if (CheckTimer((uint32_t)bt_timer))
		{
			bt_timer = -1;
			if (hci_get_route(0) < 0)
			{
				// Some BT dongles get stuck after boot.
				// Kicking of USB port usually make it work.
				printf("*** reset bt ***\n");
				system("/bin/bluetoothd hcireset &");
			}
		}
	}

	switch (user_io_core_type())
	{
	case CORE_TYPE_8BIT:
	case CORE_TYPE_SHARPMZ:
		break;

	default:
		// No UI in unknown cores.
		return;
	}

	static char opensave;
	static char ioctl_index;
	char *p;
	static char s[256];
	unsigned char m = 0, up, down, select, menu, back, right, left, plus, minus, recent;
	char enable;
	static int reboot_req = 0;
	static uint32_t helptext_timer;
	static int helptext_idx = 0;
	static int helptext_idx_old = 0;
	static char helpstate = 0;
	static char flag;
	static int cr = 0;
	static uint32_t cheatsub = 0;
	static uint8_t card_cid[32];
	static uint32_t hdmask = 0;
	static pid_t ttypid = 0;
	static int has_fb_terminal = 0;
	static unsigned long flash_timer = 0;
	static int flash_state = 0;
	static uint32_t dip_submenu, dip2_submenu, dipv;
	static int need_reset = 0;
	static int flat = 0;
	static int menusub_parent = 0;
	static char title[32] = {};
	static uint32_t saved_menustate = 0;
	static char addon[1024];
	static int store_name;
	static int vfilter_type;

	static char	cp_MenuCancel;

	// get user control codes
	uint32_t c = menu_key_get();

	int release = 0;
	if (c & UPSTROKE) release = 1;

	// decode and set events
	menu = false;
	back = false;
	select = false;
	up = false;
	down = false;
	left = false;
	right = false;
	plus = false;
	minus = false;
	recent = false;

	if (c && cfg.bootcore[0] != '\0') cfg.bootcore[0] = '\0';

	if (is_menu() && cfg.osd_timeout >= 5)
	{
		static int menu_visible = 1;
		static unsigned long timeout = 0;
		if (!video_fb_state() && cfg.fb_terminal)
		{
			if (timeout && CheckTimer(timeout))
			{
				timeout = 0;
				if (menu_visible > 0)
				{
					menu_visible = 0;
					video_menu_bg((user_io_8bit_set_status(0, 0) & 0xE) >> 1, 1);
					OsdMenuCtl(0);
				}
				else if (!menu_visible)
				{
					menu_visible--;
					video_menu_bg((user_io_8bit_set_status(0, 0) & 0xE) >> 1, 2);
				}
			}

			if (c || menustate != MENU_FILE_SELECT2)
			{
				timeout = 0;
				if (menu_visible <= 0)
				{
					c = 0;
					menu_visible = 1;
					video_menu_bg((user_io_8bit_set_status(0, 0) & 0xE) >> 1);
					OsdMenuCtl(1);
				}
			}

			if (!timeout)
			{
				timeout = GetTimer(cfg.osd_timeout * 1000);
			}
		}
		else
		{
			timeout = 0;
			menu_visible = 1;
		}
	}

	//prevent OSD control while script is executing on framebuffer
	if (!video_fb_state() || video_chvt(0) != 2)
	{
		switch (c)
		{
		case KEY_F12:
			menu = true;
			menu_key_set(KEY_F12 | UPSTROKE);
			if(video_fb_state()) video_menu_bg((user_io_8bit_set_status(0, 0) & 0xE) >> 1);
			video_fb_enable(0);
			break;

		case KEY_F1:
			if (is_menu() && cfg.fb_terminal)
			{
				unsigned long status = (user_io_8bit_set_status(0, 0)+ 2) & 0xE;
				user_io_8bit_set_status(status, 0xE);
				FileSaveConfig(user_io_create_config_name(), &status, 4);
				video_menu_bg(status >> 1);
			}
			break;

		case KEY_F11:
			if (user_io_osd_is_visible() && (menustate != MENU_SCRIPTS1 || script_finished))
			{
				menustate = MENU_BTPAIR;
			}
			break;

		case KEY_F10:
			if (input_has_lightgun())
			{
				menustate = MENU_LGCAL;
			}
			break;

		case KEY_F9:
			if ((is_menu() || ((get_key_mod() & (LALT | RALT)) && (get_key_mod() & (LCTRL | RCTRL))) || has_fb_terminal) && cfg.fb_terminal)
			{
				video_chvt(1);
				video_fb_enable(!video_fb_state());
				if (video_fb_state())
				{
					menustate = MENU_NONE1;
					has_fb_terminal = 1;
				}
			}
			break;

			// Within the menu the esc key acts as the menu key. problem:
			// if the menu is left with a press of ESC, then the follwing
			// break code for the ESC key when the key is released will
			// reach the core which never saw the make code. Simple solution:
			// react on break code instead of make code
		case KEY_ESC | UPSTROKE:
			if (menustate != MENU_NONE2) menu = true;
			break;
		case KEY_BACK | UPSTROKE:
			if (saved_menustate) back = true;
			else menu = true;
			break;
		case KEY_BACKSPACE | UPSTROKE:
			if (saved_menustate) back = true;
			break;
		case KEY_ENTER:
		case KEY_SPACE:
		case KEY_KPENTER:
			select = true;
			break;
		case KEY_UP:
			up = true;
			break;
		case KEY_DOWN:
			down = true;
			break;
		case KEY_LEFT:
			left = true;
			break;
		case KEY_RIGHT:
			right = true;
			break;
		case KEY_KPPLUS:
		case KEY_EQUAL: // =/+
			plus = true;
			break;
		case KEY_KPMINUS:
		case KEY_MINUS: // -/_
			minus = true;
			break;
		case KEY_GRAVE:
			recent = true;
			break;
		}
	}

	if (menu || select || up || down || left || right || (helptext_idx_old != helptext_idx))
	{
		helptext_idx_old = helptext_idx;
		if (helpstate) OsdWrite(OsdGetSize()-1, STD_EXIT, (menumask - ((1 << (menusub + 1)) - 1)) <= 0, 0); // Redraw the Exit line...
		helpstate = 0;
		helptext_timer = GetTimer(helptext_timeouts[helptext_idx]);
	}

	if (helptext_idx)
	{
		if (helpstate<9)
		{
			if (CheckTimer(helptext_timer))
			{
				helptext_timer = GetTimer(32);
				OsdShiftDown(OsdGetSize() - 1);
				++helpstate;
			}
		}
		else if (helpstate == 9)
		{
			ScrollReset(1);
			++helpstate;
		}
		else
		{
			ScrollText(OsdGetSize() - 1, helptexts[helptext_idx], 0, 0, 0, 0, 1);
		}
	}

	// Standardised menu up/down.
	// The screen should set menumask, bit 0 to make the top line selectable, bit 1 for the 2nd line, etc.
	// (Lines in this context don't have to correspond to rows on the OSD.)
	// Also set parentstate to the appropriate menustate.
	if (menumask)
	{
		if (down)
		{
            if((menumask >= ((uint64_t)1 << (menusub + 1))))	// Any active entries left?
            {
			    do
			    {
				    menusub++;
			    } while ((menumask & ((uint64_t)1 << menusub)) == 0);
            }
            else
            {
                menusub = 0; // jump to first item
            }

            menustate = parentstate;
		}

		if (up)
		{
            if (menusub > 0)
            {
			    do
			    {
				    --menusub;
			    } while ((menumask & ((uint64_t)1 << menusub)) == 0);
            }
            else
            {
                do
                {
                    menusub++;
                } while ((menumask & ((uint64_t)(~0) << (menusub + 1))) != 0); // jump to last item
            }
			menustate = parentstate;
		}
	}

    // SHARPMZ Series Menu - This has been located within the sharpmz.cpp code base in order to keep updates to common code
    // base to a minimum and shrink its size. The UI is called with the basic state data and it handles everything internally,
    // only updating values in this function as necessary.
    //
	if (user_io_core_type() == CORE_TYPE_SHARPMZ)
        sharpmz_ui(MENU_NONE1, MENU_NONE2, MENU_COMMON1, MENU_FILE_SELECT1,
			       &parentstate, &menustate, &menusub, &menusub_last,
			       &menumask, /*Selected_F[0]*/ selPath, &helptext_idx, helptext_custom,
			       &fs_ExtLen, &fs_Options, &fs_MenuSelect, &fs_MenuCancel,
			       fs_pFileExt,
			       menu, select, up, down,
			       left, right, plus, minus);

	switch (menustate)
	{
	case MENU_NONE1:
	case MENU_NONE2:
	case MENU_INFO:
		break;

	default:
		saved_menustate = 0;
		break;
	}

	// Switch to current menu screen
	switch (menustate)
	{
		/******************************************************************/
		/* no menu selected                                               */
		/******************************************************************/
	case MENU_NONE1:
		helptext_idx = 0;
		menumask = 0;
		menustate = MENU_NONE2;
		firstmenu = 0;
		vga_nag();
		OsdSetSize(8);
		break;

	case MENU_INFO:
		if (CheckTimer(menu_timer)) menustate = MENU_NONE1;
		// fall through
	case MENU_NONE2:
		if (menu || (is_menu() && !video_fb_state()))
		{
			OsdSetSize(16);
			if(!is_menu() && (get_key_mod() & (LALT | RALT))) //Alt+Menu
			{
				SelectFile("", 0, SCANO_CORES, MENU_CORE_FILE_SELECTED1, MENU_NONE1);
			}
			else if (saved_menustate)
			{
				menustate = saved_menustate;
				menusub = 0;
			}
			else if (is_st()) menustate = MENU_ST_MAIN1;
			else if (is_archie()) menustate = MENU_ARCHIE_MAIN1;
			else {
				if (is_menu())
				{
					SelectFile("", 0, SCANO_CORES, MENU_CORE_FILE_SELECTED1, MENU_SYSTEM1);
				}
				else if (is_minimig())
				{
					menustate = MENU_MINIMIG_MAIN1;
				}
				else
				{
					if ((get_key_mod() & (LGUI | RGUI)) && !is_x86() && has_menu()) //Win+Menu
					{
						menustate = MENU_COMMON1;
					}
					else
					{
						parentstate = MENU_NONE1;
						menustate = MENU_GENERIC_MAIN1;
					}
				}
			}
			menusub = 0;
			OsdClear();
			OsdEnable(DISABLE_KEYBOARD);
		}
		break;

		/******************************************************************/
		/* archimedes main menu                                           */
		/******************************************************************/

	case MENU_ARCHIE_MAIN1:
		OsdSetTitle(CoreName, OSD_ARROW_RIGHT | OSD_ARROW_LEFT);

		m = 0;
		menumask = 0x1fff;

		strcpy(s, " Floppy 0: ");
		strncat(s, get_image_name(0) ? get_image_name(0) : "* no disk *",27);
		OsdWrite(m++, s, menusub == 0);

		strcpy(s, " Floppy 1: ");
		strncat(s, get_image_name(1) ? get_image_name(1) : "* no disk *", 27);
		OsdWrite(m++, s, menusub == 1);

		OsdWrite(m++);

		strcpy(s, " HDD 0: ");
		strncat(s, archie_get_hdd_name(0) ? archie_get_hdd_name(0) : "* no disk *", 27);
		OsdWrite(m++, s, menusub == 2);

		strcpy(s, " HDD 1: ");
		strncat(s, archie_get_hdd_name(1) ? archie_get_hdd_name(1) : "* no disk *", 27);
		OsdWrite(m++, s, menusub == 3);

		OsdWrite(m++);

		strcpy(s, " OS ROM: ");
		strcat(s, archie_get_rom_name());
		OsdWrite(m++, s, menusub == 4);

		OsdWrite(m++);

		strcpy(s, " Aspect Ratio:    ");
		archie_set_ar(get_ar_name(archie_get_ar(), s));
		OsdWrite(m++, s, menusub == 5);

		strcpy(s, " Scale:           ");
		strcat(s, config_scale[archie_get_scale()]);
		OsdWrite(m++, s, menusub == 6);

		strcpy(s, " Refresh Rate:    ");
		strcat(s, archie_get_60() ? "Variable" : "60Hz");
		OsdWrite(m++, s, menusub == 7);

		sprintf(s, " Stereo Mix:      %s", config_stereo_msg[archie_get_amix()]);
		OsdWrite(m++, s, menusub == 8);

		strcpy(s, " 25MHz Audio Fix: ");
		strcat(s, archie_get_afix() ? "Enable" : "Disable");
		OsdWrite(m++, s, menusub == 9);

		sprintf(s, " Swap Joysticks:  %s", user_io_get_joyswap() ? "Yes" : "No");
		OsdWrite(m++, s, menusub == 10);
		sprintf(s, " Swap Btn 2/3:    %s", archie_get_mswap() ? "Yes" : "No");
		OsdWrite(m++, s, menusub == 11);

		while(m<15) OsdWrite(m++);

		OsdWrite(15, STD_EXIT, menusub == 12, 0);
		menustate = MENU_ARCHIE_MAIN2;
		parentstate = MENU_ARCHIE_MAIN1;

		// set helptext with core display on top of basic info
		sprintf(helptext_custom, HELPTEXT_SPACER);
		strcat(helptext_custom, OsdCoreNameGet());
		strcat(helptext_custom, helptexts[HELPTEXT_MAIN]);
		helptext_idx = HELPTEXT_CUSTOM;
		break;

	case MENU_ARCHIE_MAIN2:
		// menu key closes menu
		if (menu) menustate = MENU_NONE1;
		if (recent && (menusub <= 3))
		{
			fs_Options = SCANO_DIR | SCANO_UMOUNT;
			fs_MenuSelect = MENU_ARCHIE_MAIN_FILE_SELECTED;
			fs_MenuCancel = MENU_ARCHIE_MAIN1;
			strcpy(fs_pFileExt, (menusub <= 1) ? "ADF" : "HDF");
			if (recent_init((menusub <= 1) ? 500 : 501)) menustate = MENU_RECENT1;
		}

		if (select || plus || minus)
		{
			switch (menusub)
			{
			case 0:  // Floppy 0
			case 1:  // Floppy 1
				if (select)
				{
					ioctl_index = 0;
					SelectFile(Selected_S[menusub], "ADF", SCANO_DIR | SCANO_UMOUNT, MENU_ARCHIE_MAIN_FILE_SELECTED, MENU_ARCHIE_MAIN1);
				}
				break;

			case 2:  // HDD 0
			case 3:  // HDD 1
				if (select)
				{
					ioctl_index = 1;
					SelectFile(Selected_S[menusub], "HDF", SCANO_DIR | SCANO_UMOUNT, MENU_ARCHIE_MAIN_FILE_SELECTED, MENU_ARCHIE_MAIN1);
				}
				break;

			case 4:  // Load ROM
				if (select)
				{
					SelectFile(Selected_F[menusub], "ROM", 0, MENU_ARCHIE_MAIN_FILE_SELECTED, MENU_ARCHIE_MAIN1);
				}
				break;

			case 5:
				archie_set_ar(next_ar(archie_get_ar(), minus));
				menustate = MENU_ARCHIE_MAIN1;
				break;

			case 6:
				archie_set_scale(archie_get_scale() + (minus ? -1 : 1));
				menustate = MENU_ARCHIE_MAIN1;
				break;

			case 7:
				archie_set_60(!archie_get_60());
				menustate = MENU_ARCHIE_MAIN1;
				break;

			case 8:
				archie_set_amix(archie_get_amix() + (minus ? -1 : 1));
				menustate = MENU_ARCHIE_MAIN1;
				break;

			case 9:
				archie_set_afix(!archie_get_afix());
				menustate = MENU_ARCHIE_MAIN1;
				break;

			case 10:
				user_io_set_joyswap(!user_io_get_joyswap());
				menustate = MENU_ARCHIE_MAIN1;
				break;

			case 11:
				archie_set_mswap(!archie_get_mswap());
				menustate = MENU_ARCHIE_MAIN1;
				break;

			case 12:  // Exit
				if (select) menustate = MENU_NONE1;
				break;
			}
		}

		if (right)
		{
			menustate = MENU_COMMON1;
			menusub = 0;
		}
		else if (left)
		{
			menustate = MENU_MISC1;
			menusub = 3;
		}
		break;

	case MENU_ARCHIE_MAIN_FILE_SELECTED:
		menustate = MENU_ARCHIE_MAIN1;
		if (menusub <= 1)
		{
			memcpy(Selected_F[menusub], selPath, sizeof(Selected_F[menusub]));
			recent_update(SelectedDir, Selected_F[menusub], SelectedLabel, 500);
			user_io_file_mount(selPath, menusub);
		}
		else if (menusub <= 3)
		{
			memcpy(Selected_F[menusub], selPath, sizeof(Selected_F[menusub]));
			recent_update(SelectedDir, Selected_F[menusub], SelectedLabel, 501);
			archie_hdd_mount(selPath, menusub - 2);
		}
		else if (menusub == 4)
		{
			memcpy(Selected_F[menusub], selPath, sizeof(Selected_F[menusub]));
			archie_set_rom(selPath);
			menustate = MENU_NONE1;
		}
		break;

		/******************************************************************/
		/* 8 bit main menu                                                */
		/******************************************************************/

	case MENU_GENERIC_MAIN1: {
		hdmask = spi_uio_cmd16(UIO_GET_OSDMASK, 0);
		user_io_read_confstr();
		uint32_t s_entry = 0;
		int entry = 0;
		while(1)
		{
			if (!menusub) firstmenu = 0;

			adjvisible = 0;
			entry = 0;
			uint32_t selentry = 0;
			menumask = 0;

			OsdSetTitle(page ? title : user_io_get_core_name());

			dip_submenu = -1;
			dip2_submenu = -1;

			int last_space = 0;

			// add options as requested by core
			int i = 2;
			do
			{
				char* pos;

				p = user_io_get_confstr(i++);
				//printf("Option %d: %s\n", i-1, p);

				if (p)
				{
					int h = 0, d = 0, inpage = !page;

					if (!strncmp(p, "DEFMRA,", 7))
					{
					}
					else if (!strcmp(p, "DIP"))
					{
						h = page;
						if (!h && arcade_sw(0)->dip_num)
						{
							dip_submenu = selentry;
							MenuWrite(entry, " DIP Switches              \x16", menusub == selentry, 0);
							entry++;
							selentry++;
							menumask = (menumask << 1) | 1;
						}
						continue;
					}
					else if (!strcmp(p, "CHEAT"))
					{
						h = page;
						if (!h && arcade_sw(1)->dip_num)
						{
							dip2_submenu = selentry;
							MenuWrite(entry, " Cheats                    \x16", menusub == selentry, 0);
							entry++;
							selentry++;
							menumask = (menumask << 1) | 1;
						}
						continue;
					}
					else
					{
						//Hide or Disable flag (small letter - opposite action)
						while ((p[0] == 'H' || p[0] == 'D' || p[0] == 'h' || p[0] == 'd') && strlen(p) > 2)
						{
							int flg = (hdmask & (1 << getOptIdx(p))) ? 1 : 0;
							if (p[0] == 'H') h |= flg;
							if (p[0] == 'h') h |= (flg ^ 1);
							if (p[0] == 'D') d |= flg;
							if (p[0] == 'd') d |= (flg ^ 1);
							p += 2;
						}

						if (p[0] == 'P')
						{
							int n = p[1] - '0';
							if (p[2] != ',')
							{
								if (page && page == n) inpage = 1;
								if (!page && n && !flat) inpage = 0;
								p += 2;

								if (flat && !page && p[0] == '-') inpage = 0;
							}
							else if (flat && !page && !last_space)
							{
								MenuWrite(entry, "", 0, d);
								entry++;
							}
						}
					}

					last_space = 0;

					if (!h && inpage)
					{
						if (p[0] == 'P')
						{
							if (flat)
							{
								strcpy(s, " \x16 ");
								substrcpy(s + 3, p, 1);

								int len = strlen(s);
								while (len < 28) s[len++] = ' ';
								s[28] = 0;
							}
							else
							{
								strcpy(s, " ");
								substrcpy(s + 1, p, 1);

								int len = strlen(s);
								while (len < 27) s[len++] = ' ';
								s[27] = 0x16;
								s[28] = 0;
							}

							MenuWrite(entry, s, menusub == selentry, d);

							// add bit in menu mask
							menumask = (menumask << 1) | 1;
							entry++;
							selentry++;
						}

						// check for 'F'ile or 'S'D image strings
						if ((p[0] == 'F') || (p[0] == 'S'))
						{
							if (p[0] == 'S') s_entry = selentry;
							substrcpy(s, p, 2);
							int num = (p[1] >= '0' && p[1] <= '9') ? p[1] - '0' : 0;

							if (is_x86() && x86_get_image_name(num))
							{
								strcpy(s, " ");
								substrcpy(s + 1, p, 2);
								strcat(s, " ");
								strcat(s, x86_get_image_name(num));
							}
							else
							{
								if (strlen(s))
								{
									strcpy(s, " ");
									substrcpy(s + 1, p, 2);
									strcat(s, " *.");
								}
								else
								{
									if (p[0] == 'F') strcpy(s, " Load *.");
									else             strcpy(s, " Mount *.");
								}
								pos = s + strlen(s);
								substrcpy(pos, p, 1);
								strcpy(pos, GetExt(pos));
							}
							MenuWrite(entry, s, menusub == selentry, d);

							// add bit in menu mask
							menumask = (menumask << 1) | 1;
							entry++;
							selentry++;
						}

						// check for 'C'heats
						if (p[0] == 'C')
						{
							substrcpy(s, p, 1);
							if (strlen(s))
							{
								strcpy(s, " ");
								substrcpy(s + 1, p, 1);
							}
							else
							{
								strcpy(s, " Cheats");
							}
							MenuWrite(entry, s, menusub == selentry, !cheats_available() || d);

							// add bit in menu mask
							menumask = (menumask << 1) | 1;
							entry++;
							selentry++;
						}

						// check for 'T'oggle and 'R'eset (toggle and then close menu) strings
						if ((p[0] == 'T') || (p[0] == 'R') || (p[0] == 't') || (p[0] == 'r'))
						{

							s[0] = ' ';
							substrcpy(s + 1, p, 1);
							MenuWrite(entry, s, menusub == selentry, d);

							// add bit in menu mask
							menumask = (menumask << 1) | 1;
							entry++;
							selentry++;
						}

						// check for 'O'ption strings
						if ((p[0] == 'O') || (p[0] == 'o'))
						{
							int ex = (p[0] == 'o');
							uint32_t status = user_io_8bit_set_status(0, 0, ex);  // 0,0 gets status

							//option handled by ARM
							if (p[1] == 'X') p++;

							uint32_t x = getStatus(p, status);

							// get currently active option
							substrcpy(s, p, 2 + x);
							int l = strlen(s);
							int arc = get_arc(s);
							if (!l || arc < 0)
							{
								// option's index is outside of available values.
								// reset to 0.
								x = 0;
								//user_io_8bit_set_status(setStatus(p, status, x), 0xffffffff);
								substrcpy(s, p, 2 + x);
								l = strlen(s);
								arc = get_arc(s);
							}

							if (arc > 0) l = strlen(cfg.custom_aspect_ratio[arc - 1]);

							s[0] = ' ';
							substrcpy(s + 1, p, 1);

							char *end = s + strlen(s) - 1;
							while ((end > s + 1) && (*end == ' ')) end--;
							*(end + 1) = 0;

							int len = strlen(s);
							if (len+l > 27) len = 27-l;
							s[len++] = ':';
							s[len] = 0;

							l = 28 - l - strlen(s);
							while (l--) strcat(s, " ");

							if (arc > 0) strcpy(s + strlen(s), cfg.custom_aspect_ratio[arc - 1]);
							else substrcpy(s + strlen(s), p, 2 + x);

							MenuWrite(entry, s, menusub == selentry, d);

							// add bit in menu mask
							menumask = (menumask << 1) | 1;
							entry++;
							selentry++;
						}

						// delimiter, text
						if (p[0] == '-')
						{
							s[0] = ' ';
							s[1] = 0;
							substrcpy(s + 1, p, 1);
							last_space = (strlen(s) == 1);
							MenuWrite(entry, s, 0, d);
							entry++;
						}
					}
				}
			} while (p);

			if (!entry) break;

			for (; entry < OsdGetSize() - 1; entry++) MenuWrite(entry, "", 0, 0);

			// exit row
			if (!page)
			{
				MenuWrite(entry, STD_EXIT, menusub == selentry, 0, OSD_ARROW_RIGHT | OSD_ARROW_LEFT);
			}
			else
			{
				MenuWrite(entry, STD_BACK, menusub == selentry, 0, 0);
			}
			menusub_last = selentry;
			menumask = (menumask << 1) | 1;

			if (parentstate == MENU_NONE1 && is_pce() && pcecd_using_cd() && menusub != s_entry)
			{
				menusub = s_entry;
				continue;
			}

			if (!adjvisible) break;
			firstmenu += adjvisible;
		}

		if (!entry)
		{
			if (page) page = 0;
			else menustate = MENU_COMMON1;
			menusub = 0;
			break;
		}

		parentstate = menustate;
		menustate = MENU_GENERIC_MAIN2;

		// set helptext with core display on top of basic info
		sprintf(helptext_custom, HELPTEXT_SPACER);
		strcat(helptext_custom, OsdCoreNameGet());
		if (is_arcade())
		{
			strcat(helptext_custom, " (");
			strcat(helptext_custom, user_io_get_core_name(1));
			strcat(helptext_custom, ")");
		}
		strcat(helptext_custom, helptexts[HELPTEXT_MAIN]);
		helptext_idx = HELPTEXT_CUSTOM;

	} break;

	case MENU_GENERIC_SAVE_WAIT:
		menumask = 0;
		parentstate = menustate;
		if (menu_save_timer && CheckTimer(menu_save_timer))
		{
			menu_save_timer = 0;
			menustate = MENU_GENERIC_MAIN1;
		}
		break;

	case MENU_GENERIC_MAIN2:
		saved_menustate = MENU_GENERIC_MAIN1;

		if (menu_save_timer && !CheckTimer(menu_save_timer))
		{
			for (int i = 0; i < 16; i++) OsdWrite(m++);
			OsdWrite(8, "          Saving...");
			menustate = MENU_GENERIC_SAVE_WAIT;
		}
		else if (is_arcade() && spi_uio_cmd(UIO_CHK_UPLOAD))
		{
			menu_save_timer = GetTimer(1000);
			arcade_nvm_save();
		}
		else if (menu)
		{
			menustate = MENU_NONE1;
		}
		else if(back || (left && page) || (menusub == menusub_last && select))
		{
			if(!page) menustate = MENU_NONE1;
			else
			{
				firstmenu = 0;
				menustate = MENU_GENERIC_MAIN1;
				menusub = menusub_parent;
				page = 0;
			}
		}
		else if (select || recent || minus || plus)
		{
			if ((dip_submenu == menusub || dip2_submenu == menusub) && select)
			{
				dipv = (dip_submenu == menusub) ? 0 : 1;
				menustate = MENU_ARCADE_DIP1;
				menusub = 0;
			}
			else
			{
				static char ext[256];
				int h = 0, d = 0, inpage = !page;
				uint32_t entry = 0;
				int i = 2;

				p = 0;
				addon[0] = 0;

				while (1)
				{
					p = user_io_get_confstr(i++);
					if (!p) break;

					h = 0;
					d = 0;
					inpage = !page;

					if (!strcmp(p, "DIP")) h = page || !arcade_sw(0)->dip_num;
					else if (!strcmp(p, "CHEAT")) h = page || !arcade_sw(1)->dip_num;
					else if (strncmp(p, "DEFMRA,", 7))
					{
						//Hide or Disable flag
						while ((p[0] == 'H' || p[0] == 'D' || p[0] == 'h' || p[0] == 'd') && strlen(p) > 2)
						{
							int flg = (hdmask & (1 << getOptIdx(p))) ? 1 : 0;
							if (p[0] == 'H') h |= flg;
							if (p[0] == 'h') h |= (flg ^ 1);
							if (p[0] == 'D') d |= flg;
							if (p[0] == 'd') d |= (flg ^ 1);
							p += 2;
						}
					}

					if (p[0] == 'P')
					{
						int n = p[1] - '0';
						if (p[2] != ',')
						{
							if (page && page == n) inpage = 1;
							if (!page && n && !flat) inpage = 0;
							p += 2;
						}
					}

					if (!inpage || h || p[0] < 'A') continue;

					// supplement files
					if (p[0] == 'f')
					{
						strcpy(addon, p);
						continue;
					}

					if (entry == menusub) break;
					entry++;

					if (p[0] == 'F' || p[0] == 'S') addon[0] = 0;
				}

				if (p && !d)
				{
					if (p[0] == 'F' && (select || recent))
					{
						store_name = 0;
						opensave = 0;
						ioctl_index = menusub + 1;
						int idx = 1;

						if (p[idx] == 'S')
						{
							opensave = 1;
							idx++;
						}

						if (p[idx] == 'C')
						{
							store_name = 1;
							idx++;
						}

						if (p[idx] >= '0' && p[idx] <= '9') ioctl_index = p[idx] - '0';
						substrcpy(ext, p, 1);
						if (is_gba() && FileExists(user_io_make_filepath(HomeDir(), "goomba.rom"))) strcat(ext, "GB GBC");
						while (strlen(ext) % 3) strcat(ext, " ");

						fs_Options = SCANO_DIR | (is_neogeo() ? SCANO_NEOGEO | SCANO_NOENTER : 0) | (store_name ? SCANO_CLEAR : 0);
						fs_MenuSelect = MENU_GENERIC_FILE_SELECTED;
						fs_MenuCancel = MENU_GENERIC_MAIN1;
						strcpy(fs_pFileExt, ext);

						load_addr = 0;
						if (substrcpy(s, p, 3))
						{
							load_addr = strtoul(s, NULL, 16);
							if (load_addr < 0x20000000 || load_addr >= 0x40000000)
							{
								printf("Loading address 0x%X is outside the supported range! Using normal load.\n", load_addr);
								load_addr = 0;
							}
						}

						if (select) SelectFile(Selected_F[ioctl_index & 15], ext, fs_Options, fs_MenuSelect, fs_MenuCancel);
						else if(recent_init(ioctl_index)) menustate = MENU_RECENT1;
					}
					else if (p[0] == 'S' && (select || recent))
					{
						store_name = 0;
						int idx = 1;

						if (p[idx] == 'C')
						{
							store_name = 1;
							idx++;
						}

						ioctl_index = 0;
						if ((p[idx] >= '0' && p[idx] <= '9') || is_x86()) ioctl_index = p[idx] - '0';
						substrcpy(ext, p, 1);
						while (strlen(ext) % 3) strcat(ext, " ");

						fs_Options = SCANO_DIR | SCANO_UMOUNT;
						fs_MenuSelect = MENU_GENERIC_IMAGE_SELECTED;
						fs_MenuCancel = MENU_GENERIC_MAIN1;
						strcpy(fs_pFileExt, ext);

						memcpy(Selected_tmp, Selected_S[(int)ioctl_index], sizeof(Selected_tmp));
						if (is_x86()) strcpy(Selected_tmp, x86_get_image_path(ioctl_index));
						if (is_psx() && (ioctl_index == 2 || ioctl_index == 3)) fs_Options |= SCANO_SAVES;

						if (is_pce() || is_megacd() || is_x86() || (is_psx() && !(fs_Options & SCANO_SAVES)))
						{
							//look for CHD too
							strcat(fs_pFileExt, "CHD");
							strcat(ext, "CHD");

							int num = ScanDirectory(Selected_tmp, SCANF_INIT, fs_pFileExt, 0);
							memcpy(Selected_tmp, Selected_S[(int)ioctl_index], sizeof(Selected_tmp));

							if (num == 1)
							{
								fs_Options |= SCANO_NOENTER;
								char *p = strrchr(Selected_tmp, '/');
								if (p) *p = 0;
							}

							fs_Options |= SCANO_NOZIP;
						}

						if (is_psx()) fs_Options |= SCANO_NOZIP;

						if (select) SelectFile(Selected_tmp, ext, fs_Options, fs_MenuSelect, fs_MenuCancel);
						else if(recent_init(ioctl_index + 500)) menustate = MENU_RECENT1;
					}
					else if (select || minus || plus)
					{
						if (p[0] == 'P' && select)
						{
							page = p[1] - '0';
							if (page < 1 || page > 9) page = 0;
							menusub_parent = menusub;
							substrcpy(title, p, 1);
							menustate = MENU_GENERIC_MAIN1;
							menusub = 0;
						}
						else if (p[0] == 'C' && cheats_available() && select)
						{
							menustate = MENU_CHEATS1;
							cheatsub = menusub;
							menusub = 0;
						}
						else if ((p[0] == 'O') || (p[0] == 'o'))
						{
							int ex = (p[0] == 'o');

							int byarm = 0;
							if (p[1] == 'X')
							{
								byarm = 1;
								p++;
							}

							uint32_t status = user_io_8bit_set_status(0, 0, ex);  // 0,0 gets status
							uint32_t x = minus ? (getStatus(p, status) - 1) : (getStatus(p, status) + 1);
							uint32_t mask = getStatusMask(p);
							x &= mask;

							if (byarm && is_x86() && p[1] == '2') x86_set_fdd_boot(!(x & 1));

							// check if next value available
							if (minus)
							{
								while(1)
								{
									substrcpy(s, p, 2 + x);
									if (strlen(s) && get_arc(s) >= 0) break;
									x = (x - 1) & mask;
								}
							}
							else
							{
								substrcpy(s, p, 2 + x);
								if (!strlen(s) || get_arc(s) < 0) x = 0;
							}

							user_io_8bit_set_status(setStatus(p, status, x), 0xffffffff, ex);

							if (is_x86() && p[1] == 'A')
							{
								int mode = GetUARTMode();
								if (mode != 0)
								{
									SetUARTMode(0);
									SetUARTMode(mode);
								}
							}
							menustate = MENU_GENERIC_MAIN1;
						}
						else if (((p[0] == 'T') || (p[0] == 'R') || (p[0] == 't') || (p[0] == 'r')) && select)
						{
							int ex = (p[0] == 't') || (p[0] == 'r');

							// determine which status bit is affected
							uint32_t mask = 1 << getOptIdx(p);
							if (mask == 1 && is_x86())
							{
								x86_init();
								ResetUART();
								menustate = MENU_NONE1;
							}
							else
							{
								if (is_megacd())
								{
									if (mask == 1) mcd_set_image(0, "");
									if (mask == 2)
									{
										mcd_reset();
										mask = 1;
									}
								}

								if (is_pce() && mask == 1) pcecd_reset();

								uint32_t status = user_io_8bit_set_status(0, 0, ex);

								user_io_8bit_set_status(status ^ mask, mask, ex);
								user_io_8bit_set_status(status, mask, ex);
								menustate = MENU_GENERIC_MAIN1;
								if (p[0] == 'R' || p[0] == 'r') menustate = MENU_NONE1;
							}
						}
					}
					else if (recent)
					{
						flat = !flat;
						page = 0;
						menustate = MENU_GENERIC_MAIN1;
						menusub = 0;
					}
				}
			}
		}
		else if (right && !page)
		{
			menustate = MENU_COMMON1;
			menusub = 0;
		}
		else if (left)
		{
			menustate = MENU_MISC1;
			menusub = 3;
		}
		else if(spi_uio_cmd16(UIO_GET_OSDMASK, 0) != hdmask)
		{
			menustate = MENU_GENERIC_MAIN1;
		}

		break;

	case MENU_GENERIC_FILE_SELECTED:
		{
			MenuHide();
			printf("File selected: %s\n", selPath);
			memcpy(Selected_F[ioctl_index & 15], selPath, sizeof(Selected_F[ioctl_index & 15]));

			if (store_name)
			{
				char str[64];
				sprintf(str, "%s.f%d", user_io_get_core_name(), ioctl_index);
				FileSaveConfig(str, selPath, sizeof(selPath));
			}

			if (selPath[0])
			{

				char idx = user_io_ext_idx(selPath, fs_pFileExt) << 6 | ioctl_index;
				if (addon[0] == 'f' && addon[1] != '1') process_addon(addon, idx);

				if (fs_Options & SCANO_NEOGEO)
				{
					neogeo_romset_tx(selPath);
				}
				else
				{
					if (is_pce())
					{
						pcecd_set_image(0, "");
						pcecd_reset();
					}
					if (!store_name) user_io_store_filename(selPath);
					user_io_file_tx(selPath, idx, opensave, 0, 0, load_addr);
					if (user_io_use_cheats() && !store_name) cheats_init(selPath, user_io_get_file_crc());
				}

				if (addon[0] == 'f' && addon[1] == '1') process_addon(addon, idx);

				recent_update(SelectedDir, Selected_F[ioctl_index & 15], SelectedLabel, ioctl_index);
			}
		}
		break;

	case MENU_GENERIC_IMAGE_SELECTED:
		{
			if (store_name)
			{
				char str[64];
				sprintf(str, "%s.s%d", user_io_get_core_name(), ioctl_index);
				FileSaveConfig(str, selPath, sizeof(selPath));
			}

			menustate = MENU_GENERIC_MAIN1;
			if (selPath[0] && !is_x86()) MenuHide();

			printf("Image selected: %s\n", selPath);
			memcpy(Selected_S[(int)ioctl_index], selPath, sizeof(Selected_S[(int)ioctl_index]));

			char idx = user_io_ext_idx(selPath, fs_pFileExt) << 6 | ioctl_index;
			if (addon[0] == 'f' && addon[1] != '1') process_addon(addon, idx);

			if (is_x86())
			{
				x86_set_image(ioctl_index, selPath);
			}
			else if (is_megacd())
			{
				mcd_set_image(ioctl_index, selPath);
			}
			else if (is_pce())
			{
				pcecd_set_image(ioctl_index, selPath);
				cheats_init(selPath, 0);
			}
			else if (is_psx() && ioctl_index == 1)
			{
				psx_mount_cd(user_io_ext_idx(selPath, fs_pFileExt) << 6 | (menusub + 1), ioctl_index, selPath);
			}
			else
			{
				user_io_set_index(user_io_ext_idx(selPath, fs_pFileExt) << 6 | (menusub + 1));
				user_io_file_mount(selPath, ioctl_index);
			}

			if (addon[0] == 'f' && addon[1] == '1') process_addon(addon, idx);

			recent_update(SelectedDir, Selected_S[(int)ioctl_index], SelectedLabel, ioctl_index + 500);
		}
		break;

	case MENU_COMMON1:
		{
			OsdSetSize(16);
			helptext_idx = 0;
			reboot_req = 0;

			OsdSetTitle("System", 0);
			menustate = MENU_COMMON2;
			parentstate = MENU_COMMON1;
			int n;

			while(1)
			{
				n = 0;
				menumask = 0x3802f;

				if (!menusub) firstmenu = 0;
				adjvisible = 0;

				MenuWrite(n++, " Core                      \x16", menusub == 0, 0);
				MenuWrite(n++);
				sprintf(s, " Define %s buttons         ", is_menu() ? "System" : user_io_get_core_name());
				s[27] = '\x16';
				s[28] = 0;
				MenuWrite(n++, s, menusub == 1, 0);
				MenuWrite(n++, " Button/Key remap for game \x16", menusub == 2, 0);
				MenuWrite(n++, " Reset player assignment", menusub == 3, 0);

				if (user_io_get_uart_mode())
				{
					menumask |= 0x10;
					MenuWrite(n++);
					int mode = GetUARTMode();
					const char *p = config_uart_msg[mode];
					while (*p == ' ') p++;
					sprintf(s, " UART mode (%s)            ",p);
					s[27] = '\x16';
					s[28] = 0;
					MenuWrite(n++, s, menusub == 4);
				}

				MenuWrite(n++);
				MenuWrite(n++, " Video processing          \x16", menusub==5);

				if (audio_filter_en() >= 0)
				{
					MenuWrite(n++);
					menumask |= 0x600;
					sprintf(s, " Audio filter - %s", config_afilter_msg[audio_filter_en() ? 1 : 0]);
					MenuWrite(n++, s, menusub == 9);

					memset(s, 0, sizeof(s));
					s[0] = ' ';
					if (strlen(audio_get_filter(1))) strncpy(s + 1, audio_get_filter(1), 25);
					else strcpy(s, " < none >");

					while (strlen(s) < 26) strcat(s, " ");
					strcat(s, " \x16 ");

					MenuWrite(n++, s, menusub == 10, !audio_filter_en() || !S_ISDIR(getFileType(AFILTER_DIR)));
				}

				if (!is_minimig() && !is_st())
				{
					menumask |= 0x6000;
					MenuWrite(n++);
					MenuWrite(n++, " Reset settings", menusub == 13, is_archie());
					MenuWrite(n++, " Save settings", menusub == 14, 0);
				}

				MenuWrite(n++);
				cr = n;
				MenuWrite(n++, " Reboot (hold \x16 cold reboot)", menusub == 15);
				MenuWrite(n++, " About", menusub == 16);

				while(n < OsdGetSize() - 1) MenuWrite(n++);
				MenuWrite(n++, STD_EXIT, menusub == 17, 0, OSD_ARROW_LEFT);
				sysinfo_timer = 0;

				if (!adjvisible) break;
				firstmenu += adjvisible;
			}

		}
		break;

	case MENU_COMMON2:
		if (menu)
        {
			switch (user_io_core_type())
			{
			    case CORE_TYPE_SHARPMZ:
				    menusub   = menusub_last;
				    menustate = sharpmz_default_ui_state();
                    break;
                default:
                    menustate = MENU_NONE1;
                    break;
            };
        }

		if (recent && menusub == 0)
		{
			fs_Options = SCANO_CORES;
			fs_MenuSelect = MENU_CORE_FILE_SELECTED1;
			fs_MenuCancel = MENU_COMMON1;

			if (recent_init(-1)) menustate = MENU_RECENT1;
			break;
		}

		if (select)
		{
			switch (menusub)
			{
			case 0:
				SelectFile("", 0, SCANO_CORES, MENU_CORE_FILE_SELECTED1, MENU_COMMON1);
				menusub = 0;
				break;

			case 1:
				if (is_minimig())
				{
					joy_bcount = 7;
					strcpy(joy_bnames[0], "A(Red/Fire)");
					strcpy(joy_bnames[1], "B(Blue)");
					strcpy(joy_bnames[2], "C(Yellow)");
					strcpy(joy_bnames[3], "D(Green)");
					strcpy(joy_bnames[4], "RT");
					strcpy(joy_bnames[5], "LT");
					strcpy(joy_bnames[6], "Pause");
				}
				else
				{
					parse_buttons();
				}
				start_map_setting(joy_bcount ? joy_bcount+4 : 8);
				menustate = MENU_JOYDIGMAP;
				menusub = 0;
				joymap_first = 1;
				break;

			case 2:
				start_map_setting(-1);
				menustate = MENU_JOYKBDMAP;
				menusub = 0;
				break;

			case 3:
				reset_players();
				menustate = MENU_NONE1;
				break;

			case 4:
				{
					menustate = MENU_UART1;
					menusub = 0;
				}
				break;

			case 5:
				{
					menustate = MENU_VIDEOPROC1;
					menusub = 0;
				}
				break;

			case 9:
				audio_set_filter_en(audio_filter_en() ? 0 : 1);
				menustate = MENU_COMMON1;
				break;

			case 10:
				if (audio_filter_en())
				{
					snprintf(Selected_tmp, sizeof(Selected_tmp), AFILTER_DIR"/%s", audio_get_filter(0));
					if (!FileExists(Selected_tmp)) snprintf(Selected_tmp, sizeof(Selected_tmp), AFILTER_DIR);
					SelectFile(Selected_tmp, 0, SCANO_DIR | SCANO_TXT, MENU_AFILTER_FILE_SELECTED, MENU_COMMON1);
				}
				break;

			case 13:
				if (!is_archie())
				{
					menustate = MENU_RESET1;
					menusub = 1;
				}
				else if (user_io_core_type() == CORE_TYPE_SHARPMZ)
				{
					menustate = sharpmz_reset_config(1);
                    menusub   = 0;
				}
				break;

			case 14:
				// Save settings
				menustate = MENU_GENERIC_MAIN1;
				menusub = 0;

				if (is_archie())
				{
					archie_save_config();
					menustate = MENU_ARCHIE_MAIN1;
				}
				else if (user_io_core_type() == CORE_TYPE_SHARPMZ)
				{
					menustate = sharpmz_save_config();
				}
				else
				{
					char *filename = user_io_create_config_name();
					uint32_t status[2] = { user_io_8bit_set_status(0, 0, 0), user_io_8bit_set_status(0, 0, 1) };
					printf("Saving config to %s\n", filename);
					FileSaveConfig(filename, status, 8);
					if (is_x86()) x86_config_save();
					if (is_arcade()) arcade_nvm_save();
				}
				break;

			case 15:
				{
					reboot_req = 1;

					int off = hold_cnt/3;
					if (off > 5) reboot(1);

					sprintf(s, " Cold Reboot");
					p = s + 5 - off;
					MenuWrite(cr, p, 1, 0);
				}
				break;

			case 16:
				menustate = MENU_ABOUT1;
				menusub = 0;
				break;

			default:
				menustate = MENU_NONE1;
				break;
			}
		}
		else if (left)
		{
			// go back to core requesting this menu
			switch (user_io_core_type())
			{
			case CORE_TYPE_8BIT:
				if (is_minimig())
				{
					menusub = 0;
					menustate = MENU_MINIMIG_MAIN1;
				}
				else if (is_archie())
				{
					menusub = 0;
					menustate = MENU_ARCHIE_MAIN1;
				}
				else if (is_st())
				{
					menusub = 0;
					menustate = MENU_ST_MAIN1;
				}
				else
				{
					menusub = 0;
					menustate = MENU_GENERIC_MAIN1;
				}
				break;
			case CORE_TYPE_SHARPMZ:
				menusub   = menusub_last;
				menustate = sharpmz_default_ui_state();
				break;
			}
		}
		else if (minus || plus)
		{
			if (menusub == 10 && audio_filter_en())
			{
				const char *newfile = flist_GetPrevNext(AFILTER_DIR, audio_get_filter(0), "TXT", plus);
				audio_set_filter(newfile ? newfile : "");
				menustate = MENU_COMMON1;
			}
		}

		if(!hold_cnt && reboot_req) fpga_load_rbf("menu.rbf");
		break;

	case MENU_VIDEOPROC1:
		helptext_idx = 0;
		menumask = 0xFFF;
		OsdSetTitle("Video Processing");
		menustate = MENU_VIDEOPROC2;
		parentstate = MENU_VIDEOPROC1;

		while (1)
		{
			int n = 0;
			if (!menusub) firstmenu = 0;
			adjvisible = 0;

			MenuWrite(n++, " Load preset", menusub == 0);
			MenuWrite(n++);

			sprintf(s, video_get_scaler_flt(VFILTER_HORZ) ?  " Horz filter: From file" : " Video filter: NearNeighbour");
			MenuWrite(n++, s, menusub == 1, cfg.direct_video);
			strcpy(s, " ");
			if (strlen(video_get_scaler_coeff(VFILTER_HORZ))) strncat(s, video_get_scaler_coeff(VFILTER_HORZ), 25);
			else strcpy(s, " < none >");
			while (strlen(s) < 26) strcat(s, " ");
			strcat(s, " \x16 ");
			MenuWrite(n++, s, menusub == 2, !video_get_scaler_flt(VFILTER_HORZ) || !S_ISDIR(getFileType(COEFF_DIR)));

			MenuWrite(n++);
			sprintf(s, " Vert filter: %s", video_get_scaler_flt(VFILTER_VERT) ? "From file" : "Same as Horz");
			MenuWrite(n++, s, menusub == 3, cfg.direct_video || !video_get_scaler_flt(VFILTER_HORZ));
			strcpy(s, " ");
			if (strlen(video_get_scaler_coeff(VFILTER_VERT))) strncat(s, video_get_scaler_coeff(VFILTER_VERT), 25);
			else strcpy(s, " < none >");
			while (strlen(s) < 26) strcat(s, " ");
			strcat(s, " \x16 ");
			MenuWrite(n++, s, menusub == 4, !video_get_scaler_flt(VFILTER_VERT) || !video_get_scaler_flt(VFILTER_HORZ) || !S_ISDIR(getFileType(COEFF_DIR)) || cfg.direct_video);

			MenuWrite(n++);
			sprintf(s, " Scan filter: %s", video_get_scaler_flt(VFILTER_SCAN) ? "From file" : "Same as Vert");
			MenuWrite(n++, s, menusub == 5, cfg.direct_video || !video_get_scaler_flt(VFILTER_HORZ));
			strcpy(s, " ");
			if (strlen(video_get_scaler_coeff(VFILTER_SCAN))) strncat(s, video_get_scaler_coeff(VFILTER_SCAN), 25);
			else strcpy(s, " < none >");
			while (strlen(s) < 26) strcat(s, " ");
			strcat(s, " \x16 ");
			MenuWrite(n++, s, menusub == 6, !video_get_scaler_flt(VFILTER_SCAN) || !video_get_scaler_flt(VFILTER_HORZ) || !S_ISDIR(getFileType(COEFF_DIR)) || cfg.direct_video);

			MenuWrite(n++);
			sprintf(s, " Gamma correction - %s", (video_get_gamma_en() > 0) ? "On" : "Off");
			MenuWrite(n++, s, menusub == 7, video_get_gamma_en() < 0);
			strcpy(s, " ");
			if (strlen(video_get_gamma_curve())) strncat(s, video_get_gamma_curve(), 25);
			else strcpy(s, " < none >");
			while (strlen(s) < 26) strcat(s, " ");
			strcat(s, " \x16 ");
			MenuWrite(n++, s, menusub == 8, (video_get_gamma_en() <= 0) || !S_ISDIR(getFileType(GAMMA_DIR)));

			MenuWrite(n++);
			sprintf(s, " Shadow Mask - %s", (video_get_shadow_mask_mode() < 0) ? config_smask_msg[0] : config_smask_msg[video_get_shadow_mask_mode()]);
			MenuWrite(n++, s, menusub == 9, video_get_shadow_mask_mode() < 0);
			strcpy(s, " ");
			if (strlen(video_get_shadow_mask())) strncat(s, video_get_shadow_mask(), 25);
			else strcpy(s, " < none >");
			while (strlen(s) < 26) strcat(s, " ");
			strcat(s, " \x16 ");
			MenuWrite(n++, s, menusub == 10, (video_get_shadow_mask_mode() <= 0) || !S_ISDIR(getFileType(SMASK_DIR)));

			MenuWrite(n++);
			MenuWrite(n++, STD_BACK, menusub == 11);

			if (!adjvisible) break;
			firstmenu += adjvisible;
		}
		break;

	case MENU_VIDEOPROC2:
		if (menu || left)
		{
			menusub = 5;
			menustate = MENU_COMMON1;
			break;
		}

		if ((select || recent) && menusub == 0)
		{
			fs_Options = SCANO_DIR | SCANO_TXT;
			fs_MenuSelect = MENU_PRESET_FILE_SELECTED;
			fs_MenuCancel = parentstate;
			strcpy(fs_pFileExt, "INI");
			if (!FileExists(Selected_F[15])) snprintf(Selected_F[15], sizeof(Selected_F[15]), PRESET_DIR);
			if (select) SelectFile(Selected_F[15], fs_pFileExt, fs_Options, fs_MenuSelect, fs_MenuCancel);
			else if (recent_init(15)) menustate = MENU_RECENT1;
			break;
		}

		if (plus || minus)
		{
			if (menusub == 9)
			{
				video_set_shadow_mask_mode(video_get_shadow_mask_mode() + (plus ? 1 : -1));
			}

			switch (menusub)
			{
			case 2:
			case 4:
			case 6:
				vfilter_type = (menusub == 2) ? VFILTER_HORZ : (menusub == 4) ? VFILTER_VERT : VFILTER_SCAN;
				if(video_get_scaler_flt(VFILTER_HORZ) && video_get_scaler_flt(vfilter_type))
				{
					const char *newfile = flist_GetPrevNext(COEFF_DIR, video_get_scaler_coeff(vfilter_type, 0), "TXT", plus);
					video_set_scaler_coeff(vfilter_type, newfile ? newfile : "");
				}
				break;

			case 8:
				if(video_get_gamma_en() > 0)
				{
					const char *newfile = flist_GetPrevNext(GAMMA_DIR, video_get_gamma_curve(0), "TXT", plus);
					video_set_gamma_curve(newfile ? newfile : "");
				}
				break;

			case 10:
				if (video_get_shadow_mask_mode() > 0)
				{
					const char *newfile = flist_GetPrevNext(SMASK_DIR, video_get_shadow_mask(0), "TXT", plus);
					video_set_shadow_mask(newfile ? newfile : "");
				}
				break;
			}

			menustate = parentstate;
			break;
		}

		if (select)
		{
			switch (menusub)
			{
			case 1:
				if (!cfg.direct_video)
				{
					video_set_scaler_flt(VFILTER_HORZ, video_get_scaler_flt(VFILTER_HORZ) ? 0 : 1);
					menustate = parentstate;
				}
				break;

			case 2:
			case 4:
			case 6:
				vfilter_type = (menusub == 2) ? VFILTER_HORZ : (menusub == 4) ? VFILTER_VERT : VFILTER_SCAN;
				if (video_get_scaler_flt(VFILTER_HORZ))
				{
					snprintf(Selected_tmp, sizeof(Selected_tmp), COEFF_DIR"/%s", video_get_scaler_coeff(vfilter_type, 0));
					if (!FileExists(Selected_tmp)) snprintf(Selected_tmp, sizeof(Selected_tmp), COEFF_DIR);
					SelectFile(Selected_tmp, 0, SCANO_DIR | SCANO_TXT, MENU_COEFF_FILE_SELECTED, parentstate);
				}
				break;

			case 3:
				if (!cfg.direct_video && video_get_scaler_flt(VFILTER_HORZ))
				{
					video_set_scaler_flt(VFILTER_VERT, video_get_scaler_flt(VFILTER_VERT) ? 0 : 1);
					menustate = parentstate;
				}
				break;

			case 5:
				if (!cfg.direct_video && video_get_scaler_flt(VFILTER_HORZ))
				{
					video_set_scaler_flt(VFILTER_SCAN, video_get_scaler_flt(VFILTER_SCAN) ? 0 : 1);
					menustate = parentstate;
				}
				break;

			case 7:
				if (video_get_gamma_en() >= 0) video_set_gamma_en(video_get_gamma_en() ? 0 : 1);
				menustate = parentstate;
				break;

			case 8:
				if (video_get_gamma_en() > 0)
				{
					snprintf(Selected_tmp, sizeof(Selected_tmp), GAMMA_DIR"/%s", video_get_gamma_curve(0));
					if (!FileExists(Selected_tmp)) snprintf(Selected_tmp, sizeof(Selected_tmp), GAMMA_DIR);
					SelectFile(Selected_tmp, 0, SCANO_DIR | SCANO_TXT, MENU_GAMMA_FILE_SELECTED, parentstate);
				}
				break;

			case 9:
				if (video_get_shadow_mask_mode() >= 0) video_set_shadow_mask_mode(video_get_shadow_mask_mode() + 1);
				menustate = parentstate;
				break;

			case 10:
				if (video_get_shadow_mask_mode() > 0)
				{
					snprintf(Selected_tmp, sizeof(Selected_tmp), SMASK_DIR"/%s", video_get_shadow_mask(0));
					if (!FileExists(Selected_tmp)) snprintf(Selected_tmp, sizeof(Selected_tmp), SMASK_DIR);
					SelectFile(Selected_tmp, 0, SCANO_DIR | SCANO_TXT, MENU_SMASK_FILE_SELECTED, parentstate);
				}
				break;

			case 11:
				menusub = 5;
				menustate = MENU_COMMON1;
				break;
			}
		}
		break;

	case MENU_ARCADE_DIP1:
		helptext_idx = 0;
		menumask = 0;
		OsdSetTitle(dipv ? "Cheats" : "DIP Switches");
		menustate = MENU_ARCADE_DIP2;
		parentstate = MENU_ARCADE_DIP1;

		while (1)
		{
			int entry = 0;
			if (!menusub) firstmenu = 0;

			adjvisible = 0;
			uint32_t selentry = 0;
			menumask = 0;

			sw_struct *sw = arcade_sw(dipv);

			int n = (sw->dip_num < OsdGetSize() - 1) ? (OsdGetSize() - 1 - sw->dip_num) / 2 : 0;
			for (; entry < n; entry++) MenuWrite(entry);

			for (int i = 0; i < sw->dip_num; i++)
			{
				uint64_t status = sw->dip_cur & sw->dip[i].mask;
				int m = 0;
				for (int n = 0; n < sw->dip[i].num; n++)
				{
					if (status == sw->dip[i].val[n])
					{
						m = n;
						break;
					}
				}

				char l = strlen(sw->dip[i].id[m]);
				s[0] = ' ';
				strcpy(s + 1, sw->dip[i].name);

				char *end = s + strlen(s) - 1;
				while ((end > s + 1) && (*end == ' ')) end--;
				*(end + 1) = 0;

				strcat(s, ":");
				l = 28 - l - strlen(s);
				while (l--) strcat(s, " ");

				strcat(s, sw->dip[i].id[m]);

				MenuWrite(entry, s, menusub == selentry);

				menumask = (menumask << 1) | 1;
				entry++;
				selentry++;
			};

			for (; entry < OsdGetSize() - 1; entry++) MenuWrite(entry, "", 0, 0);

			MenuWrite(entry, dipv ? STD_BACK : "       Reset to apply", menusub == selentry);
			menusub_last = selentry;
			menumask = (menumask << 1) | 1;

			if (!adjvisible) break;
			firstmenu += adjvisible;
		}
		break;

	case MENU_ARCADE_DIP2:
		if (menu || left)
		{
			menustate = MENU_GENERIC_MAIN1;
			menusub = dipv ? dip2_submenu : dip_submenu;
			arcade_sw_save(0);
		}

		if (select)
		{
			if (menusub == menusub_last)
			{
				if (!dipv)
				{
					arcade_sw_save(dipv);
					user_io_8bit_set_status(UIO_STATUS_RESET, UIO_STATUS_RESET);
					user_io_8bit_set_status(0, UIO_STATUS_RESET);
					menustate = MENU_NONE1;
				}
				else
				{
					menusub = dip2_submenu;
					menustate = MENU_GENERIC_MAIN1;
				}
			}
			else
			{
				sw_struct *sw = arcade_sw(dipv);
				uint64_t status = sw->dip_cur & sw->dip[menusub].mask;
				int m = 0;
				for (int n = 0; n < sw->dip[menusub].num; n++)
				{
					if (status == sw->dip[menusub].val[n])
					{
						m = n;
						break;
					}
				}

				m = (m + 1) % sw->dip[menusub].num;
				sw->dip_cur = (sw->dip_cur & ~sw->dip[menusub].mask) | sw->dip[menusub].val[m];
				menustate = MENU_ARCADE_DIP1;
				arcade_sw_send(dipv);
			}
		}
		break;

	case MENU_UART1:
		{
			helptext_idx = 0;
			menumask = 0x181;

			OsdSetTitle("UART Mode");
			menustate = MENU_UART2;
			parentstate = MENU_UART1;

			int mode = GetUARTMode();
			int midilink = GetMidiLinkMode();

			m = 0;
			OsdWrite(m++);
			sprintf(s, " Connection:      %s", config_uart_msg[mode]);
			OsdWrite(m++, s, menusub == 0, 0);

			OsdWrite(m++);
			if (mode == 4)
			{
				sprintf(s, " Link:            %s", (midilink == 6) ? "USB Serial" : (midilink == 5) ? "       UDP" : "       TCP");
				OsdWrite(m++, s, menusub == 1);
				menumask |= 2;
			}

			if (mode == 3)
			{
				sprintf(s, " MidiLink:             %s", config_midilink_mode[midilink]);
				OsdWrite(m++, s, menusub == 2);

				if (midilink < 2)
				{
					sprintf(s, " Type:                %s", midilink ? "  MUNT" : "FSYNTH");
					OsdWrite(m++, s, menusub == 3);

					OsdWrite(m++);
					OsdWrite(m++, " Change Soundfont          \x16", menusub == 4, midilink);
					menumask |= 0x18;
				}
				OsdWrite(m++);

				menumask |= 0x4;
			}

			if (mode)
			{
				strcpy(s, " Baud                      \x16");
				sprintf(s + 6, "(%s)", GetUARTbaud_label(GetUARTMode()));
				s[strlen(s)] = ' ';
				OsdWrite(m++, s, menusub == 5, !mode);

				OsdWrite(m++);
				OsdWrite(m++, " Reset UART connection", menusub == 6, mode ? 0 : 1);

				menumask |= 0x60;
			}

			OsdWrite(m++, " Save", menusub == 7);

			for (; m < 15; m++) OsdWrite(m);
			OsdWrite(15, STD_EXIT, menusub == 8);
		}
		break;

	case MENU_UART2:
		if (menu || left)
		{
			menustate = MENU_COMMON1;
			menusub = 4;
			break;
		}

		if (select || minus || plus)
		{
			switch (menusub)
			{
			case 0:
				menusub = GetUARTMode();
				menustate = MENU_UART3;
				break;

			case 1:
				{
					int mode = GetUARTMode();
					int midilink = GetMidiLinkMode();
					SetUARTMode(0);
					if (minus)
					{
						if (midilink <= 4) midilink = 6;
						else midilink--;
					}
					else
					{
						if (midilink >= 6) midilink = 4;
						else midilink++;
					}
					SetMidiLinkMode(midilink);
					SetUARTMode(mode);
					menustate = MENU_UART1;
				}
				break;

			case 2:
				{
					int mode = GetUARTMode();
					int midilink = GetMidiLinkMode();
					SetUARTMode(0);

					if (minus)
					{
						if (midilink < 2 || midilink > 3) midilink = 3;
						else if (midilink == 3)
						{
							struct stat filestat;
							midilink = (!stat("/dev/midi1", &filestat) || !stat("/dev/ttyUSB0", &filestat)) ? 2 : 0;
						}
						else midilink = 0;
					}
					else
					{
						if (midilink < 2)
						{
							struct stat filestat;
							midilink = (!stat("/dev/midi1", &filestat) || !stat("/dev/ttyUSB0", &filestat)) ? 2 : 3;
						}
						else if (midilink == 2) midilink = 3;
						else midilink = 0;
					}

					SetMidiLinkMode(midilink);
					SetUARTMode(mode);
					menustate = MENU_UART1;
				}
				break;

			case 3:
				{
					int mode = GetUARTMode();
					int midilink = GetMidiLinkMode();
					SetUARTMode(0);
					SetMidiLinkMode(midilink ? 0 : 1);
					SetUARTMode(mode);
					menustate = MENU_UART1;
				}
				break;

			case 4:
				if(select && GetMidiLinkMode() == 0)
				{
					sprintf(Selected_tmp, GetMidiLinkSoundfont());
					SelectFile(Selected_tmp, "SF2", SCANO_DIR | SCANO_TXT, MENU_SFONT_FILE_SELECTED, MENU_UART1);
				}
				break;

			case 5:
				if (select)
				{
					menustate = MENU_BAUD1;
					menusub = GetUARTbaud_idx(GetUARTMode());
				}
				break;

			case 6:
				if (select)
				{
					ResetUART();
					menustate = MENU_COMMON1;
					menusub = 4;
				}
				break;

			case 7:
				if (select)
				{
					int mode = GetUARTMode() | (GetMidiLinkMode() << 8);
					sprintf(s, "uartmode.%s", user_io_get_core_name());
					FileSaveConfig(s, &mode, 4);
					uint32_t speeds[3];
					speeds[0] = GetUARTbaud(1);
					speeds[1] = GetUARTbaud(3);
					speeds[2] = GetUARTbaud(4);
					sprintf(s, "uartspeed.%s", user_io_get_core_name());
					FileSaveConfig(s, speeds, sizeof(speeds));
					menustate = MENU_COMMON1;
					menusub = 4;
				}
				break;

			default:
				menustate = MENU_NONE1;
				break;
			}
		}
		break;

	case MENU_UART3:
        {
            helptext_idx = 0;
            menumask = 0x00;
            OsdSetTitle("UART MODE");
            menustate = MENU_UART4;
            parentstate = MENU_UART3;

            uint32_t max = (sizeof(config_uart_msg) / sizeof(config_uart_msg[0]));
			m = 0;

            for (uint32_t i = 0; i < 15; i++)
            {
				if((i >= (14-max)/2) && (m < max))
                {
                    menumask |= 1 << m;
                    const char * uart_msg = config_uart_msg[m];
                    while (*uart_msg == ' ') {uart_msg++;}//skip spaces
                    sprintf(s, "         %s", uart_msg);
                    OsdWrite(i, s, menusub == m, 0);
					m++;
                }
				else
				{
					OsdWrite(i);
				}
            }
            menumask |= 0x10000;
            OsdWrite(15, STD_EXIT, menusub == 16);
        }
        break;

    case MENU_UART4:
        {
			if (menu)
			{
				menustate = MENU_UART1;
				menusub = 0;
			}
			else if (select)
			{
				if (menusub != 16)
				{
					uint32_t max = (sizeof(config_uart_msg) / sizeof(config_uart_msg[0]));
					if (menusub < max)
					{
						int midilink = GetMidiLinkMode();
						if (menusub == 4) midilink = (midilink == 2) ? 6 : 4;
						if (menusub == 3) midilink = (midilink == 6) ? 2 : (midilink > 3) ? 0 : midilink;
						SetMidiLinkMode(midilink);
						SetUARTMode(menusub);
					}
				}
				menustate = MENU_UART1;
				menusub = 0;
			}
		}
		break;

	case MENU_SFONT_FILE_SELECTED:
		{
			printf("MENU_SFONT_FILE_SELECTED --> '%s'\n", selPath);
			sprintf(Selected_tmp, "/sbin/mlinkutil FSSFONT /media/fat/\"%s\"", selPath);
			system(Selected_tmp);
			AdjustDirectory(selPath);
			// MENU_FILE_SELECT1 to file select OSD
			menustate = MENU_UART1; //MENU_FILE_SELECT1;
		}
		break;

	case MENU_BAUD1:
		{
			helptext_idx = 0;
			OsdSetTitle("UART Baud Rate");
			menustate = MENU_BAUD2;
			parentstate = MENU_BAUD1;

			m = 0;
			menumask = 0;
			int mode = GetUARTMode();
			const uint32_t *bauds = GetUARTbauds(mode);
			for (uint32_t i = 0; i < 13; i++)
			{
				if (!bauds[i]) break;
				menumask |= 1 << i;
				m = i;
			}

			uint32_t start = (16 - m)/2;
			uint32_t k = 0;
			while (k < start) OsdWrite(k++);

			for (uint32_t i = 0; i < 13; i++)
			{
				if (!bauds[i]) break;

				sprintf(s, " %s", GetUARTbaud_label(mode, i));
				OsdWrite(k++, s, menusub == i, 0);
			}

			while (k < 15) OsdWrite(k++);
			m++;
			menumask |= 1 << m;
			OsdWrite(15, STD_EXIT, menusub == m);
		}
		break;

	case MENU_BAUD2:
		{
			if (menu)
			{
				menustate = MENU_UART1;
				menusub = 5;
				break;
			}
			else if (select)
			{
				const uint32_t *bauds = GetUARTbauds(GetUARTMode());
				for (uint32_t i = 0; i < 13; i++)
				{
					if (!bauds[i]) break;
					if (menusub == i)
					{
						ValidateUARTbaud(GetUARTMode(), bauds[i]);
						if (GetUARTMode() >= 3)
						{
							sprintf(s, "/sbin/mlinkutil BAUD %d", GetUARTbaud(GetUARTMode()));
							system(s);
						}
						else
						{
							int mode = GetUARTMode();
							SetUARTMode(0);
							SetUARTMode(mode);
						}
					}
				}
				menusub = 5;
				menustate = MENU_UART1;
			}
		}
		break;

	case MENU_AFILTER_FILE_SELECTED:
		{
			char *p = strcasestr(selPath, AFILTER_DIR"/");
			if (!p) audio_set_filter(selPath);
			else
			{
				p += strlen(AFILTER_DIR);
				while (*p == '/') p++;
				audio_set_filter(p);
			}
			menustate = MENU_COMMON1;
		}
		break;

	case MENU_COEFF_FILE_SELECTED:
		{
			char *p = strcasestr(selPath, COEFF_DIR"/");
			if (!p) video_set_scaler_coeff(vfilter_type, selPath);
			else
			{
				p += strlen(COEFF_DIR);
				while (*p == '/') p++;
				video_set_scaler_coeff(vfilter_type, p);
			}
			menustate = MENU_VIDEOPROC1;
		}
		break;

	case MENU_GAMMA_FILE_SELECTED:
		{
			char *p = strcasestr(selPath, GAMMA_DIR"/");
			if (!p) video_set_gamma_curve(selPath);
			else
			{
				p += strlen(GAMMA_DIR);
				while (*p == '/') p++;
				video_set_gamma_curve(p);
			}
			menustate = MENU_VIDEOPROC1;
		}
		break;

	case MENU_SMASK_FILE_SELECTED:
		{
			char *p = strcasestr(selPath, SMASK_DIR"/");
			if (!p) video_set_shadow_mask(selPath);
			else
			{
				p += strlen(SMASK_DIR);
				while (*p == '/') p++;
				video_set_shadow_mask(p);
			}
			menustate = MENU_VIDEOPROC1;
		}
		break;

	case MENU_PRESET_FILE_SELECTED:
		memcpy(Selected_F[15], selPath, sizeof(Selected_F[15]));
		recent_update(SelectedDir, selPath, SelectedLabel, 15);
		video_loadPreset(selPath);
		menustate = MENU_VIDEOPROC1;
		break;

	case MENU_MISC1:
		OsdSetSize(16);
		helptext_idx = 0;
		menumask = 0xF;
		menustate = MENU_MISC2;
		OsdSetTitle("Misc. Options", OSD_ARROW_RIGHT);

		if (parentstate != MENU_MISC1)
		{
			for (int i = 0; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
			flag = 1;
			for (int i = 1; i < 4; i++) if (FileExists(cfg_get_name(i))) flag |= 1 << i;
			flag |= altcfg() << 4;
			menusub = 3;
		}
		parentstate = MENU_MISC1;

		OsdWrite(1, "         Information");

		if (menusub != 0) flag = (flag & 0xF) | (altcfg() << 4);
		strcpy(s, " Config:");
		m = 0;
		for (int i = 0; i < 4; i++)
		{
			int en = flag & (1 << i);
			if (i == (flag >> 4) && en) strcat(s, "\xc");
			strcat(s, " ");
			if (m) strcat(s, "\xc ");
			m = (i == (flag >> 4) && en);
			if (!en) strcat(s, "\xb");
			strcat(s, (!i) ? "Main" : (i == 1) ? "Alt1" : (i == 2) ? "Alt2" : "Alt3");
			if (!en) strcat(s, "\xb");
		}
		strcat(s, " ");
		if (m) strcat(s, "\xc");
		OsdWrite(10, s, menusub == 0);

		m = get_core_volume();
		{
			strcpy(s, "     Core Volume: ");
			if (audio_filter_en() >= 0) s[4] = 0x1b;
			memset(s + strlen(s), 0, 10);
			char *bar = s + strlen(s);
			memset(bar, 0x8C, 8);
			memset(bar, 0x7f, 8 - m);
		}
		OsdWrite(12, s, menusub == 1);

		m = get_volume();
		strcpy(s, "   Global Volume: ");
		if (m & 0x10)
		{
			strcat(s, "< Mute >");
		}
		else
		{
			memset(s+strlen(s), 0, 10);
			char *bar = s + strlen(s);
			int vol = (audio_filter_en() < 0) ? get_core_volume() : 0;
			memset(bar, 0x8C, 8 - vol);
			memset(bar, 0x7f, 8 - vol - m);
		}
		OsdWrite(13, s, menusub == 2);

		OsdWrite(15, STD_EXIT, menusub == 3, 0, OSD_ARROW_RIGHT);
		break;

	case MENU_MISC2:
		printSysInfo();
		if ((select && menusub == 3) || menu)
		{
			menustate = MENU_NONE1;
			break;
		}
		else if (menusub == 0 && (right || left || minus || plus || select))
		{
			uint8_t i = flag >> 4;
			if (select)
			{
				user_io_set_ini(i);
			}
			else
			{
				do
				{
					if (right || plus) i = (i + 1) & 3;
					else i = (i - 1) & 3;
				} while (!(flag & (1 << i)));

				flag = (flag & 0xF) | (i << 4);
			}
			menustate = MENU_MISC1;
		}
		else if(menusub == 1 && (right || left || minus || plus))
		{
			set_core_volume((right || plus) ? 1 : -1);
			menustate = MENU_MISC1;
		}
		else if (menusub == 2 && (right || left || minus || plus || select))
		{
			set_volume((right || plus) ? 1 : (left || minus) ? -1 : 0);
			menustate = MENU_MISC1;
		}
		else if (right)
		{
			// go back to core requesting this menu
			switch (user_io_core_type())
			{
			case CORE_TYPE_8BIT:
				if (is_menu())
				{
					menusub = 4;
					menustate = MENU_SYSTEM1;
				}
				else if (is_minimig())
				{
					menusub = 0;
					menustate = MENU_MINIMIG_MAIN1;
				}
				else if (is_archie())
				{
					menusub = 0;
					menustate = MENU_ARCHIE_MAIN1;
				}
				else if (is_st())
				{
					menusub = 0;
					menustate = MENU_ST_MAIN1;
				}
				else
				{
					menusub = 0;
					menustate = MENU_GENERIC_MAIN1;
				}
				break;
			case CORE_TYPE_SHARPMZ:
				menusub = menusub_last;
				menustate = sharpmz_default_ui_state();
				break;
			}
		}
		break;

	case MENU_JOYRESET:
		OsdWrite(3);
		OsdWrite(4, "       Reset to default");
		OsdWrite(5);
		menustate = MENU_JOYRESET1;
		break;

	case MENU_JOYRESET1:
		if (!user_io_user_button())
		{
			finish_map_setting(2);
			menustate = MENU_COMMON1;
			menusub = 1;
		}
		break;

	case MENU_JOYDIGMAP:
		helptext_idx = 0;
		menumask = 1;
		OsdSetTitle("Define buttons", 0);
		menustate = MENU_JOYDIGMAP1;
		parentstate = MENU_JOYDIGMAP;
		flash_timer = 0;
		flash_state = 0;
		for (int i = 0; i < OsdGetSize(); i++) OsdWrite(i);
		if (is_menu())
		{
			OsdWrite(8, "          Esc \x16 Cancel");
			OsdWrite(9, "        Enter \x16 Finish");
		}
		else
		{
			OsdWrite(8, "    Menu-hold \x16 Cancel");
			OsdWrite(9, "        Enter \x16 Finish");
		}
		break;

	case MENU_JOYDIGMAP1:
		{
			int line_info = 0;
			if (get_map_clear())
			{
				OsdWrite(3);
				OsdWrite(4, "           Clearing");
				OsdWrite(5);
				joymap_first = 1;
				break;
			}

			if (get_map_cancel())
			{
				OsdWrite(3);
				OsdWrite(4, "           Canceling");
				OsdWrite(5);
				break;
			}

			if (is_menu() && !get_map_button()) OsdWrite(7);

			const char* p = 0;
			if (get_map_button() < 0)
			{
				strcpy(s, joy_ana_map[get_map_button() + 6]);
				OsdWrite(7, "   Space/User \x16 Skip");
			}
			else if (get_map_button() < DPAD_NAMES)
			{
				p = joy_button_map[get_map_button()];
			}
			else if (joy_bcount)
			{
				p = joy_bnames[get_map_button() - DPAD_NAMES];
				if (is_menu())
				{
					if (!get_map_type()) joy_bcount = 17;
					if (get_map_button() == SYS_BTN_OSD_KTGL)
					{
						p = joy_button_map[DPAD_BUTTON_NAMES + get_map_type()];
						if (get_map_type())
						{
							OsdWrite(12, "   (can use 2-button combo)");
							line_info = 1;
						}
					}
				}
			}
			else
			{
				p = (get_map_button() < DPAD_BUTTON_NAMES) ? joy_button_map[get_map_button()] : joy_button_map[DPAD_BUTTON_NAMES + get_map_type()];
			}

			if (get_map_button() >= 0)
			{
				if (is_menu() && get_map_button() > SYS_BTN_CNT_ESC)
				{
					strcpy(s, joy_button_map[(get_map_button() - SYS_BTN_CNT_ESC - 1) + DPAD_BUTTON_NAMES + 2]);
				}
				else
				{
					s[0] = 0;
					int len = (30 - (strlen(p) + 7)) / 2;
					while (len > 0)
					{
						strcat(s, " ");
						len--;
					}

					strcat(s, "Press: ");
					strcat(s, p);
				}
			}

			OsdWrite(3, s, 0, 0);
			OsdWrite(4);

			if(is_menu() && joy_bcount && get_map_button() >= SYS_BTN_RIGHT && get_map_button() <= SYS_BTN_START)
			{
				// draw an on-screen gamepad to help with central button mapping
				if (!flash_timer || CheckTimer(flash_timer))
				{
					flash_timer = GetTimer(100);
					if (flash_state)
					{
						switch (get_map_button())
						{
							case SYS_BTN_L:      OsdWrite(10, "  \x86   \x88               \x86 R \x88  "); break;
							case SYS_BTN_R:      OsdWrite(10, "  \x86 L \x88               \x86   \x88  "); break;
							case SYS_BTN_UP:     OsdWrite(12, " \x83                     X   \x83");        break;
							case SYS_BTN_X:      OsdWrite(12, " \x83   U                     \x83");        break;
							case SYS_BTN_A:      OsdWrite(13, " \x83 L \x1b R  Sel Start  Y     \x83");     break;
							case SYS_BTN_Y:      OsdWrite(13, " \x83 L \x1b R  Sel Start      A \x83");     break;
							case SYS_BTN_LEFT:   OsdWrite(13, " \x83   \x1b R  Sel Start  Y   A \x83");     break;
							case SYS_BTN_RIGHT:  OsdWrite(13, " \x83 L \x1b    Sel Start  Y   A \x83");     break;
							case SYS_BTN_SELECT: OsdWrite(13, " \x83 L \x1b R      Start  Y   A \x83");     break;
							case SYS_BTN_START:  OsdWrite(13, " \x83 L \x1b R  Sel        Y   A \x83");     break;
							case SYS_BTN_DOWN:   OsdWrite(14, " \x83       \x86\x81\x81\x81\x81\x81\x81\x81\x81\x81\x88   B   \x83"); break;
							case SYS_BTN_B:      OsdWrite(14, " \x83   D   \x86\x81\x81\x81\x81\x81\x81\x81\x81\x81\x88       \x83"); break;
						}
					}
					else
					{
						OsdWrite(10, "  \x86 L \x88               \x86 R \x88  ");
						OsdWrite(11, " \x86\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x88");
						OsdWrite(12, " \x83   U                 X   \x83");
						OsdWrite(13, " \x83 L \x1b R  Sel Start  Y   A \x83");
						OsdWrite(14, " \x83   D   \x86\x81\x81\x81\x81\x81\x81\x81\x81\x81\x88   B   \x83");
						OsdWrite(15, " \x8b\x81\x81\x81\x81\x81\x81\x81\x8a         \x8b\x81\x81\x81\x81\x81\x81\x81\x8a");
					}
					flash_state = !flash_state;
				}
			}
			else
			{
				if(flash_timer)
				{
					//clear all gamepad gfx
					OsdWrite(10);
					OsdWrite(11);
					OsdWrite(12);
					OsdWrite(13);
					OsdWrite(14);
					OsdWrite(15);
					flash_timer = 0;
				}

				if (!line_info) OsdWrite(12);
			}

			if (get_map_vid() || get_map_pid())
			{
				if (!is_menu() && get_map_type() && !has_default_map() && !get_map_set())
				{
					for (int i = 0; i < OsdGetSize(); i++) OsdWrite(i);
					OsdWrite(6, "   You need to define this");
					OsdWrite(7, " joystick in Menu core first");
					OsdWrite(9, "      Press ESC/Enter");
					finish_map_setting(1);
					menustate = MENU_JOYDIGMAP2;
				}
				else
				{
					sprintf(s, "   %s ID: %04x:%04x", get_map_type() ? "Joystick" : "Keyboard", get_map_vid(), get_map_pid());
					if (get_map_button() > 0 || !joymap_first)
					{
						OsdWrite(7, (!get_map_type()) ? "         User \x16 Undefine" :
							is_menu() ? "   User/Space \x16 Undefine" : "    User/Menu \x16 Undefine");

						if (!get_map_type()) OsdWrite(9);
					}
					OsdWrite(5, s);
					if (!is_menu()) OsdWrite(10, "          F12 \x16 Clear all");
				}
			}

			if (!is_menu() && (get_map_button() >= (joy_bcount ? joy_bcount + 4 : 8) || (select & get_map_vid() & get_map_pid())) && joymap_first && get_map_type())
			{
				finish_map_setting(0);
				menustate = MENU_JOYDIGMAP3;
				menusub = 0;
			}
			else if (select || menu || get_map_button() >= (joy_bcount ? joy_bcount + 4 : 8))
			{
				finish_map_setting(menu);
				if (is_menu())
				{
					menustate = MENU_SYSTEM1;
					menusub = 2;
				}
				else
				{
					menustate = MENU_COMMON1;
					menusub = 1;
				}
			}
		}
		break;

	case MENU_JOYDIGMAP2:
		if (select || menu)
		{
			menustate = MENU_COMMON1;
			menusub = 1;
		}
		break;

	case MENU_JOYDIGMAP3:
		for (int i = 0; i < OsdGetSize(); i++) OsdWrite(i);
		m = 6;
		menumask = 3;
		OsdWrite(m++, "    Do you want to setup");
		OsdWrite(m++, "    alternative buttons?");
		OsdWrite(m++, "           No", menusub == 0);
		OsdWrite(m++, "           Yes", menusub == 1);
		parentstate = menustate;
		menustate = MENU_JOYDIGMAP4;
		break;

	case MENU_JOYDIGMAP4:
		if (menu)
		{
			menustate = MENU_COMMON1;
			menusub = 1;
			break;
		}
		else if (select)
		{
			switch (menusub)
			{
			case 0:
				menustate = MENU_COMMON1;
				menusub = 1;
				break;

			case 1:
				start_map_setting(joy_bcount ? joy_bcount + 4 : 8, 1);
				menustate = MENU_JOYDIGMAP;
				menusub = 0;
				joymap_first = 0;
				break;
			}
		}
		break;

	case MENU_JOYKBDMAP:
		helptext_idx = 0;
		menumask = 1;
		menustate = MENU_JOYKBDMAP1;
		parentstate = MENU_JOYKBDMAP;

		OsdSetTitle("Button/Key remap", 0);
		for (int i = 0; i < 5; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(5, info_top, 0, 0);
		infowrite(6, "Supported mapping:");
		infowrite( 7, "");
		infowrite( 8, "Button -> Key");
		infowrite( 9, "Button -> Button same pad");
		infowrite(10, "Key -> Key");
		infowrite(11, "");
		infowrite(12, "It will be cleared when you");
		infowrite(13, "load the new core");
		OsdWrite(14, info_bottom, 0, 0);
		OsdWrite(OsdGetSize() - 1, "           Cancel", menusub == 0, 0);
		break;

	case MENU_JOYKBDMAP1:
		if (!get_map_button())
		{
			OsdWrite(1, " Press button/key to change", 0, 0);
			if (get_map_vid())
			{
				OsdWrite(2, "", 0, 0);
				sprintf(s, "    on device %04x:%04x", get_map_vid(), get_map_pid());
				OsdWrite(3, s, 0, 0);
			}
			OsdWrite(OsdGetSize() - 1, " Enter \x16 Finish, Esc \x16 Clear", menusub == 0, 0);
		}
		else
		{
			if (get_map_button() <= 256)
			{
				OsdWrite(1, "     Press key to map to", 0, 0);
				OsdWrite(2, "", 0, 0);
				OsdWrite(3, "        on a keyboard", 0, 0);
			}
			else
			{
				OsdWrite(1, "   Press button to map to", 0, 0);
				OsdWrite(2, "      on the same pad", 0, 0);
				OsdWrite(3, "    or key on a keyboard", 0, 0);
			}
			OsdWrite(OsdGetSize() - 1);
		}

		if (select || menu)
		{
			finish_map_setting(menu);
			menustate = MENU_COMMON1;
			menusub = 2;
		}
		break;

	case MENU_ABOUT1:
		OsdSetSize(16);
		menumask = 0;
		helptext_idx = 0;
		OsdSetTitle("About", 0);
		menustate = MENU_ABOUT2;
		parentstate = MENU_ABOUT1;
		StarsInit();
		ScrollReset();
		for (int i = 5; i < OsdGetSize(); i++) OsdWrite(i, "", 0, 0);
		break;

	case MENU_ABOUT2:
		StarsUpdate();
		m = 0;
		while (m < 10) OsdDrawLogo(m++);
		OsdWrite(m++, "     www.MiSTerFPGA.org", 0, 0, 1);
		OsdWrite(m++, "", 0, 0, 1);
		sprintf(s, "       MiSTer v%s", version + 5);
		OsdWrite(m++, s, 0, 0, 1);

		s[0] = 0;
		{
			int len = strlen(OsdCoreNameGet());
			if (len > 30) len = 30;
			int sp = (30 - len) / 2;
			for (int i = 0; i < sp; i++) strcat(s, " ");
			char *s2 = s + strlen(s);
			char *s3 = OsdCoreNameGet();
			for (int i = 0; i < len; i++) *s2++ = *s3++;
			*s2++ = 0;
		}
		OsdWrite(m++, s, 0, 0, 1);
		OsdWrite(m++, "", 0, 0, 1);
		ScrollText(m++, "                                 MiSTer by Alexey Melnikov, based on MiST by Till Harbaum, Minimig by Dennis van Weeren and other projects. MiSTer hardware and software is distributed under the terms of the GNU General Public License version 3. MiSTer FPGA cores are the work of their respective authors under individual licensing. Go to www.MiSTerFPGA.org for more details.", 0, 0, 0, 0);

		if (menu | select | left)
		{
			menustate = MENU_COMMON1;
			menusub = 16;
		}
		break;


		/******************************************************************/
		/* st main menu                                                 */
		/******************************************************************/

	case MENU_ST_MAIN1:
		OsdSetSize(16);
		menumask = 0x77f;
		OsdSetTitle("AtariST", 0);
		firstmenu = 0;
		m = 0;

		OsdWrite(m++);
		for (uint32_t i = 0; i < 2; i++)
		{
			snprintf(s, 29, " %c: %s%s", 'A' + i, (tos_system_ctrl() & (TOS_CONTROL_FDC_WR_PROT_A << i)) ? "\x17" : "", tos_get_disk_name(i));
			OsdWrite(m++, s, menusub == i, 0);
		}
		strcpy(s, " Write protect:  ");
		strcat(s, config_tos_wrprot[(tos_system_ctrl() >> 6) & 3]);
		OsdWrite(m++, s, menusub == 2, 0);
		OsdWrite(m++);

		snprintf(s, 29, " Joysticks swap: %s", user_io_get_joyswap() ? "Yes" : "No");
		OsdWrite(m++, s, menusub == 3);
		OsdWrite(m++);

		OsdWrite(m++, " Modify config             \x16", menusub == 4);
		OsdWrite(m++, " Load config               \x16", menusub == 5);
		OsdWrite(m++, " Save config               \x16", menusub == 6);
		OsdWrite(m++);

		if (spi_uio_cmd16(UIO_GET_OSDMASK, 0) & 1)
		{
			menumask |= 0x80;
			OsdWrite(m++, " MT32-pi                   \x16", menusub == 7);
			OsdWrite(m++);
		}

		OsdWrite(m++, " Reset", menusub == 8);
		OsdWrite(m++, " Cold Boot", menusub == 9);

		for (; m < OsdGetSize()-1; m++) OsdWrite(m);
		OsdWrite(15, STD_EXIT, menusub == 10, 0, OSD_ARROW_RIGHT | OSD_ARROW_LEFT);

		menustate = MENU_ST_MAIN2;
		parentstate = MENU_ST_MAIN1;
		break;

	case MENU_ST_MAIN2:
		// menu key closes menu
		if (menu)
		{
			menustate = MENU_NONE1;
		}
		else if (right)
		{
			menustate = MENU_COMMON1;
			menusub = 0;
		}
		else if (left)
		{
			menustate = MENU_MISC1;
			menusub = 3;
		}
		else if (menusub <= 1 && (select || recent))
		{
			if (tos_disk_is_inserted(menusub))
			{
				tos_insert_disk(menusub, "");
				menustate = MENU_ST_MAIN1;
			}
			else
			{
				fs_Options = SCANO_DIR;
				fs_MenuSelect = MENU_ST_FDD_FILE_SELECTED;
				fs_MenuCancel = MENU_ST_MAIN1;
				strcpy(fs_pFileExt, "ST");
				if (select) SelectFile(Selected_F[menusub], "ST", fs_Options, fs_MenuSelect, fs_MenuCancel);
				else if (recent_init(menusub)) menustate = MENU_RECENT1;
			}
		}
		else if (select || minus || plus)
		{
			switch (menusub)
			{
			case 2:
				// remove current write protect bits and increase by one
				tos_update_sysctrl((tos_system_ctrl() & ~(TOS_CONTROL_FDC_WR_PROT_A | TOS_CONTROL_FDC_WR_PROT_B))
					| (((((tos_system_ctrl() >> 6) & 3) + (minus ? -1 : 1)) & 3) << 6));
				menustate = MENU_ST_MAIN1;
				break;

			case 3:
				if (select)
				{
					user_io_set_joyswap(!user_io_get_joyswap());
					menustate = MENU_ST_MAIN1;
				}
				break;

			case 4:  // System submenu
				if (select)
				{
					menustate = MENU_ST_SYSTEM1;
					need_reset = 0;
					menusub = 0;
				}
				break;

			case 5:  // Load config
				if (select)
				{
					menustate = MENU_ST_LOAD_CONFIG1;
					menusub = 0;
				}
				break;

			case 6:  // Save config
				if (select)
				{
					menustate = MENU_ST_SAVE_CONFIG1;
					menusub = 0;
				}
				break;

			case 7:
				if (select)
				{
					menustate = MENU_MT32PI_MAIN1;
					menusub = 0;
				}
				break;

			case 8:  // Reset
				if (select)
				{
					tos_reset(0);
					menustate = MENU_NONE1;
				}
				break;

			case 9:  // Cold Boot
				if (select)
				{
					tos_insert_disk(0, "");
					tos_insert_disk(1, "");
					tos_reset(1);
					menustate = MENU_NONE1;
				}
				break;

			case 10:  // Exit
				if (select)
				{
					menustate = MENU_NONE1;
				}
				break;
			}
		}
		break;

	case MENU_ST_FDD_FILE_SELECTED:
		memcpy(Selected_F[menusub], selPath, sizeof(Selected_F[menusub]));
		recent_update(SelectedDir, selPath, SelectedLabel, menusub);
		tos_insert_disk(menusub, selPath);
		menustate = MENU_ST_MAIN1;
		break;

	case MENU_ST_SYSTEM1:
		menumask = 0xffff;
		OsdSetTitle("Config", 0);
		helptext_idx = 0;

		while (1)
		{
			if (!menusub) firstmenu = 0;
			adjvisible = 0;
			m = 0;

			for (uint32_t i = 0; i < 2; i++)
			{
				snprintf(s, 29, " HDD%d: %s", i, tos_get_disk_name(2 + i));
				MenuWrite(m++, s, menusub == i);
			}
			MenuWrite(m++);

			snprintf(s, 29, " Cart: %s", tos_get_cartridge_name());
			MenuWrite(m++, s, menusub == 2);
			MenuWrite(m++);

			strcpy(s, " Memory:     ");
			strcat(s, tos_mem[(tos_system_ctrl() >> 1) & 7]);
			MenuWrite(m++, s, menusub == 3);

			snprintf(s, 29, " TOS:        %s", tos_get_image_name());
			MenuWrite(m++, s, menusub == 4);

			strcpy(s, " Chipset:    ");
			// extract  TOS_CONTROL_STE and  TOS_CONTROL_MSTE bits
			strcat(s, tos_chipset[(tos_system_ctrl() >> 23) & 3]);
			MenuWrite(m++, s, menusub == 5);
			MenuWrite(m++);

			// Blitter is always present in >= STE
			enable = (tos_system_ctrl() & (TOS_CONTROL_STE | TOS_CONTROL_MSTE)) ? 1 : 0;
			strcpy(s, " Blitter:    ");
			strcat(s, ((tos_system_ctrl() & TOS_CONTROL_BLITTER) || enable) ? "On" : "Off");
			MenuWrite(m++, s, menusub == 6, enable);

			// Viking card can only be enabled with max 8MB RAM
			enable = (tos_system_ctrl() & 0xe) <= TOS_MEMCONFIG_8M;
			strcpy(s, " Viking:     ");
			strcat(s, ((tos_system_ctrl() & TOS_CONTROL_VIKING) && enable) ? "On" : "Off");
			MenuWrite(m++, s, menusub == 7, enable ? 0 : 1);

			strcpy(s, " Aspect:     ");
			tos_set_ar(get_ar_name(tos_get_ar(), s));
			MenuWrite(m++, s, menusub == 8);

			strcpy(s, " Screen:     ");
			if (tos_system_ctrl() & TOS_CONTROL_VIDEO_COLOR) strcat(s, "Color");
			else                                             strcat(s, "Mono");
			MenuWrite(m++, s, menusub == 9);

			strcpy(s, " Mono 60Hz:  ");
			if (tos_system_ctrl() & TOS_CONTROL_MDE60) strcat(s, "On");
			else                                       strcat(s, "Off");
			MenuWrite(m++, s, menusub == 10);

			strcpy(s, " Video Crop: ");
			if (tos_system_ctrl() & TOS_CONTROL_BORDER) strcat(s, (tos_get_extctrl() & 0x400) ? "Visible 216p(5x)" : "Visible");
			else                                        strcat(s, "Full");
			MenuWrite(m++, s, menusub == 11);

			strcpy(s, " Scale:      ");
			strcat(s, config_scale[(tos_get_extctrl() >> 11) & 3]);
			MenuWrite(m++, s, menusub == 12);

			strcpy(s, " Scanlines:  ");
			strcat(s, tos_scanlines[(tos_system_ctrl() >> 20) & 3]);
			MenuWrite(m++, s, menusub == 13);
			MenuWrite(m++);

			strcpy(s, " YM-Audio:   ");
			strcat(s, tos_stereo[(tos_system_ctrl() & TOS_CONTROL_STEREO) ? 1 : 0]);
			MenuWrite(m++, s, menusub == 14);
			MenuWrite(m++);

			MenuWrite(m++, STD_BACK, menusub == 15);

			if (!adjvisible) break;
			firstmenu += adjvisible;
		}

		parentstate = menustate;
		menustate = MENU_ST_SYSTEM2;
		break;

	case MENU_ST_SYSTEM2:
		saved_menustate = MENU_ST_SYSTEM1;

		if (menu)
		{
			menustate = MENU_NONE1;
		}
		else if (back || left)
		{
			menustate = MENU_ST_MAIN1;
			menusub = 4;
			if (need_reset)
			{
				tos_reset(1);
				saved_menustate = 0;
			}
		}
		else if (menusub <= 2 && (select || recent))
		{
			if (menusub <= 1)
			{
				fs_Options = SCANO_DIR | SCANO_UMOUNT;
				fs_MenuSelect = MENU_ST_HDD_FILE_SELECTED;
				fs_MenuCancel = MENU_ST_SYSTEM1;
				strcpy(fs_pFileExt, "VHD");
				if (select) SelectFile(Selected_S[menusub], "VHD", fs_Options, fs_MenuSelect, fs_MenuCancel);
				else if (recent_init(menusub + 500)) menustate = MENU_RECENT1;
			}
			else
			{
				if (tos_cartridge_is_inserted())
				{
					tos_load_cartridge("");
					menustate = MENU_ST_SYSTEM1;
				}
				else
				{
					fs_Options = SCANO_DIR;
					fs_MenuSelect = MENU_ST_SYSTEM_FILE_SELECTED;
					fs_MenuCancel = MENU_ST_SYSTEM1;
					strcpy(fs_pFileExt, "IMG");
					if (select) SelectFile(Selected_F[menusub], "IMG", fs_Options, fs_MenuSelect, fs_MenuCancel);
					else if (recent_init(menusub)) menustate = MENU_RECENT1;
				}
			}

		}
		else if (select || plus || minus)
		{
			switch (menusub)
			{
			case 3:
				{
					// RAM
					int mem = (tos_system_ctrl() >> 1) & 7;   // current memory config
					if (minus)
					{
						mem--;
						if (mem < 0) mem = 5;
					}
					else
					{
						mem++;
						if (mem > 5) mem = 0;
					}
					tos_update_sysctrl((tos_system_ctrl() & ~0x0e) | (mem << 1));
					need_reset = 1;
					menustate = MENU_ST_SYSTEM1;
				}
				break;

			case 4:  // TOS
				if (select) SelectFile(Selected_F[menusub], "IMG", SCANO_DIR, MENU_ST_SYSTEM_FILE_SELECTED, MENU_ST_SYSTEM1);
				break;

			case 5:
				{
					int chipset = (((tos_system_ctrl() >> 23) + (minus ? -1 : 1)) & 3);
					tos_update_sysctrl((tos_system_ctrl() & ~(TOS_CONTROL_STE | TOS_CONTROL_MSTE)) | (chipset << 23));
					menustate = MENU_ST_SYSTEM1;
				}
				break;

			case 6:
				if (!(tos_system_ctrl() & TOS_CONTROL_STE))
				{
					tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_BLITTER);
					menustate = MENU_ST_SYSTEM1;
				}
				break;

			case 7:
				// viking/sm194
				tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_VIKING);
				menustate = MENU_ST_SYSTEM1;
				break;

			case 8:
				tos_set_ar(next_ar(tos_get_ar(), minus));
				tos_update_sysctrl(tos_system_ctrl());
				menustate = MENU_ST_SYSTEM1;
				break;

			case 9:
				tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_VIDEO_COLOR);
				menustate = MENU_ST_SYSTEM1;
				break;

			case 10:
				tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_MDE60);
				menustate = MENU_ST_SYSTEM1;
				break;

			case 11:
				{
					int mode = ((tos_system_ctrl() & TOS_CONTROL_BORDER) ? 1 : 0) | ((tos_get_extctrl() & 0x400) ? 2 : 0);
					if (minus)
					{
						mode = (mode == 0) ? 3 : (mode == 3) ? 1 : 0;
					}
					else
					{
						mode = (mode == 0) ? 1 : (mode == 1) ? 3 : 0;
					}

					tos_update_sysctrl((mode & 1) ? (tos_system_ctrl() | TOS_CONTROL_BORDER) : (tos_system_ctrl() & ~TOS_CONTROL_BORDER));
					tos_set_extctrl((mode & 2) ? (tos_get_extctrl() | 0x400) : (tos_get_extctrl() & ~0x400));
					menustate = MENU_ST_SYSTEM1;
				}
				break;

			case 12:
				{
					int mode = ((tos_get_extctrl() >> 11) + (minus ? -1 : 1)) & 3;
					tos_set_extctrl((tos_get_extctrl() & ~0x1800) | (mode << 11));
					menustate = MENU_ST_SYSTEM1;
				}
				break;

			case 13:
				{
					// next scanline state
					int scan = ((tos_system_ctrl() >> 20) + (minus ? -1 : 1)) & 3;
					tos_update_sysctrl((tos_system_ctrl() & ~TOS_CONTROL_SCANLINES) | (scan << 20));
					menustate = MENU_ST_SYSTEM1;
				}
				break;

			case 14:
				tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_STEREO);
				menustate = MENU_ST_SYSTEM1;
				break;


			case 15:
				menustate = MENU_ST_MAIN1;
				menusub = 4;
				if (need_reset)
				{
					tos_reset(1);
					saved_menustate = 0;
				}
				break;
			}
		}
		break;

	case MENU_ST_HDD_FILE_SELECTED:
		printf("Insert image for disk %d\n", menusub);
		memcpy(Selected_S[menusub], selPath, sizeof(Selected_S[menusub]));
		recent_update(SelectedDir, selPath, SelectedLabel, menusub + 500);
		tos_insert_disk(menusub+2, selPath);
		menustate = MENU_ST_SYSTEM1;
		break;

	case MENU_ST_SYSTEM_FILE_SELECTED: // file successfully selected
		if (menusub == 4)
		{
			memcpy(Selected_F[menusub], selPath, sizeof(Selected_F[menusub]));
			tos_upload(selPath);
			menustate = MENU_ST_SYSTEM1;
		}

		if (menusub == 2)
		{
			memcpy(Selected_F[menusub], selPath, sizeof(Selected_F[menusub]));
			recent_update(SelectedDir, selPath, SelectedLabel, menusub);
			tos_load_cartridge(selPath);
			menustate = MENU_ST_SYSTEM1;
		}
		break;

	case MENU_ST_LOAD_CONFIG1:
		helptext_idx = 0;
		OsdSetTitle("Load Config", 0);

		if (parentstate != menustate)	// First run?
		{
			parentstate = menustate;
			menumask = 0x201;
			for (uint32_t i = 1; i < 9; i++) if (tos_config_exists(i)) menumask |= 1<<i;
		}

		m = 0;
		OsdWrite(m++);
		OsdWrite(m++, " Startup config:");
		for (uint32_t i = 0; i < 9; i++)
		{
			snprintf(s, 29, "  %s", (menumask & (1 << i)) ? tos_get_cfg_string(i) : "");
			OsdWrite(m++, s, menusub == i, !(menumask & (1<<i)));
			if (!i)
			{
				OsdWrite(m++);
				OsdWrite(m++, " Other configs:");
			}
		}

		for (; m < OsdGetSize() - 1; m++) OsdWrite(m);
		OsdWrite(15, STD_BACK, menusub == 9, 0);

		menustate = MENU_ST_LOAD_CONFIG2;
		break;

	case MENU_ST_LOAD_CONFIG2:
		if (menu || left)
		{
			menustate = MENU_ST_MAIN1;
			menusub = 5;
		}

		if (select)
		{
			if (menusub < 9)
			{
				tos_config_load(menusub);
				tos_upload(NULL);
				menustate = MENU_NONE1;
			}
			else
			{
				menustate = MENU_ST_MAIN1;
				menusub = 5;
			}
		}
		break;

	case MENU_ST_SAVE_CONFIG1:
		helptext_idx = 0;
		OsdSetTitle("Save Config", 0);

		parentstate = menustate;
		menumask = 0x3FF;

		m = 0;
		OsdWrite(m++);
		OsdWrite(m++, " Startup config:");
		for (uint32_t i = 0; i < 9; i++)
		{
			snprintf(s, 29, "  %s", tos_get_cfg_string(i));
			OsdWrite(m++, s, menusub == i, !(menumask & (1 << i)));
			if (!i)
			{
				OsdWrite(m++);
				OsdWrite(m++, " Other configs:");
			}
		}

		for (; m < OsdGetSize() - 1; m++) OsdWrite(m);
		OsdWrite(15, STD_BACK, menusub == 9, 0);

		menustate = MENU_ST_SAVE_CONFIG2;
		break;

	case MENU_ST_SAVE_CONFIG2:
		if (menu || left)
		{
			menustate = MENU_ST_MAIN1;
			menusub = 6;
		}

		if (select)
		{
			if (menusub < 9)
			{
				tos_config_save(menusub);
				menustate = MENU_NONE1;
			}
			else
			{
				menustate = MENU_ST_MAIN1;
				menusub = 6;
			}
		}
		break;


	case MENU_MT32PI_MAIN1:
		{
			parentstate = menustate;
			OsdSetTitle("MT32-pi");
			menumask = 0x7F;
			uint32_t mt32_cfg = is_minimig() ? minimig_get_extcfg() : tos_get_extctrl();

			m = 0;
			OsdWrite(m++);
			strcpy(s, " Use MT32-pi:            ");
			strcat(s, (mt32_cfg & 0x2) ? " No" : "Yes");
			OsdWrite(m++, s, menusub == 0);

			strcpy(s, " Show Info: ");
			switch ((mt32_cfg >> 8) & 3)
			{
			case 0:
				strcat(s, "              No");
				break;

			case 1:
				strcat(s, "             Yes");
				break;

			case 2:
				strcat(s, "  LCD-On(non-FB)");
				break;

			case 3:
				strcat(s, "LCD-Auto(non-FB)");
				break;
			}
			OsdWrite(m++, s, menusub == 1);

			OsdWrite(m++);
			OsdWrite(m++, " Default Config:");

			strcpy(s, " Synth:           ");
			strcat(s, (mt32_cfg & 0x4) ? "FluidSynth" : "      Munt");
			OsdWrite(m++, s, menusub == 2);

			strcpy(s, " Munt ROM:          ");
			switch ((mt32_cfg >> 3) & 3)
			{
			case 0:
				strcat(s, "MT-32 v1");
				break;

			case 1:
				strcat(s, "MT-32 v2");
				break;

			case 2:
				strcat(s, "  CM-32L");
				break;

			case 3:
				strcat(s, " Unknown");
				break;
			}
			OsdWrite(m++, s, menusub == 3);

			sprintf(s, " SoundFont:                %d", (mt32_cfg >> 5) & 7);
			OsdWrite(m++, s, menusub == 4);

			OsdWrite(m++);
			strcpy(s, " Current Config: ");
			hdmask = spi_uio_cmd16(UIO_GET_OSDMASK, 0);
			if (((hdmask >> 1) & 3) == 1)
			{
				switch ((hdmask >> 3) & 3)
				{
				case 0:
					strcat(s, "   MT-32 v1");
					break;

				case 1:
					strcat(s, "   MT-32 v2");
					break;

				case 2:
					strcat(s, "     CM-32L");
					break;

				default:
					strcat(s, "    Unknown");
					break;
				}
			}
			else if (((hdmask >> 1) & 3) == 2)
			{
				sprintf(s + strlen(s), "SoundFont %d", (hdmask >> 3) & 7);
			}
			else
			{
				strcat(s, "    Unknown");
			}
			OsdWrite(m++, s);

			OsdWrite(m++);
			OsdWrite(m++, " Reset Hanging Notes", menusub == 5);

			while (m < 15) OsdWrite(m++);
			OsdWrite(15, STD_BACK, menusub == 6, 0);

			menustate = MENU_MT32PI_MAIN2;
		}
		break;


	case MENU_MT32PI_MAIN2:
		if (menu || back || left || (select && menusub == 6))
		{
			if (is_minimig())
			{
				menustate = MENU_MINIMIG_MAIN1;
				menusub = 8;
			}
			else
			{
				menustate = MENU_ST_MAIN1;
				menusub = 7;
			}
		}
		else if (select || plus || minus)
		{
			uint32_t mt32_cfg = is_minimig() ? minimig_get_extcfg() : tos_get_extctrl();

			switch (menusub)
			{
			case 0:
				mt32_cfg ^= 0x2;
				menustate = MENU_MT32PI_MAIN1;
				break;

			case 1:
				m = (mt32_cfg >> 8) & 3;
				m = (m + (minus ? -1 : 1)) & 3;
				mt32_cfg = (mt32_cfg & ~0x300) | (m<<8);
				menustate = MENU_MT32PI_MAIN1;
				break;

			case 2:
				mt32_cfg ^= 0x4;
				menustate = MENU_MT32PI_MAIN1;
				break;

			case 3:
				m = (mt32_cfg >> 3) & 3;
				if (minus)
				{
					m = (m - 1) & 3;
					if (m == 3) m = 2;
				}
				else
				{
					m = (m + 1) & 3;
					if (m == 3) m = 0;
				}
				mt32_cfg = (mt32_cfg & ~0x18) | (m << 3);
				menustate = MENU_MT32PI_MAIN1;
				break;

			case 4:
				m = (mt32_cfg >> 5) & 7;
				m = (m + (minus ? -1 : 1)) & 7;
				mt32_cfg = (mt32_cfg & ~0xE0) | (m << 5);
				menustate = MENU_MT32PI_MAIN1;
				break;

			case 5:
				if (select)
				{
					if (is_minimig()) minimig_set_extcfg(mt32_cfg | 1);
					else tos_set_extctrl(mt32_cfg | 1);
					mt32_cfg &= ~1;
					menustate = MENU_MT32PI_MAIN1;
				}
				break;
			}

			if (is_minimig()) minimig_set_extcfg(mt32_cfg);
			else tos_set_extctrl(mt32_cfg);
		}
		else if (spi_uio_cmd16(UIO_GET_OSDMASK, 0) != hdmask)
		{
			menustate = MENU_MT32PI_MAIN1;
		}
		break;

		/******************************************************************/
		/* file selection menu                                            */
		/******************************************************************/
	case MENU_FILE_SELECT1:
		helptext_idx = (fs_Options & SCANO_UMOUNT) ? HELPTEXT_EJECT : (fs_Options & SCANO_CLEAR) ? HELPTEXT_CLEAR : 0;
		OsdSetTitle((fs_Options & SCANO_CORES) ? "Cores" : "Select", 0);
		PrintDirectory(hold_cnt<2);
		menustate = MENU_FILE_SELECT2;
		if (cfg.log_file_entry)
		{
			//Write out paths infos for external integration
			FILE* filePtr = fopen("/tmp/CURRENTPATH", "w");
			FILE* pathPtr = fopen("/tmp/FULLPATH", "w");
			fprintf(filePtr, "%s", flist_SelectedItem()->altname);
			fprintf(pathPtr, "%s", selPath);
			fclose(filePtr);
			fclose(pathPtr);
		}
		break;

	case MENU_FILE_SELECT2:
		menumask = 0;

		if (c == KEY_BACKSPACE && (fs_Options & (SCANO_UMOUNT | SCANO_CLEAR)) && !strlen(filter))
		{
			for (int i = 0; i < OsdGetSize(); i++) OsdWrite(i, "", 0, 0);
			if (fs_Options & SCANO_CLEAR)
			{
				int i = (OsdGetSize() / 2) - 2;
				OsdWrite(i++, "     Clearing the option");
				OsdWrite(i++);
				OsdWrite(i++, " You have to reload the core");
				OsdWrite(i++, "    to use default value.");
				OsdUpdate();
				sleep(2);
			}
			else
			{
				OsdWrite(OsdGetSize() / 2, "    Unmounting the image", 0, 0);
				OsdUpdate();
				sleep(1);
			}
			input_poll(0);
			menu_key_set(0);
			selPath[0] = 0;
			menustate = fs_MenuSelect;
			helptext_idx = 0;
			break;
		}

		if (menu)
		{
			if (flist_nDirEntries() && flist_SelectedItem()->de.d_type != DT_DIR)
			{
				SelectedDir[0] = 0;
				if (strlen(selPath))
				{
					strcpy(SelectedDir, selPath);
					strcat(selPath, "/");
				}
				strcat(selPath, flist_SelectedItem()->de.d_name);
			}

			if (!strcasecmp(fs_pFileExt, "RBF")) selPath[0] = 0;
			menustate = fs_MenuCancel;
			helptext_idx = 0;
		}

		if (recent && recent_init((fs_Options & SCANO_CORES) ? -1 : (fs_Options & SCANO_UMOUNT) ? ioctl_index + 500 : ioctl_index))
		{
			menustate = MENU_RECENT1;
		}

		if (c == KEY_BACKSPACE)
		{
			filter[0] = 0;
			filter_typing_timer = 0;
			ScanDirectory(selPath, SCANF_INIT, fs_pFileExt, fs_Options);
			menustate = MENU_FILE_SELECT1;
		}

		if (flist_nDirEntries())
		{
			ScrollLongName(); // scrolls file name if longer than display line

			if (c == KEY_HOME || c == KEY_TAB)
			{
				filter_typing_timer = 0;
				ScanDirectory(selPath, SCANF_INIT, fs_pFileExt, fs_Options);
				menustate = MENU_FILE_SELECT1;
				select = (c == KEY_TAB && flist_SelectedItem()->de.d_type == DT_DIR && !strcmp(flist_SelectedItem()->de.d_name, ".."));
			}

			if (c == KEY_END)
			{
				filter_typing_timer = 0;
				ScanDirectory(selPath, SCANF_END, fs_pFileExt, fs_Options);
				menustate = MENU_FILE_SELECT1;
			}

			if ((c == KEY_PAGEUP) || (c == KEY_LEFT))
			{
				filter_typing_timer = 0;
				ScanDirectory(selPath, SCANF_PREV_PAGE, fs_pFileExt, fs_Options);
				menustate = MENU_FILE_SELECT1;
			}

			if ((c == KEY_PAGEDOWN) || (c == KEY_RIGHT))
			{
				filter_typing_timer = 0;
				ScanDirectory(selPath, SCANF_NEXT_PAGE, fs_pFileExt, fs_Options);
				menustate = MENU_FILE_SELECT1;
			}

			if (down) // scroll down one entry
			{
				filter_typing_timer = 0;
				ScanDirectory(selPath, SCANF_NEXT, fs_pFileExt, fs_Options);
				menustate = MENU_FILE_SELECT1;
			}

			if (up) // scroll up one entry
			{
				filter_typing_timer = 0;
				ScanDirectory(selPath, SCANF_PREV, fs_pFileExt, fs_Options);
				menustate = MENU_FILE_SELECT1;
			}

			{
				char i;
				if ((i = GetASCIIKey(c)) > 1)
				{
					int filter_len = strlen(filter);
					if (CheckTimer(filter_typing_timer))
					{
						filter[0] = i;
						filter[1] = 0;

						// You need both ScanDirectory calls here: the first
						// call "clears" the filter, the second one scrolls to
						// the right place in the list
						ScanDirectory(selPath, SCANF_INIT, fs_pFileExt, fs_Options);
						ScanDirectory(selPath, i, fs_pFileExt, fs_Options);
					}
					else if (filter_len < 255)
					{
						filter[filter_len++] = i;
						filter[filter_len] = 0;
						ScanDirectory(selPath, SCANF_INIT, fs_pFileExt, fs_Options, NULL, filter);
					}

					filter_typing_timer = GetTimer(2000);
					printf("filter is: %s\n", filter);

					menustate = MENU_FILE_SELECT1;
				}
			}

			if (select)
			{
				static char name[256];
				char type = flist_SelectedItem()->de.d_type;
				memcpy(name, flist_SelectedItem()->de.d_name, sizeof(name));

				if ((fs_Options & SCANO_UMOUNT) && (is_megacd() || is_pce() || (is_psx() && !(fs_Options & SCANO_SAVES))) && type == DT_DIR && strcmp(flist_SelectedItem()->de.d_name, ".."))
				{
					int len = strlen(selPath);
					strcat(selPath, "/");
					strcat(selPath, name);
					int num = ScanDirectory(selPath, SCANF_INIT, fs_pFileExt, 0);
					if (num != 1) selPath[len] = 0;
					else
					{
						type = flist_SelectedItem()->de.d_type;
						memcpy(name, flist_SelectedItem()->de.d_name, sizeof(name));
					}
				}

				if (type == DT_DIR)
				{
					changeDir(name);
					menustate = MENU_FILE_SELECT1;
				}
				else
				{
					if (flist_nDirEntries())
					{
						SelectedDir[0] = 0;
						if (strlen(selPath))
						{
							strcpy(SelectedDir, selPath);
							strcat(selPath, "/");
						}
						strcpy(SelectedLabel, flist_SelectedItem()->altname);
						if (fs_Options & SCANO_CORES)
						{
							int len = strlen(SelectedLabel);
							if (SelectedLabel[len - 4] == '.') SelectedLabel[len - 4] = 0;
							char *p = strstr(SelectedLabel, "_20");
							if (p) *p = 0;
						}
						strcat(selPath, name);
						menustate = fs_MenuSelect;
						helptext_idx = 0;
					}
				}
			}
		}

		if (release) PrintDirectory(1);
		break;

		/******************************************************************/
		/* cheats menu                                                    */
		/******************************************************************/
	case MENU_CHEATS1:
		helptext_idx = 0;
		sprintf(s, "Cheats (%d)", cheats_loaded());
		OsdSetTitle(s);
		cheats_print();
		menustate = MENU_CHEATS2;
		parentstate = menustate;
		break;

	case MENU_CHEATS2:
		menumask = 0;

		if (menu)
		{
			menustate = MENU_GENERIC_MAIN1;
			menusub = cheatsub;
			break;
		}

		cheats_scroll_name();

		if (c == KEY_HOME)
		{
			cheats_scan(SCANF_INIT);
			menustate = MENU_CHEATS1;
		}

		if (c == KEY_END)
		{
			cheats_scan(SCANF_END);
			menustate = MENU_CHEATS1;
		}

		if ((c == KEY_PAGEUP) || (c == KEY_LEFT))
		{
			cheats_scan(SCANF_PREV_PAGE);
			menustate = MENU_CHEATS1;
		}

		if ((c == KEY_PAGEDOWN) || (c == KEY_RIGHT))
		{
			cheats_scan(SCANF_NEXT_PAGE);
			menustate = MENU_CHEATS1;
		}

		if (down) // scroll down one entry
		{
			cheats_scan(SCANF_NEXT);
			menustate = MENU_CHEATS1;
		}

		if (up) // scroll up one entry
		{
			cheats_scan(SCANF_PREV);
			menustate = MENU_CHEATS1;
		}

		if (select)
		{
			cheats_toggle();
			menustate = MENU_CHEATS1;
		}
		break;

		/******************************************************************/
		/* last rom menu                                                    */
		/******************************************************************/
	case MENU_RECENT1:
		helptext_idx = 0;
		OsdSetTitle((fs_Options & SCANO_CORES) ? "Recent Cores" : "Recent Files");
		recent_print();
		menustate = MENU_RECENT2;
		parentstate = menustate;
		break;

	case MENU_RECENT2:
		menumask = 0;

		if (menu || recent)
		{
			menustate = fs_MenuCancel;
			if (is_menu()) menustate = MENU_FILE_SELECT1;
			break;
		}

		recent_scroll_name();

		if (c == KEY_HOME)
		{
			recent_scan(SCANF_INIT);
			menustate = MENU_RECENT1;
		}

		if (c == KEY_END)
		{
			recent_scan(SCANF_END);
			menustate = MENU_RECENT1;
		}

		if ((c == KEY_PAGEUP) || (c == KEY_LEFT))
		{
			recent_scan(SCANF_PREV_PAGE);
			menustate = MENU_RECENT1;
		}

		if ((c == KEY_PAGEDOWN) || (c == KEY_RIGHT))
		{
			recent_scan(SCANF_NEXT_PAGE);
			menustate = MENU_RECENT1;
		}

		if (c == KEY_BACKSPACE)
		{
			menusub_last = menusub;
			menusub = 0;
			menustate = MENU_RECENT3;
			break;
		}

		if (down) // scroll down one entry
		{
			recent_scan(SCANF_NEXT);
			menustate = MENU_RECENT1;
		}

		if (up) // scroll up one entry
		{
			recent_scan(SCANF_PREV);
			menustate = MENU_RECENT1;
		}

		if (select)
		{
			menustate = recent_select(SelectedDir, selPath, SelectedLabel) ? (enum MENU)fs_MenuSelect : MENU_RECENT1;
		}
		break;

	case MENU_RECENT3:
		menumask = 0x03;
		parentstate = menustate;
		m = 0;
		OsdWrite(m++);
		OsdWrite(m++);
		OsdWrite(m++);
		OsdWrite(m++);
		OsdWrite(m++);
		OsdWrite(m++);
		OsdWrite(m++, "        Clear the List?");
		OsdWrite(m++);
		OsdWrite(m++, "             No", menusub == 0);
		OsdWrite(m++, "             Yes", menusub == 1);
		while(m < OsdGetSize()) OsdWrite(m++);
		menustate = MENU_RECENT4;
		break;

	case MENU_RECENT4:
		if (select && menusub == 1)
		{
			for (int i = 0; i < OsdGetSize(); i++) OsdWrite(i, "", 0, 0);
			OsdWrite(OsdGetSize() / 2, "    Clearing the recents", 0, 0);
			OsdUpdate();
			sleep(1);
			recent_clear((fs_Options & SCANO_CORES) ? -1 : (fs_Options & SCANO_UMOUNT) ? ioctl_index + 500 : ioctl_index);
			menustate = fs_MenuCancel;
			menusub = menusub_last;
			if (is_menu()) menustate = MENU_FILE_SELECT1;

		}
		else if (select || menu || back)
		{
			menustate = fs_MenuCancel;
			menusub = menusub_last;
			if (is_menu()) menustate = MENU_FILE_SELECT1;
		}
		break;

		/******************************************************************/
		/* reset menu                                                     */
		/******************************************************************/
	case MENU_RESET1:
		m = 0;
		if (is_minimig()) m = 1;
		helptext_idx = 0;
		OsdSetTitle("Reset", 0);
		menumask = 0x03;	// Yes / No
		parentstate = menustate;

		OsdWrite(0, "", 0, 0);
		OsdWrite(1, m ? "       Reset Minimig?" : "       Reset settings?", 0, 0);
		OsdWrite(2, "", 0, 0);
		OsdWrite(3, "             yes", menusub == 0, 0);
		OsdWrite(4, "             no", menusub == 1, 0);
		OsdWrite(5, "", 0, 0);
		OsdWrite(6, "", 0, 0);
		for (int i = 7; i < OsdGetSize(); i++) OsdWrite(i, "", 0, 0);

		menustate = MENU_RESET2;
		break;

	case MENU_RESET2:
		m = 0;
		if (is_minimig()) m = 1;
		if (user_io_core_type() == CORE_TYPE_SHARPMZ)  m = 2;

		if (select && menusub == 0)
		{
			if (m)
			{
				menustate = MENU_NONE1;
				minimig_reset();
			}
            else if(m == 2)
            {
				menustate = MENU_COMMON1;
                sharpmz_reset_config(1);
            }
			else
			{
				char *filename = user_io_create_config_name();
				uint32_t status[2] = { user_io_8bit_set_status(0, 0xffffffff, 0), user_io_8bit_set_status(0, 0xffffffff, 1) };
				printf("Saving config to %s\n", filename);
				FileSaveConfig(filename, status, 8);
				menustate = MENU_GENERIC_MAIN1;
				for (int n = 0; n < 2; n++)
				{
					if (arcade_sw(n)->dip_num)
					{
						arcade_sw(n)->dip_cur = arcade_sw(n)->dip_def;
						arcade_sw_send(n);
						user_io_8bit_set_status(UIO_STATUS_RESET, UIO_STATUS_RESET);
						user_io_8bit_set_status(0, UIO_STATUS_RESET);
						arcade_sw_save(n);
					}
				}
				menustate = MENU_NONE1;
				menusub = 0;
			}
		}

		if (menu || (select && (menusub == 1))) // exit menu
		{
			menustate = MENU_COMMON1;
			menusub = 11;
		}
		break;

		/******************************************************************/
		/* minimig main menu                                              */
		/******************************************************************/
	case MENU_MINIMIG_MAIN1:
		menumask = 0x1EF0;
		OsdSetTitle("Minimig", OSD_ARROW_RIGHT | OSD_ARROW_LEFT);
		helptext_idx = HELPTEXT_MAIN;

		while (1)
		{
			if (!menusub) firstmenu = 0;
			adjvisible = 0;
			// floppy drive info
			// We display a line for each drive that's active
			// in the config file, but grey out any that the FPGA doesn't think are active.
			// We also print a help text in place of the last drive if it's inactive.
			for (int i = 0; i < 4; i++)
			{
				if (i == minimig_config.floppy.drives + 1) MenuWrite(i, " KP +/- to add/remove drives", 0, 1);
				else
				{
					strcpy(s, " dfx: ");
					s[3] = i + '0';
					if (i <= drives)
					{
						menumask |= (1 << i);	// Make enabled drives selectable

						if (df[i].status & DSK_INSERTED) // floppy disk is inserted
						{
							char *p;
							if ((p = strrchr(df[i].name, '/')))
							{
								p++;
							}
							else
							{
								p = df[i].name;
							}

							int len = strlen(p);
							if (len > 22) len = 21;
							strncpy(&s[6], p, len);
							s[6 + len] = ' ';
							s[6 + len + 1] = 0;
							s[6 + len + 2] = 0;
							if (!(df[i].status & DSK_WRITABLE)) s[6 + len + 1] = '\x17'; // padlock icon for write-protected disks
						}
						else // no floppy disk
						{
							strcat(s, "* no disk *");
						}
					}
					else if (i <= minimig_config.floppy.drives)
					{
						strcat(s, "* active after reset *");
					}
					else
						strcpy(s, "");
					MenuWrite(i, s, menusub == (uint32_t)i, (i > drives) || (i > minimig_config.floppy.drives));
				}
			}

			m = 4;
			strcpy(s,      " Joystick Swap:          ");
			strcat(s, (minimig_config.autofire & 0x8) ? " ON" : "OFF");
			MenuWrite(m++, s, menusub == 4, 0);
			MenuWrite(m++),

			MenuWrite(m++, " Drives                    \x16", menusub == 5, 0);
			MenuWrite(m++, " System                    \x16", menusub == 6, 0);
			MenuWrite(m++, " Audio & Video             \x16", menusub == 7, 0);
			if (spi_uio_cmd16(UIO_GET_OSDMASK, 0) & 1)
			{
				menumask |= 0x100;
				MenuWrite(m++, " MT32-pi                   \x16", menusub == 8);
			}

			MenuWrite(m++);
			MenuWrite(m++, " Save configuration        \x16", menusub == 9, 0);
			MenuWrite(m++, " Load configuration        \x16", menusub == 10, 0);

			while (m < 14) MenuWrite(m++);
			MenuWrite(m++, " Reset", menusub == 11, 0);
			MenuWrite(m, STD_EXIT, menusub == 12, 0);

			if (!adjvisible) break;
			firstmenu += adjvisible;
		}

		menustate = MENU_MINIMIG_MAIN2;
		parentstate = MENU_MINIMIG_MAIN1;
		break;

	case MENU_MINIMIG_MAIN2:
		if (menu) menustate = MENU_NONE1;
		else if (plus && (minimig_config.floppy.drives < 3) && menusub < 4)
		{
			minimig_config.floppy.drives++;
			minimig_ConfigFloppy(minimig_config.floppy.drives, minimig_config.floppy.speed);
			menustate = MENU_MINIMIG_MAIN1;
		}
		else if (minus && (minimig_config.floppy.drives > 0) && menusub < 4)
		{
			minimig_config.floppy.drives--;
			minimig_ConfigFloppy(minimig_config.floppy.drives, minimig_config.floppy.speed);
			menustate = MENU_MINIMIG_MAIN1;
		}
		else if (select || recent || minus || plus)
		{
			if (menusub < 4)
			{
				ioctl_index = 0;
				if (df[menusub].status & DSK_INSERTED) // eject selected floppy
				{
					df[menusub].status = 0;
					FileClose(&df[menusub].file);
					menustate = MENU_MINIMIG_MAIN1;
				}
				else
				{
					df[menusub].status = 0;
					fs_Options = SCANO_DIR;
					fs_MenuSelect = MENU_MINIMIG_ADFFILE_SELECTED;
					fs_MenuCancel = MENU_MINIMIG_MAIN1;
					strcpy(fs_pFileExt, "ADF");
					if (select) SelectFile(Selected_F[menusub], "ADF", fs_Options, fs_MenuSelect, fs_MenuCancel);
					else if (recent_init(0)) menustate = MENU_RECENT1;
				}
			}
			else if (menusub == 4)
			{
				minimig_config.autofire ^= 0x8;
				menustate = MENU_MINIMIG_CHIPSET1;
				minimig_ConfigAutofire(minimig_config.autofire, 0x8);
				menustate = MENU_MINIMIG_MAIN1;
			}
			else if (select)
			{
				if (menusub == 5)
				{
					menustate = MENU_MINIMIG_DISK1;
					menusub = 0;
				}
				else if (menusub == 6)
				{
					menustate = MENU_MINIMIG_CHIPSET1;
					menusub = 0;
				}
				else if (menusub == 7)
				{
					menustate = MENU_MINIMIG_VIDEO1;
					menusub = 0;
				}
				else if (menusub == 8)
				{
					menusub = 0;
					menustate = MENU_MT32PI_MAIN1;
				}
				else if (menusub == 9)
				{
					menusub = 0;
					menustate = MENU_MINIMIG_SAVECONFIG1;
				}
				else if (menusub == 10)
				{
					menusub = 0;
					menustate = MENU_MINIMIG_LOADCONFIG1;
				}
				else if (menusub == 11)
				{
					menustate = MENU_NONE1;
					minimig_reset();
				}
				else if (menusub == 12)
				{
					menustate = MENU_NONE1;
				}
			}
		}
		else if (c == KEY_BACKSPACE) // eject all floppies
		{
			for (int i = 0; i <= drives; i++) df[i].status = 0;
			menustate = MENU_MINIMIG_MAIN1;
		}
		else if (right)
		{
			menustate = MENU_COMMON1;
			menusub = 0;
		}
		else if (left)
		{
			menustate = MENU_MISC1;
			menusub = 3;
		}
		break;

	case MENU_MINIMIG_ADFFILE_SELECTED:
		memcpy(Selected_F[menusub], selPath, sizeof(Selected_F[menusub]));
		recent_update(SelectedDir, selPath, SelectedLabel, 0);
		InsertFloppy(&df[menusub], selPath);
		if (menusub < drives) menusub++;
		menustate = MENU_MINIMIG_MAIN1;
		break;

	case MENU_MINIMIG_LOADCONFIG1:
		helptext_idx = 0;
		if (parentstate != menustate) menumask = 0x400;

		parentstate = menustate;
		OsdSetTitle("Load config", 0);

		m = 0;
		OsdWrite(m++, "", 0, 0);
		OsdWrite(m++, " Startup config:");
		for (uint i = 0; i < 10; i++)
		{
			const char *info = minimig_get_cfg_info(i, menusub != i);
			static char name[128];

			if (info)
			{
				menumask |= 1 << i;
				sprintf(name, "  %s", strlen(info) ? info : "NO INFO");
				char *p = strchr(name, '\n');
				if (p) *p = 0;
				OsdWrite(m++, name, menusub == i);

				if (menusub == i && p)
				{
					sprintf(name, "  %s", strchr(info, '\n') + 1);
					OsdWrite(m++, name, 1, !(menumask & (1 << i)));
				}
			}

			if (!i)
			{
				OsdWrite(m++, "", 0, 0);
				m = 4;
				OsdWrite(m++, " Other configs:");
			}
		}

		while (m < OsdGetSize() - 1) OsdWrite(m++);
		OsdWrite(OsdGetSize() - 1, STD_BACK, menusub == 10, 0);

		menustate = MENU_MINIMIG_LOADCONFIG2;
		break;

	case MENU_MINIMIG_LOADCONFIG2:
		if (down)
		{
			if (menusub < 9) menusub++;
			menustate = MENU_MINIMIG_LOADCONFIG1;
		}
		else if (select)
		{
			if (menusub < 10)
			{
				OsdDisable();
				minimig_cfg_load(menusub);
				menustate = MENU_NONE1;
			}
			else
			{
				menustate = MENU_MINIMIG_MAIN1;
				menusub = 10;
			}
		}
		if (menu || left)
		{
			menustate = MENU_MINIMIG_MAIN1;
			menusub = 10;
		}
		break;

	case MENU_MINIMIG_SAVECONFIG1:
		helptext_idx = 0;
		menumask = 0x7ff;
		parentstate = menustate;
		OsdSetTitle("Save config", 0);

		m = 0;
		OsdWrite(m++, "", 0, 0);
		OsdWrite(m++, " Startup config:");
		for (uint i = 0; i < 10; i++)
		{
			const char *info = minimig_get_cfg_info(i, menusub != i);
			static char name[128];

			if (info)
			{
				sprintf(name, "  %s", strlen(info) ? info : "NO INFO");
				char *p = strchr(name, '\n');
				if (p) *p = 0;
				OsdWrite(m++, name, menusub == i);
				if (menusub == i && p)
				{
					sprintf(name, "  %s", strchr(info, '\n') + 1);
					OsdWrite(m++, name, 1);
				}
			}
			else
			{
				OsdWrite(m++, "  < EMPTY >", menusub == i);
			}

			if (!i)
			{
				OsdWrite(m++, "", 0, 0);
				m = 4;
				OsdWrite(m++, " Other configs:");
			}
		}

		while (m < OsdGetSize() - 1) OsdWrite(m++);
		OsdWrite(OsdGetSize() - 1, STD_BACK, menusub == 10, 0);

		menustate = MENU_MINIMIG_SAVECONFIG2;
		break;

	case MENU_MINIMIG_SAVECONFIG2:
		if (select)
		{
			int fastcfg = ((minimig_config.memory >> 4) & 0x03) | ((minimig_config.memory & 0x80) >> 5);
			sprintf(minimig_config.info, "%s/%s/%s%s %s%s%s%s%s%s\n",
				config_cpu_msg[minimig_config.cpu & 0x03] + 2,
				config_chipset_msg[(minimig_config.chipset >> 2) & 7],
				minimig_config.chipset & CONFIG_NTSC ? "N" : "P",
				((minimig_config.ide_cfg & 1) && (minimig_config.hardfile[0].cfg ||
					minimig_config.hardfile[1].cfg ||
					minimig_config.hardfile[2].cfg ||
					minimig_config.hardfile[3].cfg)) ? "/HD" : "",
				config_memory_chip_msg[minimig_config.memory & 0x03],
				fastcfg ? "+" : "",
				fastcfg ? config_memory_fast_msg[(minimig_config.cpu>>1) & 1][fastcfg] : "",
				((minimig_config.memory >> 2) & 0x03) ? "+" : "",
				((minimig_config.memory >> 2) & 0x03) ? config_memory_slow_msg[(minimig_config.memory >> 2) & 0x03] : "",
				(minimig_config.memory & 0x40) ? " HRT" : ""
			);

			char *p = strrchr(minimig_config.kickstart, '/');
			if (!p) p = minimig_config.kickstart;
			else p++;

			strncat(minimig_config.info, p, sizeof(minimig_config.info) - strlen(minimig_config.info) - 1);
			minimig_config.info[sizeof(minimig_config.info) - 1] = 0;

			if (menusub<10) minimig_cfg_save(menusub);
			menustate = MENU_MINIMIG_MAIN1;
			menusub = 9;
		}
		else
		if (menu || left) // exit menu
		{
			menustate = MENU_MINIMIG_MAIN1;
			menusub = 9;
		}
		break;

	case MENU_MINIMIG_CHIPSET1:
		helptext_idx = HELPTEXT_CHIPSET;
		menumask = 0x3FF;
		OsdSetTitle("System");
		parentstate = menustate;

		m = 0;
		OsdWrite(m++, "", 0, 0);
		strcpy(s, " CPU      : ");
		strcat(s, config_cpu_msg[minimig_config.cpu & 0x03]);
		OsdWrite(m++, s, menusub == 0, 0);
		strcpy(s, " D-Cache  : ");
		strcat(s, (minimig_config.cpu & 16) ? "ON" : "OFF");
		OsdWrite(m++, s, menusub == 1, !(minimig_config.cpu & 0x2));
		OsdWrite(m++, "", 0, 0);
		strcpy(s, " Chipset  : ");
		strcat(s, config_chipset_msg[(minimig_config.chipset >> 2) & 7]);
		OsdWrite(m++, s, menusub == 2, 0);
		strcpy(s, " ChipRAM  : ");
		strcat(s, config_memory_chip_msg[minimig_config.memory & 0x03]);
		OsdWrite(m++, s, menusub == 3, 0);
		strcpy(s, " FastRAM  : ");
		strcat(s, config_memory_fast_msg[(minimig_config.cpu >> 1) & 1][((minimig_config.memory >> 4) & 0x03) | ((minimig_config.memory & 0x80) >> 5)]);
		OsdWrite(m++, s, menusub == 4, 0);
		strcpy(s, " SlowRAM  : ");
		strcat(s, config_memory_slow_msg[(minimig_config.memory >> 2) & 0x03]);
		OsdWrite(m++, s, menusub == 5, 0);

		OsdWrite(m++, "", 0, 0);
		strcpy(s, " CD32 Pad : ");
		strcat(s, config_cd32pad_msg[(minimig_config.autofire >> 2) & 1]);
		OsdWrite(m++, s, menusub == 6, 0);

		OsdWrite(m++, "", 0, 0);
		strcpy(s, " ROM    : ");
		{
			char *path = user_io_get_core_path();
			int len = strlen(path);
			char *name = minimig_config.kickstart;
			if (!strncasecmp(name, path, len))  name += len + 1;
			strncat(&s[3], name, 24);
		}

		OsdWrite(m++, s, menusub == 7, 0);
		strcpy(s, " HRTmon : ");
		strcat(s, (minimig_config.memory & 0x40) ? "enabled " : "disabled");
		OsdWrite(m++, s, menusub == 8, 0);

		for (int i = m; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, STD_BACK, menusub == 9, 0);

		menustate = MENU_MINIMIG_CHIPSET2;
		break;

	case MENU_MINIMIG_CHIPSET2:
		saved_menustate = MENU_MINIMIG_CHIPSET1;

		if (select || minus || plus)
		{
			if (menusub == 0)
			{
				menustate = MENU_MINIMIG_CHIPSET1;
				minimig_config.cpu = (minimig_config.cpu & 0xfc) | ((minimig_config.cpu & 1) ? 0 : 3);
				minimig_ConfigCPU(minimig_config.cpu);
			}
			/*
			else if (menusub == 1 && (minimig_config.cpu & 0x2))
			{
				menustate = MENU_MINIMIG_CHIPSET1;
				minimig_config.cpu ^= 4;
				minimig_ConfigCPU(minimig_config.cpu);
			}
			else if (menusub == 2 && (minimig_config.cpu & 0x2))
			{
				menustate = MENU_MINIMIG_CHIPSET1;
				minimig_config.cpu ^= 8;
				minimig_ConfigCPU(minimig_config.cpu);
			}
			*/
			else if (menusub == 1 && (minimig_config.cpu & 0x2))
			{
				menustate = MENU_MINIMIG_CHIPSET1;
				minimig_config.cpu ^= 16;
				minimig_ConfigCPU(minimig_config.cpu);
			}
			else if (menusub == 2)
			{
				if (minus)
				{
					switch (minimig_config.chipset & 0x1c)
					{
					case (CONFIG_AGA | CONFIG_ECS):
						minimig_config.chipset = (minimig_config.chipset & 3) | CONFIG_ECS;
						break;
					case CONFIG_ECS:
						minimig_config.chipset = (minimig_config.chipset & 3) | CONFIG_A1000;
						break;
					case CONFIG_A1000:
						minimig_config.chipset = (minimig_config.chipset & 3) | 0;
						break;
					case 0:
						minimig_config.chipset = (minimig_config.chipset & 3) | CONFIG_AGA | CONFIG_ECS;
						break;
					}
				}
				else
				{
					switch (minimig_config.chipset & 0x1c)
					{
					case 0:
						minimig_config.chipset = (minimig_config.chipset & 3) | CONFIG_A1000;
						break;
					case CONFIG_A1000:
						minimig_config.chipset = (minimig_config.chipset & 3) | CONFIG_ECS;
						break;
					case CONFIG_ECS:
						minimig_config.chipset = (minimig_config.chipset & 3) | CONFIG_AGA | CONFIG_ECS;
						break;
					case (CONFIG_AGA | CONFIG_ECS):
						minimig_config.chipset = (minimig_config.chipset & 3) | 0;
						break;
					}
				}

				menustate = MENU_MINIMIG_CHIPSET1;
				minimig_ConfigChipset(minimig_config.chipset);
			}
			else if (menusub == 3)
			{
				minimig_config.memory = ((minimig_config.memory + (minus ? -1 : 1)) & 0x03) | (minimig_config.memory & ~0x03);
				menustate = MENU_MINIMIG_CHIPSET1;
			}
			else if (menusub == 4)
			{
				int c = (((minimig_config.memory >> 4) & 0x03) | ((minimig_config.memory & 0x80) >> 5));
				if (minus)
				{
					c--;
					if (c < 0) c = 5;
					if (!(minimig_config.cpu & 2) && c > 3) c = 3;
				}
				else
				{
					c++;
					if (c > 5) c = 0;
					if (!(minimig_config.cpu & 2) && c > 3) c = 0;
				}
				minimig_config.memory = ((c << 4) & 0x30) | ((c << 5) & 0x80) | (minimig_config.memory & ~0xB0);
				menustate = MENU_MINIMIG_CHIPSET1;
			}
			else if (menusub == 5)
			{
				minimig_config.memory = ((minimig_config.memory + (minus ? -4 : 4)) & 0x0C) | (minimig_config.memory & ~0x0C);
				menustate = MENU_MINIMIG_CHIPSET1;
			}
			else if (menusub == 6)
			{
				minimig_config.autofire ^= 0x4;
				menustate = MENU_MINIMIG_CHIPSET1;
				minimig_ConfigAutofire(minimig_config.autofire, 0x4);
			}
			else if (menusub == 7 && select)
			{
				ioctl_index = 1;
				SelectFile(Selected_F[4], "ROM", 0, MENU_MINIMIG_ROMFILE_SELECTED, MENU_MINIMIG_CHIPSET1);
			}
			else if (menusub == 8)
			{
				minimig_config.memory ^= 0x40;
				menustate = MENU_MINIMIG_CHIPSET1;
			}
			else if (menusub == 9)
			{
				menustate = MENU_MINIMIG_MAIN1;
				menusub = 6;
			}
		}

		if (menu)
		{
			menustate = MENU_NONE1;
		}
		else if (back || left)
		{
			menustate = MENU_MINIMIG_MAIN1;
			menusub = 6;
		}
		break;

	case MENU_MINIMIG_ROMFILE_SELECTED:
		memcpy(Selected_F[4], selPath, sizeof(Selected_F[4]));
		minimig_set_kickstart(selPath);
		menustate = MENU_MINIMIG_CHIPSET1;
		break;

	case MENU_MINIMIG_DISK1:
		helptext_idx = HELPTEXT_HARDFILE;
		OsdSetTitle("Drives");

		m = 0;
		parentstate = menustate;
		menumask = 0xC01;
		if (minimig_config.ide_cfg & 1) menumask |= 0x156;
		OsdWrite(m++, "", 0, 0);
		strcpy(s, " IDE A600/A1200    : ");
		strcat(s, (minimig_config.ide_cfg & 1) ? "On " : "Off");
		OsdWrite(m++, s, menusub == 0, 0);
		strcpy(s, " Fast-IDE (68020)  : ");
		strcat(s, (minimig_config.ide_cfg & 0x20) ? "Off" : "On");
		OsdWrite(m++, s, menusub == 1,  !(minimig_config.ide_cfg & 1) || !(minimig_config.cpu & 2));
		if (!(minimig_config.cpu & 2)) menumask &= ~2;
		OsdWrite(m++);

		{
			uint n = 2, t = 8;
			for (uint i = 0; i < 4; i++)
			{
				strcpy(s, (i & 2) ? " Sec. " : " Pri. ");
				strcat(s, (i & 1) ? " Slave: " : "Master: ");
				strcat(s, (minimig_config.hardfile[i].cfg == 2) ? "Removable/CD" : minimig_config.hardfile[i].cfg ? "Fixed/HDD" : "Disabled");
				OsdWrite(m++, s, (minimig_config.ide_cfg & 1) ? (menusub == n++) : 0, !(minimig_config.ide_cfg & 1));
				if (minimig_config.hardfile[i].filename[0])
				{
					strcpy(s, "                                ");
					char *path = user_io_get_core_path();
					int len = strlen(path);
					char *name = minimig_config.hardfile[i].filename;
					if (!strncasecmp(name, path, len))  name += len + 1;
					strncpy(&s[3], name, 25);
				}
				else
				{
					strcpy(s, "   ** not selected **");
				}
				enable = (minimig_config.ide_cfg & 1) && minimig_config.hardfile[i].cfg;
				if (enable) menumask |= t;	// Make hardfile selectable
				OsdWrite(m++, s, menusub == n++, enable == 0);
				t <<= 2;
				if(i == 1) OsdWrite(m++);
			}
		}

		OsdWrite(m++);
		sprintf(s, " Floppy Disk Turbo : %s", minimig_config.floppy.speed ? "On" : "Off");
		OsdWrite(m++, s, menusub == 10, 0);
		OsdWrite(m++);

		OsdWrite(OsdGetSize() - 1, STD_BACK, menusub == 11, 0);
		menustate = MENU_MINIMIG_DISK2;
		break;

	case MENU_MINIMIG_DISK2:
		saved_menustate = MENU_MINIMIG_DISK1;

		if (select || recent || minus || plus)
		{
			if (menusub == 0)
			{
				if (select)
				{
					minimig_config.ide_cfg ^= 1;
					menustate = MENU_MINIMIG_DISK1;
				}
			}
			else if (menusub == 1)
			{
				if (select)
				{
					minimig_config.ide_cfg ^= 0x20;
					menustate = MENU_MINIMIG_DISK1;
				}
			}
			else if (menusub < 10)
			{
				if (!(menusub & 1))
				{
					if (select || minus || plus)
					{
						int idx = (menusub - 2) / 2;
						if (minus)
						{
							if (!minimig_config.hardfile[idx].cfg) minimig_config.hardfile[idx].cfg = 2;
							else minimig_config.hardfile[idx].cfg--;
						}
						else
						{
							minimig_config.hardfile[idx].cfg++;
							if (minimig_config.hardfile[idx].cfg > 2) minimig_config.hardfile[idx].cfg = 0;
						}
						menustate = MENU_MINIMIG_DISK1;
					}
				}
				else if(select || recent)
				{
					fs_Options = SCANO_DIR | SCANO_UMOUNT;
					fs_MenuSelect = MENU_MINIMIG_HDFFILE_SELECTED;
					fs_MenuCancel = MENU_MINIMIG_DISK1;
					int idx = (menusub - 3) / 2;
					strcpy(fs_pFileExt, (minimig_config.hardfile[idx].cfg == 2) ? "ISOCUECHDIMG" : "HDFVHDIMGDSK");
					if (select)
					{
						if (!Selected_S[idx][0]) memcpy(Selected_S[idx], minimig_config.hardfile[idx].filename, sizeof(Selected_S[idx]));
						SelectFile(Selected_S[idx], fs_pFileExt, fs_Options, fs_MenuSelect, fs_MenuCancel);
					}
					else if (recent_init(500)) menustate = MENU_RECENT1;
				}
			}
			else if (menusub == 10 && select) // return to previous menu
			{
				minimig_config.floppy.speed ^= 1;
				minimig_ConfigFloppy(minimig_config.floppy.drives, minimig_config.floppy.speed);
				menustate = MENU_MINIMIG_DISK1;
			}
			else if (menusub == 11 && select) // return to previous menu
			{
				menustate = MENU_MINIMIG_MAIN1;
				menusub = 5;
			}
		}

		if (menu)
		{
			menustate = MENU_NONE1;
		}
		else if (back || left)
		{
			menustate = MENU_MINIMIG_MAIN1;
			menusub = 5;
		}
		break;

	case MENU_MINIMIG_HDFFILE_SELECTED:
		{
			memcpy(Selected_S[(menusub - 2) / 2], selPath, sizeof(Selected_S[(menusub - 2) / 2]));
			recent_update(SelectedDir, selPath, SelectedLabel, 500);
			int num = (menusub - 2) / 2;
			uint len = strlen(selPath);
			if (len > sizeof(minimig_config.hardfile[num].filename) - 1) len = sizeof(minimig_config.hardfile[num].filename) - 1;
			if(len) memcpy(minimig_config.hardfile[num].filename, selPath, len);
			minimig_config.hardfile[num].filename[len] = 0;

			if (ide_is_placeholder(num))
			{
				if (ide_check() & 0x8000) ide_open(num, minimig_config.hardfile[num].filename);
				else OpenHardfile(num, minimig_config.hardfile[num].filename);
			}

			menustate = MENU_MINIMIG_DISK1;
		}
		break;

	case MENU_MINIMIG_VIDEO1:
		menumask = 0x1fff;
		parentstate = menustate;
		helptext_idx = 0; // helptexts[HELPTEXT_VIDEO];

		m = 0;
		OsdSetTitle("Audio & Video");

		strcpy(s, " TV Standard   : ");
		strcat(s, minimig_config.chipset & CONFIG_NTSC ? "NTSC" : "PAL");
		OsdWrite(m++, s, menusub == 0, 0);
		OsdWrite(m++, "", 0, 0);
		strcpy(s, " Scandoubler FX: ");
		strcat(s, config_scanlines_msg[minimig_config.scanlines & 7]);
		OsdWrite(m++, s, menusub == 1, 0);
		strcpy(s, " Video area by : ");
		strcat(s, config_blank_msg[(minimig_config.scanlines >> 6) & 3]);
		OsdWrite(m++, s, menusub == 2, 0);
		strcpy(s, " Aspect Ratio  : ");
		minimig_config.scanlines = (get_ar_name((minimig_config.scanlines >> 4) & 3, s) << 4) | (minimig_config.scanlines & ~0x30);
		OsdWrite(m++, s, menusub == 3, 0);
		strcpy(s, " Pixel Clock   : ");
		strcat(s, (minimig_get_extcfg() & 0x400) ? "Adaptive" : "28MHz");
		OsdWrite(m++, s, menusub == 4, 0);
		strcpy(s, " Scaling       : ");
		strcat(s,config_scale[(minimig_get_extcfg() >> 11) & 7]);
		OsdWrite(m++, s, menusub == 5, 0);
		strcpy(s, " RTG Upscaling : ");
		strcat(s, (minimig_get_extcfg() & 0x4000) ? "HV-Integer" : "Normal");
		OsdWrite(m++, s, menusub == 6, 0);

		OsdWrite(m++);
		strcpy(s, " Stereo mix    : ");
		strcat(s, config_stereo_msg[minimig_config.audio & 3]);
		OsdWrite(m++, s, menusub == 7, 0);
		strcpy(s, " Audio Filter  : ");
		strcat(s, (~minimig_get_extcfg() & 0x10000) ? "Auto(LED)" : (minimig_get_extcfg() & 0x8000) ? "On" : "Off");
		OsdWrite(m++, s, menusub == 8, 0);
		strcpy(s, " Model         : ");
		strcat(s, (minimig_get_extcfg() & 0x20000) ? "A1200" : "A500");
		OsdWrite(m++, s, menusub == 9, 0);
		strcpy(s, " Paula Output  : ");
		strcat(s, (minimig_get_extcfg() & 0x40000) ? "PWM" : "Normal");
		OsdWrite(m++, s, menusub == 10, 0);

		OsdWrite(m++);
		OsdWrite(m++, minimig_get_adjust() ? " Finish screen adjustment" : " Adjust screen position", menusub == 11, 0);
		for (; m < OsdGetSize() - 1; m++) OsdWrite(m);
		OsdWrite(OsdGetSize() - 1, STD_BACK, menusub == 12, 0);

		menustate = MENU_MINIMIG_VIDEO2;
		break;

	case MENU_MINIMIG_VIDEO2:
		saved_menustate = MENU_MINIMIG_VIDEO1;
		if (select || minus || plus)
		{
			menustate = MENU_MINIMIG_VIDEO1;
			switch(menusub)
			{
			case 0:
				minimig_config.chipset ^= CONFIG_NTSC;
				minimig_ConfigChipset(minimig_config.chipset);
				break;

			case 1:
				{
					int scanlines = minimig_config.scanlines & 7;
					if (minus)
					{
						scanlines--;
						if (scanlines < 0) scanlines = 4;
					}
					else
					{
						scanlines++;
						if (scanlines > 4) scanlines = 0;
					}
					minimig_config.scanlines = scanlines | (minimig_config.scanlines & 0xf8);
					minimig_ConfigVideo(minimig_config.scanlines);
				}
				break;

			case 2:
				minimig_config.scanlines &= ~0x80;
				minimig_config.scanlines ^= 0x40;
				minimig_ConfigVideo(minimig_config.scanlines);
				break;

			case 3:
				minimig_config.scanlines = (next_ar((minimig_config.scanlines >> 4) & 3, minus) << 4) | (minimig_config.scanlines & ~0x30);
				minimig_ConfigVideo(minimig_config.scanlines);
				break;

			case 4:
				minimig_set_extcfg(minimig_get_extcfg() ^ 0x400);
				break;

			case 5:
				{
					int mode = (minimig_get_extcfg() >> 11) & 7;
					if (minus) mode = (mode <= 0) ? 4 : (mode - 1);
					else mode = (mode >= 4) ? 0 : (mode + 1);
					minimig_set_extcfg((minimig_get_extcfg() & ~0x3800) | (mode << 11));
				}
				break;

			case 6:
				minimig_set_extcfg(minimig_get_extcfg() ^ 0x4000);
				break;

			case 7:
				minimig_config.audio = (minimig_config.audio + (minus ? -1 : 1)) & 3;
				minimig_ConfigAudio(minimig_config.audio);
				break;

			case 8:
				{
					int mode = (minimig_get_extcfg() >> 15) & 3;
					if (minus) mode = (mode == 2) ? 0 : (mode - 1);
					else mode = (mode == 0) ? 2 : (mode + 1);
					minimig_set_extcfg((minimig_get_extcfg() & ~0x18000) | ((mode & 3) << 15));
				}
				break;

			case 9:
				minimig_set_extcfg(minimig_get_extcfg() ^ 0x20000);
				break;

			case 10:
				minimig_set_extcfg(minimig_get_extcfg() ^ 0x40000);
				break;

			case 11:
				if (select)
				{
					menustate = MENU_NONE1;
					minimig_set_adjust(minimig_get_adjust() ? 0 : 1);
				}
				break;

			case 12:
				if (select)
				{
					menustate = MENU_MINIMIG_MAIN1;
					menusub = 7;
				}
				break;
			}
		}
		else if (menu)
		{
			menustate = MENU_NONE1;
		}
		else if (back || left)
		{
			menustate = MENU_MINIMIG_MAIN1;
			menusub = 7;
		}
		break;

		/******************************************************************/
		/* system menu */
		/******************************************************************/
	case MENU_SYSTEM1:
		if (video_fb_state())
		{
			menustate = MENU_NONE1;
			break;
		}

		OsdSetSize(16);
		helptext_idx = 0;
		parentstate = menustate;

		m = 0;
		OsdSetTitle("System Settings", OSD_ARROW_LEFT);
		menumask = 0x3F;

		OsdWrite(m++);
		sprintf(s, "       MiSTer v%s", version + 5);
		{
			char str[8] = {};
			FILE *f = fopen("/MiSTer.version", "r");
			if (f)
			{
				if (fread(str, 6, 1, f)) sprintf(s, " MiSTer v%s,  OS v%s", version + 5, str);
				fclose(f);
			}
		}

		OsdWrite(m++, s);

		{
			uint64_t avail = 0;
			struct statvfs buf;
			memset(&buf, 0, sizeof(buf));
			if (!statvfs(getRootDir(), &buf)) avail = buf.f_bsize * buf.f_bavail;
			if(avail < (10ull*1024*1024*1024)) sprintf(s, "   Available space: %llumb", avail / (1024 * 1024));
			else sprintf(s, "   Available space: %llugb", avail / (1024 * 1024 * 1024));
			OsdWrite(m+2, s, 0, 0);
		}

		OsdWrite(m++, "");
		if (getStorage(0))
		{
			OsdWrite(m++, "        Storage: USB");
			m++;
			OsdWrite(m++, "      Switch to SD card", menusub == 0);
		}
		else
		{
			if (getStorage(1))
			{
				OsdWrite(m++, " No USB found, using SD card");
				m++;
				OsdWrite(m++, "      Switch to SD card", menusub == 0);
			}
			else
			{
				OsdWrite(m++, "      Storage: SD card");
				m++;
				OsdWrite(m++, "        Switch to USB", menusub == 0, !isUSBMounted());
			}
		}
		OsdWrite(m++, "");
		OsdWrite(m++, " Remap keyboard            \x16", menusub == 1);
		OsdWrite(m++, " Define joystick buttons   \x16", menusub == 2);
		OsdWrite(m++, " Scripts                   \x16", menusub == 3);
		OsdWrite(m++, "");
		cr = m;
		OsdWrite(m++, " Reboot (hold \x16 cold reboot)", menusub == 4);
		sysinfo_timer = 0;

		reboot_req = 0;

		while(m < OsdGetSize()-1) OsdWrite(m++, "");
		OsdWrite(15, STD_EXIT, menusub == 5);
		menustate = MENU_SYSTEM2;
		break;

	case MENU_SYSTEM2:
		if (menu)
		{
			SelectFile("", 0, SCANO_CORES, MENU_CORE_FILE_SELECTED1, MENU_SYSTEM1);
			break;
		}
		else if (select)
		{
			switch (menusub)
			{
			case 0:
				if (getStorage(1) || isUSBMounted()) setStorage(!getStorage(1));
				break;
			case 1:
				start_map_setting(0);
				menustate = MENU_KBDMAP;
				menusub = 0;
				break;
			case 2:
				menustate = MENU_JOYSYSMAP;
				break;
			case 3:
				{
					uint8_t confirm[32] = {};
					int match = 0;
					int fd = open("/sys/block/mmcblk0/device/cid", O_RDONLY);
					if (fd >= 0)
					{
						int ret = read(fd, card_cid, 32);
						close(fd);
						if (ret == 32)
						{
							if (FileLoadConfig("script_confirm", confirm, 32))
							{
								match = !memcmp(card_cid, confirm, 32);
							}
						}
					}

					if (match) SelectFile(Selected_F[0], "SH", SCANO_DIR, MENU_SCRIPTS_FB, MENU_SYSTEM1);
					else
					{
						menustate = MENU_SCRIPTS_PRE;
						menusub = 0;
					}
				}
				break;

			case 4:
				{
					reboot_req = 1;

					int off = hold_cnt / 3;
					if (off > 5) reboot(1);

					sprintf(s, " Cold Reboot");
					p = s + 5 - off;
					MenuWrite(cr, p, 1, 0);
				}
				break;

			case 5:
				menustate = MENU_NONE1;
				break;
			}
		}
		else if (left)
		{
			menustate = MENU_MISC1;
		}

		if (!hold_cnt && reboot_req) fpga_load_rbf("menu.rbf");
		break;

	case MENU_JOYSYSMAP:
		strcpy(joy_bnames[SYS_BTN_A - DPAD_NAMES], "A");
		strcpy(joy_bnames[SYS_BTN_B - DPAD_NAMES], "B");
		strcpy(joy_bnames[SYS_BTN_X - DPAD_NAMES], "X");
		strcpy(joy_bnames[SYS_BTN_Y - DPAD_NAMES], "Y");
		strcpy(joy_bnames[SYS_BTN_L - DPAD_NAMES], "L");
		strcpy(joy_bnames[SYS_BTN_R - DPAD_NAMES], "R");
		strcpy(joy_bnames[SYS_BTN_SELECT - DPAD_NAMES], "Select");
		strcpy(joy_bnames[SYS_BTN_START - DPAD_NAMES], "Start");
		strcpy(joy_bnames[SYS_MS_RIGHT - DPAD_NAMES], "Mouse Move RIGHT");
		strcpy(joy_bnames[SYS_MS_LEFT - DPAD_NAMES], "Mouse Move LEFT");
		strcpy(joy_bnames[SYS_MS_DOWN - DPAD_NAMES], "Mouse Move DOWN");
		strcpy(joy_bnames[SYS_MS_UP - DPAD_NAMES], "Mouse Move UP");
		strcpy(joy_bnames[SYS_MS_BTN_L - DPAD_NAMES], "Mouse Btn Left");
		strcpy(joy_bnames[SYS_MS_BTN_R - DPAD_NAMES], "Mouse Btn Right");
		strcpy(joy_bnames[SYS_MS_BTN_M - DPAD_NAMES], "Mouse Btn Middle");
		strcpy(joy_bnames[SYS_MS_BTN_EMU - DPAD_NAMES], "Mouse Emu/Sniper");
		strcpy(joy_bnames[SYS_BTN_OSD_KTGL - DPAD_NAMES], "Menu");
		strcpy(joy_bnames[SYS_BTN_CNT_OK - DPAD_NAMES], "Menu: OK");
		strcpy(joy_bnames[SYS_BTN_CNT_ESC - DPAD_NAMES], "Menu: Back");
		joy_bcount = 20 + 1; //buttons + OSD/KTGL button
		start_map_setting(joy_bcount + 6); // + dpad + Analog X/Y
		menustate = MENU_JOYDIGMAP;
		menusub = 0;
		break;

	case MENU_LGCAL:
		helptext_idx = 0;
		OsdSetTitle("Lightgun Calibration", 0);
		for (int i = 0; i < OsdGetSize(); i++) OsdWrite(i);
		OsdWrite(9,  "     Point to the edge of");
		OsdWrite(10, "   screen and press trigger");
		OsdWrite(11, "         to confirm");
		OsdWrite(OsdGetSize() - 1, "           Cancel", menusub == 0, 0);
		gun_ok = 0;
		gun_side = 0;
		gun_y = 0;
		gun_x = 0;
		memset(gun_pos, 0, sizeof(gun_pos));
		menustate = MENU_LGCAL1;
		menusub = 0;
		break;

	case MENU_LGCAL1:
		{
			static int state = 0;
			static uint32_t blink = 0;
			if (!blink || CheckTimer(blink))
			{
				blink = GetTimer(300);
				state = !state;
			}

			m = !state;
		}

		if (gun_side < 4) gun_pos[gun_side] = (gun_side < 2) ? gun_y : gun_x;
		sprintf(s, "           %c%04d%c", (gun_side == 0 && m) ? 17 : 32, (gun_side == 0) ? gun_y : gun_pos[0], (gun_side == 0 && m) ? 16 : 32);
		OsdWrite(0, s);
		sprintf(s, "%c%04d%c                 %c%04d%c", (gun_side == 2 && m) ? 17 : 32, (gun_side == 2) ? gun_x : gun_pos[2], (gun_side == 2 && m) ? 16 : 32,
		                                                (gun_side == 3 && m) ? 17 : 32, (gun_side == 3) ? gun_x : gun_pos[3], (gun_side == 3 && m) ? 16 : 32);
		OsdWrite(7, s);
		sprintf(s, "           %c%04d%c", (gun_side == 1 && m) ? 17 : 32, (gun_side == 1) ? gun_y : gun_pos[1], (gun_side == 1 && m) ? 16 : 32);
		OsdWrite(13, s);
		if (menu || select) menustate = MENU_NONE1;

		if (gun_ok == 1)
		{
			gun_ok = 0;
			gun_side++;
		}

		if (gun_ok == 2)
		{
			gun_ok = 0;
			if (gun_side == 4)
			{
				input_lightgun_save(gun_idx, gun_pos);
				menustate = MENU_NONE1;
			}
		}
		break;

	case MENU_SCRIPTS_PRE:
		OsdSetTitle("Warning!!!", 0);
		helptext_idx = 0;
		menumask = 7;
		m = 0;
		OsdWrite(m++);
		OsdWrite(m++, "         Attention:");
		OsdWrite(m++, " This is dangerous operation!");
		OsdWrite(m++);
		OsdWrite(m++, " Script has control over the");
		OsdWrite(m++, " whole system and may damage");
		OsdWrite(m++, " the files or settings, then");
		OsdWrite(m++, " MiSTer won't boot, so you");
		OsdWrite(m++, " will have to re-format the");
		OsdWrite(m++, " SD card and fill with files");
		OsdWrite(m++, " in order to use it again.");
		OsdWrite(m++);
		OsdWrite(m++, "  Do you want to continue?");
		OsdWrite(m++, "            No", menusub == 0);
		OsdWrite(m++, "            Yes", menusub == 1);
		OsdWrite(m++, "  Yes, and don't ask again", menusub == 2);
		menustate = MENU_SCRIPTS_PRE1;
		parentstate = MENU_SCRIPTS_PRE;
		break;

	case MENU_SCRIPTS_PRE1:
		if (menu) menustate = MENU_SYSTEM1;
		else if (select)
		{
			switch (menusub)
			{
			case 0:
				menustate = MENU_SYSTEM1;
				break;

			case 2:
				FileSaveConfig("script_confirm", card_cid, 32);
				// fall through

			case 1:
				SelectFile(Selected_F[0], "SH", SCANO_DIR, MENU_SCRIPTS_FB, MENU_SYSTEM1);
				break;
			}
		}
		break;

	case MENU_SCRIPTS_FB:
		if (cfg.fb_terminal)
		{
			memcpy(Selected_F[0], selPath, sizeof(Selected_F[0]));
			static char cmd[1024 * 2];
			const char *path = getFullPath(selPath);
			menustate = MENU_SCRIPTS_FB2;
			video_chvt(2);
			video_fb_enable(1);
			vga_nag();
			sprintf(cmd, "#!/bin/bash\nexport LC_ALL=en_US.UTF-8\nexport HOME=/root\ncd $(dirname %s)\n%s\necho \"Press any key to continue\"\n", path, path);
			unlink("/tmp/script");
			FileSave("/tmp/script", cmd, strlen(cmd));
			ttypid = fork();
			if (!ttypid)
			{
				execl("/sbin/agetty", "/sbin/agetty", "-a", "root", "-l", "/tmp/script", "--nohostname", "-L", "tty2", "linux", NULL);
				exit(0); //should never be reached
			}
		}
		else
		{
			menustate = MENU_SCRIPTS;
		}
		break;

	case MENU_SCRIPTS_FB2:
		if (ttypid)
		{
			if (waitpid(ttypid, 0, WNOHANG) > 0)
			{
				ttypid = 0;
				user_io_osd_key_enable(1);
			}
		}
		else
		{
			if (c & UPSTROKE)
			{
				video_menu_bg((user_io_8bit_set_status(0, 0) & 0xE) >> 1);
				video_fb_enable(0);
				menustate = MENU_SYSTEM1;
				menusub = 3;
				OsdClear();
				OsdEnable(DISABLE_KEYBOARD);
			}
		}
		break;

	case MENU_BTPAIR2:
		if (select || menu)
		{
			menustate = MENU_NONE1;
		}
		break;

	case MENU_BTPAIR:
		OsdSetSize(16);
		OsdEnable(DISABLE_KEYBOARD);
		parentstate = MENU_BTPAIR;
		OsdSetTitle("BT Pairing");
		if (hci_get_route(0) < 0)
		{
			helptext_idx = 0;
			menumask = 1;
			menusub = 0;
			for (int i = 0; i < OsdGetSize() - 1; i++) OsdWrite(i);
			OsdWrite(7, "    No Bluetooth available");
			OsdWrite(OsdGetSize() - 1, STD_EXIT, menusub == 0);
			menustate = MENU_BTPAIR2;
			break;
		}
		//fall through

	case MENU_SCRIPTS:
		helptext_idx = 0;
		menumask = 0;
		menusub = 0;
		if(parentstate != MENU_BTPAIR) OsdSetTitle(flist_SelectedItem()->de.d_name);
		menustate = MENU_SCRIPTS1;
		if (parentstate != MENU_BTPAIR) parentstate = MENU_SCRIPTS;
		for (int i = 0; i < OsdGetSize() - 1; i++) OsdWrite(i);
		OsdWrite(OsdGetSize() - 1, (parentstate == MENU_BTPAIR) ? "           Finish" : "           Cancel", menusub == 0, 0);
		for (int i = 0; i < script_lines; i++) strcpy(script_output[i], "");
		script_line=0;
		script_finished = false;
		cpu_set_t set;
		CPU_ZERO(&set);
		CPU_SET(0, &set);
		CPU_SET(1, &set);
		sched_setaffinity(0, sizeof(set), &set);
		if (parentstate == MENU_BTPAIR)
		{
			OsdUpdate();
			if(cfg.bt_reset_before_pair) system("hciconfig hci0 reset");
			script_pipe = popen("/usr/sbin/btpair", "r");
		}
		else
		{
			script_pipe = popen(getFullPath(selPath), "r");
		}
		script_file = fileno(script_pipe);
		fcntl(script_file, F_SETFL, O_NONBLOCK);
		break;

	case MENU_SCRIPTS1:
		if (!script_finished)
		{
			if (!feof(script_pipe)) {
				if (fgets(script_line_output, script_line_length, script_pipe) != NULL)
				{
					script_line_output[strcspn(script_line_output, "\n")] = 0;
					if (script_line < OsdGetSize() - 2)
					{
						strcpy(script_output[script_line++], script_line_output);
					}
					else
					{
						strcpy(script_output[script_line], script_line_output);
						for (int i = 0; i < script_line; i++) strcpy(script_output[i], script_output[i+1]);
					};
					for (int i = 0; i < OsdGetSize() - 2; i++) OsdWrite(i, script_output[i], 0, 0);
				};
			}
			else {
				pclose(script_pipe);
				cpu_set_t set;
				CPU_ZERO(&set);
				CPU_SET(1, &set);
				sched_setaffinity(0, sizeof(set), &set);
				script_finished=true;
				OsdWrite(OsdGetSize() - 1, "             OK", menusub == 0, 0);
			};
		};

		if (select || menu || script_finished || c == KEY_BACKSPACE)
		{
			if (!script_finished)
			{
				strcpy(script_command, "killall ");
				strcat(script_command, (parentstate == MENU_BTPAIR) ? "-SIGINT btctl" : flist_SelectedItem()->de.d_name);
				system(script_command);
				pclose(script_pipe);
				cpu_set_t set;
				CPU_ZERO(&set);
				CPU_SET(1, &set);
				sched_setaffinity(0, sizeof(set), &set);
				script_finished = true;
			};

			if (c == KEY_BACKSPACE && (parentstate == MENU_BTPAIR))
			{
				for (int i = 0; i < OsdGetSize() - 1; i++) OsdWrite(i);
				OsdWrite(7, "   Delete all pairings...");
				OsdUpdate();
				system("/bin/bluetoothd renew");
				menustate = MENU_BTPAIR;
			}
			else
			{
				if (parentstate == MENU_BTPAIR)
				{
					menustate = MENU_NONE1;
				}
				else
				{
					menustate = MENU_SYSTEM1;
					menusub = 3;
				}
			}
		}
		break;

	case MENU_KBDMAP:
		helptext_idx = 0;
		menumask = 1;
		OsdSetTitle("Keyboard", 0);
		menustate = MENU_KBDMAP1;
		parentstate = MENU_KBDMAP;
		for (int i = 0; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, "           cancel", menusub == 0, 0);
		flag = 0;
		break;

	case MENU_KBDMAP1:
		if(!get_map_button())
		{
			OsdWrite(3, "     Press key to remap", 0, 0);
			s[0] = 0;
			if(flag)
			{
				sprintf(s, "    on keyboard %04x:%04x", get_map_vid(), get_map_pid());
			}
			OsdWrite(5, s, 0, 0);
			OsdWrite(OsdGetSize() - 1, "           finish", menusub == 0, 0);
		}
		else
		{
			flag = 1;
			sprintf(s, "  Press key to map 0x%02X to", get_map_button() & 0xFF);
			OsdWrite(3, s, 0, 0);
			OsdWrite(5, "      on any keyboard", 0, 0);
			OsdWrite(OsdGetSize() - 1);
		}

		if (select || menu)
		{
			finish_map_setting(menu);
			menustate = MENU_SYSTEM1;
			menusub = 1;
		}
		break;

	case MENU_CORE_FILE_SELECTED1:
		recent_update(SelectedDir, selPath, SelectedLabel, -1);
		menustate = MENU_NONE1;
		memcpy(Selected_tmp, selPath, sizeof(Selected_tmp));
		if (!getStorage(0)) // multiboot is only on SD card.
		{
			selPath[strlen(selPath) - 4] = 0;
			int off = strlen(SelectedDir);
			if (off) off++;
			int fnum = ScanDirectory(SelectedDir, SCANF_INIT, "TXT", 0, selPath + off);
			if (fnum)
			{
				if (fnum == 1)
				{
					//Check if the only choice is <core>.txt
					strcat(selPath, ".txt");
					if (FileLoad(selPath, 0, 0))
					{
						menustate = MENU_CORE_FILE_SELECTED2;
						break;
					}
				}

				strcpy(selPath, Selected_tmp);
				AdjustDirectory(selPath);
				cp_MenuCancel = fs_MenuCancel;
				strcpy(fs_pFileExt, "TXT");
				fs_ExtLen = 3;
				fs_Options = SCANO_CORES;
				fs_MenuSelect = MENU_CORE_FILE_SELECTED2;
				fs_MenuCancel = MENU_CORE_FILE_CANCELED;
				menustate = MENU_FILE_SELECT1;
				break;
			}
		}

		if (!strcasecmp(".mra",&(Selected_tmp[strlen(Selected_tmp) - 4])))
		{
			// find the RBF file from the XML
			arcade_load(getFullPath(Selected_tmp));
		}
		else
		{
			fpga_load_rbf(Selected_tmp);
		}
		break;

	case MENU_CORE_FILE_SELECTED2:
		fpga_load_rbf(Selected_tmp, selPath);
		menustate = MENU_NONE1;
		break;

	case MENU_CORE_FILE_CANCELED:
		SelectFile("", 0, SCANO_CORES, MENU_CORE_FILE_SELECTED1, cp_MenuCancel);
		break;

		/******************************************************************/
		/* we should never come here                                      */
		/******************************************************************/
	default:
		break;
	}

	if (is_menu())
	{
		static unsigned long rtc_timer = 0;
		static int init_wait = 0;

		if (!rtc_timer || CheckTimer(rtc_timer))
		{
			rtc_timer = GetTimer(cfg.bootcore[0] != '\0' ? 100 : 1000);
			char str[64] = { 0 };
			char straux[64];

			if (cfg.bootcore[0] != '\0')
			{
				if (btimeout > 0)
				{
					OsdWrite(12, "\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81");
					snprintf(str, sizeof(str), " Bootcore -> %s", bootcoretype);
					OsdWrite(13, str, 0, 0);
					strcpy(straux, cfg.bootcore);
					sprintf(str, " %s", get_rbf_name_bootcore(straux));

					char s[40];
					memset(s, ' ', 32); // clear line buffer
					s[32] = 0; // set temporary string length to OSD line length

					int len = strlen(str);
					if (len > 28)
					{
						len = 27; // trim display length if longer than 30 characters
						s[28] = 22;
					}

					strncpy(s + 1, str, len); // display only name
					OsdWrite(14, s, 1, 0, 0, (32 * btimeout) / cfg.bootcore_timeout);

					sprintf(str, "   Press any key to cancel");
					OsdWrite(15, str, 0, 0);
					btimeout--;
					if (!btimeout)
					{
						OsdWrite(13, "", 0, 0);
						OsdWrite(14, s, 1, 0, 0, 0);
						sprintf(str, "           Loading...");
						OsdWrite(15, str, 1, 0);
						isMraName(cfg.bootcore) ? arcade_load(getFullPath(cfg.bootcore)) : fpga_load_rbf(cfg.bootcore);
					}
				}
			}

			if (init_wait < 1)
			{
				sprintf(str, "       www.MiSTerFPGA.org       ");
				init_wait++;
			}
			else
			{
				sprintf(str, " MiSTer      ");

				time_t t = time(NULL);
				struct tm tm = *localtime(&t);
				if (tm.tm_year >= 117)
				{
					strftime(str + strlen(str), sizeof(str) - 1 - strlen(str), "%b %d %a%H:%M:%S", &tm);
				}

				int n = 8;
				if (getNet(2)) str[n++] = 0x1d;
				if (getNet(1)) str[n++] = 0x1c;
				if (hci_get_route(0) >= 0) str[n++] = 4;
				if (user_io_get_sdram_cfg() & 0x8000)
				{
					switch (user_io_get_sdram_cfg() & 7)
					{
					case 7:
						str[n] = 0x95;
						break;
					case 3:
						str[n] = 0x94;
						break;
					case 1:
						str[n] = 0x93;
						break;
					default:
						str[n] = 0x92;
						break;
					}
				}

				str[22] = ' ';
			}

			OsdWrite(16, "", 1, 0);
			OsdWrite(17, str, 1, 0);
			OsdWrite(18, "", 1, 0);
		}
	}
}

void open_joystick_setup()
{
	OsdSetSize(16);
	menusub = 0;
	OsdClear();
	OsdEnable(DISABLE_KEYBOARD);
	start_map_setting(joy_bcount ? joy_bcount + 4 : 8);
	menustate = MENU_JOYDIGMAP;
	joymap_first = 1;
}

void ScrollLongName(void)
{
	// this function is called periodically when file selection window is displayed
	// it checks if predefined period of time has elapsed and scrolls the name if necessary

	static int len;
	int max_len;

	len = strlen(flist_SelectedItem()->altname); // get name length

	max_len = 30; // number of file name characters to display (one more required for scrolling)
	if (flist_SelectedItem()->de.d_type == DT_DIR)
	{
		max_len = 24; // number of directory name characters to display
	}

	if (flist_SelectedItem()->de.d_type != DT_DIR) // if a file
	{
		if (!cfg.rbf_hide_datecode && flist_SelectedItem()->datecode[0])
		{
			max_len = 20; // __.__.__ remove that from the end
		}
		else if (cfg.browse_expand && len < 55)
		{
			return;
		}
	}

	ScrollText(flist_iSelectedEntry()-flist_iFirstEntry(), flist_SelectedItem()->altname, 0, len, max_len, 1);
}

// print directory contents
void PrintDirectory(int expand)
{
	char s[40];
	ScrollReset();

	if (!cfg.browse_expand) expand = 0;

	if (expand)
	{
		int k = flist_iFirstEntry() + OsdGetSize() - 1;
		if (flist_nDirEntries() && k == flist_iSelectedEntry() && k <= flist_nDirEntries()
			&& strlen(flist_DirItem(k)->altname) > 28 && !(!cfg.rbf_hide_datecode && flist_DirItem(k)->datecode[0])
			&& flist_DirItem(k)->de.d_type != DT_DIR)
		{
			//make room for last expanded line
			flist_iFirstEntryInc();
		}
	}

	int i = 0;
	int k = flist_iFirstEntry();
	while(i < OsdGetSize())
	{
		char leftchar = 0;
		memset(s, ' ', 32); // clear line buffer
		s[32] = 0;
		int len2 = 0;
		leftchar = 0;
		int len = 0;

		if (k < flist_nDirEntries())
		{
			len = strlen(flist_DirItem(k)->altname); // get name length
			if (len > 28)
			{
				len2 = len - 27;
				if (len2 > 27) len2 = 27;
				if (!expand) len2 = 0;

				len = 27; // trim display length if longer than 30 characters
				s[28] = 22;
			}

			if((flist_DirItem(k)->de.d_type == DT_DIR) && (fs_Options & SCANO_CORES) && (flist_DirItem(k)->altname[0] == '_'))
			{
				strncpy(s + 1, flist_DirItem(k)->altname+1, len-1);
			}
			else
			{
				strncpy(s + 1, flist_DirItem(k)->altname, len); // display only name
			}

			char *datecode = flist_DirItem(k)->datecode;
			if (flist_DirItem(k)->de.d_type == DT_DIR) // mark directory with suffix
			{
				if (!strcmp(flist_DirItem(k)->altname, ".."))
				{
					strcpy(&s[19], " <UP-DIR>");
				}
				else
				{
					strcpy(&s[22], " <DIR>");
				}
				len2 = 0;
			}
			else if (!cfg.rbf_hide_datecode && datecode[0])
			{
				int n = 19;
				s[n++] = ' ';
				s[n++] = datecode[0];
				s[n++] = datecode[1];
				s[n++] = '.';
				s[n++] = datecode[2];
				s[n++] = datecode[3];
				s[n++] = '.';
				s[n++] = datecode[4];
				s[n++] = datecode[5];

				if (len >= 19)
				{
					s[19] = 22;
					s[28] = ' ';
				}
				len2 = 0;
			}

			if (!i && k) leftchar = 17;
			if (i && k < flist_nDirEntries() - 1) leftchar = 16;
		}
		else if(!flist_nDirEntries()) // selected directory is empty
		{
			if (!i) strcpy(s, "          No files!");
			if (home_dir && !filter[0])
			{
				if (i == 6) strcpy(s, "      Missing directory:");
				if (i == 8)
				{
					len = strlen(home_dir);
					if (len > 27) len = 27;
					strncpy(s + 1 + ((27 - len) / 2), home_dir, len);
				}
			}
		}

		int sel = (i == (flist_iSelectedEntry() - flist_iFirstEntry()));
		OsdWriteOffset(i, s, sel, 0, 0, leftchar);
		i++;

		if (sel && len2)
		{
			len = strlen(flist_DirItem(k)->altname);
			strcpy(s+1, flist_DirItem(k)->altname + len - len2);
			OsdWriteOffset(i, s, sel, 0, 0, leftchar);
			i++;
		}

		k++;
	}
}

static void set_text(const char *message, unsigned char code)
{
	char s[40];
	int i = 0, l = 1;

	OsdWrite(0, "", 0, 0);

	do
	{
		s[i++] = *message;

		// line full or line break
		if ((i == 29) || (*message == '\n') || !*message)
		{
			s[--i] = 0;
			OsdWrite(l++, s, 0, 0);
			i = 0;  // start next line
		}
	} while (*message++);

	if (code && (l <= 7))
	{
		sprintf(s, " Code: #%d", code);
		OsdWrite(l++, s, 0, 0);
	}

	while (l <= 7) OsdWrite(l++, "", 0, 0);
}

void InfoMessage(const char *message, int timeout, const char *title)
{
	if (menustate != MENU_INFO)
	{
		OsdSetTitle(title, 0);
		OsdEnable(OSD_MSG); // do not disable keyboard
	}

	set_text(message, 0);

	menu_timer = GetTimer(timeout);
	menustate = MENU_INFO;
	HandleUI();
	OsdUpdate();
}

void MenuHide()
{
	menustate = MENU_NONE1;
	HandleUI();
}

void Info(const char *message, int timeout, int width, int height, int frame)
{
	if (!user_io_osd_is_visible())
	{
		OSD_PrintInfo(message, &width, &height, frame);
		InfoEnable(20, (cfg.direct_video && get_vga_fb()) ? 30 : 10, width, height);
		OsdSetSize(16);

		menu_timer = GetTimer(timeout);
		menustate = MENU_INFO;
		OsdUpdate();
	}
}

int menu_lightgun_cb(int idx, uint16_t type, uint16_t code, int value)
{
	if (type == EV_ABS)
	{
		if (code == 0 && value) gun_x = value;
		if (code == 1 && value != 1023) gun_y = value;
	}

	if (type == EV_KEY)
	{
		if ((code == 0x130 || code == 0x131) && menustate == MENU_LGCAL1)
		{
			gun_idx = idx;
			if (value == 1) gun_ok = 1;
			if (value == 0) gun_ok = 2;
			return 1;
		}
	}
	return 0;
}

int menu_allow_cfg_switch()
{
	if (user_io_osd_is_visible())
	{
		switch (menustate)
		{
		case MENU_ST_MAIN2:
		case MENU_ARCHIE_MAIN2:
		case MENU_MINIMIG_MAIN2:
		case MENU_COMMON2:
		case MENU_SYSTEM2:
			return 1;

		case MENU_FILE_SELECT2:
			if (is_menu() && (fs_Options & SCANO_CORES)) return 1;
			break;

		case MENU_GENERIC_MAIN2:
			if (!page) return 1;
			break;
		}
	}

	return 0;
}

void menu_process_save()
{
	menu_save_timer = GetTimer(1000);
}

static char pchar[] = { 0x8C, 0x8E, 0x8F, 0x90, 0x91, 0x7F };

#define PROGRESS_CNT    28
#define PROGRESS_CHARS  (int)(sizeof(pchar)/sizeof(pchar[0]))
#define PROGRESS_MAX    ((PROGRESS_CHARS*PROGRESS_CNT)-1)

void ProgressMessage(const char* title, const char* text, int current, int max)
{
	static int progress;
	if (!current && !max)
	{
		progress = -1;
		MenuHide();
		return;
	}

	int new_progress = (((uint64_t)current)*PROGRESS_MAX) / max;
	if (progress != new_progress)
	{
		progress = new_progress;
		static char progress_buf[128];
		memset(progress_buf, 0, sizeof(progress_buf));

		if (new_progress > PROGRESS_MAX) new_progress = PROGRESS_MAX;
		char c = pchar[new_progress % PROGRESS_CHARS];
		new_progress /= PROGRESS_CHARS;

		char *buf = progress_buf;
		sprintf(buf, "\n\n %.27s\n ", text);
		buf += strlen(buf);

		for (int i = 0; i <= new_progress; i++) buf[i] = (i < new_progress) ? 0x7F : c;
		buf[PROGRESS_CNT] = 0;

		InfoMessage(progress_buf, 2000, title);
	}
}
