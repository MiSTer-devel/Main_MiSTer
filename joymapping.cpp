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

static void get_buttons()
{
	int i = 2;
	char *p;

	memset(joy_bnames, 0, sizeof(joy_bnames));
	joy_bcount = 0;

	while(1)
	{
		p = user_io_8bit_get_string(i);
		if (!p) break;

		if (p[0] == 'J')
		{
			for (int n = 0; n < 28; n++)
			{
				substrcpy(joy_bnames[n], p, n + 1);
				if (!joy_bnames[n][0]) break;

				printf("joy_bname[%d] = %s\n", n, joy_bnames[n]);

				char *sstr = strchr(joy_bnames[n], '(');
				if (sstr) *sstr = 0;
				trim(joy_bnames[n]);

				if (!joy_bnames[n][0]) break;
				joy_bcount++;
			}
			break;
		}
		i++;
	}
}

static int has_X_button()
{
	for (int i = 0; i < joy_bcount; i++)
	{
		if (!strcasecmp(joy_bnames[i], "X")) return 1;
	}
	return 0;
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
	static char mapinfo[1024];
	strcpy(mapinfo, "This joystick is not defined.\nDefault map:");
	/*
	attemps to centrally defined core joy mapping to the joystick declaredy by a core config string
	we use the names declared by core with some special handling for specific edge cases

	Input button order is "virtual SNES" i.e.:
		A, B, X, Y, L, R, Select, Start
	*/
	get_buttons();

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
	for (int i=0; i<joy_bcount; i++)
	{
		int idx = i+DPAD_COUNT;
		char *btn_name = joy_bnames[i];

		strcat(mapinfo, "\n* btn ");
		strcat(mapinfo, btn_name);

		if(!strcasecmp(btn_name, "A")
		|| !strcasecmp(btn_name, "Jump")
		|| is_fire(btn_name) == 1
		|| !strcasecmp(btn_name, "Button I"))
		{
			map[idx] = mmap[SYS_BTN_A];
			strcat(mapinfo, " -> A");
		}

		else if(!strcasecmp(btn_name, "B")
		|| is_fire(btn_name) == 2
		|| !strcasecmp(btn_name, "Button II"))
		{
			map[idx] = mmap[SYS_BTN_B];
			strcat(mapinfo, " -> B");
		}

		else if(!strcasecmp(btn_name, "X")
		|| (!strcasecmp(btn_name, "C") && !has_X_button())
		|| is_fire(btn_name) == 3
		|| !strcasecmp(btn_name, "Button III"))
		{
			map[idx] = mmap[SYS_BTN_X];
			strcat(mapinfo, " -> X");
		}

		else if(!strcasecmp(btn_name, "Y")
		|| !strcasecmp(btn_name, "D")
		|| is_fire(btn_name) == 4
		|| !strcasecmp(btn_name, "Button IV"))
		{
			map[idx] = mmap[SYS_BTN_Y];
			strcat(mapinfo, " -> Y");
		}

		// Genesis C and Z  and TG16 V and VI
		else if(!strcasecmp(btn_name, "R")
		|| !strcasecmp(btn_name, "C")
		|| !strcasecmp(btn_name, "RT")
		|| !strcasecmp(btn_name, "Button V")
		|| !strcasecmp(btn_name, "Coin"))
		{
			map[idx] = mmap[SYS_BTN_R];
			strcat(mapinfo, " -> R");
		}

		else if(!strcasecmp(btn_name, "L")
		|| !strcasecmp(btn_name, "Z")
		|| !strcasecmp(btn_name, "LT")
		|| !strcasecmp(btn_name, "Button VI"))
		{
			map[idx] = mmap[SYS_BTN_L];
			strcat(mapinfo, " -> L");
		}

		else if(!strcasecmp(btn_name, "Select")
		|| !strcasecmp(btn_name, "Mode")
		|| !strcasecmp(btn_name, "Game Select")
		|| !strcasecmp(btn_name, "Start 2P"))
		{
			map[idx] = mmap[SYS_BTN_SELECT];
			strcat(mapinfo, " -> Select");
		}

		else if(!strcasecmp(btn_name, "Start")
		|| !strcasecmp(btn_name, "Run")
		|| !strcasecmp(btn_name, "Pause")
		|| !strcasecmp(btn_name, "Start 1P"))
		{
			map[idx] = mmap[SYS_BTN_START];
			strcat(mapinfo, " -> Start");
		}
		else
		{
			strcat(mapinfo, " -> <undefined>");
		}
	}

	Info(mapinfo, 4000);
}
