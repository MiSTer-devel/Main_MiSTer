#ifndef MARTY_H
#define MARTY_H

#include <stdint.h>

#define MARTY_SLOT_CD     0
#define MARTY_SLOT_FDD    1
#define MARTY_SLOT_CARD   2
#define MARTY_SLOT_CMOS   3
#define MARTY_SLOT_HDD    4
#define MARTY_SLOT_FDD2   5
#define MARTY_SLOT_HDD2   6

#define MARTY_CD_SECTOR   2352
#define MARTY_CD_SUB      96
#define MARTY_CD_BLOCK    (MARTY_CD_SECTOR + MARTY_CD_SUB)
#define MARTY_TRACK_REC   16384
#define MARTY_TOC_INDEX   250

void marty_init();
void marty_mgl_premount();
void marty_poll();
void marty_set_image(int index, const char *filename);
int  marty_block_size(int index, int wire_size);
int  marty_sd_service(int disk, int op, uint32_t lba, int sz, int ack);

#endif
