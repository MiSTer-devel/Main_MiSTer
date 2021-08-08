/*
This file contains lookup information on known controllers
*/

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>

#include "joymapping.h"
#include "menu.h"
#include "input.h"
#include "user_io.h"
#include "cfg.h"

#define DPAD_COUNT 4

/*****************************************************************************/
static void trim(char * s)
{
	char *p = s;
	int l = strlen(p);
	if (!l) return;

	while (p[l - 1] == ' ') p[--l] = 0;
	while (*p && (*p == ' ')) ++p, --l;

	memmove(s, p, l + 1);
}

static char joy_names[NUMBUTTONS][32];
static int joy_count = 0;

static char joy_nnames[NUMBUTTONS][32];
static char joy_pnames[NUMBUTTONS][32];
static int defaults = 0;

static void read_buttons()
{
	char *p;

	memset(joy_names, 0, sizeof(joy_names));
	memset(joy_nnames, 0, sizeof(joy_nnames));
	memset(joy_pnames, 0, sizeof(joy_pnames));
	joy_count = 0;
	defaults = 0;

	user_io_read_confstr();

	// this option used as default name map (unless jn/jp is supplied)
	p = get_buttons(0);
	if (p)
	{
		for (int n = 0; n < NUMBUTTONS - DPAD_COUNT; n++)
		{
			substrcpy(joy_names[n], p, n);
			if (!joy_names[n][0]) break;

			printf("joy_name[%d] = %s\n", n, joy_names[n]);

			memcpy(joy_nnames[n], joy_names[n], sizeof(joy_nnames[0]));
			char *sstr = strchr(joy_nnames[n], '(');
			if (sstr) *sstr = 0;
			trim(joy_nnames[n]);

			if (!joy_nnames[n][0]) break;
			joy_count++;
		}
		printf("\n");
	}

	// - supports empty name to skip the button from default map
	// - only base button names must be used (ABXYLR Start Select)

	// name default map
	p = get_buttons(1);
	if (p)
	{
		memset(joy_nnames, 0, sizeof(joy_nnames));
		for (int n = 0; n < joy_count; n++)
		{
			substrcpy(joy_nnames[n], p, n);
			trim(joy_nnames[n]);
			if (joy_nnames[n][0]) printf("joy_nname[%d] = %s\n", n, joy_nnames[n]);
		}
		printf("\n");
	}

	// positional default map
	p = get_buttons(2);
	if (p)
	{
		defaults = cfg.gamepad_defaults;
		for (int n = 0; n < joy_count; n++)
		{
			substrcpy(joy_pnames[n], p, n);
			trim(joy_pnames[n]);
			if (joy_pnames[n][0]) printf("joy_pname[%d] = %s\n", n, joy_pnames[n]);
		}
		printf("\n");
	}
}

static int is_fire(char* name)
{
	if (!strncasecmp(name, "fire", 4) || !strncasecmp(name, "button", 6))
	{
		if (!strcasecmp(name, "fire") || strchr(name, '1')) return 1;
		if (strchr(name, '2')) return 2;
		if (strchr(name, '3')) return 3;
		if (strchr(name, '4')) return 4;
	}

	return 0;
}

void map_joystick(uint32_t *map, uint32_t *mmap)
{
	/*
	attemps to centrally defined core joy mapping to the joystick declaredy by a core config string
	we use the names declared by core with some special handling for specific edge cases

	Input button order is "virtual SNES" i.e.:
		A, B, X, Y, L, R, Select, Start
	*/
	read_buttons();

	map[SYS_BTN_RIGHT] = mmap[SYS_BTN_RIGHT] & 0xFFFF;
	map[SYS_BTN_LEFT]  = mmap[SYS_BTN_LEFT]  & 0xFFFF;
	map[SYS_BTN_DOWN]  = mmap[SYS_BTN_DOWN]  & 0xFFFF;
	map[SYS_BTN_UP]    = mmap[SYS_BTN_UP]    & 0xFFFF;

	if (mmap[SYS_AXIS_X])
	{
		uint32_t key = KEY_EMU + (((uint16_t)mmap[SYS_AXIS_X]) << 1);
		map[SYS_BTN_LEFT] = (key << 16) | map[SYS_BTN_LEFT];
		map[SYS_BTN_RIGHT] = ((key+1) << 16) | map[SYS_BTN_RIGHT];
	}

	if (mmap[SYS_AXIS_Y])
	{
		uint32_t key = KEY_EMU + (((uint16_t)mmap[SYS_AXIS_Y]) << 1);
		map[SYS_BTN_UP] = (key << 16) | map[SYS_BTN_UP];
		map[SYS_BTN_DOWN] = ((key + 1) << 16) | map[SYS_BTN_DOWN];
	}

	// loop through core requested buttons and construct result map
	for (int i=0, n=0; i<joy_count; i++)
	{
		if (!strcmp(joy_names[i], "-")) continue;

		int idx = i+DPAD_COUNT;
		char btn_name[32];
		strcpy(btn_name, defaults ? joy_pnames[n] : joy_nnames[n]);

		char *p = strchr(btn_name, '|');
		if (p) *p = 0;

		if(!strcasecmp(btn_name, "A")
		|| !strcasecmp(btn_name, "Jump")
		|| is_fire(btn_name) == 1)
		{
			map[idx] = mmap[SYS_BTN_A];
		}

		else if(!strcasecmp(btn_name, "B")
		|| is_fire(btn_name) == 2)
		{
			map[idx] = mmap[SYS_BTN_B];
		}

		else if(!strcasecmp(btn_name, "X")
		|| !strcasecmp(btn_name, "C")
		|| is_fire(btn_name) == 3)
		{
			map[idx] = mmap[SYS_BTN_X];
		}

		else if(!strcasecmp(btn_name, "Y")
		|| !strcasecmp(btn_name, "D")
		|| is_fire(btn_name) == 4)
		{
			map[idx] = mmap[SYS_BTN_Y];
		}

		// Genesis C and Z  and TG16 V and VI
		else if(!strcasecmp(btn_name, "R")
		|| !strcasecmp(btn_name, "RT")
		|| !strcasecmp(btn_name, "Coin"))
		{
			map[idx] = mmap[SYS_BTN_R];
		}

		else if(!strcasecmp(btn_name, "L")
		|| !strcasecmp(btn_name, "LT"))
		{
			map[idx] = mmap[SYS_BTN_L];
		}

		else if(!strcasecmp(btn_name, "Select")
		|| !strcasecmp(btn_name, "Mode")
		|| !strcasecmp(btn_name, "Game Select")
		|| !strcasecmp(btn_name, "Start 2P"))
		{
			map[idx] = mmap[SYS_BTN_SELECT];
		}

		else if(!strcasecmp(btn_name, "Start")
		|| !strcasecmp(btn_name, "Run")
		|| !strcasecmp(btn_name, "Pause")
		|| !strcasecmp(btn_name, "Start 1P"))
		{
			map[idx] = mmap[SYS_BTN_START];
		}

		n++;
	}
}

int map_paddle_btn()
{
	read_buttons();
	for (int i = 0, n = 0; i < joy_count; i++)
	{
		if (!strcmp(joy_names[i], "-")) continue;
		char *p = strchr(defaults ? joy_pnames[n] : joy_nnames[n], '|');
		if (p && !strcasecmp(p, "|P")) return i + SYS_BTN_A;
		n++;
	}
	return SYS_BTN_A;
}

static const char* get_std_name(uint16_t code, uint32_t *mmap)
{
	if (code)
	{
		if (code == mmap[SYS_BTN_A]) return "[A]";
		if (code == mmap[SYS_BTN_B]) return "[B]";
		if (code == mmap[SYS_BTN_X]) return "[X]";
		if (code == mmap[SYS_BTN_Y]) return "[Y]";
		if (code == mmap[SYS_BTN_L]) return "[L]";
		if (code == mmap[SYS_BTN_R]) return "[R]";
		if (code == mmap[SYS_BTN_SELECT]) return "[\x96]";
		if (code == mmap[SYS_BTN_START]) return "[\x16]";
		return "[ ]";
	}
	return NULL;
}

void map_joystick_show(uint32_t *map, uint32_t *mmap, int num)
{
	static char mapinfo[1024];
	read_buttons();

	sprintf(mapinfo, "Map (P%d):", num);
	if (!num) sprintf(mapinfo, " Map:");
	char *list = mapinfo + strlen(mapinfo);

	// loop through core requested buttons and construct result map
	for (int i = 0; i < joy_count; i++)
	{
		if (!strcmp(joy_names[i], "-")) continue;

		const char *btn = get_std_name((uint16_t)(map[i + DPAD_COUNT]), mmap);
		if (btn)
		{
			strcat(mapinfo, "\n");
			strcat(mapinfo, btn);
			strcat(mapinfo, ": ");
			strcat(mapinfo, joy_names[i]);
		}
	}

	if(strlen(list) && cfg.controller_info) Info(mapinfo, cfg.controller_info * 1000);
}
