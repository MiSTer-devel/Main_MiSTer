// Sun CD-ROM slot, data only: the core has no CD-DA path.

#ifndef SUN_CDROM_H
#define SUN_CDROM_H

#include <stdint.h>

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

#endif
