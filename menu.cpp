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
#include "bootcore.h"
#include "cheats.h"
#include "video.h"
#include "joymapping.h"

#include "support.h"

/*menu states*/
enum MENU
{
	MENU_NONE1,
	MENU_NONE2,
	MENU_MAIN1,
	MENU_MAIN2,
	MENU_FILE_SELECT1,
	MENU_FILE_SELECT2,
	MENU_FILE_SELECTED,
	MENU_RESET1,
	MENU_RESET2,
	MENU_RECONF1,
	MENU_RECONF2,
	MENU_SETTINGS1,
	MENU_SETTINGS2,
	MENU_ROMFILE_SELECTED,
	MENU_SETTINGS_VIDEO1,
	MENU_SETTINGS_VIDEO2,
	MENU_SETTINGS_MEMORY1,
	MENU_SETTINGS_MEMORY2,
	MENU_SETTINGS_CHIPSET1,
	MENU_SETTINGS_CHIPSET2,
	MENU_SETTINGS_DRIVES1,
	MENU_SETTINGS_DRIVES2,
	MENU_SETTINGS_HARDFILE1,
	MENU_SETTINGS_HARDFILE2,
	MENU_HARDFILE_SELECT1,
	MENU_HARDFILE_SELECT2,
	MENU_HARDFILE_SELECTED,
	MENU_HARDFILE_SELECTED2,
	MENU_HARDFILE_SELECTED3,
	MENU_LOADCONFIG_1,
	MENU_LOADCONFIG_2,
	MENU_SAVECONFIG_1,
	MENU_SAVECONFIG_2,
	MENU_SYSTEM1,
	MENU_SYSTEM2,
	MENU_CORE_FILE_SELECTED1,
	MENU_CORE_FILE_SELECTED2,
	MENU_CORE_FILE_CANCELED,
	MENU_ERROR,
	MENU_INFO,
	MENU_JOYDIGMAP,
	MENU_JOYDIGMAP1,
	MENU_JOYDIGMAP2,
	MENU_JOYDIGMAP3,
	MENU_JOYDIGMAP4,
	MENU_JOYKBDMAP,
	MENU_JOYKBDMAP1,
	MENU_KBDMAP,
	MENU_KBDMAP1,
	MENU_SCRIPTS_PRE,
	MENU_SCRIPTS_PRE1,
	MENU_SCRIPTS,
	MENU_SCRIPTS1,
	MENU_SCRIPTS_FB,
	MENU_SCRIPTS_FB2,
	MENU_BTPAIR,
	MENU_WMPAIR,
	MENU_WMPAIR1,
	MENU_LGCAL,
	MENU_LGCAL1,
	MENU_LGCAL2,
	MENU_CHEATS1,
	MENU_CHEATS2,

	// Mist/atari specific pages
	MENU_MIST_MAIN1,
	MENU_MIST_MAIN2,
	MENU_MIST_MAIN_FILE_SELECTED,
	MENU_MIST_STORAGE1,
	MENU_MIST_STORAGE2,
	MENU_MIST_STORAGE_FILE_SELECTED,
	MENU_MIST_SYSTEM1,
	MENU_MIST_SYSTEM2,
	MENU_MIST_SYSTEM_FILE_SELECTED,
	MENU_MIST_VIDEO1,
	MENU_MIST_VIDEO2,
	MENU_MIST_VIDEO_ADJUST1,
	MENU_MIST_VIDEO_ADJUST2,

	// archimedes menu entries
	MENU_ARCHIE_MAIN1,
	MENU_ARCHIE_MAIN2,
	MENU_ARCHIE_MAIN_FILE_SELECTED,

	MENU_UART1,
	MENU_UART2,

	// 8bit menu entries
	MENU_8BIT_MAIN1,
	MENU_8BIT_MAIN2,
	MENU_8BIT_MAIN_FILE_SELECTED,
	MENU_8BIT_MAIN_IMAGE_SELECTED,
	MENU_8BIT_SYSTEM1,
	MENU_8BIT_SYSTEM2,
	MENU_COEFF_FILE_SELECTED,
	MENU_GAMMA_FILE_SELECTED,
	MENU_8BIT_INFO,
	MENU_8BIT_INFO2,
	MENU_8BIT_ABOUT1,
	MENU_8BIT_ABOUT2
};

static uint32_t menustate = MENU_NONE1;
static uint32_t parentstate;
static uint32_t menusub = 0;
static uint32_t menusub_last = 0; //for when we allocate it dynamically and need to know last row
static uint32_t menumask = 0; // Used to determine which rows are selectable...
static uint32_t menu_timer = 0;

extern const char *version;

const char *config_tos_mem[] = { "512 kB", "1 MB", "2 MB", "4 MB", "8 MB", "14 MB", "--", "--" };
const char *config_tos_wrprot[] = { "none", "A:", "B:", "A: and B:" };
const char *config_tos_usb[] = { "none", "control", "debug", "serial", "parallel", "midi" };

const char *config_memory_chip_msg[] = { "512K", "1M",   "1.5M", "2M"   };
const char *config_memory_slow_msg[] = { "none", "512K", "1M",   "1.5M" };
const char *config_memory_fast_msg[] = { "none", "2M",   "4M",   "8M", "256M", "384M", "256M" };

const char *config_scanlines_msg[] = { "Off", "HQ2x", "CRT 25%" , "CRT 50%" , "CRT 75%" };
const char *config_ar_msg[] = { "4:3", "16:9" };
const char *config_blank_msg[] = { "Blank", "Blank+" };
const char *config_dither_msg[] = { "off", "SPT", "RND", "S+R" };
const char *config_cpu_msg[] = { "68000", "68010", "-----","68020" };
const char *config_chipset_msg[] = { "OCS-A500", "OCS-A1000", "ECS", "---", "---", "---", "AGA", "---" };
const char *config_turbo_msg[] = { "none", "CHIPRAM", "KICK", "BOTH" };
const char *config_autofire_msg[] = { "        AUTOFIRE OFF", "        AUTOFIRE FAST", "        AUTOFIRE MEDIUM", "        AUTOFIRE SLOW" };
const char *config_cd32pad_msg[] = { "OFF", "ON" };
const char *config_button_turbo_msg[] = { "OFF", "FAST", "MEDIUM", "SLOW" };
const char *config_button_turbo_choice_msg[] = { "A only", "B only", "A & B" };
const char *joy_button_map[] = { "RIGHT", "LEFT", "DOWN", "UP", "BUTTON A", "BUTTON B", "BUTTON X", "BUTTON Y", "BUTTON L", "BUTTON R", "SELECT", "START", "KBD TOGGLE", "MENU", "     Stick X: Tilt RIGHT", "     Stick Y: Tilt DOWN", "   Mouse emu X: Tilt RIGHT", "   Mouse emu Y: Tilt DOWN" };
const char *joy_ana_map[] = { "    DPAD test: Press RIGHT", "    DPAD test: Press DOWN", "   Stick 1 Test: Tilt RIGHT", "   Stick 1 Test: Tilt DOWN", "   Stick 2 Test: Tilt RIGHT", "   Stick 2 Test: Tilt DOWN" };
const char *config_stereo_msg[] = { "0%", "25%", "50%", "100%" };
const char *config_uart_msg[] = { "     None", "      PPP", "  Console", "     MIDI" };
const char *config_scaler_msg[] = { "Internal","Custom" };
const char *config_gamma_msg[] = { "Off","On" };

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
static bool script_exited;

enum HelpText_Message { HELPTEXT_NONE, HELPTEXT_MAIN, HELPTEXT_HARDFILE, HELPTEXT_CHIPSET, HELPTEXT_MEMORY, HELPTEXT_VIDEO };
static const char *helptexts[] =
{
	0,
	"                                Welcome to MiSTer! Use the cursor keys to navigate the menus. Use space bar or enter to select an item. Press Esc or F12 to exit the menus. Joystick emulation on the numeric keypad can be toggled with the numlock or scrlock key, while pressing Ctrl-Alt-0 (numeric keypad) toggles autofire mode.",
	"                                Minimig can emulate an A600/A1200 IDE harddisk interface. The emulation can make use of Minimig-style hardfiles (complete disk images) or UAE-style hardfiles (filesystem images with no partition table).",
	"                                Minimig's processor core can emulate a 68000 or 68020 processor (though the 68020 mode is still experimental.) If you're running software built for 68000, there's no advantage to using the 68020 mode, since the 68000 emulation runs just as fast.",
	"                                Minimig can make use of up to 2 megabytes of Chip RAM, up to 1.5 megabytes of Slow RAM (A500 Trapdoor RAM), and up to 384 megabytes of Fast RAM. To use the HRTmon feature you will need a file on the SD card named hrtmon.rom.",
	"                                Minimig's video features include a blur filter, to simulate the poorer picture quality on older monitors, and also scanline generation to simulate the appearance of a screen with low vertical resolution.",
	0
};

static const char *info_top = "\x80\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x82";
static const char *info_bottom = "\x85\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x84";

// one screen width
static const char* HELPTEXT_SPACER = "                                ";
static char helptext_custom[1024];

const char* scanlines[] = { "Off","25%","50%","75%" };
const char* stereo[] = { "Mono","Stereo" };
const char* atari_chipset[] = { "ST","STE","MegaSTE","STEroids" };

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

static char SelectedRBF[1024] = { 0 };
static char SelectedDir[1024] = { 0 };
static char SelectedPath[1024] = { 0 };

static int changeDir(char *dir)
{
	char curdir[128];
	memset(curdir, 0, sizeof(curdir));
	if(!dir || !strcmp(dir, ".."))
	{
		if (!strlen(SelectedPath))
		{
			return 0;
		}

		char *p = strrchr(SelectedPath, '/');
		if (p)
		{
			*p = 0;
			uint32_t len = strlen(p+1);
			if (len > sizeof(curdir) - 1) len = sizeof(curdir) - 1;
			strncpy(curdir, p+1, len);
		}
		else
		{
			uint32_t len = strlen(SelectedPath);
			if (len > sizeof(curdir) - 1) len = sizeof(curdir) - 1;
			strncpy(curdir, SelectedPath, len);
			SelectedPath[0] = 0;
		}
	}
	else
	{
		if (strlen(SelectedPath) + strlen(dir) > sizeof(SelectedPath) - 100)
		{
			return 0;
		}

		if (strlen(SelectedPath)) strcat(SelectedPath, "/");
		strcat(SelectedPath, dir);
	}

	ScanDirectory(SelectedPath, SCANF_INIT, fs_pFileExt, fs_Options);
	if(curdir[0])
	{
		ScanDirectory(SelectedPath, SCANF_SET_ITEM, curdir, fs_Options);
	}
	return 1;
}

// this function displays file selection menu
static void SelectFile(const char* pFileExt, unsigned char Options, unsigned char MenuSelect, unsigned char MenuCancel)
{
	printf("pFileExt = %s\n", pFileExt);

	if (Options & SCANO_CORES)
	{
		strcpy(SelectedPath, get_rbf_dir());
		if (strlen(get_rbf_name()))
		{
			strcat(SelectedPath, "/");
			strcat(SelectedPath, get_rbf_name());
		}
		pFileExt = "RBF";
	}
	else if (Options & SCANO_TXT)
	{
		pFileExt = "TXT";
	}
	else if (strncasecmp(HomeDir, SelectedPath, strlen(HomeDir)) || !strcasecmp(HomeDir, SelectedPath))
	{
		Options &= ~SCANO_NOENTER;
		strcpy(SelectedPath, HomeDir);
	}

	if (!strcasecmp(HomeDir, SelectedPath))
		FileCreatePath(SelectedPath);

	ScanDirectory(SelectedPath, SCANF_INIT, pFileExt, Options);
	if (!flist_nDirEntries())
	{
		SelectedPath[0] = 0;
		ScanDirectory(SelectedPath, SCANF_INIT, pFileExt, Options);
	}

	AdjustDirectory(SelectedPath);

	strcpy(fs_pFileExt, pFileExt);
	fs_ExtLen = strlen(fs_pFileExt);
	fs_Options = Options & ~SCANO_NOENTER;
	fs_MenuSelect = MenuSelect;
	fs_MenuCancel = MenuCancel;

	menustate = MENU_FILE_SELECT1;
}


void substrcpy(char *d, char *s, char idx)
{
	char p = 0;

	while (*s) {
		if ((p == idx) && *s && (*s != ','))
			*d++ = *s;

		if (*s == ',')
			p++;

		s++;
	}

	*d = 0;
}

#define STD_EXIT       "            exit"
#define STD_SPACE_EXIT "        SPACE to exit"
#define STD_COMBO_EXIT "      Ctrl+ESC to exit"

#define HELPTEXT_DELAY 10000
#define FRAME_DELAY 150

static unsigned char getIdx(char *opt)
{
	if ((opt[1] >= '0') && (opt[1] <= '9')) return opt[1] - '0';
	if ((opt[1] >= 'A') && (opt[1] <= 'V')) return opt[1] - 'A' + 10;
	return 0; // basically 0 cannot be valid because used as a reset. Thus can be used as a error.
}

uint32_t getStatus(char *opt, uint32_t status)
{
	char idx1 = getIdx(opt);
	char idx2 = getIdx(opt + 1);
	uint32_t x = (status & (1 << idx1)) ? 1 : 0;

	if (idx2>idx1) {
		x = status >> idx1;
		x = x & ~(0xffffffff << (idx2 - idx1 + 1));
	}

	return x;
}

uint32_t setStatus(char *opt, uint32_t status, uint32_t value)
{
	unsigned char idx1 = getIdx(opt);
	unsigned char idx2 = getIdx(opt + 1);
	uint32_t x = 1;

	if (idx2>idx1) x = ~(0xffffffff << (idx2 - idx1 + 1));
	x = x << idx1;

	return (status & ~x) | ((value << idx1) & x);
}

uint32_t getStatusMask(char *opt)
{
	char idx1 = getIdx(opt);
	char idx2 = getIdx(opt + 1);
	uint32_t x = 1;

	if (idx2>idx1) x = ~(0xffffffff << (idx2 - idx1 + 1));

	//printf("grtStatusMask %d %d %x\n", idx1, idx2, x);

	return x << idx1;
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
	static uint32_t c2;
	static unsigned long repeat;
	uint32_t c1, c;

	c1 = menu_key;
	c = 0;
	if (c1 != c2)
	{
		c = c1;
		hold_cnt = 1;
	}
	c2 = c1;

	// inject a fake "MENU_KEY" if no menu is visible and the menu key is loaded
	if (!user_io_osd_is_visible() && !video_fb_state() && is_menu_core()) c = KEY_F12;

	// generate repeat "key-pressed" events
	if ((c1 & UPSTROKE) || (!c1))
	{
		hold_cnt = 0;
		repeat = GetTimer(REPEATDELAY);
	}
	else if (CheckTimer(repeat))
	{
		repeat = GetTimer(REPEATRATE);
		if (GetASCIIKey(c1) || ((menustate == MENU_8BIT_SYSTEM2) && (menusub == 11)))
		{
			c = c1;
			hold_cnt++;
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
			menustate = MENU_BTPAIR;
		}

		if (!but && last_but && !longpress_consumed) c = KEY_F12;

		if (!but) longpress_consumed = 0;
		last_but = but;
	}
	return(c);
}

static int has_bt()
{
	FILE *fp;
	static char out[1035];

	fp = popen("hcitool dev | grep hci0", "r");
	if (!fp) return 0;

	int ret = 0;
	while (fgets(out, sizeof(out) - 1, fp) != NULL)
	{
		if (strlen(out)) ret = 1;
	}

	pclose(fp);
	return ret;
}

static int toggle_wminput()
{
	if (access("/bin/wminput", F_OK) < 0 || access("/media/fat/linux/wiimote.cfg", F_OK) < 0) return -1;

	FILE *fp;
	static char out[1035];

	fp = popen("pidof wminput", "r");
	if (!fp) return -1;

	int ret = -1;
	if (fgets(out, sizeof(out) - 1, fp) != NULL)
	{
		if (strlen(out))
		{
			system("killall wminput");
			ret = 0;
		}
	}
	else
	{
		system("taskset 1 wminput --daemon --config /media/fat/linux/wiimote.cfg &");
		ret = 1;
	}

	pclose(fp);
	return ret;
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
		int n = is_menu_core() ? 10 : 5;

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
static char lastrow[256];

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

	if (row == (OsdGetSize() - 1))
	{
		int len = strlen(s);
		if (len > 255) len = 255;
		memcpy(lastrow, s, len);
		lastrow[len] = 0;
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
	if (spl && !strcmp(spl, ".rbf"))
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
		OsdEnable(0);
		EnableOsd_on(OSD_HDMI);
	}

	OsdDisable();
	EnableOsd_on(OSD_ALL);
}

static int joymap_first = 0;

static int wm_x = 0;
static int wm_y = 0;
static int wm_ok = 0;
static int wm_side = 0;
static uint16_t wm_pos[4] = {};

void HandleUI(void)
{
	switch (user_io_core_type())
	{
	case CORE_TYPE_MIST:
	case CORE_TYPE_8BIT:
	case CORE_TYPE_SHARPMZ:
	case CORE_TYPE_ARCHIE:
		break;

	default:
		// No UI in unknown cores.
		return;
	}

	struct RigidDiskBlock *rdb = nullptr;

	static char opensave;
	static char ioctl_index;
	char *p;
	static char s[256];
	unsigned char m = 0, up, down, select, menu, right, left, plus, minus;
	char enable;
	static int reboot_req = 0;
	static long helptext_timer;
	static const char *helptext;
	static char helpstate = 0;
	static char drive_num = 0;
	static char flag;
	static int cr = 0;
	static uint32_t cheatsub = 0;
	static uint8_t card_cid[32];
	static uint32_t hdmask = 0;
	static pid_t ttypid = 0;
	static int has_fb_terminal = 0;
	static unsigned long flash_timer = 0;
	static int flash_state = 0;

	static char	cp_MenuCancel;

	// get user control codes
	uint32_t c = menu_key_get();

	// decode and set events
	menu = false;
	select = false;
	up = false;
	down = false;
	left = false;
	right = false;
	plus = false;
	minus = false;

	if (c && c != KEY_F12 && cfg.bootcore[0] != '\0') cfg.bootcore[0] = '\0';

	if (is_menu_core())
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
					spi_osd_cmd(OSD_CMD_DISABLE);
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
					spi_osd_cmd(OSD_CMD_WRITE | 8);
					spi_osd_cmd(OSD_CMD_ENABLE);
				}
			}

			if (!timeout)
			{
				if (!cfg.osd_timeout) cfg.osd_timeout = 30;
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
			video_fb_enable(0);
			break;

		case KEY_F1:
			if (is_menu_core() && cfg.fb_terminal)
			{
				unsigned long status = (user_io_8bit_set_status(0, 0)+ 2) & 0xE;
				user_io_8bit_set_status(status, 0xE);
				FileSaveConfig(user_io_create_config_name(), &status, 4);
				video_menu_bg(status >> 1);
			}
			break;

		case KEY_F11:
			if (user_io_osd_is_visible())
			{
				menustate = MENU_BTPAIR;
			}
			break;

		case KEY_F10:
			if (user_io_osd_is_visible() && !access("/bin/wminput", F_OK))
			{
				menustate = MENU_WMPAIR;
			}
			else if (input_has_lightgun())
			{
				menustate = MENU_LGCAL;
			}
			break;

		case KEY_F9:
			if ((is_menu_core() || ((get_key_mod() & (LALT | RALT)) && (get_key_mod() & (LCTRL | RCTRL))) || has_fb_terminal) && cfg.fb_terminal)
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
		case KEY_ENTER:
		case KEY_SPACE:
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
		}
	}

	if (menu || select || up || down || left || right)
	{
		if (helpstate) OsdWrite(OsdGetSize()-1, STD_EXIT, (menumask - ((1 << (menusub + 1)) - 1)) <= 0, 0); // Redraw the Exit line...
		helpstate = 0;
		helptext_timer = GetTimer(HELPTEXT_DELAY);
	}

	if (helptext)
	{
		if (helpstate<9)
		{
			if (CheckTimer(helptext_timer))
			{
				helptext_timer = GetTimer(FRAME_DELAY);
				OsdWriteOffset(OsdGetSize() - 1, (menustate == MENU_8BIT_MAIN2) ? lastrow : STD_EXIT, 0, 0, helpstate, 0);
				++helpstate;
			}
		}
		else if (helpstate == 9)
		{
			ScrollReset();
			++helpstate;
		}
		else
		{
			ScrollText(OsdGetSize() - 1, helptext, 0, 0, 0, 0);
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
            if((menumask >= ((uint32_t)1 << (menusub + 1))))	// Any active entries left?
            {
			    do
			    {
				    menusub++;
			    } while ((menumask & ((uint32_t)1 << menusub)) == 0);
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
			    } while ((menumask & ((uint32_t)1 << menusub)) == 0);
            }
            else
            {
                do
                {
                    menusub++;
                } while ((menumask & ((uint32_t)(~0) << (menusub + 1))) != 0); // jump to last item
            }
			menustate = parentstate;
		}
	}


    // SHARPMZ Series Menu - This has been located within the sharpmz.cpp code base in order to keep updates to common code
    // base to a minimum and shrink its size. The UI is called with the basic state data and it handles everything internally,
    // only updating values in this function as necessary.
    //
    if (user_io_core_type() == CORE_TYPE_SHARPMZ)
        sharpmz_ui(MENU_NONE1, MENU_NONE2, MENU_8BIT_SYSTEM1, MENU_FILE_SELECT1,
			       &parentstate, &menustate, &menusub, &menusub_last,
			       &menumask, SelectedPath, &helptext, helptext_custom,
			       &fs_ExtLen, &fs_Options, &fs_MenuSelect, &fs_MenuCancel,
			       fs_pFileExt,
			       menu, select, up, down,
			       left, right, plus, minus);

	// Switch to current menu screen
	switch (menustate)
	{
		/******************************************************************/
		/* no menu selected                                               */
		/******************************************************************/
	case MENU_NONE1:
		helptext = helptexts[HELPTEXT_NONE];
		menumask = 0;
		menustate = MENU_NONE2;
		firstmenu = 0;
		vga_nag();
		OsdSetSize(8);
		break;

	case MENU_INFO:
		if (CheckTimer(menu_timer)) menustate = MENU_NONE1;
		// fall through
	case MENU_ERROR:
	case MENU_NONE2:
		if (menu)
		{
			OsdSetSize(16);
			if(!is_menu_core() && (get_key_mod() & (LALT | RALT))) //Alt+Menu
			{
				SelectFile(0, SCANO_CORES, MENU_CORE_FILE_SELECTED1, MENU_NONE1);
			}
			else if (user_io_core_type() == CORE_TYPE_MIST) menustate = MENU_MIST_MAIN1;
			else if (user_io_core_type() == CORE_TYPE_ARCHIE) menustate = MENU_ARCHIE_MAIN1;
			else {
				if (is_menu_core())
				{
					OsdCoreNameSet("");
					SelectFile(0, SCANO_CORES, MENU_CORE_FILE_SELECTED1, MENU_SYSTEM1);
				}
				else if (is_minimig())
				{
					menustate = MENU_MAIN1;
				}
				else
				{
					if ((get_key_mod() & (LGUI | RGUI)) && !is_x86_core() && has_menu()) //Win+Menu
					{
						menustate = MENU_8BIT_SYSTEM1;
					}
					else
					{
						menustate = MENU_8BIT_MAIN1;
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
		OsdSetTitle(user_io_get_core_name(), OSD_ARROW_RIGHT | OSD_ARROW_LEFT);

		m = 0;
		menumask = 0x3ff;
		OsdWrite(m++);

		strcpy(s, " Floppy 0: ");
		strncat(s, get_image_name(0) ? get_image_name(0) : "* no disk *",27);
		OsdWrite(m++, s, menusub == 0);

		strcpy(s, " Floppy 1: ");
		strncat(s, get_image_name(1) ? get_image_name(1) : "* no disk *", 27);
		OsdWrite(m++, s, menusub == 1);

		OsdWrite(m++);

		strcpy(s, " OS ROM: ");
		strcat(s, archie_get_rom_name());
		OsdWrite(m++, s, menusub == 2);

		OsdWrite(m++);

		strcpy(s, " Aspect ratio:       ");
		strcat(s, archie_get_ar() ? "16:9" : "4:3");
		OsdWrite(m++, s, menusub == 3);

		strcpy(s, " Refresh rate:       ");
		strcat(s, archie_get_60() ? "Variable" : "60Hz");
		OsdWrite(m++, s, menusub == 4);

		OsdWrite(m++);

		sprintf(s, " Stereo mix:         %s", config_stereo_msg[archie_get_amix()]);
		OsdWrite(m++, s, menusub == 5);

		strcpy(s, " 25MHz audio fix:    ");
		strcat(s, archie_get_afix() ? "Enable" : "Disable");
		OsdWrite(m++, s, menusub == 6);

		OsdWrite(m++);

		sprintf(s, " Swap joysticks:     %s", user_io_get_joyswap() ? "Yes" : "No");
		OsdWrite(m++, s, menusub == 7);
		sprintf(s, " Swap mouse btn 2/3: %s", archie_get_mswap() ? "Yes" : "No");
		OsdWrite(m++, s, menusub == 8);

		while(m<15) OsdWrite(m++);

		OsdWrite(15, STD_EXIT, menusub == 9, 0);
		menustate = MENU_ARCHIE_MAIN2;
		parentstate = MENU_ARCHIE_MAIN1;

		// set helptext with core display on top of basic info
		sprintf(helptext_custom, HELPTEXT_SPACER);
		strcat(helptext_custom, OsdCoreName());
		strcat(helptext_custom, helptexts[HELPTEXT_MAIN]);
		helptext = helptext_custom;
		break;

	case MENU_ARCHIE_MAIN2:
		// menu key closes menu
		if (menu)
			menustate = MENU_NONE1;
		if (select) {
			switch (menusub) {
			case 0:  // Floppy 0
			case 1:  // Floppy 1
				SelectFile("ADF", SCANO_DIR | SCANO_UMOUNT, MENU_ARCHIE_MAIN_FILE_SELECTED, MENU_ARCHIE_MAIN1);
				break;

			case 2:  // Load ROM
				SelectFile("ROM", 0, MENU_ARCHIE_MAIN_FILE_SELECTED, MENU_ARCHIE_MAIN1);
				break;

			case 3:
				archie_set_ar(!archie_get_ar());
				menustate = MENU_ARCHIE_MAIN1;
				break;

			case 4:
				archie_set_60(!archie_get_60());
				menustate = MENU_ARCHIE_MAIN1;
				break;

			case 5:
				archie_set_amix(archie_get_amix()+1);
				menustate = MENU_ARCHIE_MAIN1;
				break;

			case 6:
				archie_set_afix(!archie_get_afix());
				menustate = MENU_ARCHIE_MAIN1;
				break;

			case 7:
				user_io_set_joyswap(!user_io_get_joyswap());
				menustate = MENU_ARCHIE_MAIN1;
				break;

			case 8:
				archie_set_mswap(!archie_get_mswap());
				menustate = MENU_ARCHIE_MAIN1;
				break;

			case 9:  // Exit
				menustate = MENU_NONE1;
				break;
			}
		}

		if (right)
		{
			menustate = MENU_8BIT_SYSTEM1;
			menusub = 0;
		}
		else if (left)
		{
			menustate = MENU_8BIT_INFO;
			menusub = 1;
		}
		break;

	case MENU_ARCHIE_MAIN_FILE_SELECTED: // file successfully selected
		if (menusub == 0) user_io_file_mount(SelectedPath, 0);
		if (menusub == 1) user_io_file_mount(SelectedPath, 1);
		if (menusub == 2) archie_set_rom(SelectedPath);
		menustate = MENU_ARCHIE_MAIN1;
		break;

		/******************************************************************/
		/* 8 bit main menu                                                */
		/******************************************************************/

	case MENU_8BIT_MAIN1: {
		spi_uio_cmd_cont(UIO_GET_OSDMASK);
		hdmask = spi_w(0);
		DisableIO();

		int entry = 0;
		while(1)
		{
			if (!menusub) firstmenu = 0;

			adjvisible = 0;
			entry = 0;
			uint32_t selentry = 0;
			joy_bcount = 0;
			menumask = 0;
			p = user_io_get_core_name();
			if (!p[0]) OsdCoreNameSet("8BIT");
			else      OsdCoreNameSet(p);

			OsdSetTitle(OsdCoreName(), 0);

			// add options as requested by core
			int i = 2;
			do
			{
				char* pos;

				p = user_io_8bit_get_string(i++);
				//printf("Option %d: %s\n", i-1, p);

				if (p)
				{
					int h = 0, d = 0;

					//Hide or Disable flag
					while((p[0] == 'H' || p[0] == 'D') && strlen(p)>2)
					{
						int flg = (hdmask & (1<<getIdx(p))) ? 1 : 0;
						if (p[0] == 'H') h |= flg; else d |= flg;
						p += 2;
					}

					if (!h)
					{
						// check for 'F'ile or 'S'D image strings
						if ((p[0] == 'F') || (p[0] == 'S'))
						{
							substrcpy(s, p, 2);
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
							char l = strlen(s);
							if (!l)
							{
								// option's index is outside of available values.
								// reset to 0.
								x = 0;
								//user_io_8bit_set_status(setStatus(p, status, x), 0xffffffff);
								substrcpy(s, p, 2 + x);
								l = strlen(s);
							}

							s[0] = ' ';
							substrcpy(s + 1, p, 1);

							char *end = s + strlen(s) - 1;
							while ((end > s + 1) && (*end == ' ')) end--;
							*(end + 1) = 0;

							strcat(s, ":");
							l = 28 - l - strlen(s);
							while (l--) strcat(s, " ");

							substrcpy(s + strlen(s), p, 2 + x);

							MenuWrite(entry, s, menusub == selentry, d);

							// add bit in menu mask
							menumask = (menumask << 1) | 1;
							entry++;
							selentry++;
						}

						// delimiter
						if (p[0] == '-')
						{
							MenuWrite(entry, "", 0, 0);
							entry++;
						}
					}

					// check for 'V'ersion strings
					if (p[0] == 'V')
					{
						// get version string
						strcpy(s, OsdCoreName());
						strcat(s, " ");
						substrcpy(s + strlen(s), p, 1);
						OsdCoreNameSet(s);
					}

					if (p[0] == 'J')
					{
						// joystick button names.
						for (int n = 0; n < 28; n++)
						{
							substrcpy(joy_bnames[n], p, n + 1);
							//printf("joy_bname = %s\n", joy_bnames[n]);
							if (!joy_bnames[n][0]) break;
							joy_bcount++;
						}

						//printf("joy_bcount = %d\n", joy_bcount);
					}
				}
			} while (p);

			if (!entry) break;

			for (; entry < OsdGetSize() - 1; entry++) MenuWrite(entry, "", 0, 0);

			// exit row
			MenuWrite(entry, STD_EXIT, menusub == selentry, 0, OSD_ARROW_RIGHT | OSD_ARROW_LEFT);
			menusub_last = selentry;
			menumask = (menumask << 1) | 1;

			if (!adjvisible) break;
			firstmenu += adjvisible;
		}

		if (!entry)
		{
			menustate = MENU_8BIT_SYSTEM1;
			menusub = 0;
			break;
		}

		parentstate = menustate;
		menustate = MENU_8BIT_MAIN2;

		// set helptext with core display on top of basic info
		sprintf(helptext_custom, HELPTEXT_SPACER);
		strcat(helptext_custom, OsdCoreName());
		strcat(helptext_custom, helptexts[HELPTEXT_MAIN]);
		helptext = helptext_custom;

	} break; // end MENU_8BIT_MAIN1

	case MENU_8BIT_MAIN2:
		// menu key closes menu
		if (menu)
		{
			menustate = MENU_NONE1;
		}
		if (select)
		{
			if (menusub == menusub_last)
			{
				menustate = MENU_NONE1;
			}
			else
			{
				static char ext[256];
				p = user_io_8bit_get_string(1);

				int h = 0, d = 0;
				uint32_t entry = 0;
				int i = 1;
				while (1)
				{
					p = user_io_8bit_get_string(i++);
					if (!p) continue;

					h = 0;
					d = 0;

					//Hide or Disable flag
					while ((p[0] == 'H' || p[0] == 'D') && strlen(p) > 2)
					{
						int flg = (hdmask & (1 << getIdx(p))) ? 1 : 0;
						if (p[0] == 'H') h |= flg; else d |= flg;
						p += 2;
					}

					if (h || p[0] < 'A') continue;
					if (entry == menusub) break;
					entry++;
				}

				if (!d)
				{
					if (p[0] == 'C' && cheats_available())
					{
						menustate = MENU_CHEATS1;
						cheatsub = menusub;
						menusub = 0;
					}
					else if (p[0] == 'F')
					{
						opensave = 0;
						ioctl_index = menusub + 1;
						int idx = 1;

						if (p[1] == 'S')
						{
							opensave = 1;
							idx++;
						}

						if (p[idx] >= '0' && p[idx] <= '9') ioctl_index = p[idx] - '0';
						substrcpy(ext, p, 1);
						while (strlen(ext) % 3) strcat(ext, " ");
						SelectFile(ext, SCANO_DIR | (is_neogeo_core() ? SCANO_NEOGEO | SCANO_NOENTER : 0), MENU_8BIT_MAIN_FILE_SELECTED, MENU_8BIT_MAIN1);
					}
					else if (p[0] == 'S')
					{
						drive_num = 0;
						if (p[1] >= '0' && p[1] <= '3') drive_num = p[1] - '0';
						substrcpy(ext, p, 1);
						while (strlen(ext) % 3) strcat(ext, " ");
						SelectFile(ext, SCANO_DIR | SCANO_UMOUNT, MENU_8BIT_MAIN_IMAGE_SELECTED, MENU_8BIT_MAIN1);
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
						uint32_t x = getStatus(p, status) + 1;

						if (byarm && is_x86_core())
						{
							if (p[1] == '2') x86_set_fdd_boot(!(x & 1));
						}
						// check if next value available
						substrcpy(s, p, 2 + x);
						if (!strlen(s)) x = 0;

						user_io_8bit_set_status(setStatus(p, status, x), 0xffffffff, ex);

						menustate = MENU_8BIT_MAIN1;
					}
					else if ((p[0] == 'T') || (p[0] == 'R') || (p[0] == 't') || (p[0] == 'r'))
					{
						int ex = (p[0] == 't') || (p[0] == 'r');

						// determine which status bit is affected
						uint32_t mask = 1 << getIdx(p);
						if (mask == 1 && is_x86_core())
						{
							x86_init();
							menustate = MENU_NONE1;
						}
						else
						{
							uint32_t status = user_io_8bit_set_status(0, 0, ex);

							user_io_8bit_set_status(status ^ mask, mask, ex);
							user_io_8bit_set_status(status, mask, ex);
							menustate = MENU_8BIT_MAIN1;
							if (p[0] == 'R') menustate = MENU_NONE1;
						}
					}
				}
			}
		}
		else if (right)
		{
			menustate = MENU_8BIT_SYSTEM1;
			menusub = 0;
		}
		else if (left)
		{
			menustate = MENU_8BIT_INFO;
			menusub = 1;
		}
		break;

	case MENU_8BIT_MAIN_FILE_SELECTED:
		printf("File selected: %s\n", SelectedPath);
		if (fs_Options & SCANO_NEOGEO)
		{
			menustate = MENU_NONE1;
			HandleUI();
			neogeo_romset_tx(SelectedPath);
		}
		else
		{
			user_io_store_filename(SelectedPath);
			user_io_file_tx(SelectedPath, user_io_ext_idx(SelectedPath, fs_pFileExt) << 6 | ioctl_index, opensave);
			if (user_io_use_cheats()) cheats_init(SelectedPath, user_io_get_file_crc());
			menustate = MENU_NONE1;
		}
		break;

	case MENU_8BIT_MAIN_IMAGE_SELECTED:
		printf("Image selected: %s\n", SelectedPath);
		if (is_x86_core())
		{
			x86_set_image(drive_num, SelectedPath);
		}
		else
		{
			user_io_set_index(user_io_ext_idx(SelectedPath, fs_pFileExt) << 6 | (menusub + 1));
			user_io_file_mount(SelectedPath, drive_num);
		}

		if (is_neogeo_core())
		{
			// ElectronAsh.
			strcpy(SelectedPath + strlen(SelectedPath) - 3, "CUE");
			printf("Checking for presence of CUE file %s\n", SelectedPath);
			if (user_io_file_mount(SelectedPath, 2))
			{
				printf("CUE file found and mounted.\n");
				parse_cue_file();
				char str[2] = "";
				neogeo_romset_tx(str);
			}
		}

		menustate = SelectedPath[0] ? MENU_NONE1 : MENU_8BIT_MAIN1;
		break;

	case MENU_8BIT_SYSTEM1:
		{
			OsdSetSize(16);
			helptext = 0;
			reboot_req = 0;

			OsdSetTitle("System", 0);
			menustate = MENU_8BIT_SYSTEM2;
			parentstate = MENU_8BIT_SYSTEM1;
			int n;

			while(1)
			{
				n = 0;
				menumask = 0x3e07;

				if (!menusub) firstmenu = 0;
				adjvisible = 0;

				MenuWrite(n++);
				MenuWrite(n++, " Core                      \x16", menusub == 0, 0);
				sprintf(s, " Define %s buttons         ", is_menu_core() ? "System" : user_io_get_core_name_ex());
				s[27] = '\x16';
				s[28] = 0;
				MenuWrite(n++, s, menusub == 1, 0);
				MenuWrite(n++, " Button/Key remap for game \x16", menusub == 2, 0);

				if (user_io_get_uart_mode())
				{
					menumask |= 0x8;
					MenuWrite(n++);
					const char *p = config_uart_msg[GetUARTMode()];
					while (*p == ' ') p++;
					sprintf(s, " UART mode (%s)            ",p);
					s[27] = '\x16';
					s[28] = 0;
					MenuWrite(n++, s, menusub == 3);
				}

				if (video_get_scaler_flt() >= 0 && !cfg.direct_video)
				{
					MenuWrite(n++);
					menumask |= 0x60;
					sprintf(s, " Scale Filter - %s", config_scaler_msg[video_get_scaler_flt() ? 1 : 0]);
					MenuWrite(n++, s, menusub == 5);

					memset(s, 0, sizeof(s));
					s[0] = ' ';
					if (strlen(video_get_scaler_coeff())) strncpy(s+1, video_get_scaler_coeff(),25);
					else strcpy(s, " < none >");

					while(strlen(s) < 26) strcat(s, " ");
					strcat(s, " \x16 ");

					MenuWrite(n++, s, menusub == 6, !video_get_scaler_flt() || !S_ISDIR(getFileType(COEFF_DIR)));
				}

				if (video_get_gamma_en() >=0 && !cfg.direct_video)
				{
					MenuWrite(n++);
					menumask |= 0x180;
					sprintf(s, " Gamma Correction - %s", config_gamma_msg[video_get_gamma_en() ? 1 : 0]);
					MenuWrite(n++, s, menusub == 7);

					memset(s, 0, sizeof(s));
					s[0] = ' ';
					if (strlen(video_get_gamma_curve())) strncpy(s+1, video_get_gamma_curve(),25);
					else strcpy(s, " < none >");

					while(strlen(s) < 26) strcat(s, " ");
					strcat(s, " \x16 ");

					MenuWrite(n++, s, menusub == 8, !video_get_gamma_en() || !S_ISDIR(getFileType(GAMMA_DIR)));
				}

				m = 0;
				if (is_minimig())
				{
					m = 1;
					menumask &= ~0x400;
				}
				MenuWrite(n++);
				MenuWrite(n++, m ? " Reset the core" : " Reset settings", menusub == 9, user_io_core_type() == CORE_TYPE_ARCHIE);
				MenuWrite(n++, m ? "" : " Save settings", menusub == 10, 0);

				MenuWrite(n++);
				cr = n;
				MenuWrite(n++, " Reboot (hold \x16 cold reboot)", menusub == 11);
				MenuWrite(n++, " About", menusub == 12);

				while(n < OsdGetSize() - 1) MenuWrite(n++);
				MenuWrite(n++, STD_EXIT, menusub == 13, 0, OSD_ARROW_LEFT);
				sysinfo_timer = 0;

				if (!adjvisible) break;
				firstmenu += adjvisible;
			}

		}
		break;

	case MENU_8BIT_SYSTEM2:
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

		if (select)
		{
			switch (menusub)
			{
			case 0:
				SelectFile(0, SCANO_CORES, MENU_CORE_FILE_SELECTED1, MENU_8BIT_SYSTEM1);
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
				{
					menustate = MENU_UART1;

					struct stat filestat;
					int mode = GetUARTMode();

					//jump straght to Softsynth selection if enabled
					menusub = ((mode != 3 && mode != 4) || !stat("/dev/midi", &filestat)) ? 0 : 2;
				}
				break;

			case 5:
				video_set_scaler_flt(video_get_scaler_flt() ? 0 : 1);
				menustate = MENU_8BIT_SYSTEM1;
				break;

			case 6:
				if (video_get_scaler_flt())
				{
					sprintf(SelectedPath, COEFF_DIR"/%s", video_get_scaler_coeff());
					SelectFile(0, SCANO_DIR | SCANO_TXT, MENU_COEFF_FILE_SELECTED, MENU_8BIT_SYSTEM1);
				}
				break;
			case 7:
				video_set_gamma_en(video_get_gamma_en() ? 0 : 1);
				menustate = MENU_8BIT_SYSTEM1;
				break;

			case 8:
				if (video_get_gamma_en())
				{
					sprintf(SelectedPath, GAMMA_DIR"/%s", video_get_gamma_curve());
					SelectFile(0, SCANO_DIR | SCANO_TXT, MENU_GAMMA_FILE_SELECTED, MENU_8BIT_SYSTEM1);
				}
				break;
			case 9:
				if (user_io_core_type() != CORE_TYPE_ARCHIE)
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

			case 10:
				// Save settings
				menustate = MENU_8BIT_MAIN1;
				menusub = 0;

				if (user_io_core_type() == CORE_TYPE_ARCHIE)
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
					if (is_x86_core()) x86_config_save();
				}
				break;

			case 11:
				{
					reboot_req = 1;

					int off = hold_cnt/3;
					if (off > 5) reboot(1);

					sprintf(s, " Cold Reboot");
					p = s + 5 - off;
					MenuWrite(cr, p, menusub == 11, 0);
				}
				break;

			case 12:
				menustate = MENU_8BIT_ABOUT1;
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
			switch (user_io_core_type()) {
			case CORE_TYPE_MIST:
				menusub = 5;
				menustate = MENU_MIST_MAIN1;
				break;
			case CORE_TYPE_ARCHIE:
				menusub = 0;
				menustate = MENU_ARCHIE_MAIN1;
				break;
			case CORE_TYPE_8BIT:
				if (is_minimig())
				{
					menusub = 0;
					menustate = MENU_MAIN1;
				}
				else
				{
					menusub = 0;
					menustate = MENU_8BIT_MAIN1;
				}
				break;
			case CORE_TYPE_SHARPMZ:
				menusub   = menusub_last;
				menustate = sharpmz_default_ui_state();
				break;
			}
		}

		if(!hold_cnt && reboot_req) fpga_load_rbf("menu.rbf");
		break;

	case MENU_UART1:
		{
			helptext = 0;
			menumask = 0x3F;

			OsdSetTitle("UART mode");
			menustate = MENU_UART2;
			parentstate = MENU_UART1;

			struct stat filestat;
			int hasmidi = !stat("/dev/midi1", &filestat);
			int mode = GetUARTMode();
			int midilink = GetMidiLinkMode();
			int m = (mode != 3 && mode != 4) || hasmidi;

			OsdWrite(0);
			sprintf(s, " Connection:       %s", config_uart_msg[mode]);
			OsdWrite(1, s, menusub == 0, 0);
			OsdWrite(2);

			sprintf(s, " MidiLink:            %s", (midilink & 2) ? "Remote" : " Local");
			OsdWrite(3, s, menusub == 1, m);
			sprintf(s, " Type:            %s", (midilink & 2) ? ((midilink & 1) ? "       UDP" : "       TCP") : ((midilink & 1) ? "      MUNT" : "FluidSynth"));
			OsdWrite(4, s, menusub == 2, m);

			OsdWrite(5);
			OsdWrite(6, " Reset UART connection", menusub == 3, mode?0:1);
			OsdWrite(7);
			OsdWrite(8, " Save", menusub == 4);

			for (int i = 9; i < 15; i++) OsdWrite(i);
			OsdWrite(15, STD_EXIT, menusub == 5);
		}
		break;

	case MENU_UART2:
		if (menu)
		{
			menustate = MENU_8BIT_SYSTEM1;
			menusub = 3;
			break;
		}

		if (select)
		{
			switch (menusub)
			{
			case 0:
				{
					uint mode = GetUARTMode() + 1;
					if (mode > sizeof(config_uart_msg) / sizeof(config_uart_msg[0])) mode = 0;

					sprintf(s, "uartmode %d", mode);
					system(s);
					menustate = MENU_UART1;
				}
				break;

			case 1:
			case 2:
				if (!m)
				{
					int mode = GetUARTMode();
					SetMidiLinkMode(GetMidiLinkMode() ^ ((menusub == 1) ? 2 : 1));
					sprintf(s, "uartmode %d", 0);
					system(s);
					sprintf(s, "uartmode %d", mode);
					system(s);
					menustate = MENU_UART1;
				}
				break;
			case 3:
				{
					int mode = GetUARTMode();
					if(mode != 0)
					{
						sprintf(s, "uartmode %d", 0);
						system(s);
						sprintf(s, "uartmode %d", mode);
						system(s);
						menustate = MENU_8BIT_SYSTEM1;
					}
				}
				break;
			case 4:
				{
					int mode = GetUARTMode() | (GetMidiLinkMode() << 8);
					sprintf(s, "uartmode.%s", user_io_get_core_name_ex());
					FileSaveConfig(s, &mode, 4);
					menustate = MENU_8BIT_SYSTEM1;
					menusub = 3;
				}
				break;

			default:
				menustate = MENU_NONE1;
				break;
			}
		}
		break;

	case MENU_COEFF_FILE_SELECTED:
		{
			char *p = strcasestr(SelectedPath, COEFF_DIR"/");
			if (!p) video_set_scaler_coeff(SelectedPath);
			else video_set_scaler_coeff(p + strlen(COEFF_DIR) + 1);
			menustate = MENU_8BIT_SYSTEM1;
		}
		break;
	case MENU_GAMMA_FILE_SELECTED:
		{
			char *p = strcasestr(SelectedPath, GAMMA_DIR"/");
			if (!p) video_set_gamma_curve(SelectedPath);
			else video_set_gamma_curve(p + strlen(GAMMA_DIR) + 1);
			menustate = MENU_8BIT_SYSTEM1;
		}
		break;
	case MENU_8BIT_INFO:
		OsdSetSize(16);
		helptext = 0;
		menumask = 3;
		menustate = MENU_8BIT_INFO2;
		OsdSetTitle("System", OSD_ARROW_RIGHT);

		if(parentstate != MENU_8BIT_INFO) for (int i = 0; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		parentstate = MENU_8BIT_INFO;

		OsdWrite(3, "         Information");

		m = get_volume();
		strcpy(s, "      Volume: ");
		if (m & 0x10)
		{
			strcat(s, "< Mute >");
		}
		else
		{
			memset(s+strlen(s), 0, 10);
			char *bar = s + strlen(s);
			memset(bar, 0x8C, 8);
			memset(bar, 0x7f, 8 - m);
		}

		OsdWrite(13, s, menusub == 0, !cfg.volumectl);
		OsdWrite(15, STD_EXIT, menusub == 1, 0, OSD_ARROW_RIGHT);
		break;

	case MENU_8BIT_INFO2:
		printSysInfo();
		if ((select && menusub == 1) || menu)
		{
			menustate = MENU_NONE1;
			break;
		}
		else if(menusub == 0 && (right || left || select))
		{
			set_volume(right ? 1 : left ? -1 : 0);
			menustate = MENU_8BIT_INFO;
		}
		else if (right)
		{
			// go back to core requesting this menu
			switch (user_io_core_type())
			{
			case CORE_TYPE_MIST:
				menusub = 5;
				menustate = MENU_MIST_MAIN1;
				break;
			case CORE_TYPE_ARCHIE:
				menusub = 0;
				menustate = MENU_ARCHIE_MAIN1;
				break;
			case CORE_TYPE_8BIT:
				if (is_minimig())
				{
					menusub = 0;
					menustate = MENU_MAIN1;
				}
				else
				{
					menusub = 0;
					menustate = MENU_8BIT_MAIN1;
				}
				break;
			case CORE_TYPE_SHARPMZ:
				menusub = menusub_last;
				menustate = sharpmz_default_ui_state();
				break;
			}
		}
		break;

	case MENU_JOYDIGMAP:
		helptext = 0;
		menumask = 1;
		OsdSetTitle("Define buttons", 0);
		menustate = MENU_JOYDIGMAP1;
		parentstate = MENU_JOYDIGMAP;
		flash_timer = 0;
		flash_state = 0;
		for (int i = 0; i < OsdGetSize(); i++) OsdWrite(i);
		if (is_menu_core())
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

			if (is_menu_core() && !get_map_button()) OsdWrite(7);

			const char* p = 0;
			if (get_map_button() < 0)
			{
				strcpy(s, joy_ana_map[get_map_button() + 6]);
				OsdWrite(7, "        Space \x16 Skip");
			}
			else if (get_map_button() < DPAD_NAMES)
			{
				p = joy_button_map[get_map_button()];
			}
			else if (joy_bcount)
			{
				p = joy_bnames[get_map_button() - DPAD_NAMES];
				if (is_menu_core())
				{
					if (get_map_type()) joy_bcount = 19;
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
				if (is_menu_core() && get_map_button() > SYS_BTN_OSD_KTGL)
				{
					strcpy(s, joy_button_map[(get_map_button() - SYS_BTN_OSD_KTGL - 1) + DPAD_BUTTON_NAMES + 2]);
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

			if(is_menu_core() && joy_bcount && get_map_button() >= SYS_BTN_RIGHT && get_map_button() <= SYS_BTN_START)
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
				if (!is_menu_core() && get_map_type() && !has_default_map())
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
					if (get_map_button() > 0)
					{
						OsdWrite(7, get_map_type() ? "   Space/Menu \x16 Undefine" : "        Space \x16 Undefine");
						if (!get_map_type()) OsdWrite(9);
					}
					OsdWrite(5, s);
					if (!is_menu_core()) OsdWrite(10, "          F12 \x16 Clear all");
				}
			}

			if (!is_menu_core() && (get_map_button() >= (joy_bcount ? joy_bcount + 4 : 8) || (select & get_map_vid() & get_map_pid())) && joymap_first && get_map_type())
			{
				finish_map_setting(0);
				menustate = MENU_JOYDIGMAP3;
				menusub = 0;
			}
			else if (select || menu || get_map_button() >= (joy_bcount ? joy_bcount + 4 : 8))
			{
				finish_map_setting(menu);
				if (is_menu_core())
				{
					menustate = MENU_SYSTEM1;
					menusub = 2;
				}
				else
				{
					menustate = MENU_8BIT_SYSTEM1;
					menusub = 1;
				}
			}
		}
		break;

	case MENU_JOYDIGMAP2:
		if (select || menu)
		{
			menustate = MENU_8BIT_SYSTEM1;
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
			menustate = MENU_8BIT_SYSTEM1;
			menusub = 1;
			break;
		}
		else if (select)
		{
			switch (menusub)
			{
			case 0:
				menustate = MENU_8BIT_SYSTEM1;
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
		helptext = 0;
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
			menustate = MENU_8BIT_SYSTEM1;
			menusub = 2;
		}
		break;

	case MENU_8BIT_ABOUT1:
		OsdSetSize(16);
		menumask = 0;
		helptext = helptexts[HELPTEXT_NONE];
		OsdSetTitle("About", 0);
		menustate = MENU_8BIT_ABOUT2;
		parentstate = MENU_8BIT_ABOUT1;
		StarsInit();
		ScrollReset();
		for (int i = 5; i < OsdGetSize(); i++) OsdWrite(i, "", 0, 0);
		break;

	case MENU_8BIT_ABOUT2:
		StarsUpdate();
		OsdDrawLogo(0);
		OsdDrawLogo(1);
		OsdDrawLogo(2);
		OsdDrawLogo(3);
		OsdDrawLogo(4);

		OsdWrite(10, "      www.MiSTerFPGA.org");
		sprintf(s, "       MiSTer v%s", version + 5);
		OsdWrite(12, s, 0, 0, 1);

		s[0] = 0;
		{
			int len = strlen(OsdCoreName());
			if (len > 30) len = 30;
			int sp = (30 - len) / 2;
			for (int i = 0; i < sp; i++) strcat(s, " ");
			char *s2 = s + strlen(s);
			char *s3 = OsdCoreName();
			for (int i = 0; i < len; i++) *s2++ = *s3++;
			*s2++ = 0;
		}
		OsdWrite(13, s, 0, 0, 1);
		OsdWrite(14, "", 0, 0, 1);
		ScrollText(15, "                                 MiSTer by Sorgelig, based on MiST by Till Harbaum, Minimig by Dennis van Weeren and other projects. MiSTer hardware and software is distributed under the terms of the GNU General Public License version 3. MiSTer FPGA cores are the work of their respective authors under individual licensing. Go to www.MiSTerFPGA.org for more details.", 0, 0, 0, 0);

		if (menu | select | left)
		{
			menustate = MENU_8BIT_SYSTEM1;
			menusub = 7 - m;
		}
		break;


		/******************************************************************/
		/* mist main menu                                                 */
		/******************************************************************/

	case MENU_MIST_MAIN1:
		OsdSetSize(8);
		menumask = 0xff;
		OsdSetTitle("Mist", 0);

		// most important: main page has setup for floppy A: and screen
		strcpy(s, " A: ");
		strcat(s, tos_get_disk_name(0));
		if (tos_system_ctrl() & TOS_CONTROL_FDC_WR_PROT_A) strcat(s, " \x17");
		OsdWrite(0, s, menusub == 0, 0);

		strcpy(s, " Screen: ");
		if (tos_system_ctrl() & TOS_CONTROL_VIDEO_COLOR) strcat(s, "Color");
		else                                          strcat(s, "Mono");
		OsdWrite(1, s, menusub == 1, 0);

		/* everything else is in submenus */
		OsdWrite(2, " Storage                   \x16", menusub == 2, 0);
		OsdWrite(3, " System                    \x16", menusub == 3, 0);
		OsdWrite(4, " Audio / Video             \x16", menusub == 4, 0);
		OsdWrite(5, " Firmware & Core           \x16", menusub == 5, 0);

		OsdWrite(6, " Save config                ", menusub == 6, 0);

		OsdWrite(7, STD_EXIT, menusub == 7, 0);

		menustate = MENU_MIST_MAIN2;
		parentstate = MENU_MIST_MAIN1;
		break;

	case MENU_MIST_MAIN2:
		// menu key closes menu
		if (menu)
			menustate = MENU_NONE1;
		if (select) {
			switch (menusub) {
			case 0:
				if (tos_disk_is_inserted(0)) {
					tos_insert_disk(0, NULL);
					menustate = MENU_MIST_MAIN1;
				}
				else
					SelectFile("ST ", SCANO_DIR, MENU_MIST_MAIN_FILE_SELECTED, MENU_MIST_MAIN1);
				break;

			case 1:
				tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_VIDEO_COLOR);
				menustate = MENU_MIST_MAIN1;
				break;

			case 2:  // Storage submenu
				menustate = MENU_MIST_STORAGE1;
				menusub = 0;
				break;

			case 3:  // System submenu
				menustate = MENU_MIST_SYSTEM1;
				menusub = 0;
				break;

			case 4:  // Video submenu
				menustate = MENU_MIST_VIDEO1;
				menusub = 0;
				break;

			case 5:  // Firmware submenu
				//menustate = MENU_FIRMWARE1;
				menusub = 0;
				break;

			case 6:  // Save config
				menustate = MENU_NONE1;
				tos_config_save();
				break;

			case 7:  // Exit
				menustate = MENU_NONE1;
				break;
			}
		}
		break;

	case MENU_MIST_MAIN_FILE_SELECTED: // file successfully selected
		tos_insert_disk(0, SelectedPath);
		menustate = MENU_MIST_MAIN1;
		break;

	case MENU_MIST_STORAGE1:
		menumask = tos_get_direct_hdd() ? 0x3f : 0x7f;
		OsdSetTitle("Storage", 0);
		// entries for both floppies
		for (uint32_t i = 0; i<2; i++) {
			strcpy(s, " A: ");
			strcat(s, tos_get_disk_name(i));
			s[1] = 'A' + i;
			if (tos_system_ctrl() & (TOS_CONTROL_FDC_WR_PROT_A << i))
				strcat(s, " \x17");
			OsdWrite(i, s, menusub == i, 0);
		}
		strcpy(s, " Write protect: ");
		strcat(s, config_tos_wrprot[(tos_system_ctrl() >> 6) & 3]);
		OsdWrite(2, s, menusub == 2, 0);
		OsdWrite(3, "", 0, 0);
		strcpy(s, " ACSI0 direct SD: ");
		strcat(s, tos_get_direct_hdd() ? "on" : "off");
		OsdWrite(4, s, menusub == 3, 0);
		for (uint32_t i = 0; i<2; i++) {
			strcpy(s, " ACSI0: ");
			s[5] = '0' + i;

			strcat(s, tos_get_disk_name(2 + i));
			OsdWrite(5 + i, s, ((i == 1) || !tos_get_direct_hdd()) ? (menusub == (!tos_get_direct_hdd() ? 4 : 3) + i) : 0,
				(i == 0) && tos_get_direct_hdd());
		}
		OsdWrite(7, STD_EXIT, !tos_get_direct_hdd() ? (menusub == 6) : (menusub == 5), 0);
		parentstate = menustate;
		menustate = MENU_MIST_STORAGE2;
		break;


	case MENU_MIST_STORAGE2:
		if (menu) {
			menustate = MENU_MIST_MAIN1;
			menusub = 2;
		}
		if (select) {
			if (menusub <= 1) {
				if (tos_disk_is_inserted(menusub)) {
					tos_insert_disk(menusub, NULL);
					menustate = MENU_MIST_STORAGE1;
				}
				else
					SelectFile("ST ", SCANO_DIR, MENU_MIST_STORAGE_FILE_SELECTED, MENU_MIST_STORAGE1);
			}
			else if (menusub == 2) {
				// remove current write protect bits and increase by one
				tos_update_sysctrl((tos_system_ctrl() & ~(TOS_CONTROL_FDC_WR_PROT_A | TOS_CONTROL_FDC_WR_PROT_B))
					| (((((tos_system_ctrl() >> 6) & 3) + 1) & 3) << 6));
				menustate = MENU_MIST_STORAGE1;

			}
			else if (menusub == 3) {
				tos_set_direct_hdd(!tos_get_direct_hdd());
				menustate = MENU_MIST_STORAGE1;

				// no direct hhd emulation: Both ACSI entries are enabled
				// or direct hhd emulation for ACSI0: Only second ACSI entry is enabled
			}
			else if ((menusub == 4) || (!tos_get_direct_hdd() && (menusub == 5))) {
				char disk_idx = menusub - (tos_get_direct_hdd() ? 1 : 2);
				printf("Select image for disk %d\n", disk_idx);

				if (tos_disk_is_inserted(disk_idx)) {
					tos_insert_disk(disk_idx, NULL);
					menustate = MENU_MIST_STORAGE1;
				}
				else
					SelectFile("HD ", 0, MENU_MIST_STORAGE_FILE_SELECTED, MENU_MIST_STORAGE1);

			}
			else if (tos_get_direct_hdd() ? (menusub == 5) : (menusub == 6)) {
				menustate = MENU_MIST_MAIN1;
				menusub = 2;
			}
		}
		break;

	case MENU_MIST_STORAGE_FILE_SELECTED: // file successfully selected
										  // floppy/hdd
		if (menusub < 2)
			tos_insert_disk(menusub, SelectedPath);
		else {
			char disk_idx = menusub - (tos_get_direct_hdd() ? 1 : 2);
			printf("Insert image for disk %d\n", disk_idx);
			tos_insert_disk(disk_idx, SelectedPath);
		}
		menustate = MENU_MIST_STORAGE1;
		break;

	case MENU_MIST_SYSTEM1:
		menumask = 0xff;
		OsdSetTitle("System", 0);

		strcpy(s, " Memory:    ");
		strcat(s, config_tos_mem[(tos_system_ctrl() >> 1) & 7]);
		OsdWrite(0, s, menusub == 0, 0);

		strcpy(s, " CPU:       ");
		strcat(s, config_cpu_msg[(tos_system_ctrl() >> 4) & 3]);
		OsdWrite(1, s, menusub == 1, 0);

		strcpy(s, " TOS:       ");
		strcat(s, tos_get_image_name());
		OsdWrite(2, s, menusub == 2, 0);

		strcpy(s, " Cartridge: ");
		strcat(s, tos_get_cartridge_name());
		OsdWrite(3, s, menusub == 3, 0);

		strcpy(s, " USB I/O:   ");
		strcat(s, "NONE"); //config_tos_usb[tos_get_cdc_control_redirect()]);
		OsdWrite(4, s, menusub == 4, 0);

		OsdWrite(5, " Reset", menusub == 5, 0);
		OsdWrite(6, " Cold boot", menusub == 6, 0);

		OsdWrite(7, STD_EXIT, menusub == 7, 0);

		parentstate = menustate;
		menustate = MENU_MIST_SYSTEM2;
		break;

	case MENU_MIST_SYSTEM2:
		if (menu) {
			menustate = MENU_MIST_MAIN1;
			menusub = 3;
		}
		if (select) {
			switch (menusub) {
			case 0: { // RAM
				int mem = (tos_system_ctrl() >> 1) & 7;   // current memory config
				mem++;
				if (mem > 5) mem = 3;                 // cycle 4MB/8MB/14MB
				tos_update_sysctrl((tos_system_ctrl() & ~0x0e) | (mem << 1));
				tos_reset(1);
				menustate = MENU_MIST_SYSTEM1;
			} break;

			case 1: { // CPU
				int cpu = (tos_system_ctrl() >> 4) & 3;   // current cpu config
				cpu = (cpu + 1) & 3;
				if (cpu == 2) cpu = 3;                 // skip unused config
				tos_update_sysctrl((tos_system_ctrl() & ~0x30) | (cpu << 4));
				tos_reset(0);
				menustate = MENU_MIST_SYSTEM1;
			} break;

			case 2:  // TOS
				SelectFile("IMG", 0, MENU_MIST_SYSTEM_FILE_SELECTED, MENU_MIST_SYSTEM1);
				break;

			case 3:  // Cart
					 // if a cart name is set, then remove it
				if (tos_cartridge_is_inserted()) {
					tos_load_cartridge("");
					menustate = MENU_MIST_SYSTEM1;
				}
				else
				{
					SelectFile("IMG", 0, MENU_MIST_SYSTEM_FILE_SELECTED, MENU_MIST_SYSTEM1);
				}
				break;

			case 4:
				menustate = MENU_MIST_SYSTEM1;
				break;

			case 5:  // Reset
				tos_reset(0);
				menustate = MENU_NONE1;
				break;

			case 6:  // Cold Boot
				tos_reset(1);
				menustate = MENU_NONE1;
				break;

			case 7:
				menustate = MENU_MIST_MAIN1;
				menusub = 3;
				break;
			}
		}
		break;

	case MENU_MIST_SYSTEM_FILE_SELECTED: // file successfully selected
		if (menusub == 2) {
			tos_upload(SelectedPath);
			menustate = MENU_MIST_SYSTEM1;
		}
		if (menusub == 3) {
			tos_load_cartridge(SelectedPath);
			menustate = MENU_MIST_SYSTEM1;
		}
		break;


	case MENU_MIST_VIDEO1:
		menumask = 0x7f;
		OsdSetTitle("A/V", 0);

		strcpy(s, " Screen:        ");
		if (tos_system_ctrl() & TOS_CONTROL_VIDEO_COLOR) strcat(s, "Color");
		else                                            strcat(s, "Mono");
		OsdWrite(0, s, menusub == 0, 0);

		// Viking card can only be enabled with max 8MB RAM
		enable = (tos_system_ctrl() & 0xe) <= TOS_MEMCONFIG_8M;
		strcpy(s, " Viking/SM194:  ");
		strcat(s, ((tos_system_ctrl() & TOS_CONTROL_VIKING) && enable) ? "on" : "off");
		OsdWrite(1, s, menusub == 1, enable ? 0 : 1);

		// Blitter is always present in >= STE
		enable = (tos_system_ctrl() & (TOS_CONTROL_STE | TOS_CONTROL_MSTE)) ? 1 : 0;
		strcpy(s, " Blitter:       ");
		strcat(s, ((tos_system_ctrl() & TOS_CONTROL_BLITTER) || enable) ? "on" : "off");
		OsdWrite(2, s, menusub == 2, enable);

		strcpy(s, " Chipset:       ");
		// extract  TOS_CONTROL_STE and  TOS_CONTROL_MSTE bits
		strcat(s, atari_chipset[(tos_system_ctrl() >> 23) & 3]);
		OsdWrite(3, s, menusub == 3, 0);

		OsdWrite(4, " Video adjust              \x16", menusub == 4, 0);

		strcpy(s, " YM-Audio:      ");
		strcat(s, stereo[(tos_system_ctrl() & TOS_CONTROL_STEREO) ? 1 : 0]);
		OsdWrite(5, s, menusub == 5, 0);
		OsdWrite(6, "", 0, 0);

		OsdWrite(7, STD_EXIT, menusub == 6, 0);

		parentstate = menustate;
		menustate = MENU_MIST_VIDEO2;
		break;

	case MENU_MIST_VIDEO2:
		if (menu) {
			menustate = MENU_MIST_MAIN1;
			menusub = 4;
		}

		if (select) {
			switch (menusub) {
			case 0:
				tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_VIDEO_COLOR);
				menustate = MENU_MIST_VIDEO1;
				break;

			case 1:
				// viking/sm194
				tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_VIKING);
				menustate = MENU_MIST_VIDEO1;
				break;

			case 2:
				if (!(tos_system_ctrl() & TOS_CONTROL_STE)) {
					tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_BLITTER);
					menustate = MENU_MIST_VIDEO1;
				}
				break;

			case 3: {
				unsigned long chipset = (tos_system_ctrl() >> 23) + 1;
				if (chipset == 4) chipset = 0;
				tos_update_sysctrl((tos_system_ctrl() & ~(TOS_CONTROL_STE | TOS_CONTROL_MSTE)) | (chipset << 23));
				menustate = MENU_MIST_VIDEO1;
			} break;

			case 4:
				menustate = MENU_MIST_VIDEO_ADJUST1;
				menusub = 0;
				break;

			case 5:
				tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_STEREO);
				menustate = MENU_MIST_VIDEO1;
				break;

			case 6:
				menustate = MENU_MIST_MAIN1;
				menusub = 4;
				break;
			}
		}
		break;

	case MENU_MIST_VIDEO_ADJUST1:
		menumask = 0x1f;
		OsdSetTitle("V-adjust", 0);

		OsdWrite(0, "", 0, 0);

		strcpy(s, " PAL mode:    ");
		if (tos_system_ctrl() & TOS_CONTROL_PAL50HZ) strcat(s, "50Hz");
		else                                      strcat(s, "56Hz");
		OsdWrite(1, s, menusub == 0, 0);

		strcpy(s, " Scanlines:   ");
		strcat(s, scanlines[(tos_system_ctrl() >> 20) & 3]);
		OsdWrite(2, s, menusub == 1, 0);

		OsdWrite(3, "", 0, 0);

		sprintf(s, " Horizontal:  %d", tos_get_video_adjust(0));
		OsdWrite(4, s, menusub == 2, 0);

		sprintf(s, " Vertical:    %d", tos_get_video_adjust(1));
		OsdWrite(5, s, menusub == 3, 0);

		OsdWrite(6, "", 0, 0);

		OsdWrite(7, STD_EXIT, menusub == 4, 0);

		parentstate = menustate;
		menustate = MENU_MIST_VIDEO_ADJUST2;
		break;

	case MENU_MIST_VIDEO_ADJUST2:
		if (menu) {
			menustate = MENU_MIST_VIDEO1;
			menusub = 4;
		}

		// use left/right to adjust video position
		if (left || right) {
			if ((menusub == 2) || (menusub == 3)) {
				if (left && ((signed char)(tos_get_video_adjust(menusub - 2)) > -100))
					tos_set_video_adjust(menusub - 2, -1);

				if (right && ((signed char)(tos_get_video_adjust(menusub - 2)) < 100))
					tos_set_video_adjust(menusub - 2, +1);

				menustate = MENU_MIST_VIDEO_ADJUST1;
			}
		}

		if (select) {
			switch (menusub) {
			case 0:
				tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_PAL50HZ);
				menustate = MENU_MIST_VIDEO_ADJUST1;
				break;

			case 1: {
				// next scanline state
				int scan = ((tos_system_ctrl() >> 20) + 1) & 3;
				tos_update_sysctrl((tos_system_ctrl() & ~TOS_CONTROL_SCANLINES) | (scan << 20));
				menustate = MENU_MIST_VIDEO_ADJUST1;
			} break;

				// entries 2 and 3 use left/right

			case 4:
				menustate = MENU_MIST_VIDEO1;
				menusub = 4;
				break;
			}
		}
		break;

		/******************************************************************/
		/* minimig main menu                                              */
		/******************************************************************/
	case MENU_MAIN1:
		menumask = 0xFF0;	// b01110000 Floppy turbo, Harddisk options & Exit.
		OsdSetTitle("Minimig", OSD_ARROW_RIGHT | OSD_ARROW_LEFT);
		helptext = helptexts[HELPTEXT_MAIN];

		OsdWrite(0, "", 0, 0);

		// floppy drive info
		// We display a line for each drive that's active
		// in the config file, but grey out any that the FPGA doesn't think are active.
		// We also print a help text in place of the last drive if it's inactive.
		for (int i = 0; i < 4; i++)
		{
			if (i == minimig_config.floppy.drives + 1)
				OsdWrite(i+1, " KP +/- to add/remove drives", 0, 1);
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
				OsdWrite(i+1, s, menusub == (uint32_t)i, (i>drives) || (i>minimig_config.floppy.drives));
			}
		}
		sprintf(s, " Floppy disk turbo : %s", minimig_config.floppy.speed ? "on" : "off");
		OsdWrite(5, s, menusub == 4, 0);
		OsdWrite(6, "", 0, 0);

		OsdWrite(7,  " Hard disks", menusub == 5, 0);
		OsdWrite(8,  " Chipset", menusub == 6, 0);
		OsdWrite(9,  " Memory", menusub == 7, 0);
		OsdWrite(10, " Audio & Video", menusub == 8, 0);
		OsdWrite(11, "", 0, 0);

		OsdWrite(12, " Save configuration", menusub == 9, 0);
		OsdWrite(13, " Load configuration", menusub == 10, 0);
		OsdWrite(14, "", 0, 0);

		OsdWrite(15, STD_EXIT, menusub == 11, 0);

		menustate = MENU_MAIN2;
		parentstate = MENU_MAIN1;
		break;

	case MENU_MAIN2:
		if (menu)
			menustate = MENU_NONE1;
		else if (plus && (minimig_config.floppy.drives<3))
		{
			minimig_config.floppy.drives++;
			minimig_ConfigFloppy(minimig_config.floppy.drives, minimig_config.floppy.speed);
			menustate = MENU_MAIN1;
		}
		else if (minus && (minimig_config.floppy.drives>0))
		{
			minimig_config.floppy.drives--;
			minimig_ConfigFloppy(minimig_config.floppy.drives, minimig_config.floppy.speed);
			menustate = MENU_MAIN1;
		}
		else if (select)
		{
			if (menusub < 4)
			{
				if (df[menusub].status & DSK_INSERTED) // eject selected floppy
				{
					df[menusub].status = 0;
					FileClose(&df[menusub].file);
					menustate = MENU_MAIN1;
				}
				else
				{
					df[menusub].status = 0;
					SelectFile("ADF", SCANO_DIR, MENU_FILE_SELECTED, MENU_MAIN1);
				}
			}
			else if (menusub == 4)	// Toggle floppy turbo
			{
				minimig_config.floppy.speed ^= 1;
				minimig_ConfigFloppy(minimig_config.floppy.drives, minimig_config.floppy.speed);
				menustate = MENU_MAIN1;
			}
			else if (menusub == 5)	// Go to harddrives page.
			{
				menustate = MENU_SETTINGS_HARDFILE1;
				menusub = 0;
			}
			else if (menusub == 6)
			{
				menustate = MENU_SETTINGS_CHIPSET1;
				menusub = 0;
			}
			else if (menusub == 7)
			{
				menustate = MENU_SETTINGS_MEMORY1;
				menusub = 0;
			}
			else if (menusub == 8)
			{
				menustate = MENU_SETTINGS_VIDEO1;
				menusub = 0;
			}
			else if (menusub == 9)
			{
				menusub = 0;
				menustate = MENU_SAVECONFIG_1;
			}
			else if (menusub == 10)
			{
				menusub = 0;
				menustate = MENU_LOADCONFIG_1;
			}
			else if (menusub == 11)
				menustate = MENU_NONE1;
		}
		else if (c == KEY_BACKSPACE) // eject all floppies
		{
			for (int i = 0; i <= drives; i++) df[i].status = 0;
			menustate = MENU_MAIN1;
		}
		else if (right)
		{
			menustate = MENU_8BIT_SYSTEM1;
			menusub = 0;
		}
		else if (left)
		{
			menustate = MENU_8BIT_INFO;
			menusub = 1;
		}
		break;

	case MENU_FILE_SELECTED: // file successfully selected
		InsertFloppy(&df[menusub], SelectedPath);
		menustate = MENU_MAIN1;
		menusub++;
		if (menusub > drives)
			menusub = 6;

		break;

	case MENU_LOADCONFIG_1:
		helptext = helptexts[HELPTEXT_NONE];
		if (parentstate != menustate) menumask = 0x400;

		parentstate = menustate;
		OsdSetTitle("Load config", 0);

		m = 0;
		OsdWrite(m++, "", 0, 0);
		OsdWrite(m++, " Startup config:");
		for (uint i = 0; i < 10; i++)
		{
			const char *info = minimig_get_cfg_info(i);
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
					sprintf(name, "  %s", strchr(info, '\n')+1);
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

		while(m < OsdGetSize() - 1) OsdWrite(m++);
		OsdWrite(OsdGetSize() - 1, STD_EXIT, menusub == 10, 0);

		menustate = MENU_LOADCONFIG_2;
		break;

	case MENU_LOADCONFIG_2:
		if (down)
		{
			if (menusub < 9) menusub++;
			menustate = MENU_LOADCONFIG_1;
		}
		else if (select)
		{
			if (menusub<10)
			{
				OsdDisable();
				minimig_cfg_load(menusub);
				menustate = MENU_NONE1;
			}
			else
			{
				menustate = MENU_MAIN1;
				menusub = 10;
			}
		}
		if (menu) // exit menu
		{
			menustate = MENU_MAIN1;
			menusub = 10;
		}
		break;

		/******************************************************************/
		/* file selection menu                                            */
		/******************************************************************/
	case MENU_FILE_SELECT1:
		helptext = helptexts[HELPTEXT_NONE];
		OsdSetTitle((fs_Options & SCANO_CORES) ? "Cores" : "Select", 0);
		PrintDirectory();
		menustate = MENU_FILE_SELECT2;
		break;

	case MENU_FILE_SELECT2:
		menumask = 0;

		if (c == KEY_BACKSPACE && (fs_Options & SCANO_UMOUNT))
		{
			for (int i = 0; i < OsdGetSize(); i++) OsdWrite(i, "", 0, 0);
			OsdWrite(OsdGetSize() / 2, "   Unmounting the image", 0, 0);
			usleep(1500000);
			SelectedPath[0] = 0;
			menustate = fs_MenuSelect;
		}

		if (menu)
		{
			if (flist_nDirEntries() && flist_SelectedItem()->de.d_type != DT_DIR)
			{
				SelectedDir[0] = 0;
				if (strlen(SelectedPath))
				{
					strcpy(SelectedDir, SelectedPath);
					strcat(SelectedPath, "/");
				}
				strcat(SelectedPath, flist_SelectedItem()->de.d_name);
			}

			if (!strcasecmp(fs_pFileExt, "RBF")) SelectedPath[0] = 0;
			menustate = fs_MenuCancel;
		}

		if (flist_nDirEntries())
		{
			ScrollLongName(); // scrolls file name if longer than display line

			if (c == KEY_HOME)
			{
				ScanDirectory(SelectedPath, SCANF_INIT, fs_pFileExt, fs_Options);
				menustate = MENU_FILE_SELECT1;
			}

			if (c == KEY_END)
			{
				ScanDirectory(SelectedPath, SCANF_END, fs_pFileExt, fs_Options);
				menustate = MENU_FILE_SELECT1;
			}


			if ((c == KEY_PAGEUP) || (c == KEY_LEFT))
			{
				ScanDirectory(SelectedPath, SCANF_PREV_PAGE, fs_pFileExt, fs_Options);
				menustate = MENU_FILE_SELECT1;
			}

			if ((c == KEY_PAGEDOWN) || (c == KEY_RIGHT))
			{
				ScanDirectory(SelectedPath, SCANF_NEXT_PAGE, fs_pFileExt, fs_Options);
				menustate = MENU_FILE_SELECT1;
			}

			if (down) // scroll down one entry
			{
				ScanDirectory(SelectedPath, SCANF_NEXT, fs_pFileExt, fs_Options);
				menustate = MENU_FILE_SELECT1;
			}

			if (up) // scroll up one entry
			{
				ScanDirectory(SelectedPath, SCANF_PREV, fs_pFileExt, fs_Options);
				menustate = MENU_FILE_SELECT1;
			}

			{
				int i;
				if ((i = GetASCIIKey(c)) > 1)
				{
					// find an entry beginning with given character
					ScanDirectory(SelectedPath, i, fs_pFileExt, fs_Options);
					menustate = MENU_FILE_SELECT1;
				}
			}

			if (select)
			{
				if (flist_SelectedItem()->de.d_type == DT_DIR)
				{
					changeDir(flist_SelectedItem()->de.d_name);
					menustate = MENU_FILE_SELECT1;
				}
				else
				{
					if (flist_nDirEntries())
					{
						SelectedDir[0] = 0;
						if (strlen(SelectedPath))
						{
							strcpy(SelectedDir, SelectedPath);
							strcat(SelectedPath, "/");
						}

						strcat(SelectedPath, flist_SelectedItem()->de.d_name);
						menustate = fs_MenuSelect;
					}
				}
			}
		}

		break;

		/******************************************************************/
		/* cheats menu                                                    */
		/******************************************************************/
	case MENU_CHEATS1:
		helptext = helptexts[HELPTEXT_NONE];
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
			menustate = MENU_8BIT_MAIN1;
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
		/* reset menu                                                     */
		/******************************************************************/
	case MENU_RESET1:
		m = 0;
		if (is_minimig()) m = 1;
		helptext = helptexts[HELPTEXT_NONE];
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
				menustate = MENU_8BIT_SYSTEM1;
                sharpmz_reset_config(1);
            }
			else
			{
				char *filename = user_io_create_config_name();
				uint32_t status[2] = { user_io_8bit_set_status(0, 0xffffffff, 0), user_io_8bit_set_status(0, 0xffffffff, 1) };
				printf("Saving config to %s\n", filename);
				FileSaveConfig(filename, status, 8);
				menustate = MENU_8BIT_MAIN1;
				menusub = 0;
			}
		}

		if (menu || (select && (menusub == 1))) // exit menu
		{
			menustate = MENU_8BIT_SYSTEM1;
			menusub = 4;
		}
		break;

	case MENU_SAVECONFIG_1:
		helptext = helptexts[HELPTEXT_NONE];
		menumask = 0x7ff;
		parentstate = menustate;
		OsdSetTitle("Save config", 0);

		m = 0;
		OsdWrite(m++, "", 0, 0);
		OsdWrite(m++, " Startup config:");
		for (uint i = 0; i < 10; i++)
		{
			const char *info = minimig_get_cfg_info(i);
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
		OsdWrite(OsdGetSize() - 1, STD_EXIT, menusub == 10, 0);

		menustate = MENU_SAVECONFIG_2;
		break;

	case MENU_SAVECONFIG_2:
		if (select)
		{
			sprintf(minimig_config.info, "%s/%s/%s%s %s+%s%s%s%s\n",
				config_cpu_msg[minimig_config.cpu & 0x03] + 2,
				config_chipset_msg[(minimig_config.chipset >> 2) & 7],
				minimig_config.chipset & CONFIG_NTSC ? "N" : "P",
				(minimig_config.enable_ide && (minimig_config.hardfile[0].enabled ||
					minimig_config.hardfile[1].enabled ||
					minimig_config.hardfile[2].enabled ||
					minimig_config.hardfile[3].enabled)) ? "/HD" : "",
				config_memory_chip_msg[minimig_config.memory & 0x03],
				config_memory_fast_msg[((minimig_config.memory >> 4) & 0x03) | ((minimig_config.memory&0x80) >> 5)],
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
			menustate = MENU_MAIN1;
			menusub = 9;
		}
		else
		if (menu) // exit menu
		{
			menustate = MENU_MAIN1;
			menusub = 9;
		}
		break;

		/******************************************************************/
		/* chipset settings menu                                          */
		/******************************************************************/
	case MENU_SETTINGS_CHIPSET1:
		helptext = helptexts[HELPTEXT_CHIPSET];
		menumask = 0;
		OsdSetTitle("Chipset", OSD_ARROW_LEFT | OSD_ARROW_RIGHT);

		m = 0;
		OsdWrite(m++, "", 0, 0);
		strcpy(s, " CPU            : ");
		strcat(s, config_cpu_msg[minimig_config.cpu & 0x03]);
		OsdWrite(m++, s, menusub == 0, 0);
		strcpy(s, " Cache ChipRAM  : ");
		strcat(s, (minimig_config.cpu & 4) ? "ON" : "OFF");
		OsdWrite(m++, s, menusub == 1, 0);
		strcpy(s, " Cache Kickstart: ");
		strcat(s, (minimig_config.cpu & 8) ? "ON" : "OFF");
		OsdWrite(m++, s, menusub == 2, 0);
		strcpy(s, " D-Cache        : ");
		strcat(s, (minimig_config.cpu & 16) ? "ON" : "OFF");
		OsdWrite(m++, s, menusub == 3, 0);
		OsdWrite(m++, "", 0, 0);
		strcpy(s, " Video          : ");
		strcat(s, minimig_config.chipset & CONFIG_NTSC ? "NTSC" : "PAL");
		OsdWrite(m++, s, menusub == 4, 0);
		strcpy(s, " Chipset        : ");
		strcat(s, config_chipset_msg[(minimig_config.chipset >> 2) & 7]);
		OsdWrite(m++, s, menusub == 5, 0);
		OsdWrite(m++, "", 0, 0);
		strcpy(s, " CD32 Pad       : ");
		strcat(s, config_cd32pad_msg[(minimig_config.autofire >> 2) & 1]);
		OsdWrite(m++, s, menusub == 6, 0);
		strcpy(s, " Joystick Swap  : ");
		strcat(s, (minimig_config.autofire & 0x8)? "ON" : "OFF");
		OsdWrite(m++, s, menusub == 7, 0);
		for (int i = m; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, STD_EXIT, menusub == 8, 0);

		menustate = MENU_SETTINGS_CHIPSET2;
		break;

	case MENU_SETTINGS_CHIPSET2:

		if (down)
		{
			menusub = (menusub+1)%9;
			menustate = MENU_SETTINGS_CHIPSET1;
		}

		if (up && menusub > 0)
		{
			if (menusub) menusub--;
			else menusub = 8;
			menustate = MENU_SETTINGS_CHIPSET1;
		}

		if (select)
		{
			if (menusub == 0)
			{
				menustate = MENU_SETTINGS_CHIPSET1;
				int _config_cpu = minimig_config.cpu & 0x3;
				_config_cpu += 1;
				if (_config_cpu == 0x02) _config_cpu += 1;
				minimig_config.cpu = (minimig_config.cpu & 0xfc) | (_config_cpu & 0x3);
				minimig_ConfigCPU(minimig_config.cpu);
			}
			else if (menusub == 1)
			{
				menustate = MENU_SETTINGS_CHIPSET1;
				minimig_config.cpu ^= 4;
				minimig_ConfigCPU(minimig_config.cpu);
			}
			else if (menusub == 2)
			{
				menustate = MENU_SETTINGS_CHIPSET1;
				minimig_config.cpu ^= 8;
				minimig_ConfigCPU(minimig_config.cpu);
			}
			else if (menusub == 3)
			{
				menustate = MENU_SETTINGS_CHIPSET1;
				minimig_config.cpu ^= 16;
				minimig_ConfigCPU(minimig_config.cpu);
			}
			else if (menusub == 4)
			{
				minimig_config.chipset ^= CONFIG_NTSC;
				menustate = MENU_SETTINGS_CHIPSET1;
				minimig_ConfigChipset(minimig_config.chipset);
			}
			else if (menusub == 5)
			{
				switch (minimig_config.chipset & 0x1c) {
				case 0:
					minimig_config.chipset = (minimig_config.chipset & 3) | CONFIG_A1000;
					break;
				case CONFIG_A1000:
					minimig_config.chipset = (minimig_config.chipset & 3) | CONFIG_ECS;
					break;
				case CONFIG_ECS:
					minimig_config.chipset = (minimig_config.chipset & 3) | CONFIG_AGA | CONFIG_ECS;
					break;
				case (CONFIG_AGA | CONFIG_ECS) :
					minimig_config.chipset = (minimig_config.chipset & 3) | 0;
					break;
				}

				menustate = MENU_SETTINGS_CHIPSET1;
				minimig_ConfigChipset(minimig_config.chipset);
			}
			else if (menusub == 6)
			{
				minimig_config.autofire ^= 0x4;
				menustate = MENU_SETTINGS_CHIPSET1;
				minimig_ConfigAutofire(minimig_config.autofire, 0x4);
			}
			else if (menusub == 7)
			{
				minimig_config.autofire ^= 0x8;
				menustate = MENU_SETTINGS_CHIPSET1;
				minimig_ConfigAutofire(minimig_config.autofire, 0x8);
			}
			else if (menusub == 8)
			{
				menustate = MENU_MAIN1;
				menusub = 6;
			}
		}

		if (menu)
		{
			menustate = MENU_MAIN1;
			menusub = 6;
		}
		else if (right)
		{
			menustate = MENU_SETTINGS_MEMORY1;
			menusub = 0;
		}
		else if (left)
		{
			menustate = MENU_SETTINGS_HARDFILE1;
			menusub = 0;
		}
		break;

		/******************************************************************/
		/* memory settings menu                                           */
		/******************************************************************/
	case MENU_SETTINGS_MEMORY1:
		helptext = helptexts[HELPTEXT_MEMORY];
		menumask = 0x3f;
		parentstate = menustate;

		OsdSetTitle("Memory", OSD_ARROW_LEFT | OSD_ARROW_RIGHT);

		OsdWrite(0, "", 0, 0);
		strcpy(s, " CHIP   : ");
		strcat(s, config_memory_chip_msg[minimig_config.memory & 0x03]);
		OsdWrite(1, s, menusub == 0, 0);
		strcpy(s, " FAST   : ");
		strcat(s, config_memory_fast_msg[((minimig_config.memory >> 4) & 0x03) | ((minimig_config.memory&0x80) >> 5)]);
		OsdWrite(2, s, menusub == 1, 0);
		strcpy(s, " SLOW   : ");
		strcat(s, config_memory_slow_msg[(minimig_config.memory >> 2) & 0x03]);
		OsdWrite(3, s, menusub == 2, 0);

		OsdWrite(4, "", 0, 0);

		strcpy(s, " ROM    : ");
		strncat(s, minimig_config.kickstart, 24);
		OsdWrite(5, s, menusub == 3, 0);

		strcpy(s, " HRTmon : ");
		strcat(s, (minimig_config.memory & 0x40) ? "enabled " : "disabled");
		OsdWrite(6, s, menusub == 4, 0);

		for (int i = 7; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, STD_EXIT, menusub == 5, 0);

		menustate = MENU_SETTINGS_MEMORY2;
		break;

	case MENU_SETTINGS_MEMORY2:
		if (select)
		{
			if (menusub == 0)
			{
				minimig_config.memory = ((minimig_config.memory + 1) & 0x03) | (minimig_config.memory & ~0x03);
				menustate = MENU_SETTINGS_MEMORY1;
			}
			else if (menusub == 1)
			{
				uint8_t c = (((minimig_config.memory >> 4) & 0x03) | ((minimig_config.memory & 0x80) >> 5))+1;
				if (c > 5) c = 0;
				minimig_config.memory = ((c<<4) & 0x30) | ((c<<5) & 0x80) | (minimig_config.memory & ~0xB0);
				menustate = MENU_SETTINGS_MEMORY1;
			}
			else if (menusub == 2)
			{
				minimig_config.memory = ((minimig_config.memory + 4) & 0x0C) | (minimig_config.memory & ~0x0C);
				menustate = MENU_SETTINGS_MEMORY1;
			}
			else if (menusub == 3)
			{
				SelectFile("ROM", 0, MENU_ROMFILE_SELECTED, MENU_SETTINGS_MEMORY1);
			}
			else if (menusub == 4)
			{
				minimig_config.memory ^= 0x40;
				menustate = MENU_SETTINGS_MEMORY1;
			}
			else if (menusub == 5)
			{
				menustate = MENU_MAIN1;
				menusub = 7;
			}
		}

		if (menu)
		{
			menustate = MENU_MAIN1;
			menusub = 7;
		}
		else if (right)
		{
			menustate = MENU_SETTINGS_VIDEO1;
			menusub = 0;
		}
		else if (left)
		{
			menustate = MENU_SETTINGS_CHIPSET1;
			menusub = 0;
		}
		break;

	case MENU_ROMFILE_SELECTED:
		minimig_set_kickstart(SelectedPath);
		menustate = MENU_SETTINGS_MEMORY1;
		break;

		/******************************************************************/
		/* hardfile settings menu                                         */
		/******************************************************************/

		// FIXME!  Nasty race condition here.  Changing HDF type has immediate effect
		// which could be disastrous if the user's writing to the drive at the time!
		// Make the menu work on the copy, not the original, and copy on acceptance,
		// not on rejection.
	case MENU_SETTINGS_HARDFILE1:
		helptext = helptexts[HELPTEXT_HARDFILE];
		OsdSetTitle("Harddisks", OSD_ARROW_LEFT | OSD_ARROW_RIGHT);

		parentstate = menustate;
		menumask = 0x201;	                      // b001000000001 - On/off & exit enabled by default...
		if (minimig_config.enable_ide) menumask |= 0xAA;  // b010101010 - HD0/1/2/3 type
		OsdWrite(0, "", 0, 0);
		strcpy(s, " A600/A1200 IDE : ");
		strcat(s, minimig_config.enable_ide ? "On " : "Off");
		OsdWrite(1, s, menusub == 0, 0);
		OsdWrite(2, "", 0, 0);

		{
			uint n = 3, m = 1, t = 4;
			for (uint i = 0; i < 4; i++)
			{
				strcpy(s, (i & 2) ? " Secondary " : " Primary ");
				strcat(s, (i & 1) ? "Slave: " : "Master: ");
				strcat(s, minimig_config.hardfile[i].enabled ? "Enabled" : "Disabled");
				OsdWrite(n++, s, minimig_config.enable_ide ? (menusub == m++) : 0, minimig_config.enable_ide == 0);
				if (minimig_config.hardfile[i].filename[0])
				{
					strcpy(s, "                                ");
					strncpy(&s[7], minimig_config.hardfile[i].filename, 21);
				}
				else
				{
					strcpy(s, "       ** not selected **");
				}
				enable = minimig_config.enable_ide && minimig_config.hardfile[i].enabled;
				if (enable) menumask |= t;	// Make hardfile selectable
				OsdWrite(n++, s, menusub == m++, enable == 0);
				t <<= 2;
				OsdWrite(n++, "", 0, 0);
			}
		}

		OsdWrite(OsdGetSize() - 1, STD_EXIT, menusub == 9, 0);
		menustate = MENU_SETTINGS_HARDFILE2;
		break;

	case MENU_SETTINGS_HARDFILE2:
		if (select)
		{
			if (menusub == 0)
			{
				minimig_config.enable_ide = (minimig_config.enable_ide == 0);
				menustate = MENU_SETTINGS_HARDFILE1;
			}
			else if (menusub < 9)
			{
				if(menusub&1)
				{
					int num = (menusub - 1) / 2;
					minimig_config.hardfile[num].enabled = minimig_config.hardfile[num].enabled ? 0 : 1;
					menustate = MENU_SETTINGS_HARDFILE1;
				}
				else
				{
					SelectFile("HDFVHDIMGDSK", SCANO_DIR | SCANO_UMOUNT, MENU_HARDFILE_SELECTED, MENU_SETTINGS_HARDFILE1);
				}
			}
			else if (menusub == 9) // return to previous menu
			{
				menustate = MENU_MAIN1;
				menusub = 5;
			}
		}

		if (menu) // return to previous menu
		{
			menustate = MENU_MAIN1;
			menusub = 5;
		}
		else if (right)
		{
			menustate = MENU_SETTINGS_CHIPSET1;
			menusub = 0;
		}
		else if (left)
		{
			menustate = MENU_SETTINGS_VIDEO1;
			menusub = 0;
		}
		break;

		/******************************************************************/
		/* hardfile selected menu                                         */
		/******************************************************************/
	case MENU_HARDFILE_SELECTED:
		{
			int num = (menusub - 2) / 2;
			uint len = strlen(SelectedPath);
			if (len > sizeof(minimig_config.hardfile[num].filename) - 1) len = sizeof(minimig_config.hardfile[num].filename) - 1;
			if(len) memcpy(minimig_config.hardfile[num].filename, SelectedPath, len);
			minimig_config.hardfile[num].filename[len] = 0;
			menustate = checkHDF(minimig_config.hardfile[num].filename, &rdb) ? MENU_SETTINGS_HARDFILE1 : MENU_HARDFILE_SELECTED2;
		}
		break;

	case MENU_HARDFILE_SELECTED2:
		m = 0;
		menumask = 0x1;
		if (!rdb)
		{
			OsdWrite(m++, "", 0, 0);
			OsdWrite(m++, "", 0, 0);
			OsdWrite(m++, "", 0, 0);
			OsdWrite(m++, "", 0, 0);
			OsdWrite(m++, "", 0, 0);
			OsdWrite(m++, "    Cannot open the file", 0, 0);
		}
		else
		{
			OsdWrite(m++, "", 0, 0);
			OsdWrite(m++, "      !! DANGEROUS !!", 0, 0);
			OsdWrite(m++, "", 0, 0);
			OsdWrite(m++, " RDB has illegal CHS values:", 0, 0);
			sprintf(s,    "   Cylinders: %lu", rdb->rdb_Cylinders);
			OsdWrite(m++, s, 0, 0);
			sprintf(s,    "   Heads:     %lu", rdb->rdb_Heads);
			OsdWrite(m++, s, 0, 0);
			sprintf(s,    "   Sectors:   %lu", rdb->rdb_Sectors);
			OsdWrite(m++, s, 0, 0);
			OsdWrite(m++, "", 0, 0);
			OsdWrite(m++, " Max legal values:", 0, 0);
			OsdWrite(m++, "   C:65536, H:16, S:255", 0, 0);
			OsdWrite(m++, "", 0, 0);
			OsdWrite(m++, "  Some functions won't work", 0, 0);
			OsdWrite(m++, "  correctly and may corrupt", 0, 0);
			OsdWrite(m++, "         the data!", 0, 0);
		}
		OsdWrite(m++, "", 0, 0);
		OsdWrite(m++,     "            OK", 1, 0);
		while (m < OsdGetSize()) OsdWrite(m++, "", 0, 0);

		menusub_last = menusub;
		menusub = 0;
		menustate = MENU_HARDFILE_SELECTED3;
		break;

	case MENU_HARDFILE_SELECTED3:
		if (select || menu)
		{
			menusub = menusub_last;
			parentstate = menustate;
			menustate = MENU_SETTINGS_HARDFILE1;
		}
		break;

		/******************************************************************/
		/* video settings menu                                            */
		/******************************************************************/
	case MENU_SETTINGS_VIDEO1:
		menumask = 0x3f;
		parentstate = menustate;
		helptext = 0; // helptexts[HELPTEXT_VIDEO];

		OsdSetTitle("Video", OSD_ARROW_LEFT | OSD_ARROW_RIGHT);
		OsdWrite(0, "", 0, 0);
		strcpy(s, " Scandoubler FX : ");
		strcat(s, config_scanlines_msg[minimig_config.scanlines & 7]);
		OsdWrite(1, s, menusub == 0, 0);
		strcpy(s, " Video area by  : ");
		strcat(s, config_blank_msg[(minimig_config.scanlines >> 6) & 3]);
		OsdWrite(2, s, menusub == 1, 0);
		strcpy(s, " Aspect Ratio   : ");
		strcat(s, config_ar_msg[(minimig_config.scanlines >> 4) & 1]);
		OsdWrite(3, s, menusub == 2, 0);
		OsdWrite(4, "", 0, 0);
		strcpy(s, " Stereo mix     : ");
		strcat(s, config_stereo_msg[minimig_config.audio & 3]);
		OsdWrite(5, s, menusub == 3, 0);
		OsdWrite(6, "", 0, 0);
		OsdWrite(7, "", 0, 0);
		OsdWrite(8, minimig_get_adjust() ? " Finish screen adjustment" : " Adjust screen position", menusub == 4, 0);
		OsdWrite(9, "", 0, 0);
		OsdWrite(10, "", 0, 0);
		OsdWrite(11, "", 0, 0);
		OsdWrite(12, "", 0, 0);
		OsdWrite(13, "", 0, 0);
		OsdWrite(14, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, STD_EXIT, menusub == 5, 0);

		menustate = MENU_SETTINGS_VIDEO2;
		break;

	case MENU_SETTINGS_VIDEO2:
		if (select)
		{
			if (menusub == 0)
			{
				minimig_config.scanlines = ((minimig_config.scanlines + 1) & 7) | (minimig_config.scanlines & 0xf8);
				if ((minimig_config.scanlines & 7) > 4) minimig_config.scanlines = minimig_config.scanlines & 0xf8;
				menustate = MENU_SETTINGS_VIDEO1;
				minimig_ConfigVideo(minimig_config.scanlines);
			}
			else if (menusub == 1)
			{
				minimig_config.scanlines &= ~0x80;
				minimig_config.scanlines ^= 0x40;
				menustate = MENU_SETTINGS_VIDEO1;
				minimig_ConfigVideo(minimig_config.scanlines);
			}
			else if (menusub == 2)
			{
				minimig_config.scanlines &= ~0x20; // reserved for auto-ar
				minimig_config.scanlines ^= 0x10;
				menustate = MENU_SETTINGS_VIDEO1;
				minimig_ConfigVideo(minimig_config.scanlines);
			}
			else if (menusub == 3)
			{
				minimig_config.audio = (minimig_config.audio + 1) & 3;
				menustate = MENU_SETTINGS_VIDEO1;
				minimig_ConfigAudio(minimig_config.audio);
			}
			else if (menusub == 4)
			{
				menustate = MENU_NONE1;
				minimig_set_adjust(minimig_get_adjust() ? 0 : 1);
			}
			else if (menusub == 5)
			{
				menustate = MENU_MAIN1;
				menusub = 8;
			}
		}

		if (menu)
		{
			menustate = MENU_MAIN1;
			menusub = 8;
		}
		else if (right)
		{
			menustate = MENU_SETTINGS_HARDFILE1;
			menusub = 0;
		}
		else if (left)
		{
			menustate = MENU_SETTINGS_MEMORY1;
			menusub = 0;
		}
		break;

		/******************************************************************/
		/* firmware menu */
		/******************************************************************/
	case MENU_SYSTEM1:
		if (video_fb_state())
		{
			menustate = MENU_NONE1;
			break;
		}

		OsdSetSize(16);
		helptext = helptexts[HELPTEXT_NONE];
		parentstate = menustate;

		OsdSetTitle("System Settings", 0);
		OsdWrite(0, "", 0, 0);
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

		OsdWrite(1, s, 0, 0);

		{
			uint64_t avail = 0;
			struct statvfs buf;
			memset(&buf, 0, sizeof(buf));
			if (!statvfs(getRootDir(), &buf)) avail = buf.f_bsize * buf.f_bavail;
			if(avail < (10ull*1024*1024*1024)) sprintf(s, "   Available space: %llumb", avail / (1024 * 1024));
			else sprintf(s, "   Available space: %llugb", avail / (1024 * 1024 * 1024));
			OsdWrite(4, s, 0, 0);
		}
		menumask = 15;
		OsdWrite(2, "", 0, 0);
		if (getStorage(0))
		{
			OsdWrite(3, "        Storage: USB", 0, 0);
			OsdWrite(5, "      Switch to SD card", menusub == 0, 0);
		}
		else
		{
			if (getStorage(1))
			{
				OsdWrite(3, " No USB found, using SD card", 0, 0);
				OsdWrite(5, "      Switch to SD card", menusub == 0, 0);
			}
			else
			{
				OsdWrite(3, "      Storage: SD card", 0, 0);
				OsdWrite(5, "        Switch to USB", menusub == 0, !isUSBMounted());
			}
		}
		OsdWrite(6, "", 0, 0);
		OsdWrite(7, " Remap keyboard            \x16", menusub == 1, 0);
		OsdWrite(8, " Define joystick buttons   \x16", menusub == 2, 0);
		OsdWrite(9, " Scripts                   \x16", menusub == 3, 0);
		sysinfo_timer = 0;

		menustate = MENU_SYSTEM2;

	case MENU_SYSTEM2:
		if (menu)
		{
			OsdCoreNameSet("");
			SelectFile(0, SCANO_CORES, MENU_CORE_FILE_SELECTED1, MENU_SYSTEM1);
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
				strcpy(joy_bnames[SYS_BTN_A - DPAD_NAMES], "A (OK/Enter)");
				strcpy(joy_bnames[SYS_BTN_B - DPAD_NAMES], "B (ESC/Back)");
				strcpy(joy_bnames[SYS_BTN_X - DPAD_NAMES], "X (Backspace)");
				strcpy(joy_bnames[SYS_BTN_Y - DPAD_NAMES], "Y");
				strcpy(joy_bnames[SYS_BTN_L - DPAD_NAMES], "L");
				strcpy(joy_bnames[SYS_BTN_R - DPAD_NAMES], "R");
				strcpy(joy_bnames[SYS_BTN_SELECT - DPAD_NAMES], "Select");
				strcpy(joy_bnames[SYS_BTN_START  - DPAD_NAMES], "Start");
				strcpy(joy_bnames[SYS_MS_RIGHT - DPAD_NAMES], "Mouse Move RIGHT");
				strcpy(joy_bnames[SYS_MS_LEFT - DPAD_NAMES], "Mouse Move LEFT");
				strcpy(joy_bnames[SYS_MS_DOWN - DPAD_NAMES], "Mouse Move DOWN");
				strcpy(joy_bnames[SYS_MS_UP - DPAD_NAMES], "Mouse Move UP");
				strcpy(joy_bnames[SYS_MS_BTN_L - DPAD_NAMES], "Mouse Btn Left");
				strcpy(joy_bnames[SYS_MS_BTN_R - DPAD_NAMES], "Mouse Btn Right");
				strcpy(joy_bnames[SYS_MS_BTN_M - DPAD_NAMES], "Mouse Btn Middle");
				strcpy(joy_bnames[SYS_MS_BTN_EMU - DPAD_NAMES], "Mouse Emu / Sniper");
				joy_bcount = 16+1; //buttons + OSD/KTGL button
				start_map_setting(joy_bcount + 6); // + dpad + Analog X/Y
				menustate = MENU_JOYDIGMAP;
				menusub = 0;
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

					if(match) SelectFile("SH", SCANO_DIR, MENU_SCRIPTS_FB, MENU_SYSTEM1);
					else
					{
						menustate = MENU_SCRIPTS_PRE;
						menusub = 0;
					}
				}
				break;
			}
		}
		printSysInfo();
		break;

	case MENU_WMPAIR:
		{
			OsdSetTitle("Wiimote", 0);
			int res = toggle_wminput();
			menu_timer = GetTimer(2000);
			for (int i = 0; i < OsdGetSize(); i++) OsdWrite(i);
			if (res < 0)       OsdWrite(7, "    Cannot enable Wiimote");
			else if (res == 0) OsdWrite(7, "       Wiimote disabled");
			else
			{
				OsdWrite(7, "       Wiimote enabled");
				OsdWrite(9, "    Press 1+2 to connect");
				menu_timer = GetTimer(3000);
			}
			menustate = MENU_WMPAIR1;
		}
		//fall through

	case MENU_WMPAIR1:
		if (CheckTimer(menu_timer)) menustate = MENU_NONE1;
		break;

	case MENU_LGCAL:
		helptext = 0;
		OsdSetTitle("Wiimote Calibration", 0);
		for (int i = 0; i < OsdGetSize(); i++) OsdWrite(i);
		OsdWrite(9, "  Point Wiimote to the edge");
		OsdWrite(10, "     of screen and press");
		OsdWrite(11, "   the button B to confirm");
		OsdWrite(OsdGetSize() - 1, "           Cancel", menusub == 0, 0);
		wm_ok = 0;
		wm_side = 0;
		memset(wm_pos, 0, sizeof(wm_pos));
		menustate = MENU_LGCAL1;
		menusub = 0;
		break;

	case MENU_LGCAL1:
		if (wm_side < 4) wm_pos[wm_side] = (wm_side < 2) ? wm_y : wm_x;
		sprintf(s, "           %c%04d%c", (wm_side == 0) ? 17 : 32, (wm_side == 0) ? wm_y : wm_pos[0], (wm_side == 0) ? 16 : 32);
		OsdWrite(0, s);
		sprintf(s, "%c%04d%c                 %c%04d%c", (wm_side == 2) ? 17 : 32, (wm_side == 2) ? wm_x : wm_pos[2], (wm_side == 2) ? 16 : 32,
		                                                (wm_side == 3) ? 17 : 32, (wm_side == 3) ? wm_x : wm_pos[3], (wm_side == 3) ? 16 : 32);
		OsdWrite(7, s);
		sprintf(s, "           %c%04d%c", (wm_side == 1) ? 17 : 32, (wm_side == 1) ? wm_y : wm_pos[1], (wm_side == 1) ? 16 : 32);
		OsdWrite(13, s);
		if (menu || select) menustate = MENU_NONE1;

		if (wm_ok == 1)
		{
			wm_ok = 0;
			wm_side++;
		}

		if (wm_ok == 2)
		{
			wm_ok = 0;
			if (wm_side == 4)
			{
				input_lightgun_cal(wm_pos);
				menustate = MENU_NONE1;
			}
		}
		break;

	case MENU_SCRIPTS_PRE:
		OsdSetTitle("Warning!!!", 0);
		helptext = 0;
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
				SelectFile("SH", SCANO_DIR, MENU_SCRIPTS_FB, MENU_SYSTEM1);
				break;
			}
		}
		break;

	case MENU_SCRIPTS_FB:
		if (cfg.fb_terminal)
		{
			static char cmd[1024 * 2];
			const char *path = getFullPath(SelectedPath);
			menustate = MENU_SCRIPTS_FB2;
			video_chvt(2);
			video_fb_enable(1);
			vga_nag();
			sprintf(cmd, "#!/bin/bash\nexport LC_ALL=en_US.UTF-8\ncd $(dirname %s)\n%s\necho \"Press any key to continue\"\n", path, path);
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
				video_fb_enable(0);
				menustate = MENU_SYSTEM1;
				menusub = 3;
				OsdClear();
				OsdEnable(DISABLE_KEYBOARD);
			}
		}
		break;

	case MENU_BTPAIR:
		OsdSetSize(16);
		OsdEnable(DISABLE_KEYBOARD);
		parentstate = MENU_BTPAIR;
		//fall through

	case MENU_SCRIPTS:
		helptext = 0;
		menumask = 1;
		menusub = 0;
		OsdSetTitle((parentstate == MENU_BTPAIR) ? "BT Pairing" : flist_SelectedItem()->de.d_name, 0);
		menustate = MENU_SCRIPTS1;
		if (parentstate != MENU_BTPAIR) parentstate = MENU_SCRIPTS;
		for (int i = 0; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, "           Cancel", menusub == 0, 0);
		for (int i = 0; i < script_lines; i++) strcpy(script_output[i], "");
		script_line=0;
		script_exited = false;
		cpu_set_t set;
		CPU_ZERO(&set);
		CPU_SET(0, &set);
		CPU_SET(1, &set);
		sched_setaffinity(0, sizeof(set), &set);
		script_pipe=popen((parentstate != MENU_BTPAIR) ? getFullPath(SelectedPath) : "/usr/sbin/btpair", "r");
		script_file = fileno(script_pipe);
		fcntl(script_file, F_SETFL, O_NONBLOCK);
		break;

	case MENU_SCRIPTS1:
		if (!script_exited)
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
				script_exited=true;
				OsdWrite(OsdGetSize() - 1, "             OK", menusub == 0, 0);
			};
		};

		if (select || (script_exited && menu))
		{
			if (!script_exited)
			{
				strcpy(script_command, "killall ");
				strcat(script_command, (parentstate == MENU_BTPAIR) ? "btpair" : flist_SelectedItem()->de.d_name);
				system(script_command);
				pclose(script_pipe);
				cpu_set_t set;
				CPU_ZERO(&set);
				CPU_SET(1, &set);
				sched_setaffinity(0, sizeof(set), &set);
				script_exited = true;
			};

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
		break;

	case MENU_KBDMAP:
		helptext = 0;
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
		menustate = MENU_NONE1;
		strcpy(SelectedRBF, SelectedPath);
		if (!getStorage(0)) // multiboot is only on SD card.
		{
			SelectedPath[strlen(SelectedPath) - 4] = 0;
			int off = strlen(SelectedDir);
			if (off) off++;
			int fnum = ScanDirectory(SelectedDir, SCANF_INIT, "TXT", 0, SelectedPath + off);
			if (fnum)
			{
				if (fnum == 1)
				{
					//Check if the only choice is <core>.txt
					strcat(SelectedPath, ".txt");
					if (FileLoad(SelectedPath, 0, 0))
					{
						menustate = MENU_CORE_FILE_SELECTED2;
						break;
					}
				}

				strcpy(SelectedPath, SelectedRBF);
				AdjustDirectory(SelectedPath);
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

		// close OSD now as the new core may not even have one
		fpga_load_rbf(SelectedRBF);
		break;

	case MENU_CORE_FILE_SELECTED2:
		fpga_load_rbf(SelectedRBF, SelectedPath);
		menustate = MENU_NONE1;
		break;

	case MENU_CORE_FILE_CANCELED:
		SelectFile(0, SCANO_CORES, MENU_CORE_FILE_SELECTED1, cp_MenuCancel);
		break;

		/******************************************************************/
		/* we should never come here                                      */
		/******************************************************************/
	default:
		break;
	}

	if (is_menu_core())
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
				if (btimeout >= 10)
				{
					sprintf(str, " Bootcore -> %s", bootcoretype);
					OsdWrite(13, str, 0, 0);
					strcpy(straux, cfg.bootcore);
					sprintf(str, " %s", get_rbf_name_bootcore(straux));
					PrintFileName(str, 14, (32 * btimeout) / cfg.bootcore_timeout);
					sprintf(str, "   Press any key to cancel");
					OsdWrite(15, str, 0, 0);
					btimeout--;
					if (btimeout < 10)
					{
						OsdWrite(13, "", 0, 0);
						strcpy(straux, cfg.bootcore);
						sprintf(str, " %s", get_rbf_name_bootcore(straux));
						PrintFileName(str, 14, 0);
						sprintf(str, "           Loading...");
						OsdWrite(15, str, 1, 0);
						fpga_load_rbf(cfg.bootcore);
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

				int netType = (int)getNet(0);
				if (netType) str[8] = 0x1b + netType;
				if (has_bt()) str[9] = 4;
				if (user_io_get_sdram_cfg() & 0x8000)
				{
					switch (user_io_get_sdram_cfg() & 7)
					{
					case 7:
						str[10] = 0x95;
						break;
					case 3:
						str[10] = 0x94;
						break;
					case 1:
						str[10] = 0x93;
						break;
					default:
						str[10] = 0x92;
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
	if (flist_SelectedItem()->de.d_type == DT_REG) // if a file
	{
		if (fs_ExtLen <= 3)
		{
			char e[5];
			memcpy(e + 1, fs_pFileExt, 3);
			if (e[3] == 0x20)
			{
				e[3] = 0;
				if (e[2] == 0x20)
				{
					e[2] = 0;
				}
			}
			e[0] = '.';
			e[4] = 0;
			int l = strlen(e);
			if ((len>l) && !strncasecmp(flist_SelectedItem()->altname + len - l, e, l)) len -= l;
		}
	}

	max_len = 30; // number of file name characters to display (one more required for scrolling)
	if (flist_SelectedItem()->de.d_type == DT_DIR)
		max_len = 25; // number of directory name characters to display

	ScrollText(flist_iSelectedEntry()-flist_iFirstEntry(), flist_SelectedItem()->altname, 0, len, max_len, 1);
}

void PrintFileName(char *name, int row, int maxinv)
{
	int len;

	char s[40];
	s[32] = 0; // set temporary string length to OSD line length

	len = strlen(name); // get name length
	memset(s, ' ', 32); // clear line buffer
	char *p = 0;
	if ((fs_Options & SCANO_CORES) && len > 9 && !strncmp(name + len - 9, "_20", 3))
	{
		p = name + len - 6;
		len -= 9;
	}

	if (len > 28)
	{
		len = 27; // trim display length if longer than 30 characters
		s[28] = 22;
	}
	strncpy(s + 1, name, len); // display only name

	if (!cfg.rbf_hide_datecode && (fs_Options & SCANO_CORES))
	{
		if (p)
		{
			int n = 19;
			s[n++] = ' ';
			s[n++] = p[0];
			s[n++] = p[1];
			s[n++] = '.';
			s[n++] = p[2];
			s[n++] = p[3];
			s[n++] = '.';
			s[n++] = p[4];
			s[n++] = p[5];
		}
		else
		{
			strcpy(&s[19], " --.--.--");
		}
	}

	OsdWrite(row, s, 1, 0, 0, maxinv);

}

// print directory contents
void PrintDirectory(void)
{
	int k;
	int len;

	char s[40];
	s[32] = 0; // set temporary string length to OSD line length

	ScrollReset();

	for(int i = 0; i < OsdGetSize(); i++)
	{
		char leftchar = 0;
		memset(s, ' ', 32); // clear line buffer
		if (i < flist_nDirEntries())
		{
			k = flist_iFirstEntry() + i;

			len = strlen(flist_DirItem(k)->altname); // get name length

			if (!(flist_DirItem(k)->de.d_type == DT_DIR)) // if a file
			{
				if (fs_ExtLen <= 3)
				{
					char e[5];
					memcpy(e + 1, fs_pFileExt, 3);
					if (e[3] == 0x20)
					{
						e[3] = 0;
						if (e[2] == 0x20)
						{
							e[2] = 0;
						}
					}
					e[0] = '.';
					e[4] = 0;
					int l = strlen(e);
					if ((len>l) && !strncasecmp(flist_DirItem(k)->altname + len - l, e, l))
					{
						len -= l;
					}
				}
			}

			char *p = 0;
			if ((fs_Options & SCANO_CORES) && len > 9 && !strncmp(flist_DirItem(k)->altname + len - 9, "_20", 3))
			{
				p = flist_DirItem(k)->altname + len - 6;
				len -= 9;
			}

			if (len > 28)
			{
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
			}
			else if (!cfg.rbf_hide_datecode && (fs_Options & SCANO_CORES))
			{
				if (p)
				{
					int n = 19;
					s[n++] = ' ';
					s[n++] = p[0];
					s[n++] = p[1];
					s[n++] = '.';
					s[n++] = p[2];
					s[n++] = p[3];
					s[n++] = '.';
					s[n++] = p[4];
					s[n++] = p[5];
				}
				else
				{
					strcpy(&s[19], " --.--.--");
				}
			}

			if (!i && k) leftchar = 17;
			if ((i == OsdGetSize() - 1) && (k < flist_nDirEntries() - 1)) leftchar = 16;
		}
		else
		{
			if (i == 0 && flist_nDirEntries() == 0) // selected directory is empty
				strcpy(s, "          No files!");
		}

		OsdWriteOffset(i, s, i == (flist_iSelectedEntry() - flist_iFirstEntry()), 0, 0, leftchar);
	}
}

void _strncpy(char* pStr1, const char* pStr2, size_t nCount)
{
	// customized strncpy() function to fill remaing destination string part with spaces

	while (*pStr2 && nCount)
	{
		*pStr1++ = *pStr2++; // copy strings
		nCount--;
	}

	while (nCount--)
		*pStr1++ = ' '; // fill remaining space with spaces
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

/*  Error Message */
void ErrorMessage(const char *message, unsigned char code)
{
	menustate = MENU_ERROR;

	OsdSetTitle("Error", 0);
	set_text(message, code);
	OsdEnable(0); // do not disable KEYBOARD
}

void InfoMessage(const char *message, int timeout)
{
	if (menustate != MENU_INFO)
	{
		OsdSetTitle("Message", 0);
		OsdEnable(0); // do not disable keyboard
	}

	set_text(message, 0);

	menu_timer = GetTimer(timeout);
	menustate = MENU_INFO;
}

void Info(const char *message, int timeout, int width, int height, int frame)
{
	if (!user_io_osd_is_visible())
	{
		OSD_PrintInfo(message, &width, &height, frame);
		InfoEnable(20, 10, width, height);

		menu_timer = GetTimer(timeout);
		menustate = MENU_INFO;
	}
}

int menu_lightgun_cb(uint16_t type, uint16_t code, int value)
{
	if (type == EV_ABS)
	{
		if (code == 0 && value) wm_x = value;
		if (code == 1 && value != 1023) wm_y = value;
	}

	if (type == EV_KEY)
	{
		if (code == 0x131 && menustate == MENU_LGCAL1)
		{
			if (value == 1) wm_ok = 1;
			if (value == 0) wm_ok = 2;
			return 1;
		}
	}
	return 0;
}
