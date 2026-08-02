// Mac SCSI family (MacLC/MacLCII, MacIIvi, LBMacTwo) — glue between the
// common code and the mac support modules. All hooks self-gate on the family.

#ifndef MAC_H
#define MAC_H

#include <stdint.h>
#include "../../file_io.h"
#include "mac_cdrom.h"
#include "mac_toolbox.h"

// Cores sharing the MacLC scsi.v target and its HPS features.
char is_mac_scsi_family();

// hps_io slots; -1 = the core lacks the device. One shared family layout
// (LBMacTwo: none). A wrong slot corrupts another device's sector stream.
int mac_toolbox_slot();
int mac_cdrom_slot();
int mac_cd_toolbox_slot();

// user_io_file_mount hook: runs CD image translation on the CD slot. Returns
// the new mount result (0 = fail the mount); on a translated mount clears
// *writable and sets f->size to the virtual disc size.
int mac_mount_hook(int index, const char *name, fileTYPE *f, int *writable);

// Once per user_io_poll: announce the Toolbox slots to the core and run the
// deferred CD work (SET NEXT CD remount, boot repulse). No-op for other cores.
void mac_poll();

// Slot service for the mac devices, SPI transfer included.
// Returns 0 = not ours (generic path serves it), 1 = serviced, -1 = ours but
// unsupported op (caller breaks the sector-service loop).
int mac_sd_service(int disk, int op, uint32_t lba, int sz, int ack);

// True when lba sits in the CD slot's raw CD-DA window (blksz 2352).
int mac_cdda_window(int disk, uint32_t lba);

#endif
