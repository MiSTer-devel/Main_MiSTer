#ifndef MINIMIG_A2065_BOARDRAM_H
#define MINIMIG_A2065_BOARDRAM_H

#include "minimig_a2065_types.h"
#include <stdint.h>

#define RAM_MASK 0x7FFF

extern volatile uint8_t *boardram;

/* The 68k (big-endian) writes the flat DDR3 window; the FPGA stores each 68k
 * 16-bit word in its DDR3 lane little-endian (byte[off]=D[7:0],
 * byte[off+1]=D[15:8]).  So a byte the 68k placed at boardram offset `off`
 * lives at host offset `off ^ 1`.  XOR every byte index with 1 so this side
 * sees boardram in 68k byte order (init block, descriptors, frames). */
static inline uint8_t  get_ram_byte(uint32_t off) { return boardram[(off ^ 1) & RAM_MASK]; }
static inline uint16_t get_ram_word(uint32_t off) { return ((uint16_t)get_ram_byte(off) << 8) | get_ram_byte(off + 1); }
static inline void     put_ram_byte(uint32_t off, uint8_t v) { boardram[(off ^ 1) & RAM_MASK] = v; }
static inline void     put_ram_word(uint32_t off, uint16_t v) { put_ram_byte(off, v >> 8); put_ram_byte(off + 1, (uint8_t)v); }

static inline void ram_read_block(uint32_t off, uint8_t *dst, int len) {
    for (int i = 0; i < len; i++) dst[i] = boardram[((off + i) ^ 1) & RAM_MASK];
}
static inline void ram_write_block(uint32_t off, const uint8_t *src, int len) {
    for (int i = 0; i < len; i++) boardram[((off + i) ^ 1) & RAM_MASK] = src[i];
}

#endif
