/*****************************************************************************/
// Knowledge base of known joystick controllers
/*****************************************************************************/

#ifndef JOYMAPPING_H
#define JOYMAPPING_H

#include <inttypes.h>

#define JOYSTICK_ALIAS_NONE             ""
#define JOYSTICK_ALIAS_8BITDO_SFC30     "8BitDo SFC30"
#define JOYSTICK_ALIAS_8BITDO_FC30      "8BitDo FC30"
#define JOYSTICK_ALIAS_BLISSTer         "BlisSTer"
#define JOYSTICK_ALIAS_CHEAP_SNES       "SNES Generic Pad"
#define JOYSTICK_ALIAS_DS3              "Sony Dual Shock 3"
#define JOYSTICK_ALIAS_DS4              "Sony Dual Shock 4"
#define JOYSTICK_ALIAS_HORI_FC4         "Hori Fighter Commander 4"
#define JOYSTICK_ALIAS_IBUFALLO_SNES    "iBuffalo SFC BSGP801"
#define JOYSTICK_ALIAS_IBUFALLO_NES     "iBuffalo FC BGCFC801"
#define JOYSTICK_ALIAS_QANBA_Q4RAF      "Qanba Q4RAF"
#define JOYSTICK_ALIAS_RETROLINK_GC     "Retrolink N64/GC"
#define JOYSTICK_ALIAS_RETROLINK_NES    "Retrolink NES"
#define JOYSTICK_ALIAS_RETRO_FREAK      "Retro Freak gamepad"
#define JOYSTICK_ALIAS_ROYDS_EX         "ROYDS Stick.EX"
#define JOYSTICK_ALIAS_SPEEDLINK_COMP   "Speedlink Competition Pro"
#define JOYSTICK_ALIAS_SWITCH_PRO       "Nintendo Switch Pro"
#define JOYSTICK_ALIAS_ATARI_DAPTOR2    "2600-daptor II"
#define JOYSTICK_ALIAS_5200_DAPTOR2     "5200-daptor"
#define JOYSTICK_ALIAS_NEOGEO_DAPTOR    "NEOGEO-daptor"
#define JOYSTICK_ALIAS_VISION_DAPTOR    "Vision-daptor"
#define JOYSTICK_ALIAS_WIIMOTE          "Nintendo WiiMote"
#define JOYSTICK_ALIAS_WIIU             "Nintendo Wii U"


// VID of vendors who are consistent
#define VID_DAPTOR          0x04D8
#define VID_NINTENDO        0x057e
#define VID_RETROLINK       0x0079
#define VID_SONY            0x054c



/*****************************************************************************/

// name known joysticks
char* get_joystick_alias( uint16_t vid, uint16_t pid );

/*****************************************************************************/

#endif // JOYMAPPING_H