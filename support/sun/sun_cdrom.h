// Sun CD-ROM slot, data as 2048-byte sectors, plus the SunSparcStation's CD-audio
// windows: the track table at SUN_CDROM_TOC_BLK, frame f (2352 + 208 bytes) at AUDIO_BLK + 5f.

#ifndef SUN_CDROM_H
#define SUN_CDROM_H

#include <stdint.h>

#define SUN_CDROM_AUDIO_BLK 0x80000000ULL
#define SUN_CDROM_TOC_BLK   0xC0000000ULL

enum
{
	SUN_CDROM_PASSTHRU = 0, // flat image: the generic path serves it
	SUN_CDROM_HANDLED  = 1, // sun_cdrom_fill serves it
	SUN_CDROM_REJECT   = 2, // fail the mount
};

int sun_cdrom_mount(int index, const char *name);
uint64_t sun_cdrom_size(int index);
int sun_cdrom_active(int index);

// lba in 512-byte units; sz a multiple of 512, up to UIO_BUFFER_SIZE.
void sun_cdrom_fill(int index, uint64_t lba, uint8_t *buf, int sz);
void sun_cdrom_unmount(int index);

// 1 (buf filled) when lba is in the TOC or audio window, else 0.
int sun_cdrom_window(int index, uint64_t lba, uint8_t *buf, int sz);

#endif
