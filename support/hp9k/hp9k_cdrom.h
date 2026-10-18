// HP 9000 family CD-ROM slot, data as 2048-byte sectors, plus the CD-audio windows:
// the track table at HP9K_CDROM_TOC_BLK, frame f (2352 + 208 bytes) at AUDIO_BLK + 5f.

#ifndef HP9K_CDROM_H
#define HP9K_CDROM_H

#include <stdint.h>

#define HP9K_CDROM_AUDIO_BLK 0x80000000ULL
#define HP9K_CDROM_TOC_BLK   0xC0000000ULL

enum
{
	HP9K_CDROM_PASSTHRU = 0, // flat image: the generic path serves it
	HP9K_CDROM_HANDLED  = 1, // hp9k_cdrom_fill serves it
	HP9K_CDROM_REJECT   = 2, // fail the mount
};

int hp9k_cdrom_mount(int index, const char *name);
uint64_t hp9k_cdrom_size(int index);
int hp9k_cdrom_active(int index);

// lba in 512-byte units; sz a multiple of 512, up to UIO_BUFFER_SIZE.
void hp9k_cdrom_fill(int index, uint64_t lba, uint8_t *buf, int sz);
void hp9k_cdrom_unmount(int index);

// 1 (buf filled) when lba is in the TOC or audio window, else 0.
int hp9k_cdrom_window(int index, uint64_t lba, uint8_t *buf, int sz);

#endif
