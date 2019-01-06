#ifndef MENU_H
#define MENU_H

#include <inttypes.h>

// UI strings, used by boot messages
extern const char *config_memory_chip_msg[];
extern const char *config_memory_slow_msg[];
extern const char *config_memory_fast_msg[];
extern const char *config_cpu_msg[];
extern const char *config_hdf_msg[];
extern const char *config_chipset_msg[];

void HandleUI(void);
void menu_key_set(unsigned int c);
void PrintDirectory(void);
void ScrollLongName(void);

void ErrorMessage(const char *message, unsigned char code);
void InfoMessage(const char *message, int timeout = 2000);
void Info(const char *message, int timeout = 2000, int width = 0, int height = 0, int frame = 0);

uint32_t getStatus(char *opt, uint32_t status);
void substrcpy(char *d, char *s, char idx);

extern char joy_bnames[12][32];
extern int  joy_bcount;

void open_joystick_setup();

#endif
