/*
 * TX/RX descriptor ring walker — ported from Amiberry a2065.cpp
 *
 * do_transmit(): walk TX ring, build frame, hand to ethernet layer
 * gotfunc():     receive frame from ethernet layer, write into RX ring
 */

#include "minimig_a2065_types.h"
#include "minimig_a2065_debug.h"
#include "minimig_a2065_boardram.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* External references — resolved at link time */
extern volatile uint8_t *boardram;
extern int mungepacket(uint8_t *packet, int len);
extern uint32_t crc32_compute(const uint8_t *data, int len);
extern void ethernet_send(const uint8_t *frame, int len);

/* Register accessors from registers.cpp */
extern uint16_t registers_csr0(void);
extern void     registers_csr0_set(uint16_t v);
extern void     registers_csr0_clr(uint16_t v);
extern uint32_t registers_rdr_rdra(void);
extern uint32_t registers_rdr_rlen(void);
extern uint32_t registers_tdr_tdra(void);
extern uint32_t registers_tdr_tlen(void);
extern int *    registers_tdr_offset(void);
extern int *    registers_rdr_offset(void);
extern uint16_t registers_mode(void);
extern int      registers_prom(void);
extern uint64_t registers_ladrf(void);
extern void     registers_get_fakemac(uint8_t *out);
extern void     rethink(void);

static uint8_t transmitbuffer[MAX_PACKET_SIZE];
static int     transmitlen = 0;

/* ── Boardram accessors (see boardram_access.h) ─ */

/* ── Forward declaration (gotfunc defined after do_transmit) ──── */
void gotfunc(const uint8_t *databuf, int len);

/* ── do_transmit ────────────────────────────────────────────────────── */
int do_transmit(void)
{
    int err = 0, outsize = 0, add_fcs;
    uint32_t addr, off;
    uint16_t tmd0, tmd1, tmd2, tmd3;

    uint16_t csr0 = registers_csr0();
    if (!(csr0 & CSR0_TXON)) return 0;

    uint32_t tdr_tdra = registers_tdr_tdra();
    uint32_t tdr_tlen = registers_tdr_tlen();
    int *tdr_offset   = registers_tdr_offset();

    if (!tdr_tlen || tdr_tlen > 512) return 0;
    if (tdr_tdra + (tdr_tlen - 1) * 8 > RAM_MASK) return 0;

    *tdr_offset %= tdr_tlen;
    off = tdr_tdra + (uint32_t)(*tdr_offset) * 8;
    tmd1 = get_ram_word(off + 2);
    if (!(tmd1 & TX_OWN) || !(tmd1 & TX_STP)) {
        int start = *tdr_offset;
        for (uint32_t i = 0; i < tdr_tlen; i++) {
            (*tdr_offset) = (start + i) % tdr_tlen;
            off = tdr_tdra + (uint32_t)(*tdr_offset) * 8;
            tmd1 = get_ram_word(off + 2);
            if ((tmd1 & TX_OWN) && (tmd1 & TX_STP)) break;
        }
        if (!(tmd1 & TX_OWN) || !(tmd1 & TX_STP)) {
            if (a2065_debug && (registers_mode() & MODE_LOOP)) {
                fprintf(stderr, "[a2065] TX scan fail: tdr_tdra=%04X tlen=%d off=%d",
                        tdr_tdra, tdr_tlen, start);
                for (uint32_t i = 0; i < tdr_tlen; i++) {
                    uint32_t d = tdr_tdra + i * 8;
                    fprintf(stderr, " [%u]=%04X", i, get_ram_word(d + 2));
                }
                fprintf(stderr, "\n");
            }
            (*tdr_offset) = start;
            return 0;
        }
    }

    add_fcs = tmd1 & TX_ADD_FCS;

    for (;;) {
        *tdr_offset %= tdr_tlen;
        off  = tdr_tdra + (uint32_t)(*tdr_offset) * 8;
        tmd0 = get_ram_word(off + 0);
        tmd1 = get_ram_word(off + 2);
        tmd2 = get_ram_word(off + 4);
        tmd3 = get_ram_word(off + 6);
        addr = (uint32_t)tmd0 | ((uint32_t)(tmd1 & 0xff) << 16);
        addr &= RAM_MASK;

        if (!(tmd1 & TX_OWN)) {
            tmd3 |= TX_BUFF | TX_UFLO;
            tmd1 |= TX_ERR;
            registers_csr0_clr(CSR0_TXON);
            DBG("[a2065] TX OWN not set\n");
            err = 1;
            put_ram_word(off + 2, tmd1);
            put_ram_word(off + 6, tmd3);
            break;
        }

        tmd1 &= ~TX_OWN;
        int size = (int)(65536 - tmd2);
        if (size > MAX_PACKET_SIZE) size = MAX_PACKET_SIZE;
        if (outsize + size > MAX_PACKET_SIZE) {
            put_ram_word(off + 2, tmd1);
            put_ram_word(off + 6, tmd3);
            break;
        }
        ram_read_block(addr, (uint8_t *)&transmitbuffer[outsize], size);
        outsize += size;
        if ((tmd1 & TX_ENP) && outsize < 60 && !(registers_mode() & MODE_LOOP)) {
            while (outsize < 60) transmitbuffer[outsize++] = 0;
        }
        (*tdr_offset)++;

        if (tmd1 & TX_ENP)
            break;

        put_ram_word(off + 2, tmd1);
        put_ram_word(off + 6, tmd3);
    }

    if (err) {
        registers_csr0_set(CSR0_TINT);
        rethink();
        return 1;
    }

    if (outsize < 60 && !(registers_mode() & MODE_LOOP)) {
        tmd3 |= TX_BUFF | TX_UFLO;
        tmd1 |= TX_ERR;
        registers_csr0_clr(CSR0_TXON);
        DBG("[a2065] TX underflow: %d bytes\n", outsize);
        put_ram_word(off + 6, tmd3);
        put_ram_word(off + 2, tmd1);
        registers_csr0_set(CSR0_TINT);
        rethink();
        return 1;
    }

    uint16_t mode = registers_mode();

    if (mode & MODE_LOOP) {
        if (mode & MODE_COLL) {
            tmd1 |= TX_ERR;
            tmd3 |= TX_RTRY;
            put_ram_word(off + 2, tmd1);
            put_ram_word(off + 6, tmd3);
            DBG("[a2065] TX LOOP+COLL tmd1=%04X tmd3=%04X\n", tmd1, tmd3);
        } else {
            put_ram_word(off + 2, tmd1);
            put_ram_word(off + 6, tmd3);
            DBG("[a2065] TX LOOP %d bytes\n", outsize);
            gotfunc(transmitbuffer, outsize);
        }
    } else {
        int coll = (mode & MODE_COLL) != 0;
        if (!coll && outsize >= 6) {
            uint8_t fakemac_buf[6];
            registers_get_fakemac(fakemac_buf);
            if (memcmp(transmitbuffer, fakemac_buf, 6) == 0) {
                coll = 1;
                DBG("[a2065] TX self-loopback collision\n");
            }
        }
        if (coll) {
            tmd1 |= TX_ERR;
            tmd3 |= TX_RTRY;
            put_ram_word(off + 2, tmd1);
            put_ram_word(off + 6, tmd3);
            DBG("[a2065] TX COLL tmd1=%04X tmd3=%04X\n", tmd1, tmd3);
        } else {
            put_ram_word(off + 2, tmd1);
            put_ram_word(off + 6, tmd3);
            if ((mode & MODE_DTCR) && !add_fcs)
                outsize -= 4;
            transmitlen = outsize;
            mungepacket(transmitbuffer, transmitlen);
            ethernet_send(transmitbuffer, transmitlen);
            DBG("[a2065] TX %d bytes DST=%02X:%02X:%02X:%02X:%02X:%02X\n",
                    transmitlen,
                    transmitbuffer[0], transmitbuffer[1], transmitbuffer[2],
                    transmitbuffer[3], transmitbuffer[4], transmitbuffer[5]);
        }
    }

    registers_csr0_set(CSR0_TINT);
    rethink();
    return 1;
}

/* ── gotfunc (RX) ───────────────────────────────────────────────────── */
static int loopback_iter = 0;
void rings_reset_loopback_count(void) { loopback_iter = 0; }

/* RX drop accounting — where frames die between the daemon and Amiga TCP. */
static unsigned long rx_drop_rxoff = 0;   /* RXON off (incl. after an OFLO) */
static unsigned long rx_drop_ringfull = 0;/* no OWN descriptor */
static unsigned long rx_delivered = 0;    /* written into the ring */
void rings_get_rx_stats(unsigned long *deliv, unsigned long *rxoff, unsigned long *ringfull)
{ *deliv = rx_delivered; *rxoff = rx_drop_rxoff; *ringfull = rx_drop_ringfull; }

/* ── rings_rx_has_space ──────────────────────────────────────────────────
 * Peek (non-destructive) whether the RX ring can accept another frame: RXON is
 * on and the descriptor at the current rdr_offset is still owned by the LANCE
 * (RX_OWN set). Used by the rx_thread for socket backpressure — when this is
 * false it stops draining the socket, so the kernel's large receive buffer
 * holds the burst losslessly instead of the daemon overflowing the tiny ring
 * (which would clear RXON via RX_OFLO and cascade into a stall). The Amiga only
 * ever frees descriptors (gives RX_OWN back) as it drains, so a positive answer
 * here cannot be invalidated by the Amiga before we deliver. */
int rings_rx_has_space(void)
{
    if (!(registers_csr0() & CSR0_RXON)) return 0;          /* RX disabled — hold */
    uint32_t rdr_rlen = registers_rdr_rlen();
    if (!rdr_rlen || rdr_rlen > 512) return 1;              /* unconfigured — don't block */
    uint32_t rdr_rdra = registers_rdr_rdra();
    if (rdr_rdra + (rdr_rlen - 1) * 8 > RAM_MASK) return 1;
    int *rdr_offset = registers_rdr_offset();
    uint32_t off = rdr_rdra + (uint32_t)((*rdr_offset) % rdr_rlen) * 8;
    uint16_t rmd1 = get_ram_word(off + 2);
    return (rmd1 & RX_OWN) ? 1 : 0;
}

void gotfunc(const uint8_t *databuf, int len)
{
    if (registers_mode() & MODE_LOOP)
        DBG("[a2065] gotfunc entry len=%d csr0=%04X\n", len, registers_csr0());
    int insize = 0, first = 1, size;
    uint32_t addr, off;
    uint16_t rmd0, rmd1, rmd2, rmd3;
    uint8_t tmp[MAX_PACKET_SIZE];
    uint8_t fakemac_buf[6];

    if (!(registers_csr0() & CSR0_RXON)) {
        if (registers_mode() & MODE_LOOP)
            DBG("[a2065] RX LOOP skip: RXON off csr0=%04X\n", registers_csr0());
        rx_drop_rxoff++;
        return;
    }
    if (len < 20) { DBG("[a2065] RX LOOP skip: len=%d < 20\n", len); return; }
    uint32_t rdr_rlen = registers_rdr_rlen();
    if (!rdr_rlen || rdr_rlen > 512) { DBG("[a2065] RX skip: bad rdr_rlen=%u\n", rdr_rlen); return; }
    if (registers_rdr_rdra() + (rdr_rlen - 1) * 8 > RAM_MASK) { DBG("[a2065] RX skip: rdr overflow\n"); return; }

    registers_get_fakemac(fakemac_buf);

    /* Munge BEFORE filtering: on the wire we use the unique realmac, so an
     * incoming reply is addressed to realmac. mungepacket() swaps realmac->
     * fakemac (the Amiga's station MAC), after which every filter below can
     * compare against fakemac as before. Filtering the raw frame would drop
     * unicast replies (dst=realmac != fakemac). */
    memcpy(tmp, databuf, len);
    uint8_t *d = tmp;
    if (!(registers_mode() & MODE_LOOP))
        mungepacket(d, len);

    const uint8_t *dstmac = d;
    const uint8_t *srcmac = d + 6;

    if (!(registers_mode() & MODE_LOOP)) {
        if (dstmac[0] & 0x01) {
            /* Multicast (group bit set). Broadcast is always accepted.
             * For other multicast the Am7990 hashes the destination address
             * into the 64-bit logical address filter (LADRF): the top 6 bits
             * of the (non-inverted) CRC-32 of the 6 address bytes select a bit;
             * the frame is accepted only if that LADRF bit is set. With LADRF=0
             * (no groups joined) all multicast is rejected — matching hardware,
             * and stopping the daemon from flooding the RX ring with IPv6
             * ND/MLD traffic the Amiga never asked for. PROM mode bypasses. */
            if (!registers_prom() &&
                memcmp(dstmac, BROADCAST_MAC, 6) != 0) {
                uint32_t hash = (~crc32_compute(dstmac, 6)) >> 26;
                if (!(registers_ladrf() & (1ULL << hash)))
                    return;
            }
        } else {
            if (!registers_prom() &&
                memcmp(dstmac, fakemac_buf, 6) != 0 &&
                memcmp(dstmac, BROADCAST_MAC, 6) != 0) {
                return;
            }
        }
    }

    /* Drop loopback: src == dst == us (skip in internal loopback mode) */
    if (!(registers_mode() & MODE_LOOP) &&
        memcmp(dstmac, fakemac_buf, 6) == 0 &&
        memcmp(srcmac, fakemac_buf, 6) == 0) return;

    if (!(registers_mode() & MODE_LOOP)) {
        if (memcmp(dstmac, BROADCAST_MAC, 6) == 0 &&
            memcmp(srcmac, fakemac_buf, 6) == 0) return;
    }

    uint32_t crc = crc32_compute(d, len);
    d[len++] = (uint8_t)(crc >> 24);
    d[len++] = (uint8_t)(crc >> 16);
    d[len++] = (uint8_t)(crc >>  8);
    d[len++] = (uint8_t)(crc);

    uint32_t rdr_rdra = registers_rdr_rdra();
    int *rdr_offset   = registers_rdr_offset();

    for (;;) {
        *rdr_offset %= rdr_rlen;
        off  = rdr_rdra + (uint32_t)(*rdr_offset) * 8;
        rmd0 = get_ram_word(off + 0);
        rmd1 = get_ram_word(off + 2);
        rmd2 = get_ram_word(off + 4);
        rmd3 = get_ram_word(off + 6);
        addr = (uint32_t)rmd0 | ((uint32_t)(rmd1 & 0xff) << 16);
        addr &= RAM_MASK;

        if (!(rmd1 & RX_OWN)) {
            DBG("[a2065] RX buffer error\n");
            rx_drop_ringfull++;
            if (!first) {
                rmd1 |= RX_BUFF | RX_OFLO;
                registers_csr0_clr(CSR0_RXON);
            } else {
                registers_csr0_set(CSR0_MISS);
            }
            put_ram_word(off + 2, rmd1);
            rethink();
            return;
        }

        rmd1 &= ~RX_OWN;
        (*rdr_offset)++;

        if (first) { rmd1 |= RX_STP; first = 0; }

        size = (int)(65536 - rmd2);
        ram_write_block(addr, &d[insize], size);
        insize += size;

        if (insize >= len) {
            rmd1 |= RX_ENP;
            rmd3 = (uint16_t)len;
        }

        put_ram_word(off + 2, rmd1);
        put_ram_word(off + 6, rmd3);

        if (insize >= len) break;
    }

    registers_csr0_set(CSR0_RINT);
    rethink();
    rx_delivered++;
    if (registers_mode() & MODE_LOOP) {
        loopback_iter++;
        DBG("[a2065] RX LOOP OK #%d %d bytes\n", loopback_iter, len - 4);
    } else {
        DBG("[a2065] RX %d bytes\n", len - 4);
    }
}
