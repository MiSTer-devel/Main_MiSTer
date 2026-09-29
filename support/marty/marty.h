// FM Towns Marty core support: CD-ROM as raw sectors, floppies as track
// records, IC card / CMOS / SCSI disk as plain block images, blank image
// creation from the OSD.
#ifndef MARTY_H
#define MARTY_H

#include <stdint.h>

// OSD slots as the core's CONF_STR orders them
#define MARTY_SLOT_CD     0
#define MARTY_SLOT_FDD    1
#define MARTY_SLOT_CARD   2
#define MARTY_SLOT_CMOS   3
#define MARTY_SLOT_HDD    4
#define MARTY_SLOT_FDD2   5
#define MARTY_SLOT_HDD2   6

#define MARTY_CD_SECTOR   2352
#define MARTY_CD_SUB      96
#define MARTY_CD_BLOCK    (MARTY_CD_SECTOR + MARTY_CD_SUB)   // raw sector then its subcode
#define MARTY_TRACK_REC   16384
#define MARTY_TOC_INDEX   250

void marty_init();
void marty_mgl_premount();
void marty_poll();
void marty_set_image(int index, const char *filename);
void marty_read_cd(uint8_t *buf, int lba, int cnt);
void marty_read_track(int index, uint8_t *buf, uint32_t lba);
void marty_read_cmos(uint8_t *buf, uint32_t lba, int cnt);
void marty_write_cmos(const uint8_t *buf, uint32_t lba);
void marty_write_track(int index, const uint8_t *buf, uint32_t lba);
void marty_fill_blank(int index, uint8_t *buf, uint32_t lba, int cnt);
int  marty_block_size(int index, int wire_size);
const uint8_t *marty_blank_image(int index, uint32_t *size);

#endif
