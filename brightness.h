#ifndef __BRIGHTNESS_H__
#define __BRIGHTNESS_H__

#define BRIGHTNESS_SET  0
#define BRIGHTNESS_UP   1
#define BRIGHTNESS_DOWN 2

void setBrightness(int cmd, int val);
int getBrightness();

#endif
