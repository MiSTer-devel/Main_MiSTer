// Atari Falcon030 core: HPS side of the SCSI targets (rtl/falcon/falcon_scsi.sv).
//
// The core keeps the NCR 5380, the bus phases, the DMA, the sector data paths
// and REQUEST SENSE.  The command responses below are built here and fetched
// by the core as one 512-byte block READ of a "window" LBA on the target's
// own hps_io slot (the NeXT-Color convention):
//
//   0x7E000000 | unit<<20 | flags<<16 | op<<8 | arg
//     response at byte 0, bytes 510..511 = response length (big endian,
//     0 = no response: the core reports CHECK CONDITION or uses its built-in
//     INQUIRY / READ CAPACITY)
//     op 0x12 INQUIRY        flags[0] EVPD, flags[3] LUN != 0, arg page
//     op 0x25 READ CAPACITY
//     op 0x1A MODE SENSE(6)  flags[0] DBD, arg = CDB byte 2 (PC | page)
//     op 0x5A MODE SENSE(10) flags[0] DBD, arg = CDB byte 2
//     op 0x43 READ TOC       flags[0] MSF, flags[2:1] format, arg start track
//     op 0x42 READ SUB-CH.   flags[0] MSF, flags[1] SubQ, arg format
//
//   0x7D000000 | unit<<20 | op<<8, block WRITE: a forwarded command, CDB at
//     bytes 496..505, parameter list at byte 0 (MODE SELECT 6/10, CD audio
//     PLAY / PAUSE-RESUME / STOP).
//
// The CD-ROM (SCSI ID 2) accepts .iso (2048-byte sectors, served by the
// generic sd path) and .cue/.bin or a raw MODE1/2352 .bin (translated here
// to a flat disc of 2048-byte sectors in the disc's LBA space; audio tracks
// are listed in READ TOC).  Normal LBAs of the hard disks go through Main's
// standard path.

#ifndef FALCON_SCSI_H
#define FALCON_SCSI_H

#include <stdint.h>
#include "../../file_io.h"

// hps_io slots of the SCSI targets in Falcon.sv: slot FALCON_SCSI_SLOT0 + n
// is SCSI ID n (0, 1 = hard disks, 2 = CD-ROM).
#define FALCON_SCSI_SLOT0  4
#define FALCON_SCSI_UNITS  3
#define FALCON_SCSI_CD_ID  2

#define FALCON_WIN_RESP    0x7Eu
#define FALCON_WIN_CMD     0x7Du

// user_io_file_mount hook: remembers the image of a SCSI slot and runs the
// CD image translation.  Returns the new mount result (0 = fail the mount);
// on a translated CD mount clears *writable and sets f->size to the size of
// the virtual 2048-byte-sector disc.
int falcon_scsi_mount_hook(int index, const char *name, fileTYPE *f, int *writable);

// Image removed from a slot.
void falcon_scsi_unmount(int index);

// Sector service hook (user_io sd request loop), SPI transfer included.
// Returns 0 = not ours (generic path), 1 = serviced, -1 = ours but an
// unsupported operation.
int falcon_sd_service(int disk, int op, uint32_t lba, int sz, int ack);

// The pieces without SPI (also used by the simulation):
// build the 512-byte response block of a window LBA (returns the length)
int falcon_scsi_window_read(int unit, uint32_t lba, uint8_t *blk);
// run a forwarded command block
void falcon_scsi_window_write(int unit, uint32_t lba, const uint8_t *blk);
// a 512-byte block of the translated CD-ROM (cue/raw bin); 0 = not translated
int falcon_scsi_cd_fill(int unit, uint32_t lba, uint8_t *blk);

// CD audio state kept for READ SUB-CHANNEL (no audio output on this core)
struct falcon_cd_audio
{
	int      status;       // 0x11 playing, 0x12 paused, 0x13 completed, 0x15 none
	uint32_t pos;          // current disc LBA
	uint32_t end;          // end of the play range (exclusive)
	uint8_t  last_cmd[10]; // last forwarded CDB
	uint8_t  mode_sel[64]; // start of the last MODE SELECT parameter list
	int      mode_sel_len;
};
const falcon_cd_audio *falcon_scsi_audio(int unit);

#endif
