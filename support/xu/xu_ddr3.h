#pragma once
#include <stdint.h>

/*
 * DDR3 shared-memory layout for XU native networking (PDP2011 DEUNA/DELUA
 * emulation, ENC424J600 SPI shim).
 *
 * Unlike A2065 (LANCE protocol runs on the ARM side, needs a register
 * ring), DEUNA's protocol runs entirely on-chip in xu.vhd's own embedded
 * CPU -- this window is a direct 1:1 mirror of the real ENC424J600's own
 * buffer+register model (Microchip DS39935C S9.0), not an invented ring.
 * Same physical base as A2065/ne2000 (0x1FF00000, the first byte of the
 * region u-boot's mem=511M reserves from Linux) -- no collision, since
 * MiSTer runs one core's bitstream at a time.
 *
 * Byte offset -> Avalon word address (rtl/xu_ddr_mailbox.vhd's own
 * convention, matches A2065's): Avalon = (ARM_byte_offset >> 3).
 */

#define XU_DDR3_BASE          0x1FF00000UL
#define XU_DDR3_WINDOW_SIZE   0x10000UL

/* ── Packet buffer (shared FPGA shim + ARM daemon, 1:1 mirror of the real
 * chip's own 24KB SRAM buffer -- exact split from roms/xubrt45.mac's own
 * erxst init value, #44000 octal = 0x4800) ────────────────────────────── */
#define XU_BUF_TX_OFF          0x0000UL   /* TX / general-purpose, 18KB */
#define XU_BUF_TX_SIZE         0x4800UL
#define XU_BUF_RX_OFF          0x4800UL   /* RX ring, 6KB */
#define XU_BUF_RX_SIZE         0x1800UL
/* If leftover to RX_OFF+RX_SIZE after this frame is below this, next_ptr
 * is ERXST (0x4800) instead of wrapping. 8-byte RSV + 1514-byte max
 * Ethernet frame pads to 1528 -- the next frame then starts at the ring
 * base, never straddles 0x5FFF. */
#define XU_RX_NOWRAP_MIN       1528UL

/* ── Control fields. Fields with different writers stay in their own
 * 8-byte-aligned slot (never pack two fields with different writers into
 * the same word -- that's the real hazard the original per-field split
 * avoided). But RXEN/TXRTS_REQ/ETXLEN/ERXST/ERXTAIL are all shim-sole-
 * writer, all rare/firmware-paced, and all needed together by the
 * daemon's own TX/RX handling -- packed into one XU_STATUS_OFF word so
 * the shim can publish all of them in a single atomic mailbox write,
 * with no ordering/clobbering question between them at all. ETXST is
 * dropped entirely: this firmware always writes it as 0 before every
 * transmit (xmitst), so the daemon just assumes 0 -- if that ever
 * changes, this needs revisiting. ─────────────────────────────────── */
#define XU_STATUS_OFF          0x6000UL   /* shim sole writer -- see xu_status_t below */
#define XU_TXRTS_DONE_OFF      0x6010UL   /* daemon sole writer, held until REQ drops */
#define XU_ERXHEAD_OFF         0x6030UL   /* daemon sole writer -- see layout below */
#define XU_MAC_ADDR_OFF        0x6040UL   /* daemon writes once at start; low 48 bits */
#define XU_MAC_VALID_OFF       0x6048UL   /* daemon writes; 0 until MAC_ADDR is valid */
/* XU_ERXHEAD_OFF 64-bit word, little-endian, daemon sole writer:
 *   bits 15:0  -- monotonic frame-enqueued count (shim PKTCNT)
 *   bits 31:16 -- unused (0)
 *   bits 47:32 -- rx_wrpos, first unwritten RX byte (readahead bound)
 *   bits 63:48 -- unused (0)
 * A count-only write (high bits 0) is the old daemon; the shim then keeps
 * the ERXTAIL readahead bound. */

/* XU_STATUS_OFF's 8-byte layout, LSB first:
 *   byte 0: bit0 = RXEN, bit1 = TXRTS_REQ, bit2 = RSTSEQ (toggles on
 *           ETHRST so the daemon can restart rx_wrpos), bits 3-7 reserved
 *   byte 1: reserved (0) -- mirrors ECON1's real 16-bit chip-protocol
 *           slot, only the low byte is ever meaningful
 *   bytes 2-3: ETXLEN (16-bit LE)
 *   bytes 4-5: ERXST  (16-bit LE)
 *   bytes 6-7: ERXTAIL (16-bit LE)
 */
#define XU_STATUS_RXEN_BIT      0
#define XU_STATUS_TXRTS_REQ_BIT 1
#define XU_STATUS_RSTSEQ_BIT    2
