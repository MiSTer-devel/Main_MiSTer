/*****************************************************************************/
// Knowledge base of known joystick controllers
/*****************************************************************************/

#ifndef JOYMAPPING_H
#define JOYMAPPING_H

#include <inttypes.h>
#include <string>

// VID of vendors who are consistent
#define VID_DAPTOR          0x04D8
#define VID_NINTENDO        0x057e
#define VID_RETROLINK       0x0079
#define VID_SONY            0x054c



/*****************************************************************************/

// name known joysticks
std::string get_joystick_alias( uint16_t vid, uint16_t pid );

/*****************************************************************************/

#endif // JOYMAPPING_H