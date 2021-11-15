#ifndef OSD_H_INCLUDED
#define OSD_H_INCLUDED

#include <inttypes.h>

// some constants
#define DISABLE_KEYBOARD 0x02        // disable keyboard while OSD is active
#define OSD_INFO         0x04        // display info
#define OSD_MSG          0x08        // display message window

#define REPEATDELAY      500         // repeat delay in 1ms units
#define REPEATRATE       50          // repeat rate in 1ms units

#define OSD_ARROW_LEFT   1
#define OSD_ARROW_RIGHT  2

/*functions*/
void OsdSetTitle(const char *s, int arrow = 0);	// arrow > 0 = display right arrow in bottom right, < 0 = display left arrow
void OsdSetArrow(int arrow);
void OsdWrite(unsigned char n, const char *s="", unsigned char inver=0, unsigned char stipple=0, char usebg = 0, int maxinv = 32, int mininv = 0);
void OsdWriteOffset(unsigned char n, const char *s, unsigned char inver, unsigned char stipple, char offset, char leftchar, char usebg = 0, int maxinv = 32, int mininv = 0); // Used for scrolling "Exit" text downwards...
void OsdClear();
void OsdEnable(unsigned char mode);
void InfoEnable(int x, int y, int width, int height);
void OsdRotation(uint8_t rotate);
void OsdDisable();
void OsdMenuCtl(int en);
void OsdUpdate();
void OSD_PrintInfo(const char *message, int *width, int *height, int frame = 0);
void OsdDrawLogo(int row);
void ScrollText(char n, const char *str, int off, int len, int max_len, unsigned char invert, int idx = 0);
void ScrollReset(int idx = 0);
void StarsInit();
void StarsUpdate();
void OsdShiftDown(unsigned char n);

// get/set core currently loaded
void OsdCoreNameSet(const char* str);
char* OsdCoreNameGet();
void OsdSetSize(int n);
int OsdGetSize();

#define OsdIsBig (OsdGetSize()>8)

#endif

