// IBM RS/6000 43P 7248 CD-ROM: CUE/BIN, CHD and the CD-audio windows.

#ifndef IBMRS6K_CDROM_H
#define IBMRS6K_CDROM_H

#include <stdint.h>

#define IBMRS6K_CDROM_AUDIO_BLK 0x80000000ULL
#define IBMRS6K_CDROM_TOC_BLK   0xC0000000ULL

enum
{
	IBMRS6K_CDROM_PASSTHRU = 0,
	IBMRS6K_CDROM_HANDLED  = 1,
	IBMRS6K_CDROM_REJECT   = 2,
};

int ibmrs6k_cdrom_mount(int index, const char *name);
uint64_t ibmrs6k_cdrom_size(int index);
int ibmrs6k_cdrom_active(int index);

void ibmrs6k_cdrom_fill(int index, uint64_t lba, uint8_t *buf, int sz);
void ibmrs6k_cdrom_unmount(int index);

int ibmrs6k_cdrom_window(int index, uint64_t lba, uint8_t *buf, int sz);

#endif
