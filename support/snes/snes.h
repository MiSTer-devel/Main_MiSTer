#ifndef SNES_H
#define SNES_H

#define SNES_FILE_RAW		0
#define SNES_FILE_ROM		1
#define SNES_FILE_SPC		2
#define SNES_FILE_BS		3

uint8_t* snes_get_header(fileTYPE *f);
void snes_patch_bs_header(fileTYPE *f, uint8_t *buf);
void snes_msu_init(const char* name);
void snes_poll(void);

#endif
