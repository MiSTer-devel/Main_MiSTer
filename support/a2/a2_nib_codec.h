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

#define A2NIB_TRACKS          35
#define A2NIB_SECTORS         16
#define A2NIB_SECTOR_SIZE     256
#define A2NIB_DSK_TRACK_SIZE  (A2NIB_SECTORS * A2NIB_SECTOR_SIZE)   // 4096
#define A2NIB_DSK_IMAGE_SIZE  (A2NIB_TRACKS * A2NIB_DSK_TRACK_SIZE)   // 143360
#define A2NIB_TRACK_SIZE      6656
#define A2NIB_IMAGE_SIZE      (A2NIB_TRACKS * A2NIB_TRACK_SIZE)   // 232960
#define A2NIB_BLOCKS          13                            // 512-byte blocks per NIB track
#define A2NIB_DEFAULT_VOLUME  254

// Sector order of the source image.
#define A2NIB_ORDER_DOS       0                             // .dsk / .do
#define A2NIB_ORDER_PRODOS    1                             // .po

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

#endif
