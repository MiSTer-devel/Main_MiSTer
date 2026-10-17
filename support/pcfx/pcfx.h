#ifndef PCFX_H
#define PCFX_H

#include <stdint.h>

#define PCFX_CD_BLOCK     2448
#define PCFX_SLOT_CD      0
#define PCFX_SLOT_BACKUP  1
#define PCFX_SLOT_FXBMP   2
#define PCFX_TOC_INDEX    250
#define PCFX_BIT_NEW_CARD 30
#define PCFX_BIT_EJECT_CARD 2

void pcfx_init();
void pcfx_mgl_premount();
void pcfx_set_image(int index, const char *filename);
int  pcfx_sd_service(int disk, int op, uint32_t lba, int sz, int ack);
void pcfx_new_card();
void pcfx_eject_card();
void pcfx_poll();

#endif
