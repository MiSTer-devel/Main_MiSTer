// Apple II / TK2000 5.25" track codec: 16-sector DOS 3.3 / ProDOS sector
// images <-> 6656-byte NIB tracks (the format the TK2000 core's
// floppy_track.sv expects: 13 x 512-byte SD blocks per track).
//
// Pure buffer-in / buffer-out functions with no I/O, so the identical file is
// used by the Verilator sim (verilator/sim/) and by Main_MiSTer
// (support/a2/). Keep the two copies in sync.
#ifndef A2_NIB_CODEC_H
#define A2_NIB_CODEC_H

#include <stdint.h>

#ifndef A2_TRACKS
#define A2_TRACKS            35
#endif
#ifndef A2_SECTORS
#define A2_SECTORS           16
#endif
#ifndef A2_SECTOR_SIZE
#define A2_SECTOR_SIZE       256
#endif
#ifndef A2_DSK_TRACK_SIZE
#define A2_DSK_TRACK_SIZE    (A2_SECTORS * A2_SECTOR_SIZE)      // 4096
#endif
#ifndef A2_DSK_IMAGE_SIZE
#define A2_DSK_IMAGE_SIZE    (A2_TRACKS * A2_DSK_TRACK_SIZE)    // 143360
#endif
#ifndef A2_NIB_TRACK_SIZE
#define A2_NIB_TRACK_SIZE    6656
#endif
#ifndef A2_NIB_IMAGE_SIZE
#define A2_NIB_IMAGE_SIZE    (A2_TRACKS * A2_NIB_TRACK_SIZE)    // 232960
#endif
#ifndef A2_NIB_BLOCKS
#define A2_NIB_BLOCKS        13                                 // 512-byte blocks per NIB track
#endif
#ifndef A2_DEFAULT_VOLUME
#define A2_DEFAULT_VOLUME    254
#endif

// Sector order of the source image.
#ifndef A2_ORDER_DOS
#define A2_ORDER_DOS     0   // .dsk / .do
#endif
#ifndef A2_ORDER_PRODOS
#define A2_ORDER_PRODOS  1   // .po
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Image-file sector (0..15 within the track) that holds the data for the
// sector whose address field carries sector number 'phys'.
int a2_file_sector(int phys, int order);

// Build one NIB track (6656 bytes) from one 4096-byte image track.
void a2_nibblize_track(const uint8_t *dsk_track, int track, int volume, int order, uint8_t *nib);

// Decode a (possibly rewritten) NIB track back into image-track order.
// Only sectors whose address field (checksum, track number) and data field
// (6-and-2 checksum) are valid are copied into dsk_track; the return value is a
// bitmask of the image-file sectors that were written. The track is treated as
// circular, so a sector that straddles the end of the buffer is still found.
uint32_t a2_denibblize_track(const uint8_t *nib, int track, int order, uint8_t *dsk_track);

#ifdef __cplusplus
}
#endif

#endif
