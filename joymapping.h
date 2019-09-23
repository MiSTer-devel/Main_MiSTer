/*****************************************************************************/
// Knowledge base of known joystick controllers
/*****************************************************************************/

#ifndef JOYMAPPING_H
#define JOYMAPPING_H

#include <inttypes.h>
#include <string>

// VID of vendors who are consistent
#define VID_8BITDO          0x1235
#define VID_DAPTOR          0x04D8
#define VID_HORI            0x0f0d
#define VID_NINTENDO        0x057e
#define VID_RETROLINK       0x0079
#define VID_SONY            0x054c

#define NUMDEV 30
#define NUMBUTTONS 32

#define SNES_A 4
#define SNES_B 5
#define SNES_X 6
#define SNES_Y 7
#define SNES_L 8
#define SNES_R 9
#define SNES_SELECT 10
#define SNES_START 11

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

// name known joysticks
const char *get_joystick_alias( uint16_t vid, uint16_t pid );

/*****************************************************************************/

// mapping for different cores from known SNES layout
int map_snes2neogeo (devInput (&input)[NUMDEV], int dev);
int map_snes2md     (devInput (&input)[NUMDEV], int dev);
int map_snes2gb     (devInput (&input)[NUMDEV], int dev);
int map_snes2pce    (devInput (&input)[NUMDEV], int dev);
int map_snes2sms    (devInput (&input)[NUMDEV], int dev);
int map_snes2c64    (devInput (&input)[NUMDEV], int dev);
int map_snes2apple2 (devInput (&input)[NUMDEV], int dev);

/*****************************************************************************/

#endif // JOYMAPPING_H 