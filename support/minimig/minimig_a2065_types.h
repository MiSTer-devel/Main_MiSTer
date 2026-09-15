#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ── CSR0 bits ─────────────────────────────────────────────────────── */
#define CSR0_ERR    0x8000
#define CSR0_BABL   0x4000
#define CSR0_CERR   0x2000
#define CSR0_MISS   0x1000
#define CSR0_MERR   0x0800
#define CSR0_RINT   0x0400
#define CSR0_TINT   0x0200
#define CSR0_IDON   0x0100
#define CSR0_INTR   0x0080
#define CSR0_INEA   0x0040
#define CSR0_RXON   0x0020
#define CSR0_TXON   0x0010
#define CSR0_TDMD   0x0008
#define CSR0_STOP   0x0004
#define CSR0_STRT   0x0002
#define CSR0_INIT   0x0001

/* ── CSR3 bits ─────────────────────────────────────────────────────── */
#define CSR3_BSWP   0x0004
#define CSR3_ACON   0x0002
#define CSR3_BCON   0x0001

/* ── Mode register bits ─────────────────────────────────────────────── */
#define MODE_PROM   0x8000
#define MODE_EMBA   0x0080
#define MODE_INTL   0x0040
#define MODE_DRTY   0x0020
#define MODE_COLL   0x0010
#define MODE_DTCR   0x0008
#define MODE_LOOP   0x0004
#define MODE_DTX    0x0002
#define MODE_DRX    0x0001

/* ── TX descriptor bits ─────────────────────────────────────────────── */
#define TX_OWN      0x8000
#define TX_ERR      0x4000
#define TX_ADD_FCS  0x2000
#define TX_MORE     0x1000
#define TX_ONE      0x0800
#define TX_DEF      0x0400
#define TX_STP      0x0200
#define TX_ENP      0x0100
#define TX_BUFF     0x8000
#define TX_UFLO     0x4000
#define TX_LCOL     0x1000
#define TX_LCAR     0x0800
#define TX_RTRY     0x0400

/* ── RX descriptor bits ─────────────────────────────────────────────── */
#define RX_OWN      0x8000
#define RX_ERR      0x4000
#define RX_FRAM     0x2000
#define RX_OFLO     0x1000
#define RX_CRC      0x0800
#define RX_BUFF     0x0400
#define RX_STP      0x0200
#define RX_ENP      0x0100

/* ── Sizes ──────────────────────────────────────────────────────────── */
#define RAP_SIZE        128
#define MAX_PACKET_SIZE 4096
#define RAM_OFFSET      0x8000
#define RAM_SIZE        0x8000

/* ── A2065 card identity ────────────────────────────────────────────── */
#define A2065_MFR_ID    0x0202          /* Commodore-Amiga */
#define A2065_PROD_ID   0x70            /* 112 */
#define COMMODORE_OUI0  0x00
#define COMMODORE_OUI1  0x80
#define COMMODORE_OUI2  0x10

/* ── Chip register offsets within card (for bridge addr field) ──────── */
#define A2065_RDP_OFF   0x00            /* Register Data Port offset */
#define A2065_RAP_OFF   0x02            /* Register Address Pointer offset */

static const uint8_t BROADCAST_MAC[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
