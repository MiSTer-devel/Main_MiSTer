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
#include "stdio.h"
#include "string.h"
#include "file_io.h"
#include "osd.h"
#include "minimig_fdd.h"
#include "minimig_hdd.h"
#include "hardware.h"
#include "minimig_config.h"
#include "menu.h"
#include "user_io.h"
#include "st_tos.h"
#include "debug.h"
#include "minimig_boot.h"
#include "archie.h"
#include "sharpmz.h"
#include "fpga_io.h"
#include <stdbool.h>
#include "cfg.h"
#include "input.h"
#include "x86.h"
#include "battery.h"

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
	MENU_FIRMWARE1,
	MENU_FIRMWARE_CORE_FILE_SELECTED1,
	MENU_FIRMWARE_CORE_FILE_SELECTED2,
	MENU_FIRMWARE_CORE_FILE_CANCELED,
	MENU_ERROR,
	MENU_INFO,
	MENU_STORAGE,
	MENU_JOYDIGMAP,
	MENU_JOYDIGMAP1,
	MENU_JOYKBDMAP,
	MENU_JOYKBDMAP1,
	MENU_KBDMAP,
	MENU_KBDMAP1,

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

	// 8bit menu entries
	MENU_8BIT_MAIN1,
	MENU_8BIT_MAIN2,
	MENU_8BIT_MAIN_FILE_SELECTED,
	MENU_8BIT_MAIN_IMAGE_SELECTED,
	MENU_8BIT_SYSTEM1,
	MENU_8BIT_SYSTEM2,
	MENU_8BIT_ABOUT1,
	MENU_8BIT_ABOUT2
};

unsigned char menustate = MENU_NONE1;
unsigned char parentstate;
unsigned char menusub = 0;
unsigned char menusub_last = 0; //for when we allocate it dynamically and need to know last row
unsigned int  menumask = 0; // Used to determine which rows are selectable...
unsigned long menu_timer = 0;

extern const char *version;

const char *config_tos_mem[] = { "512 kB", "1 MB", "2 MB", "4 MB", "8 MB", "14 MB", "--", "--" };
const char *config_tos_wrprot[] = { "none", "A:", "B:", "A: and B:" };
const char *config_tos_usb[] = { "none", "control", "debug", "serial", "parallel", "midi" };

const char *config_memory_chip_msg[] = { "512kb", "1mb", "1.5mb", "2mb" };
const char *config_memory_slow_msg[] = { "none", "512kb", "1mb", "1.5mb" };
const char *config_memory_fast_msg[] = { "none", "2mb", "4mb","24mb","24mb" };

const char *config_scanlines_msg[] = { "off", "dim", "black" };
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
const char *joy_button_map[] = { "RIGHT", "LEFT", "DOWN", "UP", "BUTTON 1", "BUTTON 2", "BUTTON 3", "BUTTON 4", "KBD TOGGLE", "BUTTON OSD" };
const char *config_stereo_msg[] = { "0%", "25%", "50%", "100%" };
const char *config_uart_msg[] = { "   None", "    PPP", "Console" };

char joy_bnames[12][32];
int  joy_bcount = 0;

enum HelpText_Message { HELPTEXT_NONE, HELPTEXT_MAIN, HELPTEXT_HARDFILE, HELPTEXT_CHIPSET, HELPTEXT_MEMORY, HELPTEXT_VIDEO };
const char *helptexts[] =
{
	0,
	"                                Welcome to MiSTer!  Use the cursor keys to navigate the menus.  Use space bar or enter to select an item.  Press Esc or F12 to exit the menus.  Joystick emulation on the numeric keypad can be toggled with the numlock or scrlock key, while pressing Ctrl-Alt-0 (numeric keypad) toggles autofire mode.",
	"                                Minimig can emulate an A600/A1200 IDE harddisk interface.  The emulation can make use of Minimig-style hardfiles (complete disk images) or UAE-style hardfiles (filesystem images with no partition table).",
	"                                Minimig's processor core can emulate a 68000 or 68020 processor (though the 68020 mode is still experimental.)  If you're running software built for 68000, there's no advantage to using the 68020 mode, since the 68000 emulation runs just as fast.",
	"                                Minimig can make use of up to 2 megabytes of Chip RAM, up to 1.5 megabytes of Slow RAM (A500 Trapdoor RAM), and up to 24 megabytes of true Fast RAM.  To use the HRTmon feature you will need a file on the SD card named hrtmon.rom. HRTMon is not compatible with Fast RAM.",
	"                                Minimig's video features include a blur filter, to simulate the poorer picture quality on older monitors, and also scanline generation to simulate the appearance of a screen with low vertical resolution.",
	0
};

const char *info_top = "\x80\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x82";
const char *info_bottom = "\x85\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x84";

// one screen width
const char* HELPTEXT_SPACER = "                                ";
char helptext_custom[1024];

const char* scanlines[] = { "Off","25%","50%","75%" };
const char* stereo[] = { "Mono","Stereo" };
const char* atari_chipset[] = { "ST","STE","MegaSTE","STEroids" };

unsigned char config_autofire = 0;

// file selection menu variables
char fs_pFileExt[13] = "xxx";
unsigned char fs_ExtLen = 0;
unsigned char fs_Options;
unsigned char fs_MenuSelect;
unsigned char fs_MenuCancel;

char* GetExt(char *ext)
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

int changeDir(char *dir)
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
			int len = strlen(p+1);
			if (len > sizeof(curdir) - 1) len = sizeof(curdir) - 1;
			strncpy(curdir, p+1, len);
		}
		else
		{
			int len = strlen(SelectedPath);
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

#define HomeDir (is_minimig() ? "Amiga" : is_archie() ? "Archie" : user_io_get_core_name())

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
	else
	{
		if (strncasecmp(HomeDir, SelectedPath, strlen(HomeDir))) strcpy(SelectedPath, HomeDir);
	}

	ScanDirectory(SelectedPath, SCANF_INIT, pFileExt, Options);
	if (!flist_nDirEntries())
	{
		SelectedPath[0] = 0;
		ScanDirectory(SelectedPath, SCANF_INIT, pFileExt, Options);
	}

	AdjustDirectory(SelectedPath);

	strcpy(fs_pFileExt, pFileExt);
	fs_ExtLen = strlen(fs_pFileExt);
	fs_Options = Options;
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

// prints input as a string of binary (on/off) values
// assumes big endian, returns using special characters (checked box/unchecked box)
void siprintbinary(char* buffer, size_t const size, void const * const ptr)
{
	unsigned char *b = (unsigned char*)ptr;
	unsigned char byte;
	int i, j;
	memset(buffer, '\0', sizeof(buffer));
	for (i = size - 1; i >= 0; i--)
	{
		for (j = 0; j<8; j++)
		{
			byte = (b[i] >> j) & 1;
			buffer[j] = byte ? '\x1a' : '\x19';
		}
	}
	return;
}

unsigned char getIdx(char *opt)
{
	if ((opt[1] >= '0') && (opt[1] <= '9')) return opt[1] - '0';
	if ((opt[1] >= 'A') && (opt[1] <= 'V')) return opt[1] - 'A' + 10;
	return 0; // basically 0 cannot be valid because used as a reset. Thus can be used as a error.
}

unsigned int getStatus(char *opt, unsigned int status)
{
	char idx1 = getIdx(opt);
	char idx2 = getIdx(opt + 1);
	unsigned int x = (status & (1 << idx1)) ? 1 : 0;

	if (idx2>idx1) {
		x = status >> idx1;
		x = x & ~(0xffffffff << (idx2 - idx1 + 1));
	}

	return x;
}

unsigned int setStatus(char *opt, unsigned int status, unsigned int value)
{
	unsigned char idx1 = getIdx(opt);
	unsigned char idx2 = getIdx(opt + 1);
	unsigned long x = 1;

	if (idx2>idx1) x = ~(0xffffffff << (idx2 - idx1 + 1));
	x = x << idx1;

	return (status & ~x) | ((value << idx1) & x);
}

unsigned int getStatusMask(char *opt)
{
	char idx1 = getIdx(opt);
	char idx2 = getIdx(opt + 1);
	unsigned int x = 1;

	if (idx2>idx1) x = ~(0xffffffff << (idx2 - idx1 + 1));

	//printf("grtStatusMask %d %d %x\n", idx1, idx2, x);

	return x << idx1;
}

// conversion table of Amiga keyboard scan codes to ASCII codes
const uint8_t keycode_table[128] =
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
	static unsigned long delay;
	static unsigned long repeat;
	static unsigned char repeat2;
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
	if (!user_io_osd_is_visible() && is_menu_core()) c = KEY_F12;

	// generate repeat "key-pressed" events
	if ((c1 & UPSTROKE) || (!c1))
	{
		hold_cnt = 0;
		repeat = GetTimer(REPEATDELAY);
	}
	else if (CheckTimer(repeat))
	{
		repeat = GetTimer(REPEATRATE);
		if (GetASCIIKey(c1) || ((menustate == MENU_8BIT_SYSTEM2) && (menusub == 6)))
		{
			c = c1;
			hold_cnt++;
		}
	}

	// currently no key pressed
	if (!c)
	{
		static unsigned char last_but = 0;
		unsigned char but = user_io_menu_button();
		if (!but && last_but) c = KEY_F12;
		last_but = but;
	}
	return(c);
}

char* getNet(int spec)
{
	int netType = 0;
	struct ifaddrs *ifaddr, *ifa, *ifae = 0, *ifaw = 0;
	int family, s;
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

void printSysInfo()
{
	if (!sysinfo_timer || CheckTimer(sysinfo_timer))
	{
		sysinfo_timer = GetTimer(2000);
		struct battery_data_t bat;
		int hasbat = getBattery(0, &bat);
		int n = 10;

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

int  firstmenu = 0;
int  adjvisible;
char lastrow[256];

void MenuWrite(unsigned char n, const char *s, unsigned char invert, unsigned char stipple, int arrow = 0)
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

void HandleUI(void)
{
	switch (user_io_core_type())
	{
	case CORE_TYPE_MIST:
	case CORE_TYPE_MINIMIG2:
	case CORE_TYPE_8BIT:
	case CORE_TYPE_SHARPMZ:
	case CORE_TYPE_ARCHIE:
		break;

	default:
		// No UI in unknown cores.
		return;
	}
	
	struct RigidDiskBlock *rdb;

	static char opensave;
	char *p;
	char s[40];
	unsigned char m, up, down, select, menu, right, left, plus, minus;
	uint8_t mod;
	unsigned long len;
	char enable;
	static int reboot_req = 0;
	static long helptext_timer;
	static const char *helptext;
	static char helpstate = 0;
	static char drive_num = 0;
	static char flag;
	uint8_t keys[6] = { 0,0,0,0,0,0 };
	uint16_t keys_ps2[6] = { 0,0,0,0,0,0 };

	char usb_id[64];

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

	switch (c)
	{
	case KEY_F12:
		menu = true;
		menu_key_set(KEY_F12 | UPSTROKE);
		break;
	case KEY_F1:
		if (is_menu_core())
		{
			unsigned long status = (user_io_8bit_set_status(0, 0)>>1)&7;
			if (status == 5) status = 0;
				else status++;
			status <<= 1;
			user_io_8bit_set_status(status, 0xE);
			FileSaveConfig(user_io_create_config_name(), &status, 4);
		}
		break;

		// Within the menu the esc key acts as the menu key. problem:
		// if the menu is left with a press of ESC, then the follwing
		// break code for the ESC key when the key is released will 
		// reach the core which never saw the make code. Simple solution:
		// react on break code instead of make code
	case KEY_ESC | UPSTROKE:
		if (menustate != MENU_NONE2)
			menu = true;
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
		if (down && (menumask >= (1 << (menusub + 1))))	// Any active entries left?
		{
			do
			{
				menusub++;
			} while ((menumask & (1 << menusub)) == 0);
			menustate = parentstate;
		}

		if (up && menusub > 0)
		{
			do
			{
				--menusub;
			} while ((menumask & (1 << menusub)) == 0);
			menustate = parentstate;
		}
	}

    // SHARPMZ Series Menu - This has been located within the sharpmz.cpp code base in order to keep updates to common code
    // base to a minimum and shrink its size. The UI is called with the basic state data and it handles everything internally,
    // only updating values in this function as necessary.
    //
    if (user_io_core_type() == CORE_TYPE_SHARPMZ)
        sharpmz_ui(MENU_NONE1, MENU_NONE2, MENU_8BIT_SYSTEM1, MENU_FILE_SELECT1, &parentstate, &menustate, &menusub, &menusub_last, &menumask, SelectedPath, &helptext, helptext_custom, &fs_ExtLen, &fs_Options, &fs_MenuSelect, &fs_MenuCancel, fs_pFileExt, menu, select, up, down, left, right, plus, minus);

	// Switch to current menu screen
	switch (menustate)
	{
		/******************************************************************/
		/* no menu selected                                               */
		/******************************************************************/
	case MENU_NONE1:
		helptext = helptexts[HELPTEXT_NONE];
		menumask = 0;
		OsdDisable();
		menustate = MENU_NONE2;
		OsdSetSize(8);
		firstmenu = 0;
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
				SelectFile(0, SCANO_CORES, MENU_FIRMWARE_CORE_FILE_SELECTED1, MENU_NONE1);
			}
			else if (user_io_core_type() == CORE_TYPE_MINIMIG2) menustate = MENU_MAIN1;
			else if (user_io_core_type() == CORE_TYPE_MIST) menustate = MENU_MIST_MAIN1;
			else if (user_io_core_type() == CORE_TYPE_ARCHIE) menustate = MENU_ARCHIE_MAIN1;
			else {
				if (is_menu_core())
				{
					OsdCoreNameSet("");
					SelectFile(0, SCANO_CORES, MENU_FIRMWARE_CORE_FILE_SELECTED1, MENU_FIRMWARE1);
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
		OsdSetTitle(user_io_get_core_name(), OSD_ARROW_RIGHT);

		menumask = 0x3f;
		OsdWrite(0, "", 0, 0);

		strcpy(s, " Floppy 0: ");
		strncat(s, archie_get_floppy_name(0),27);
		OsdWrite(1, s, menusub == 0, 0);

		strcpy(s, " Floppy 1: ");
		strncat(s, archie_get_floppy_name(1), 27);
		OsdWrite(2, s, menusub == 1, 0);

		OsdWrite(3, "", 0, 0);

		strcpy(s, " OS ROM: ");
		strcat(s, archie_get_rom_name());
		OsdWrite(4, s, menusub == 2, 0);

		OsdWrite(5, "", 0, 0);

		strcpy(s, " Aspect ratio:      ");
		strcat(s, archie_get_ar() ? "16:9" : "4:3");
		OsdWrite(6, s, menusub == 3, 0);

		OsdWrite(7, "", 0, 0);
		sprintf(s, " Stereo mix:        %s", config_stereo_msg[archie_get_amix()]);
		OsdWrite(8, s, menusub == 4, 0);

		OsdWrite(9, "", 0, 0);
		sprintf(s, " Swap joysticks:    %s", user_io_get_joyswap() ? "Yes" : "No");
		OsdWrite(10, s, menusub == 5, 0);

		for (int i = 11; i<15; i++) OsdWrite(i, "", 0, 0);

		OsdWrite(15, STD_EXIT, menusub == 6, 0);
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
				if (archie_floppy_is_inserted(menusub)) {
					archie_set_floppy(menusub, NULL);
					menustate = MENU_ARCHIE_MAIN1;
				}
				else
					SelectFile("ADF", SCANO_DIR, MENU_ARCHIE_MAIN_FILE_SELECTED, MENU_ARCHIE_MAIN1);
				break;

			case 2:  // Load ROM
				SelectFile("ROM", 0, MENU_ARCHIE_MAIN_FILE_SELECTED, MENU_ARCHIE_MAIN1);
				break;

			case 3:
				archie_set_ar(!archie_get_ar());
				menustate = MENU_ARCHIE_MAIN1;
				break;

			case 4:
				archie_set_amix(archie_get_amix()+1);
				menustate = MENU_ARCHIE_MAIN1;
				break;

			case 5:
				user_io_set_joyswap(!user_io_get_joyswap());
				menustate = MENU_ARCHIE_MAIN1;
				break;

			case 6:  // Exit
				menustate = MENU_NONE1;
				break;
			}
		}

		if (right)
		{
			menustate = MENU_8BIT_SYSTEM1;
			menusub = 0;
		}
		break;

	case MENU_ARCHIE_MAIN_FILE_SELECTED: // file successfully selected
		if (menusub == 0) archie_set_floppy(0, SelectedPath);
		if (menusub == 1) archie_set_floppy(1, SelectedPath);
		if (menusub == 2) archie_set_rom(SelectedPath);
		menustate = MENU_ARCHIE_MAIN1;
		break;

		/******************************************************************/
		/* 8 bit main menu                                                */
		/******************************************************************/

	case MENU_8BIT_MAIN1: {
		int entry = 0;
		while(1)
		{
			adjvisible = 0;
			entry = 0;
			int selentry = 0;
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
				unsigned long status = user_io_8bit_set_status(0, 0);  // 0,0 gets status

				p = user_io_8bit_get_string(i++);
				//printf("Option %d: %s\n", i-1, p);

				// check for 'F'ile or 'S'D image strings
				if (p && ((p[0] == 'F') || (p[0] == 'S')))
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
						else            strcpy(s, " Mount *.");
					}
					pos = s + strlen(s);
					substrcpy(pos, p, 1);
					strcpy(pos, GetExt(pos));
					MenuWrite(entry, s, menusub == selentry, 0);

					// add bit in menu mask
					menumask = (menumask << 1) | 1;
					entry++;
					selentry++;
				}

				// check for 'T'oggle and 'R'eset (toggle and then close menu) strings
				if (p && ((p[0] == 'T') || (p[0] == 'R')))
				{

					s[0] = ' ';
					substrcpy(s + 1, p, 1);
					MenuWrite(entry, s, menusub == selentry, 0);

					// add bit in menu mask
					menumask = (menumask << 1) | 1;
					entry++;
					selentry++;
				}

				// check for 'O'ption strings
				if (p && (p[0] == 'O'))
				{
					//option handled by ARM
					if (p[1] == 'X') p++;

					unsigned long x = getStatus(p, status);

					// get currently active option
					substrcpy(s, p, 2 + x);
					char l = strlen(s);
					if (!l)
					{
						// option's index is outside of available values.
						// reset to 0.
						x = 0;
						user_io_8bit_set_status(setStatus(p, status, x), 0xffffffff);
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

					MenuWrite(entry, s, menusub == selentry, 0);

					// add bit in menu mask
					menumask = (menumask << 1) | 1;
					entry++;
					selentry++;
				}

				// delimiter
				if (p && (p[0] == '-'))
				{
					MenuWrite(entry, "", 0, 0);
					entry++;
				}

				// check for 'V'ersion strings
				if (p && (p[0] == 'V'))
				{
					// get version string
					strcpy(s, OsdCoreName());
					strcat(s, " ");
					substrcpy(s + strlen(s), p, 1);
					OsdCoreNameSet(s);
				}

				if (p && (p[0] == 'J'))
				{
					// joystick button names.
					for (int n = 0; n < 12; n++)
					{
						substrcpy(joy_bnames[n], p, n + 1);
						//printf("joy_bname = %s\n", joy_bnames[n]);
						if (!joy_bnames[n][0]) break;
						joy_bcount++;
					}

					//printf("joy_bcount = %d\n", joy_bcount);
				}
			} while (p);

			if (!entry) break;

			for (; entry < OsdGetSize() - 1; entry++) MenuWrite(entry, "", 0, 0);

			// exit row
			MenuWrite(entry, STD_EXIT, menusub == selentry, 0, OSD_ARROW_RIGHT);
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
				static char ext[13];
				char fs_present;
				p = user_io_8bit_get_string(1);
				fs_present = p && strlen(p);

				int entry = 0;
				int i = 1;
				while (1)
				{
					p = user_io_8bit_get_string(i++);
					if (!p || p[0] < 'A') continue;
					if (entry == menusub) break;
					entry++;
				}

				if (p[0] == 'F')
				{
					opensave = (p[1] == 'S');
					substrcpy(ext, p, 1);
					while (strlen(ext) < 3) strcat(ext, " ");
					SelectFile(ext, SCANO_DIR, MENU_8BIT_MAIN_FILE_SELECTED, MENU_8BIT_MAIN1);
				}
				else if (p[0] == 'S')
				{
					drive_num = 0;
					if (p[1] >= '0' && p[1] <= '3') drive_num = p[1] - '0';
					substrcpy(ext, p, 1);
					while (strlen(ext) < 3) strcat(ext, " ");
					SelectFile(ext, SCANO_DIR | SCANO_UMOUNT, MENU_8BIT_MAIN_IMAGE_SELECTED, MENU_8BIT_MAIN1);
				}
				else if (p[0] == 'O')
				{
					int byarm = 0;
					if (p[1] == 'X')
					{
						byarm = 1;
						p++;
					}

					unsigned long status = user_io_8bit_set_status(0, 0);  // 0,0 gets status
					unsigned long x = getStatus(p, status) + 1;

					if (byarm && is_x86_core())
					{
						if (p[1] == '2') x86_set_fdd_boot(!(x&1));
					}
					// check if next value available
					substrcpy(s, p, 2 + x);
					if (!strlen(s)) x = 0;

					user_io_8bit_set_status(setStatus(p, status, x), 0xffffffff);

					menustate = MENU_8BIT_MAIN1;
				}
				else if ((p[0] == 'T') || (p[0] == 'R'))
				{
					// determine which status bit is affected
					unsigned long mask = 1 << getIdx(p);
					if (mask == 1 && is_x86_core())
					{
						x86_init();
						menustate = MENU_NONE1;
					}
					else
					{
						unsigned long status = user_io_8bit_set_status(0, 0);

						user_io_8bit_set_status(status ^ mask, mask);
						user_io_8bit_set_status(status, mask);
						menustate = MENU_8BIT_MAIN1;
						if (p[0] == 'R') menustate = MENU_NONE1;
					}

				}
			}
		}
		else if (right)
		{
			menustate = MENU_8BIT_SYSTEM1;
			menusub = 0;
		}
		break;

	case MENU_8BIT_MAIN_FILE_SELECTED:
		printf("File selected: %s\n", SelectedPath);
		user_io_file_tx(SelectedPath, user_io_ext_idx(SelectedPath, fs_pFileExt) << 6 | (menusub + 1), opensave);
		menustate = MENU_NONE1;
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
		menustate = SelectedPath[0] ? MENU_NONE1 : MENU_8BIT_MAIN1;
		break;

	case MENU_8BIT_SYSTEM1:
		OsdSetSize(16);
		helptext = 0;
		menumask = 0xf7;
		reboot_req = 0;

		OsdSetTitle("System", OSD_ARROW_LEFT);
		menustate = MENU_8BIT_SYSTEM2;
		parentstate = MENU_8BIT_SYSTEM1;
		
		s[0] = 0;
		m = 0;
		if(user_io_get_uart_mode())
		{
			int mode = 0;
			struct stat filestat;
			if (!stat("/tmp/uartmode1", &filestat)) mode = 1;
			if (!stat("/tmp/uartmode2", &filestat)) mode = 2;
			
			menumask |= 8;
			sprintf(s, " UART connection     %s", config_uart_msg[mode]);
			OsdWrite(3, s, menusub == 3, 0);
		}
		else
		{
			OsdWrite(m++, "", 0, 0);
		}

		OsdWrite(m++, " Core                      \x16", menusub == 0, 0);
		OsdWrite(m++, " Define joystick buttons   \x16", menusub == 1, 0);
		OsdWrite(m++, " Button/Key remap for game \x16", menusub == 2, 0);
		OsdWrite(4, "", 0, 0);

		m = 0;
		if (user_io_core_type() == CORE_TYPE_MINIMIG2)
		{
			m = 1;
			menumask &= ~0x20;
		}
		OsdWrite(5, m ? " Reset the core" : " Reset settings", menusub == 4, user_io_core_type() == CORE_TYPE_ARCHIE);
		OsdWrite(6, m ? "" : " Save settings", menusub == 5, 0);
		OsdWrite(7, "", 0, 0);
		OsdWrite(8, " Reboot (hold \x16 cold reboot)", menusub == 6, 0);
		OsdWrite(9, " About", menusub == 7, 0);
		sysinfo_timer = 0;
		break;

	case MENU_8BIT_SYSTEM2:
		if (menu)
                {
			switch (user_io_core_type()) {
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
				SelectFile(0, SCANO_CORES, MENU_FIRMWARE_CORE_FILE_SELECTED1, MENU_8BIT_SYSTEM1);
				menusub = 0;
				break;
			case 1:
				if (is_minimig())
				{
					joy_bcount = 7;
					strcpy(joy_bnames[0], "Red/Fire");
					strcpy(joy_bnames[1], "Blue");
					strcpy(joy_bnames[2], "Yellow");
					strcpy(joy_bnames[3], "Green");
					strcpy(joy_bnames[4], "Right Trigger");
					strcpy(joy_bnames[5], "Left Trigger");
					strcpy(joy_bnames[6], "Pause");
				}
				start_map_setting(joy_bcount ? joy_bcount+5 : 9);
				menustate = MENU_JOYDIGMAP;
				menusub = 0;
				break;
			case 2:
				start_map_setting(-1);
				menustate = MENU_JOYKBDMAP;
				menusub = 0;
				break;
			case 3:
				{
					int mode = 0;
					struct stat filestat;
					if (!stat("/tmp/uartmode1", &filestat)) mode = 1;
					if (!stat("/tmp/uartmode2", &filestat)) mode = 2;
					mode++;
					if (mode > 3) mode = 0;
					sprintf(s, "uartmode %d", mode);
					system(s);
					menustate = MENU_8BIT_SYSTEM1;

					sprintf(s, "uartmode.%s", user_io_get_core_name_ex());
					FileSaveConfig(s, &mode, 4);
				}
				break;
			case 4:
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
			case 5:
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
					unsigned long status = user_io_8bit_set_status(0, 0);
					printf("Saving config to %s\n", filename);
					FileSaveConfig(filename, &status, 4);
					if (is_x86_core()) x86_config_save();
				}
				break;
			case 6:
				{
					reboot_req = 1;

					int off = hold_cnt/3;
					if (off > 5) reboot(1);

					sprintf(s, " Cold Reboot");
					p = s + 5 - off;
					OsdWrite(8, p, menusub == 6, 0);
				}
				break;
			case 7:
				menustate = MENU_8BIT_ABOUT1;
				menusub = 0;
				break;
			}
		}
		else if (left)
		{
			// go back to core requesting this menu
			switch (user_io_core_type()) {
			case CORE_TYPE_MINIMIG2:
				menusub = 0;
				menustate = MENU_MAIN1;
				break;
			case CORE_TYPE_MIST:
				menusub = 5;
				menustate = MENU_MIST_MAIN1;
				break;
			case CORE_TYPE_ARCHIE:
				menusub = 0;
				menustate = MENU_ARCHIE_MAIN1;
				break;
			case CORE_TYPE_8BIT:
				menusub = 0;
				menustate = MENU_8BIT_MAIN1;
				break;
			case CORE_TYPE_SHARPMZ:
				menusub   = menusub_last;
				menustate = sharpmz_default_ui_state();
				break;
			}
		}

		if(!hold_cnt && reboot_req) fpga_load_rbf("menu.rbf");

		printSysInfo();
		break;

	case MENU_JOYDIGMAP:
		helptext = 0;
		menumask = 1;
		OsdSetTitle("Joystick", 0);
		menustate = MENU_JOYDIGMAP1;
		parentstate = MENU_JOYDIGMAP;
		for (int i = 0; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, "           Cancel", menusub == 0, 0);
		break;

	case MENU_JOYDIGMAP1:
		{
			const char* p = 0;
			if (get_map_button() < 4)
			{
				p = joy_button_map[get_map_button()];
			}
			else if (joy_bcount)
			{
				p = (get_map_button() < joy_bcount + 4) ? joy_bnames[get_map_button() - 4] : joy_button_map[8 + get_map_type()];
			}
			else
			{
				p = (get_map_button() < 8) ? joy_button_map[get_map_button()] : joy_button_map[8 + get_map_type()];
			}

			s[0] = 0;
			int len = (30 - (strlen(p) + 7)) / 2;
			while (len > 0)
			{
				strcat(s, " ");
				len--;
			}

			strcat(s, "Press: ");
			strcat(s, p);
			OsdWrite(3, s, 0, 0);
			if (get_map_button())
			{
				if (get_map_type()) OsdWrite(OsdGetSize() - 1, "    finish (SPACE - skip)", menusub == 0, 0);
				else OsdWrite(OsdGetSize() - 1, "", 0, 0);

				sprintf(s, "   %s ID: %04x:%04x", get_map_type() ? "Joystick" : "Keyboard", get_map_vid(), get_map_pid());
				OsdWrite(5, s, 0, 0);
			}

			if (select || menu || get_map_button() >= (joy_bcount ? joy_bcount + 5 : 9))
			{
				finish_map_setting(menu);
				if (is_menu_core())
				{
					menustate = MENU_FIRMWARE1;
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
			OsdWrite(OsdGetSize() - 1, " Enter \x16 Finish, Esc \x16 Clear", menusub == 0, 0);
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
		OsdDrawLogo(0, 0, 1);
		OsdDrawLogo(1, 1, 1);
		OsdDrawLogo(2, 2, 1);
		OsdDrawLogo(3, 3, 1);
		OsdDrawLogo(4, 4, 1);

		sprintf(s, "   ARM  s/w ver. %s", version + 5);
		OsdWrite(10, s, 0, 0, 1);

		s[0] = 0;
		if (user_io_core_type() != CORE_TYPE_MINIMIG2)
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
		OsdWrite(11, s, 0, 0, 1);
		OsdWrite(12, "", 0, 0, 1);
		ScrollText(13, "                                 MiSTer by Sorgelig, based on MiST by Till Harbaum, Minimig by Dennis van Weeren and other projects. MiSTer hardware and software is distributed under the terms of the GNU General Public License version 3. MiSTer FPGA cores are the work of their respective authors under individual licensing.", 0, 0, 0, 0);
		OsdWrite(14, "", 0, 0, 1);
		OsdWrite(15, "", 0, 0, 1);

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
		for (int i = 0; i<2; i++) {
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
		for (int i = 0; i<2; i++) {
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
				tos_update_sysctrl(tos_system_ctrl() & ~(TOS_CONTROL_STE | TOS_CONTROL_MSTE) |
					(chipset << 23));
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
				if (left && (tos_get_video_adjust(menusub - 2) > -100))
					tos_set_video_adjust(menusub - 2, -1);

				if (right && (tos_get_video_adjust(menusub - 2) < 100))
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
		OsdSetTitle("Minimig", OSD_ARROW_RIGHT);
		helptext = helptexts[HELPTEXT_MAIN];

		OsdWrite(0, "", 0, 0);

		// floppy drive info
		// We display a line for each drive that's active
		// in the config file, but grey out any that the FPGA doesn't think are active.
		// We also print a help text in place of the last drive if it's inactive.
		for (int i = 0; i < 4; i++)
		{
			if (i == config.floppy.drives + 1)
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
						if (p = strrchr(df[i].name, '/'))
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
				else if (i <= config.floppy.drives)
				{
					strcat(s, "* active after reset *");
				}
				else
					strcpy(s, "");
				OsdWrite(i+1, s, menusub == i, (i>drives) || (i>config.floppy.drives));
			}
		}
		sprintf(s, " Floppy disk turbo : %s", config.floppy.speed ? "on" : "off");
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
		else if (plus && (config.floppy.drives<3))
		{
			config.floppy.drives++;
			ConfigFloppy(config.floppy.drives, config.floppy.speed);
			menustate = MENU_MAIN1;
		}
		else if (minus && (config.floppy.drives>0))
		{
			config.floppy.drives--;
			ConfigFloppy(config.floppy.drives, config.floppy.speed);
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
				config.floppy.speed ^= 1;
				ConfigFloppy(config.floppy.drives, config.floppy.speed);
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
		if (parentstate != menustate)
		{
			menumask = 0x400;
			for (int i = 0; i < 10; i++) if (GetConfigDisplayName(i)) menumask |= 1<<i;
		}
		parentstate = menustate;
		OsdSetTitle("Load config", 0);

		OsdWrite(0, "", 0, 0);
		OsdWrite(1, "", 0, 0);
		OsdWrite(2, " Default", menusub == 0, (menumask & 1) == 0);
		OsdWrite(3, "", 0, 0);
		for (int i = 1; i < 10; i++)
		{
			static char name[64];
			sprintf(name, " %d ", i);
			if (menumask & (1 << i)) strcat(name, GetConfigDisplayName(i));
			OsdWrite(3+i, name, menusub == i, !(menumask & (1 << i)));
		}

		for (int i = 13; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
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
				LoadConfiguration(menusub);
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

		ScrollLongName(); // scrolls file name if longer than display line

		if (c == KEY_HOME)
		{
			ScanDirectory(SelectedPath, SCANF_INIT, fs_pFileExt, fs_Options);
			menustate = MENU_FILE_SELECT1;
		}

		if (c == KEY_BACKSPACE)
		{
			if (fs_Options & SCANO_UMOUNT)
			{
				for (int i = 0; i < OsdGetSize(); i++) OsdWrite(i, "", 0, 0);
				OsdWrite(OsdGetSize() / 2, "   Unmounting the image", 0, 0);
				usleep(1500000);
				SelectedPath[0] = 0;
				menustate = fs_MenuSelect;
			}
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
			if(flist_SelectedItem()->d_type == DT_DIR)
			{
				changeDir(flist_SelectedItem()->d_name);
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
					strcat(SelectedPath, flist_SelectedItem()->d_name);

					menustate = fs_MenuSelect;
				}
			}
		}

		if (menu)
		{
			if (flist_nDirEntries() && flist_SelectedItem()->d_type != DT_DIR)
			{
				SelectedDir[0] = 0;
				if (strlen(SelectedPath))
				{
					strcpy(SelectedDir, SelectedPath);
					strcat(SelectedPath, "/");
				}
				strcat(SelectedPath, flist_SelectedItem()->d_name);
			}

			if (!strcasecmp(fs_pFileExt, "RBF")) SelectedPath[0] = 0;
			menustate = fs_MenuCancel;
		}

		break;

		/******************************************************************/
		/* reset menu                                                     */
		/******************************************************************/
	case MENU_RESET1:
		m = 0;
		if (user_io_core_type() == CORE_TYPE_MINIMIG2) m = 1;
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
		if (user_io_core_type() == CORE_TYPE_MINIMIG2) m = 1;
		if (user_io_core_type() == CORE_TYPE_SHARPMZ)  m = 2;

		if (select && menusub == 0)
		{
			if (m)
			{
				menustate = MENU_NONE1;
				MinimigReset();
			}
            else if(m == 2)
            {
				menustate = MENU_8BIT_SYSTEM1;
                sharpmz_reset_config(1);
            }
			else
			{
				char *filename = user_io_create_config_name();
				unsigned long status = user_io_8bit_set_status(0, 0xffffffff);
				printf("Saving config to %s\n", filename);
				FileSaveConfig(filename, &status, 4);
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

		OsdWrite(0, "", 0, 0);
		OsdWrite(1, "", 0, 0);
		OsdWrite(2, " Default", menusub == 0, 0);
		OsdWrite(3, "", 0, 0);

		for (int i = 1; i < 10; i++)
		{
			static char name[64];
			sprintf(name, " %d ", i);
			const char *cfgname = GetConfigDisplayName(i);
			if (cfgname) strcat(name, GetConfigDisplayName(i));
			OsdWrite(3 + i, name, menusub == i, !(menumask & (1 << i)));
		}

		for (int i = 13; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, STD_EXIT, menusub == 10, 0);

		menustate = MENU_SAVECONFIG_2;
		break;

	case MENU_SAVECONFIG_2:
		if (select)
		{
			if (menusub<10) SaveConfiguration(menusub);
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

		OsdWrite(0, "", 0, 0);
		strcpy(s, "         CPU : ");
		strcat(s, config_cpu_msg[config.cpu & 0x03]);
		OsdWrite(1, s, menusub == 0, 0);
		strcpy(s, "       Turbo : ");
		strcat(s, config_turbo_msg[(config.cpu >> 2) & 0x03]);
		OsdWrite(2, s, menusub == 1, 0);
		OsdWrite(3, "", 0, 0);
		strcpy(s, "       Video : ");
		strcat(s, config.chipset & CONFIG_NTSC ? "NTSC" : "PAL");
		OsdWrite(4, s, menusub == 2, 0);
		strcpy(s, "     Chipset : ");
		strcat(s, config_chipset_msg[(config.chipset >> 2) & 7]);
		OsdWrite(5, s, menusub == 3, 0);
		OsdWrite(6, "", 0, 0);
		strcpy(s, "     CD32Pad : ");
		strcat(s, config_cd32pad_msg[(config.autofire >> 2) & 1]);
		OsdWrite(7, s, menusub == 4, 0);
		strcpy(s, "    Joy Swap : ");
		strcat(s, (config.autofire & 0x8)? "ON" : "OFF");
		OsdWrite(8, s, menusub == 5, 0);
		for (int i = 9; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, STD_EXIT, menusub == 6, 0);

		menustate = MENU_SETTINGS_CHIPSET2;
		break;

	case MENU_SETTINGS_CHIPSET2:

		if (down && menusub < 6)
		{
			menusub++;
			menustate = MENU_SETTINGS_CHIPSET1;
		}

		if (up && menusub > 0)
		{
			menusub--;
			menustate = MENU_SETTINGS_CHIPSET1;
		}

		if (select)
		{
			if (menusub == 0)
			{
				menustate = MENU_SETTINGS_CHIPSET1;
				int _config_cpu = config.cpu & 0x3;
				_config_cpu += 1;
				if (_config_cpu == 0x02) _config_cpu += 1;
				config.cpu = (config.cpu & 0xfc) | (_config_cpu & 0x3);
				ConfigCPU(config.cpu);
			}
			else if (menusub == 1)
			{
				menustate = MENU_SETTINGS_CHIPSET1;
				int _config_turbo = (config.cpu >> 2) & 0x3;
				_config_turbo += 1;
				config.cpu = (config.cpu & 0x3) | ((_config_turbo & 0x3) << 2);
				ConfigCPU(config.cpu);
			}
			else if (menusub == 2)
			{
				config.chipset ^= CONFIG_NTSC;
				menustate = MENU_SETTINGS_CHIPSET1;
				ConfigChipset(config.chipset);
			}
			else if (menusub == 3)
			{
				switch (config.chipset & 0x1c) {
				case 0:
					config.chipset = (config.chipset & 3) | CONFIG_A1000;
					break;
				case CONFIG_A1000:
					config.chipset = (config.chipset & 3) | CONFIG_ECS;
					break;
				case CONFIG_ECS:
					config.chipset = (config.chipset & 3) | CONFIG_AGA | CONFIG_ECS;
					break;
				case (CONFIG_AGA | CONFIG_ECS) :
					config.chipset = (config.chipset & 3) | 0;
					break;
				}

				menustate = MENU_SETTINGS_CHIPSET1;
				ConfigChipset(config.chipset);
			}
			else if (menusub == 4)
			{
				config.autofire ^= 0x4;
				menustate = MENU_SETTINGS_CHIPSET1;
				ConfigAutofire(config.autofire, 0x4);
			}
			else if (menusub == 5)
			{
				config.autofire ^= 0x8;
				menustate = MENU_SETTINGS_CHIPSET1;
				ConfigAutofire(config.autofire, 0x8);
			}
			else if (menusub == 6)
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
		strcpy(s, " CHIP  : ");
		strcat(s, config_memory_chip_msg[config.memory & 0x03]);
		OsdWrite(1, s, menusub == 0, 0);
		strcpy(s, " SLOW  : ");
		strcat(s, config_memory_slow_msg[config.memory >> 2 & 0x03]);
		OsdWrite(2, s, menusub == 1, 0);
		strcpy(s, " FAST  : ");
		strcat(s, config_memory_fast_msg[config.memory >> 4 & 0x03]);
		OsdWrite(3, s, menusub == 2, 0);

		OsdWrite(4, "", 0, 0);

		strcpy(s, " ROM   : ");
		strncat(s, config.kickstart, 25);
		OsdWrite(5, s, menusub == 3, 0);

		strcpy(s, " HRTmon: ");
		strcat(s, (config.memory & 0x40) ? "enabled " : "disabled");
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
				config.memory = ((config.memory + 1) & 0x03) | (config.memory & ~0x03);
				menustate = MENU_SETTINGS_MEMORY1;
			}
			else if (menusub == 1)
			{
				config.memory = ((config.memory + 4) & 0x0C) | (config.memory & ~0x0C);
				menustate = MENU_SETTINGS_MEMORY1;
			}
			else if (menusub == 2)
			{
				config.memory = ((config.memory + 0x10) & 0x30) | (config.memory & ~0x30);
				menustate = MENU_SETTINGS_MEMORY1;
			}
			else if (menusub == 3)
			{
				SelectFile("ROM", 0, MENU_ROMFILE_SELECTED, MENU_SETTINGS_MEMORY1);
			}
			else if (menusub == 4)
			{
				config.memory ^= 0x40;
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
		SetKickstart(SelectedPath);
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
		if (config.enable_ide) menumask |= 0xAA;  // b010101010 - HD0/1/2/3 type
		OsdWrite(0, "", 0, 0);
		strcpy(s, " A600/A1200 IDE : ");
		strcat(s, config.enable_ide ? "On " : "Off");
		OsdWrite(1, s, menusub == 0, 0);
		OsdWrite(2, "", 0, 0);

		{
			int n = 3, m = 1, t = 4;
			for (int i = 0; i < 4; i++)
			{
				strcpy(s, (i & 2) ? " Secondary " : " Primary ");
				strcat(s, (i & 1) ? "Slave: " : "Master: ");
				strcat(s, config.hardfile[i].enabled ? "Enabled" : "Disabled");
				OsdWrite(n++, s, config.enable_ide ? (menusub == m++) : 0, config.enable_ide == 0);
				if (config.hardfile[i].filename[0])
				{
					strcpy(s, "                                ");
					strncpy(&s[7], config.hardfile[i].filename, 21);
				}
				else
				{
					strcpy(s, "       ** not selected **");
				}
				enable = config.enable_ide && config.hardfile[i].enabled;
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
				config.enable_ide = (config.enable_ide == 0);
				menustate = MENU_SETTINGS_HARDFILE1;
			}
			else if (menusub < 9)
			{
				if(menusub&1)
				{
					int num = (menusub - 1) / 2;
					config.hardfile[num].enabled = config.hardfile[num].enabled ? 0 : 1;
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
			int len = strlen(SelectedPath);
			if (len > sizeof(config.hardfile[num].filename) - 1) len = sizeof(config.hardfile[num].filename) - 1;
			if(len) memcpy(config.hardfile[num].filename, SelectedPath, len);
			config.hardfile[num].filename[len] = 0;
			menustate = checkHDF(config.hardfile[num].filename, &rdb) ? MENU_SETTINGS_HARDFILE1 : MENU_HARDFILE_SELECTED2;
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
			sprintf(s,    "   Cylinders: %d", rdb->rdb_Cylinders);
			OsdWrite(m++, s, 0, 0);
			sprintf(s,    "   Heads:     %d", rdb->rdb_Heads);
			OsdWrite(m++, s, 0, 0);
			sprintf(s,    "   Sectors:   %d", rdb->rdb_Sectors);
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
		menumask = 0x1f;
		parentstate = menustate;
		helptext = 0; // helptexts[HELPTEXT_VIDEO];

		OsdSetTitle("Video", OSD_ARROW_LEFT | OSD_ARROW_RIGHT);
		OsdWrite(0, "", 0, 0);
		strcpy(s, "  Scanlines     : ");
		strcat(s, config_scanlines_msg[config.scanlines & 0x3]);
		OsdWrite(1, s, menusub == 0, 0);
		strcpy(s, "  Video area by : ");
		strcat(s, config_blank_msg[(config.scanlines >> 6) & 3]);
		OsdWrite(2, s, menusub == 1, 0);
		strcpy(s, "  Aspect Ratio  : ");
		strcat(s, config_ar_msg[(config.scanlines >> 4) & 1]);
		OsdWrite(3, s, menusub == 2, 0);
		OsdWrite(4, "", 0, 0);
		strcpy(s, "  Stereo mix    : ");
		strcat(s, config_stereo_msg[config.audio & 3]);
		OsdWrite(5, s, menusub == 3, 0);
		for (int i = 6; i < OsdGetSize() - 1; i++) OsdWrite(i, "", 0, 0);
		OsdWrite(OsdGetSize() - 1, STD_EXIT, menusub == 4, 0);

		menustate = MENU_SETTINGS_VIDEO2;
		break;

	case MENU_SETTINGS_VIDEO2:
		if (select)
		{
			if (menusub == 0)
			{
				config.scanlines = ((config.scanlines + 1) & 0x03) | (config.scanlines & 0xfc);
				if ((config.scanlines & 0x03) > 2) config.scanlines = config.scanlines & 0xfc;
				menustate = MENU_SETTINGS_VIDEO1;
				ConfigVideo(config.filter.hires, config.filter.lores, config.scanlines);
			}
			else if (menusub == 1)
			{
				config.scanlines &= ~0x80;
				config.scanlines ^= 0x40;
				menustate = MENU_SETTINGS_VIDEO1;
				ConfigVideo(config.filter.hires, config.filter.lores, config.scanlines);
			}
			else if (menusub == 2)
			{
				config.scanlines &= ~0x20; // reserved for auto-ar
				config.scanlines ^= 0x10;
				menustate = MENU_SETTINGS_VIDEO1;
				ConfigVideo(config.filter.hires, config.filter.lores, config.scanlines);
			}
			else if (menusub == 3)
			{
				config.audio = (config.audio + 1) & 3;
				menustate = MENU_SETTINGS_VIDEO1;
				ConfigAudio(config.audio);
			}
			else if (menusub == 4)
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
	case MENU_FIRMWARE1:
		helptext = helptexts[HELPTEXT_NONE];
		parentstate = menustate;

		OsdSetTitle("System Settings", 0);
		OsdWrite(0, "", 0, 0);
		sprintf(s, "   ARM  s/w ver. %s", version + 5);
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
		menumask = 7;
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
		OsdWrite(9, "", 0, 0);
		sysinfo_timer = 0;

		menustate = MENU_STORAGE;

	case MENU_STORAGE:
		if (menu)
		{
			switch (user_io_core_type()) {
			case CORE_TYPE_MIST:
				menusub = 5;
				menustate = MENU_MIST_MAIN1;
				break;
			case CORE_TYPE_ARCHIE:
				menusub = 3;
				menustate = MENU_ARCHIE_MAIN1;
				break;
			default:
				menusub = 0;
				menustate = MENU_NONE1;
				break;
			}
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
				joy_bcount = 12;
				strcpy(joy_bnames[0], "BUTTON 1");
				strcpy(joy_bnames[1], "BUTTON 2");
				strcpy(joy_bnames[2], "BUTTON 3");
				strcpy(joy_bnames[3], "BUTTON 4");
				strcpy(joy_bnames[4], "RIGHT (Alt/M)");
				strcpy(joy_bnames[5], "LEFT (Alt/M)");
				strcpy(joy_bnames[6], "DOWN (Alt/M)");
				strcpy(joy_bnames[7], "UP (Alt/M)");
				strcpy(joy_bnames[8], "L.MOUSE");
				strcpy(joy_bnames[9], "R.MOUSE");
				strcpy(joy_bnames[10], "M.MOUSE");
				strcpy(joy_bnames[11], "Mouse Emu/Sniper");
				start_map_setting(17);
				menustate = MENU_JOYDIGMAP;
				menusub = 0;
				break;
			}
		}
		printSysInfo();
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
		}
		else
		{
			flag = 1;
			sprintf(s, "   Press key to map %02X to", get_map_button() & 0xFF);
			OsdWrite(3, s, 0, 0);
			OsdWrite(5, "      on any keyboard", 0, 0);
		}

		OsdWrite(OsdGetSize() - 1, "           finish", menusub == 0, 0);

		if (select || menu)
		{
			finish_map_setting(menu);
			menustate = MENU_FIRMWARE1;
			menusub = 1;
		}
		break;

	case MENU_FIRMWARE_CORE_FILE_SELECTED1:
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
						menustate = MENU_FIRMWARE_CORE_FILE_SELECTED2;
						break;
					}
				}

				strcpy(SelectedPath, SelectedRBF);
				AdjustDirectory(SelectedPath);
				cp_MenuCancel = fs_MenuCancel;
				fs_Options = 0;
				fs_MenuSelect = MENU_FIRMWARE_CORE_FILE_SELECTED2;
				fs_MenuCancel = MENU_FIRMWARE_CORE_FILE_CANCELED;
				menustate = MENU_FILE_SELECT1;
				break;
			}
		}

		// close OSD now as the new core may not even have one
		OsdDisable();
		fpga_load_rbf(SelectedRBF);
		break;

	case MENU_FIRMWARE_CORE_FILE_SELECTED2:
		OsdDisable();
		fpga_load_rbf(SelectedRBF, SelectedPath);
		menustate = MENU_NONE1;
		break;

	case MENU_FIRMWARE_CORE_FILE_CANCELED:
		SelectFile(0, SCANO_CORES, MENU_FIRMWARE_CORE_FILE_SELECTED1, cp_MenuCancel);
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

		if (!rtc_timer || CheckTimer(rtc_timer))
		{
			rtc_timer = GetTimer(1000);
			char str[64] = { 0 };
			sprintf(str, "  MiSTer   ");

			time_t t = time(NULL);
			struct tm tm = *localtime(&t);
			if (tm.tm_year >= 117)
			{
				strftime(str + strlen(str), sizeof(str) - 1 - strlen(str), "%b %d %a %H:%M:%S", &tm);
			}

			int netType = (int)getNet(0);
			if (netType) str[9] = 0x1b + netType;

			OsdWrite(16, "", 1, 0);
			OsdWrite(17, str, 1, 0);
			OsdWrite(18, "", 1, 0);
		}
	}
}

void ScrollLongName(void)
{
	// this function is called periodically when file selection window is displayed
	// it checks if predefined period of time has elapsed and scrolls the name if necessary

	static int len;
	int max_len;

	len = strlen(flist_SelectedItem()->d_name); // get name length
	if (flist_SelectedItem()->d_type == DT_REG) // if a file
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
			if ((len>l) && !strncasecmp(flist_SelectedItem()->d_name + len - l, e, l)) len -= l;
		}
	}

	max_len = 30; // number of file name characters to display (one more required for scrolling)
	if (flist_SelectedItem()->d_type == DT_DIR)
		max_len = 25; // number of directory name characters to display

	ScrollText(flist_iSelectedEntry()-flist_iFirstEntry(), flist_SelectedItem()->d_name, 2, len, max_len, 1);
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

			len = strlen(flist_DirItem(k)->d_name); // get name length

			if (!(flist_DirItem(k)->d_type == DT_DIR)) // if a file
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
					if ((len>l) && !strncasecmp(flist_DirItem(k)->d_name + len - l, e, l))
					{
						len -= l;
					}
				}
			}

			if (len > 28)
			{
				len = 27; // trim display length if longer than 30 characters
				s[28] = 22;
			}

			if((flist_DirItem(k)->d_type == DT_DIR) && (fs_Options & SCANO_CORES) && (flist_DirItem(k)->d_name[0] == '_'))
			{
				strncpy(s + 1, flist_DirItem(k)->d_name+1, len-1);
			}
			else
			{
				strncpy(s + 1, flist_DirItem(k)->d_name, len); // display only name
			}

			if (flist_DirItem(k)->d_type == DT_DIR) // mark directory with suffix
			{
				if (!strcmp(flist_DirItem(k)->d_name, ".."))
				{
					strcpy(&s[19], " <UP-DIR>");
				}
				else
				{
					strcpy(&s[22], " <DIR>");
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
	char i = 0, l = 1;

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
