#ifndef OSD_H_INCLUDED
#define OSD_H_INCLUDED

#include <inttypes.h>

// some constants
#define OSDLINELEN       256         // single line length in bytes

#define OSD_CMD_WRITE    0x20      // OSD write video data command
#define OSD_CMD_ENABLE   0x41      // OSD enable command
#define OSD_CMD_DISABLE  0x40      // OSD disable command

// ---- Minimig v2 constants -------
#define OSD_CMD_READ     0x00
#define OSD_CMD_RST      0x08
#define OSD_CMD_CLK      0x18
#define OSD_CMD_CHIP     0x04
#define OSD_CMD_CPU      0x14
#define OSD_CMD_MEM      0x24
#define OSD_CMD_VID      0x34
#define OSD_CMD_FLP      0x44
#define OSD_CMD_HDD      0x54
#define OSD_CMD_JOY      0x64
#define OSD_CMD_AUD      0x74
#define OSD_CMD_WR       0x1c
#define OSD_CMD_VERSION  0x88

#define DISABLE_KEYBOARD 0x02        // disable keyboard while OSD is active
#define OSD_INFO         0x04        // display info

#define REPEATDELAY      500         // repeat delay in 1ms units
#define REPEATRATE       50          // repeat rate in 1ms units
#define BUTTONDELAY      20          // repeat rate in 1ms units

#define CONFIG_TURBO     1
#define CONFIG_NTSC      2
#define CONFIG_A1000     4
#define CONFIG_ECS       8
#define CONFIG_AGA       16

#define CONFIG_FLOPPY1X  0
#define CONFIG_FLOPPY2X  1

#define OSD_ARROW_LEFT   1
#define OSD_ARROW_RIGHT  2

/*functions*/
void OsdSetTitle(const char *s, int arrow = 0);	// arrow > 0 = display right arrow in bottom right, < 0 = display left arrow
void OsdSetArrow(int arrow);
void OsdWrite(unsigned char n, const char *s="", unsigned char inver=0, unsigned char stipple=0, char usebg = 0, int maxinv = 32);
void OsdWriteOffset(unsigned char n, const char *s, unsigned char inver, unsigned char stipple, char offset, char leftchar, char usebg = 0, int maxinv = 32); // Used for scrolling "Exit" text downwards...
void OsdClear(void);
void OsdEnable(unsigned char mode);
void InfoEnable(int x, int y, int width, int height);
void OsdRotation(uint8_t rotate);
void OsdDisable(void);
void ConfigVideo(unsigned char hires, unsigned char lores, unsigned char scanlines);
void ConfigAudio(unsigned char audio);
void ConfigMemory(unsigned char memory);
void ConfigCPU(unsigned char cpu);
void ConfigChipset(unsigned char chipset);
void ConfigFloppy(unsigned char drives, unsigned char speed);
void ConfigAutofire(unsigned char autofire, unsigned char mask);
void OSD_PrintText(unsigned char line, const char *text, unsigned long start, unsigned long width, unsigned long offset, unsigned char invert);
void OSD_PrintInfo(const char *message, int *width, int *height, int frame = 0);
void OsdDrawLogo(int row);
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

