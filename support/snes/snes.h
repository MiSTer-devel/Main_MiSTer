#ifndef SNES_H
#define SNES_H

uint8_t* snes_get_header(fileTYPE *f);
void snes_patch_bs_header(fileTYPE *f, uint8_t *buf);
void snes_msu_init(const char* name);
void snes_poll(void);

#endif
