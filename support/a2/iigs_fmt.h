// Apple IIgs disk-format conversions (pure, dependency-free codec).
//
// These functions translate between the disk-image formats users have on disk
// and the two formats the IIgs core consumes on the wire: raw ProDOS blocks
// (hard disks) and WOZ (floppies). See IIGS_DISK_SUPPORT.md for the design.
//
// This header intentionally depends on nothing from MiSTer (no fileTYPE, no
// SPI) so the codec can be unit-tested natively on a dev machine. The MiSTer
// integration layer (the a2_read*/a2_write* glue, like dsk2nib_lib.cpp) wraps
// these buffer-in/buffer-out functions.

#ifndef IIGS_FMT_H
#define IIGS_FMT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- canonical geometry ----
#define A2_SECTOR_SIZE        256
#define A2_SECTORS_PER_TRACK  16
#define A2_TRACKS_525         35
#define A2_TRACK_SIZE         (A2_SECTORS_PER_TRACK * A2_SECTOR_SIZE)   // 4096
#define A2_525_IMAGE_SIZE     (A2_TRACKS_525 * A2_TRACK_SIZE)           // 143360
#define A2_NIB_TRACK_SIZE     6656
#define A2_NIB_IMAGE_SIZE     (A2_TRACKS_525 * A2_NIB_TRACK_SIZE)       // 232960
#define A2_BLOCK_SIZE         512
#define A2_35_IMAGE_SIZE      819200                                    // 1600 * 512

// ---- device classification ----
typedef enum {
	DC_UNKNOWN = 0,
	DC_HDD,          // raw ProDOS blocks, any size
	DC_FLOPPY_525,   // 140K logical / NIB / 5.25" WOZ
	DC_FLOPPY_35,    // 800K logical / 3.5" WOZ
} DiskClass;

// Classify a disk image from its leading bytes + size (+ optional extension,
// lowercase, no dot, may be NULL). Probes content first (WOZ/2MG/DC42 magic),
// falls back to size/extension for bare images. See IIGS_DISK_SUPPORT.md §4.
DiskClass iigs_classify(const uint8_t *buf, size_t size, const char *ext);

// ---- 2MG / 2IMG ----
typedef struct {
	uint32_t format;          // 0=DOS,1=ProDOS,2=NIB,3=CP/M
	uint32_t flags;           // bit31 = write-protected
	uint32_t blocks;
	uint32_t data_offset;     // header_offset to strip
	uint32_t data_len;        // payload size (excludes trailing chunks)
	int      write_protected;
} TwoMG;

// Parse + validate a 2MG header. Returns 1 on success (out filled), 0 if not a
// valid 2MG. Mirrors the validation in sim_blkdevice.cpp.
int twomg_parse(const uint8_t *buf, size_t size, TwoMG *out);

// Build a minimal 2MG wrapping payload. Writes 64-byte header + payload into
// out (must hold 64 + payload_len). Returns total bytes written, or 0 on error.
size_t twomg_build(uint8_t *out, size_t out_cap,
                   const uint8_t *payload, uint32_t payload_len, uint32_t format);

// ---- DiskCopy 4.2 (big-endian header). Detected by content, never extension. ----
typedef struct {
	uint32_t data_size;
	uint32_t tag_size;
	uint32_t data_checksum;
	uint32_t tag_checksum;
	uint8_t  disk_format;
	uint8_t  format_byte;
} DC42;

// Returns 1 if buf is a structurally valid DC42 (magic + size equation +
// modulo/range invariants), else 0. Safe against raw Apple II images.
int dc42_probe(const uint8_t *buf, size_t size);
int dc42_parse(const uint8_t *buf, size_t size, DC42 *out);   // 1 ok
// DiskCopy data/tag checksum: rotate-right-add over big-endian 16-bit words.
uint32_t dc42_checksum(const uint8_t *data, size_t len);
// Build a DC42 (no tags) wrapping payload. name may be NULL.
size_t dc42_build(uint8_t *out, size_t out_cap, const uint8_t *payload,
                  uint32_t payload_len, uint8_t format_byte, const char *name);

// ---- WOZ ----
// Returns INFO disk type: 1 = 5.25", 2 = 3.5", or -1 if not a WOZ file.
int woz_disk_type(const uint8_t *buf, size_t size);
// zlib-compatible CRC32 (poly 0xEDB88320), init 0.
uint32_t woz_crc32(const uint8_t *data, size_t len);

// ---- 140K sector order (DOS 3.3 <-> ProDOS) ----
// dst and src are 143360-byte 5.25" images; in-place safe only if dst!=src.
void a2_dos_to_prodos(uint8_t *dst, const uint8_t *src);
void a2_prodos_to_dos(uint8_t *dst, const uint8_t *src);

// ---- 5.25" 6-and-2 GCR (DSK <-> NIB) ----
// dsk is 143360 bytes in DOS order; nib is 232960 bytes. Ported from the live
// dsk2nib_lib.cpp path so encode/decode are exact inverses.
void a2_dsk_to_nib(uint8_t *nib, const uint8_t *dsk);
int  a2_nib_to_dsk(uint8_t *dsk, const uint8_t *nib);   // 1 if all tracks parsed

// ---- 5.25" "easy WOZ" (DOS-order DSK <-> WOZ2) ----
// Builds a fully-allocated standard-layout WOZ2 (all 35 tracks present) so the
// core can read and write it; trivially reversible. Returns bytes written / 0.
size_t a2_dsk_to_woz525(uint8_t *woz, size_t woz_cap, const uint8_t *dsk);
int    a2_woz525_to_dsk(uint8_t *dsk, const uint8_t *woz, size_t woz_size);

// ---- 3.5" 800K (ProDOS-order PO <-> WOZ2), Apple zoned GCR ----
// po is an 819200-byte ProDOS-order image (double-sided, 1600 blocks). The WOZ
// is a standard 160-track WOZ2; encode/decode are exact inverses. Apple 3.5"
// GCR (6-and-2 + 3-register scrambling checksum) ported from Clemens/CiderPress.
// Buffer the WOZ at >= 1.4MB. Returns bytes written / 0.
size_t a2_po_to_woz35(uint8_t *woz, size_t woz_cap, const uint8_t *po);
int    a2_woz35_to_po(uint8_t *po, const uint8_t *woz, size_t woz_size);

// ---- per-track decode + LBA mapping (for floppy write-back) ----
// Map a WOZ file LBA (512-block index) to its track index, or -1 (header/unmapped).
int a2_woz_track_for_lba(const uint8_t *woz, size_t woz_size, uint32_t lba);
// 3.5": decode one track into po (819200); reports the ProDOS block range written.
int a2_woz35_decode_track(const uint8_t *woz, size_t woz_size, int track, uint8_t *po,
                          int *base_block, int *block_count);
// 5.25": decode one track into dsk (143360) at track*4096 (16 sectors, DOS order).
int a2_woz525_decode_track(const uint8_t *woz, size_t woz_size, int track, uint8_t *dsk);

#ifdef __cplusplus
}
#endif

#endif // IIGS_FMT_H
