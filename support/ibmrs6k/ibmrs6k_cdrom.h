// IBM RS/6000 43P 7248 CD-ROM slot, data as 2048-byte sectors, plus the CD-audio windows:
// the track table at IBMRS6K_CDROM_TOC_BLK, frame f (2352 + 208 bytes) at AUDIO_BLK + 5f.

#ifndef IBMRS6K_CDROM_H
#define IBMRS6K_CDROM_H

#include <stdint.h>

#define IBMRS6K_CDROM_AUDIO_BLK 0x80000000ULL
#define IBMRS6K_CDROM_TOC_BLK   0xC0000000ULL

enum
{
	IBMRS6K_CDROM_PASSTHRU = 0, // flat image: the generic path serves it
	IBMRS6K_CDROM_HANDLED  = 1, // ibmrs6k_cdrom_fill serves it
	IBMRS6K_CDROM_REJECT   = 2, // fail the mount
};

int ibmrs6k_cdrom_mount(int index, const char *name);
uint64_t ibmrs6k_cdrom_size(int index);
int ibmrs6k_cdrom_active(int index);

// lba in 512-byte units; sz a multiple of 512, up to UIO_BUFFER_SIZE.
void ibmrs6k_cdrom_fill(int index, uint64_t lba, uint8_t *buf, int sz);
void ibmrs6k_cdrom_unmount(int index);

// 1 (buf filled) when lba is in the TOC or audio window, else 0.
int ibmrs6k_cdrom_window(int index, uint64_t lba, uint8_t *buf, int sz);

#endif
