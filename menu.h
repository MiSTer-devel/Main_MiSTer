#ifndef MENU_H
#define MENU_H

#include "fdd.h" // for adfTYPE definition

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

unsigned long getStatus(char *opt, unsigned long status);

void menu_key_set(uint32_t c);
void menu_mod_set(uint32_t m);

#endif
