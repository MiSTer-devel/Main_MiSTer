#ifndef DECALPHA_CDROM_H
#define DECALPHA_CDROM_H

#include <stdint.h>

#define DECALPHA_CDROM_AUDIO_BLK 0x80000000ULL
#define DECALPHA_CDROM_TOC_BLK   0xC0000000ULL

enum
{
	DECALPHA_CDROM_PASSTHRU = 0,
	DECALPHA_CDROM_HANDLED  = 1,
	DECALPHA_CDROM_REJECT   = 2,
};

int decalpha_cdrom_mount(int index, const char *name);
uint64_t decalpha_cdrom_size(int index);
int decalpha_cdrom_active(int index);

void decalpha_cdrom_fill(int index, uint64_t lba, uint8_t *buf, int sz);
void decalpha_cdrom_unmount(int index);

int decalpha_cdrom_window(int index, uint64_t lba, uint8_t *buf, int sz);

#endif
