// MiSTer integration glue for the Apple IIgs core disk handling.
//
// This is the bridge between the pure codec (iigs_fmt.{h,cpp}) and MiSTer's
// SD-block plumbing in user_io.cpp. It is active for the "Apple-IIgs" and
// "Apple-II" (//e) cores, whose Disk II / IWM engines take WOZ only; every
// other core (including the TK2000 NIB flow via SD_TYPE_A2) is untouched.
//
// Slots: IIgs (Apple-IIgs.sv, VDNUM=4): S0,S1 = hard disk; S2 = 3.5"; S3 = 5.25".
//        //e  (Apple-II.sv,   VDNUM=3): S0,S2 = 5.25" drives; S1 = hard disk.

#ifndef IIGS_DISK_H
#define IIGS_DISK_H

#include <stdint.h>
// Relies on fileTYPE being defined already (file_io.h), matching dsk2nib_lib.h:
// this header is reached via support.h, which is included after file_io.h.

// sd_type value used to route IIgs slots into iigs_read/iigs_write. Continues
// the series after SD_TYPE_A2 (=2) defined in user_io.cpp.
#define SD_TYPE_IIGS 3

// iigs_mount return codes.
#define IIGS_PASSTHRU  0   // not handled — serve normally (raw .po/.hdv/native .woz)
#define IIGS_HANDLED   1   // set up for iigs_read/iigs_write; size/sd_type adjusted
#define IIGS_REJECT   (-1) // wrong disk for this slot — caller aborts mount (OSD shown)

// True if the running core is "Apple-IIgs" or "Apple-II" (//e).
int iigs_is_core(void);

// Called from user_io_file_mount after the image is opened, for the IIgs core.
// May convert a floppy image to WOZ (in memory) or set a header offset for a
// 2MG/DC42 hard disk, validate the image against the slot (strict, with OSD
// guidance on mismatch), and adjust f->size to what the core should see.
// *out_writable is set to the read/write policy for HANDLED mounts.
int  iigs_mount(int index, const char *name, fileTYPE *f, int *out_writable);

// Release any per-slot resources (converted WOZ buffer). Safe to call anytime.
void iigs_unmount(int index);

// Block dispatch, called from user_io.cpp like a2_readDSK/a2_writeDSK.
void iigs_read(int disk, fileTYPE *f, uint64_t lba, int ack);
void iigs_write(int disk, fileTYPE *f, uint64_t lba, int ack);

// ---- WOZ file-level CRC32 refresh ----
// Core never checks the field, but external tools do (AppleWin, WOZtools)
// Clean ejects are not guaranteed, so the CRC is refreshed ~1s after last core write.
void iigs_woz_write_notify(int slot);  // after each successful core block write
int  iigs_woz_crc_due(int slot);       // 1 if a CRC refresh is due now
void iigs_woz_crc_done(int slot);      // call after attempting the refresh
// Recompute + patch the CRC of an open WOZ file (one whole-file read +
// 4-byte write). Returns 1 if patched, 0 if already correct or not a WOZ,
// -1 on error.
int  a2_woz_fix_crc(fileTYPE *f);

#endif // IIGS_DISK_H
