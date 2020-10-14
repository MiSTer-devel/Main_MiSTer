#ifndef SNES_H
#define SNES_H

uint8_t* snes_get_header(fileTYPE *f);
void snes_patch_bs_header(fileTYPE *f, uint8_t *buf);

#endif
