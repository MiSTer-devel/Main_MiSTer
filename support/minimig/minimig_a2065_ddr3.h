#pragma once
#include <stdint.h>

/*
 * DDR3 shared-memory layout for flat boardram + CSR doorbell architecture.
 *
 * DDR3 physical base (ARM):  0x1FF00000
 * Avalon (f2sdram2) base:    0x03FE0000  (= ARM physical >> 3, 64-bit words)
 *
 * Region +0x0000..+0x7FFF is the 32KB boardram — both 68k (via FPGA DDR3
 * window) and ARM (via /dev/mem mmap) access the same bytes.
 *
 * Control slots start at +0x8000 (one 64-bit DDR3 word each).
 * Avalon address = ARM_byte_offset >> 3.
 */

#define DDR3_FLAT_BASE          0x1FF00000UL
#define DDR3_FLAT_WINDOW_SIZE   0x10000UL

/* ── Boardram (shared 68k + ARM, 32 KB) ──────────────────────────── */
#define DDR3_BRAM_OFF           0x0000UL
#define DDR3_BRAM_SIZE          0x8000UL

/* ── CMD: FPGA→ARM doorbell (Avalon 0x1000) ──────────────────────── */
#define DDR3_CMD_OFF            0x8000UL
#define DDR3_CMD_PENDING_BIT    (1ULL << 0)
#define DDR3_CMD_RAP_SHIFT      1
#define DDR3_CMD_RAP_MASK       0x7FULL
#define DDR3_CMD_DATA_SHIFT     8
#define DDR3_CMD_DATA_MASK      0xFFFFULL

/* ── CMD_ACK: ARM→FPGA (write 0 to release, Avalon 0x1001) ──────── */
#define DDR3_CMD_ACK_OFF        0x8008UL

/* ── CSR_SHADOW: ARM→FPGA packed CSR0..CSR3 (Avalon 0x1002) ─────── */
#define DDR3_CSR_OFF            0x8010UL
#define DDR3_CSR_SHIFT(n)       ((n) * 16)
#define DDR3_CSR_MASK           0xFFFFULL

/* ── INT_STATE: ARM→FPGA interrupt line (Avalon 0x1003) ─────────── */
#define DDR3_INT_OFF            0x8018UL
#define DDR3_INT_ASSERT_BIT     (1ULL << 0)

/* ── RAP_MIRROR: FPGA→ARM current RAP (Avalon 0x1004) ───────────── */
#define DDR3_RAP_MIRROR_OFF     0x8020UL

/* ── MAC: ARM→FPGA dynamic MAC address (Avalon 0x1005) ──────────── */
#define DDR3_MAC_OFF            0x8028UL
#define DDR3_MAC_VALID_BIT      (1ULL << 0)
#define DDR3_MAC_DATA_SHIFT     16

/* ── Avalon equivalents ─────────────────────────────────────────── */
#define AV_CMD          0x1000
#define AV_CMD_ACK      0x1001
#define AV_CSR          0x1002
#define AV_INT          0x1003
#define AV_RAP_MIRROR   0x1004
#define AV_MAC          0x1005
