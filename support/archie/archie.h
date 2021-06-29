#ifndef ARCHIE_H
#define ARCHIE_H

#include "../../file_io.h"

void archie_init(void);
void archie_poll(void);
void archie_kbd(unsigned short code);
void archie_mouse(unsigned char b, int16_t x, int16_t y);
const char *archie_get_rom_name(void);
void archie_set_rom(char *);
void archie_save_config(void);

void archie_set_ar(char i);
int  archie_get_ar();
void archie_set_amix(char i);
int  archie_get_amix();
void archie_set_mswap(char i);
int  archie_get_mswap();
void archie_set_60(char i);
int  archie_get_60();
void archie_set_afix(char i);
int  archie_get_afix();
void archie_set_scale(char i);
int  archie_get_scale();

const char *archie_get_hdd_name(int i);
void archie_hdd_mount(char *filename, int idx);

#endif // ARCHIE_H
