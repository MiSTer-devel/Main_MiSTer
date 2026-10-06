// Sun CD-ROM slot: serves CUE/CHD/raw-sector images as a flat disc of
// 2048-byte sectors; flat 2048 images pass through the generic sd path.
//
// Two windows above the data, for the SunSparcStation's CD audio
// (rtl/sun4m/scsi_targets.vhd in its repository), on every image (a flat
// ISO included):
//  - SUN_CDROM_TOC_BLK, 2 blocks: the track table. Bytes 0-3: first track,
//    last track, entries (the tracks and the lead-out), 1. Entry i at
//    8 + 8i: ADR/control (0x10 audio, 0x14 data), track number (0xAA for
//    the lead-out), M, S, F (the address + 150 frames), the address
//    (disc LBA) in 3 bytes, big-endian.
//  - SUN_CDROM_AUDIO_BLK + 5f: disc frame f, its 2352 bytes as on the
//    disc (16-bit little-endian samples, left then right), then 208 zero
//    bytes; zeros for a frame that is not audio.

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

// The TOC and audio windows (lba at or above SUN_CDROM_AUDIO_BLK): 1 when
// lba is in them (buf filled), 0 otherwise.
int sun_cdrom_window(int index, uint64_t lba, uint8_t *buf, int sz);

#endif
