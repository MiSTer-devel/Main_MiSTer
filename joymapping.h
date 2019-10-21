/*****************************************************************************/
// Handle mapping of various joystick controllers
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
#define MENU_JOY_L       8  // menu.cpp skips 4 buttons for mouse directions
#define MENU_JOY_R       9
#define MENU_JOY_SELECT 10
#define MENU_JOY_START  11

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

/*****************************************************************************/

int map_joystick(const char *core_name, devInput (&input)[NUMDEV], int dev);

/*****************************************************************************/

#endif // JOYMAPPING_H 