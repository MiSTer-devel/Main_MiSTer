/*****************************************************************************/
// Knowledge base of known joystick controllers
/*****************************************************************************/

#ifndef JOYMAPPING_H
#define JOYMAPPING_H

#include <inttypes.h>
#include <string>

#define NUMDEV 30
#define NUMBUTTONS 32

//Defined as per main menu USB joypad mapping, see menu.cpp
#define MENU_JOY_A       4
#define MENU_JOY_B       5
#define MENU_JOY_X       6
#define MENU_JOY_Y       7
#define MENU_JOY_L      12  // menu.cpp skips 4 buttons for mouse directions
#define MENU_JOY_R      13
#define MENU_JOY_SELECT 14
#define MENU_JOY_START  15

typedef struct
{
	uint16_t vid, pid;
	uint8_t  led;
	uint8_t  mouse;
	uint8_t  axis_edge[256];
	int8_t   axis_pos[256];

	uint8_t  num;
	uint8_t  has_map;
	uint32_t map[NUMBUTTONS];

	uint8_t  osd_combo;

	uint8_t  has_mmap;
	uint32_t mmap[NUMBUTTONS];
	uint16_t jkmap[1024];

	uint8_t  has_kbdmap;
	uint8_t  kbdmap[256];

	uint16_t guncal[4];

	int      accx, accy;
	int      quirk;

	int      lightgun_req;
	int      lightgun;

	int      bind;
	char     devname[32];
	char     uniq[32];
	char     name[128];
} devInput;

#endif // JOYMAPPING_H 