#ifndef MENU_H
#define MENU_H

#include "fdd.h" // for adfTYPE definition

#define KEY_AMI_UPSTROKE  0x80
#define KEY_AMI_MENU      0x69
#define KEY_AMI_PGUP      0x6C
#define KEY_AMI_PGDN      0x6D
#define KEY_AMI_HOME      0x6A
#define KEY_AMI_ESC       0x45
#define KEY_AMI_KPENTER   0x43
#define KEY_AMI_ENTER     0x44
#define KEY_AMI_BACK      0x41
#define KEY_AMI_SPACE     0x40
#define KEY_AMI_UP        0x4C
#define KEY_AMI_DOWN      0x4D
#define KEY_AMI_LEFT      0x4F
#define KEY_AMI_RIGHT     0x4E
#define KEY_AMI_KPPLUS    0x5E
#define KEY_AMI_KPMINUS   0x4A

// UI strings, used by boot messages
extern const char *config_filter_msg[];
extern const char *config_memory_chip_msg[];
extern const char *config_memory_slow_msg[];
extern const char *config_memory_fast_msg[];
extern const char *config_scanline_msg[];
extern const char *config_cpu_msg[];
extern const char *config_hdf_msg[];
extern const char *config_chipset_msg[];

void InsertFloppy(adfTYPE *drive, char* path);
void HandleUI(void);
void PrintDirectory(void);
void ScrollLongName(void);
void ErrorMessage(const char *message, unsigned char code);
void InfoMessage(char *message);
void ShowSplash();
void HideSplash();
void EjectAllFloppies();

void menu_key_set(unsigned char c);
void menu_mod_set(uint8_t m);

#endif
