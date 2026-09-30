#ifndef A3_WOZ_H
#define A3_WOZ_H

#include <stdint.h>
#include <stddef.h>

size_t   a3_dsk_to_woz(uint8_t *woz, size_t cap, const uint8_t *dsk, int key, uint8_t volume);
size_t   a3_nib_to_woz(uint8_t *woz, size_t cap, const uint8_t *nib);
int      a3_decode_track(const uint8_t *woz, size_t size, int track, uint8_t *trk, uint8_t *volumes);
void     a3_nib_track(uint8_t *nib, const uint8_t *trk, int track, const uint8_t *volumes);
uint8_t  a3_dos33_volume(const uint8_t *dsk);
int      a3_sos_interp_encrypted(const uint8_t *dsk);

#endif
