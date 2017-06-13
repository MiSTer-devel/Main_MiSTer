#ifndef ARCHIE_H
#define ARCHIE_H

#include "file_io.h"

void archie_init(void);
void archie_poll(void);
void archie_kbd(unsigned short code);
void archie_mouse(unsigned char b, char x, char y);
char *archie_get_rom_name(void);
char *archie_get_floppy_name(char b);
void archie_set_rom(char *);
void archie_set_floppy(char i, char *);
char archie_floppy_is_inserted(char i);
void archie_save_config(void);

#endif // ARCHIE_H
