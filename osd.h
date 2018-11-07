#ifndef OSD_H_INCLUDED
#define OSD_H_INCLUDED

/*constants*/
#define OSDCTRLUP        0x01        /*OSD up control*/
#define OSDCTRLDOWN      0x02        /*OSD down control*/
#define OSDCTRLSELECT    0x04        /*OSD select control*/
#define OSDCTRLMENU      0x08        /*OSD menu control*/
#define OSDCTRLRIGHT     0x10        /*OSD right control*/
#define OSDCTRLLEFT      0x20        /*OSD left control*/

// some constants
#define OSDLINELEN       256         // single line length in bytes

// ---- old Minimig v1 constants -------
#define MM1_OSDCMDREAD     0x00      // OSD read controller/key status
#define MM1_OSDCMDWRITE    0x20      // OSD write video data command
#define MM1_OSDCMDENABLE   0x41      // OSD enable command
#define MM1_OSDCMDDISABLE  0x40      // OSD disable command
#define MM1_OSDCMDRST      0x80      // OSD reset command
#define MM1_OSDCMDAUTOFIRE 0x84      // OSD autofire command
#define MM1_OSDCMDCFGSCL   0xA0      // OSD settings: scanlines effect
#define MM1_OSDCMDCFGIDE   0xB0      // OSD enable HDD command
#define MM1_OSDCMDCFGFLP   0xC0      // OSD settings: floppy config
#define MM1_OSDCMDCFGCHP   0xD0      // OSD settings: chipset config
#define MM1_OSDCMDCFGFLT   0xE0      // OSD settings: filter
#define MM1_OSDCMDCFGMEM   0xF0      // OSD settings: memory config
#define MM1_OSDCMDCFGCPU   0xFC      // OSD settings: CPU config

// ---- new Minimig v2 constants -------
#define OSD_CMD_READ      0x00
#define OSD_CMD_RST       0x08
#define OSD_CMD_CLK       0x18
#define OSD_CMD_OSD       0x28
#define OSD_CMD_CHIP      0x04
#define OSD_CMD_CPU       0x14
#define OSD_CMD_MEM       0x24
#define OSD_CMD_VID       0x34
#define OSD_CMD_FLP       0x44
#define OSD_CMD_HDD       0x54
#define OSD_CMD_JOY       0x64
#define OSD_CMD_AUD       0x74
#define OSD_CMD_OSD_WR    0x0c
#define OSD_CMD_WR        0x1c
#define OSD_CMD_VERSION   0x88

#define DISABLE_KEYBOARD 0x02        // disable keyboard while OSD is active
#define OSD_INFO         0x04        // display info

#define REPEATDELAY      500         // repeat delay in 1ms units
#define REPEATRATE       50          // repeat rate in 1ms units
#define BUTTONDELAY      20          // repeat rate in 1ms units

#define CONFIG_TURBO     1
#define CONFIG_NTSC      2
#define CONFIG_A1000     4
#define CONFIG_ECS       8
#define CONFIG_AGA      16

#define CONFIG_FLOPPY1X  0
#define CONFIG_FLOPPY2X  1

#define OSD_ARROW_LEFT 1
#define OSD_ARROW_RIGHT 2

#include <inttypes.h>

/*functions*/
void OsdSetTitle(const char *s, int arrow = 0);	// arrow > 0 = display right arrow in bottom right, < 0 = display left arrow
void OsdSetArrow(int arrow);
void OsdWrite(unsigned char n, const char *s="", unsigned char inver=0, unsigned char stipple=0, char usebg = 0);
void OsdWriteOffset(unsigned char n, const char *s, unsigned char inver, unsigned char stipple, char offset, char leftchar, char usebg = 0); // Used for scrolling "Exit" text downwards...
void OsdClear(void);
void OsdEnable(unsigned char mode);
void InfoEnable(int x, int y, int width, int height);
void OsdDisable(void);
void ConfigFilter(unsigned char lores, unsigned char hires);
void ConfigVideo(unsigned char hires, unsigned char lores, unsigned char scanlines);
void ConfigAudio(unsigned char audio);
void ConfigMemory(unsigned char memory);
void ConfigCPU(unsigned char cpu);
void ConfigChipset(unsigned char chipset);
void ConfigFloppy(unsigned char drives, unsigned char speed);
void ConfigAutofire(unsigned char autofire, unsigned char mask);
void OSD_PrintText(unsigned char line, const char *text, unsigned long start, unsigned long width, unsigned long offset, unsigned char invert);
void OSD_PrintInfo(const char *message, int *width, int *height, int frame = 0);
void OsdDrawLogo(unsigned char n, char row, char superimpose);
void ScrollText(char n, const char *str, int off, int len, int max_len, unsigned char invert);
void ScrollReset();
void StarsInit();
void StarsUpdate();

// get/set core currently loaded
void OsdCoreNameSet(const char* str);
char* OsdCoreName();
void OsdSetSize(int n);
int OsdGetSize();

#define OsdIsBig (OsdGetSize()>8)

#endif

