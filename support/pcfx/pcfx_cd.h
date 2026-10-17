#ifndef PCFX_CD_H
#define PCFX_CD_H

#include "pcfx.h"

// A raw sector followed by 96 subcode bytes with P..W in bits 7..0.
// LBA 150 is track 1, 00:02:00.
#define PCFX_CD_SECTOR 2352
#define PCFX_CD_SUB    96

int  pcfx_cd_load(const char *filename);
void pcfx_cd_unload();
void pcfx_cd_read(uint8_t *buf, int lba, int cnt);
void pcfx_cd_read_ahead(int lba);
void pcfx_cd_send_toc(int index);

#endif
