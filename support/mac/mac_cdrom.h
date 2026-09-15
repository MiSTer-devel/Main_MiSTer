// Mac CD-ROM slot: serves CUE/CHD/raw-sector images as a flat disc of
// 2048-byte sectors; flat 2048 images pass through the generic sd path.

#ifndef MAC_CDROM_H
#define MAC_CDROM_H

#include <stdint.h>

#define MAC_CDROM_SLOT 4   // MacLC.sv VD_CDROM (per-core routing: mac_cdrom_slot())

// Out-of-band windows above the data region, addressed directly by the RTL and
// invisible to the guest. Disc LBAs are 0-based; the RTL adds the +150 for MSF.
#define MAC_CDROM_TOC_BLK    0x7FFF0000u   // "MCDA" TOC blob (see build_toc_blob)
#define MAC_CDROM_TOC_BLKS   2u            // TOC blob length, 512-byte blocks
#define MAC_CDROM_AUDIO_BLK  0x40000000u   // CD-DA frames: lba = base + disc_lba

enum
{
	MAC_CDROM_PASSTHRU = 0, // flat 2048-byte image: use the generic path
	MAC_CDROM_HANDLED  = 1, // translation active: serve via mac_cdrom_fill
	MAC_CDROM_REJECT   = 2, // unusable image (bad cue/chd): fail the mount
};

int mac_cdrom_mount(int index, const char *name);
uint64_t mac_cdrom_size(int index);

// True when translation is live on this index (mac_cdrom_fill serves it).
int mac_cdrom_active(int index);

// sz=512: data/TOC blocks (lba in 512-byte units); sz=2352: whole CD-DA frames.
void mac_cdrom_fill(int index, uint64_t lba, uint8_t *buf, int sz);
void mac_cdrom_unmount(int index);

// One-shot boot repulse (from user_io_poll): re-fires the mount ~60 s after
// attach unless the guest read the disc (AppleCD misses the early attach pulse).
void mac_cdrom_poll(void);

#endif
