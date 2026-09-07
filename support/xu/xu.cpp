/*
 * xu.cpp -- Phase 3 daemon for XU native networking. Plays the missing
 * ENC424J600 hardware for rtl/xu_enc424j600_shim.vhd, in the real chip's
 * own terms (Microchip DS39935C S9.0), against the DDR3 window laid out
 * in xu_ddr3.h.
 *
 * TX/RX host networking (ethernet_open/send/recv_nb/etc) is genuinely
 * called, not copied, from support/minimig/minimig_a2065_ethernet.cpp --
 * that code has no A2065-specific state, just a plain Linux AF_PACKET/BPF
 * wrapper. XU doesn't need A2065's mungepacket/delivery-mode machinery at
 * all: unlike A2065 (which rewrites a fixed "fakemac" the Amiga always
 * uses into a real, unique wire MAC on every frame), XU's MAC is
 * published to firmware *before* it ever builds a frame (see
 * xu_set_default_mac), so firmware's own outgoing frames already carry
 * the correct, real source MAC -- no rewriting needed, so no reason to
 * replicate A2065's direct/BPF/macvlan delivery-mode selection here. A
 * single shared-port, promiscuous-socket-plus-BPF-filter mode (matching
 * A2065's own MODE_BPF) is enough for a first, correct implementation.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>

#include "xu.h"
#include "xu_ddr3.h"
#include "../../user_io.h"
#include "../../shmem.h"

/* Main's stdout/stderr go to /dev/console (a real, separate physical UART
 * from the guest PDP-11's own console) -- easy to miss without a second
 * serial cable. Mirror every log line into a plain file too, so it's
 * checkable over SSH with no extra hardware. */
static void xu_log(const char *fmt, ...)
{
	static FILE *logf;
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);

	if (!logf)
		logf = fopen("/tmp/xu.log", "a");
	if (logf)
	{
		va_start(ap, fmt);
		vfprintf(logf, fmt, ap);
		va_end(ap);
		fflush(logf);
	}
}

extern int  ethernet_open(const char *iface, int promiscuous);
extern void ethernet_close(void);
extern void ethernet_send(const uint8_t *frame, int len);
extern int  ethernet_recv_nb(uint8_t *buf, int maxlen);
extern int  ethernet_read_iface_mac(const char *iface, uint8_t *out);
extern int  ethernet_set_mac_filter(const uint8_t *mac);
extern void ethernet_clear_filter(void);

#define XU_IFACE        "eth0"   /* shared onboard port, matching A2065's own default */
#define XU_MAX_FRAME    1514     /* Ethernet header+payload, no FCS, no VLAN tag */
#define XU_RECV_ROOM    1518     /* intake buffer: larger than MAX so oversize is dropped, not truncated */
#define XU_RX_BATCH_MAX 64       /* socket-intake bound per poll; DDR drain is 6 */

/* real DEC OUI, matching a2065_set_default_mac()'s Commodore-OUI reasoning:
 * a real, documented vendor OUI, never an arbitrary/zero one, so two
 * boards on one LAN never collide (low 3 bytes come from a real NIC MAC). */
static const uint8_t XU_OUI0 = 0x08, XU_OUI1 = 0x00, XU_OUI2 = 0x2B;

static volatile uint8_t *map = 0;
static int card_up = 0;
static int tx_pending = 0;
static uint32_t erxhead_count = 0;      /* daemon's own monotonic frame-enqueued count */
static uint32_t rx_wrpos = XU_BUF_RX_OFF; /* real byte position within the RX ring */
static int rx_tail_moved = 0;           /* 0 until firmware has consumed into the
                                          * ring interior (or finished a throttle
                                          * drain). Distinguishes "first lap still
                                          * unread at 0x4800" from "caught up, tail
                                          * wrapped back to 0x5FFE". */
static int rxen_was = 0;                /* previous poll's RXEN, for 1->0->1 latch */
static int rx_saw_rxen_clear = 0;       /* set when RXEN drops with frames queued */
static int rstseq_seen = 0;             /* 0 = not sampled yet; then 1/2 = last RSTSEQ bit+1 */
static uint8_t g_mac[6];                /* published MAC, cached for xu_poll_rx()'s
                                          * unicast-vs-broadcast classification */

/* Two real, persistent priority queues -- not per-call staging. See
 * xu_poll_rx() for the full rationale. Declared here (before xu_start())
 * so xu_start() can reset them on card-up. */
#define XU_RXQ_DEPTH 100         /* backlog buffer, not ring capacity -- frames
                                   * beyond this are simply dropped (xu_rxq_push) */
struct xu_rx_pending { uint8_t buf[XU_MAX_FRAME]; uint32_t len; };
struct xu_rxq { struct xu_rx_pending item[XU_RXQ_DEPTH]; int head, tail, count; };
static struct xu_rxq rxq_uni, rxq_other;

static int xu_rxq_push(struct xu_rxq *q, const uint8_t *buf, uint32_t len)
{
	if (q->count >= XU_RXQ_DEPTH) return 0;  /* backlog buffer itself full -- drop */
	struct xu_rx_pending *p = &q->item[q->tail];
	memcpy(p->buf, buf, len);
	p->len = len;
	q->tail = (q->tail + 1) % XU_RXQ_DEPTH;
	q->count++;
	return 1;
}

static void xu_rxq_reset(struct xu_rxq *q)
{
	q->head = q->tail = q->count = 0;
}

static inline uint64_t rd64(uint32_t off)
{
	return *(volatile uint64_t*)(map + off);
}

static inline void wr64(uint32_t off, uint64_t v)
{
	*(volatile uint64_t*)(map + off) = v;
}

/* XU_ERXHEAD_OFF: bits 31:0 = frame count, bits 47:32 = rx_wrpos (first
 * unwritten RX byte). Published together so the shim's PKTCNT and
 * readahead bound advance on the same word. */
static void xu_publish_erxhead(void)
{
	wr64(XU_ERXHEAD_OFF, (uint64_t)erxhead_count | ((uint64_t)rx_wrpos << 32));
}

static int xu_enabled(void)
{
	/* REAL BUG fixed here (found live, 2026-08-27: de0 attaching on real
	 * hardware proved have_xu was genuinely 1, but this always read back
	 * 0 anyway). user_io_status_bits() requires strict end>start for the
	 * "[hi:lo]" two-number form -- "[1:1]" (equal hi/lo) fails that check
	 * and returns 0 *silently*, which user_io_status_get() then reports
	 * as plain "false" with no error signal at all. The single-bit form
	 * is the OTHER syntax, "[N]" (one bracketed number, no colon) --
	 * pdp2011.sv: "O[1],External Ethernet,No,Yes;", status bit 1. */
	return user_io_status_get("[1]") != 0;
}

/* Same reasoning as a2065_set_default_mac(): a real vendor OUI plus the
 * onboard NIC's own low three bytes, so this doesn't collide with the
 * host's own real interface MAC or any other board on the LAN. */
static void xu_set_default_mac(uint8_t *mac_out)
{
	uint8_t low[6] = {0};
	const char *src = XU_IFACE;

	if (!ethernet_read_iface_mac(XU_IFACE, low) || (low[3] | low[4] | low[5]) == 0)
	{
		if (ethernet_read_iface_mac("eth0", low)) src = "eth0";
		else memset(low, 0, 6);
	}

	mac_out[0] = XU_OUI0; mac_out[1] = XU_OUI1; mac_out[2] = XU_OUI2;
	mac_out[3] = low[3];  mac_out[4] = low[4];  mac_out[5] = low[5];

	xu_log("[xu] MAC low bytes from %s -> %02X:%02X:%02X:%02X:%02X:%02X\n", src,
		mac_out[0], mac_out[1], mac_out[2], mac_out[3], mac_out[4], mac_out[5]);
}

static void xu_start(void)
{
	if (card_up) return;

	if (!map)
	{
		map = (volatile uint8_t*)shmem_map(XU_DDR3_BASE, XU_DDR3_WINDOW_SIZE);
		if (!map) { xu_log("[xu] cannot map DDR3 mailbox\n"); return; }
	}

	if (!ethernet_open(XU_IFACE, 1))
	{
		xu_log("[xu] cannot open %s\n", XU_IFACE);
		return;
	}

	uint8_t mac[6];
	xu_set_default_mac(mac);
	ethernet_set_mac_filter(mac);
	memcpy(g_mac, mac, 6);

	uint64_t macword = (uint64_t)mac[0]        | ((uint64_t)mac[1] << 8)
	                 | ((uint64_t)mac[2] << 16) | ((uint64_t)mac[3] << 24)
	                 | ((uint64_t)mac[4] << 32) | ((uint64_t)mac[5] << 40);
	wr64(XU_MAC_ADDR_OFF, macword);
	wr64(XU_MAC_VALID_OFF, 1);

	rx_wrpos = XU_BUF_RX_OFF;
	erxhead_count = 0;
	rx_tail_moved = 0;
	rxen_was = 0;
	rx_saw_rxen_clear = 0;
	xu_publish_erxhead();
	wr64(XU_TXRTS_DONE_OFF, 0);
	tx_pending = 0;
	xu_rxq_reset(&rxq_uni);
	xu_rxq_reset(&rxq_other);
	rstseq_seen = 0;

	card_up = 1;
	xu_log("[xu] started on %s\n", XU_IFACE);
}

/* one-shot "core detected" log, armed here rather than tracked inside
 * xu_poll() -- see xu_poll()'s own comment. Reset unconditionally (not
 * just when card_up) so a PDP2011 session that never got past the
 * External Ethernet check still re-logs "detected" if the core is
 * reloaded later. */
static int logged_active = 0;

void xu_stop(void)
{
	logged_active = 0;
	if (!card_up) return;
	card_up = 0;
	ethernet_clear_filter();
	ethernet_close();
	xu_log("[xu] stopped\n");
}

/* TXRTS_REQ/TXRTS_DONE: held-not-pulsed handshake, matching the offset
 * table's own design -- respond once per REQ assertion, then wait for
 * the shim to withdraw REQ before clearing DONE (mirrors
 * xu_ddr_mailbox.vhd's own held-ack CDC discipline one layer up). */
static void xu_poll_tx(void)
{
	uint64_t status = rd64(XU_STATUS_OFF);

	if ((status >> XU_STATUS_TXRTS_REQ_BIT) & 1)
	{
		if (!tx_pending)
		{
			/* ETXST is never read: this firmware always writes it as 0
			 * before every transmit (xmitst), so it's dropped from
			 * XU_STATUS_OFF entirely -- see the field's own comment. */
			uint32_t etxst  = 0;
			uint32_t etxlen = (uint32_t)((status >> 16) & 0xFFFF);

			if (etxlen > 0 && etxlen <= XU_MAX_FRAME && etxst < XU_BUF_TX_SIZE)
			{
				uint8_t frame[XU_MAX_FRAME];
				uint32_t n = etxlen;
				/* firmware always writes a fresh ETXST per transmit rather
				 * than maintaining a persistent ring the way RX does, so
				 * there's no real wraparound case here -- just clamp
				 * defensively against a malformed length. */
				if (etxst + n > XU_BUF_TX_SIZE) n = XU_BUF_TX_SIZE - etxst;
				memcpy(frame, (const void*)(map + XU_BUF_TX_OFF + etxst), n);
				ethernet_send(frame, (int)n);
			}
			else
			{
				xu_log("[xu] TX skipped: bad etxst/etxlen\n");
			}

			wr64(XU_TXRTS_DONE_OFF, 1);
			tx_pending = 1;
		}
	}
	else if (tx_pending)
	{
		wr64(XU_TXRTS_DONE_OFF, 0);
		tx_pending = 0;
	}
}

/* Enqueues one already-filtered frame into the RX ring. Returns 0 if it
 * didn't fit (ring full against the given erxtail snapshot) -- caller
 * decides whether that's worth logging differently for unicast vs.
 * broadcast/multicast. Split out of xu_poll_rx() so both priority passes
 * below can share it. */
static int xu_rx_enqueue(const uint8_t *buf, uint32_t framelen, uint64_t erxtail)
{
	if (framelen > XU_MAX_FRAME) return 0;

	uint32_t need = 8 + framelen;
	/* Pad to an 8-byte (DDR3 word) boundary, not just even -- real chip
	 * only requires even, but since rx_wrpos starts at XU_BUF_RX_OFF
	 * (already a multiple of 8) and XU_BUF_RX_SIZE is also a multiple
	 * of 8, padding need this way keeps every frame's start 8-byte
	 * aligned for the life of the ring (by induction). That
	 * guarantees the 8-byte header always lands entirely within one
	 * DDR3 word. */
	if (need & 7) need += 8 - (need & 7);

	uint32_t phys_end = XU_BUF_RX_OFF + XU_BUF_RX_SIZE;
	uint32_t start = rx_wrpos;

	/* Firmware initenc and a full consume-to-wrap both leave ERXTAIL
	 * at 0x5FFE. An interior tail means pktin drained at least one
	 * frame -- also latched from RXEN 1->0->1 in xu_poll, because
	 * the first-lap drain runs with RXEN clear so this path never
	 * sees those tails. */
	if (erxtail > XU_BUF_RX_OFF && erxtail < phys_end - 2)
		rx_tail_moved = 1;

	/* Jumped next_ptr to ERXST while the first lap is still unread.
	 * Do not write at 0x4800 until firmware has consumed (live log
	 * frames 10/11). After catch-up, tail_moved stays set even if
	 * tail later wraps back to 0x5FFE. */
	if (start == XU_BUF_RX_OFF && erxhead_count > 0 && !rx_tail_moved)
		return 0;

	/* Linear only -- never straddle 0x5FFF. The previous header's
	 * next_ptr should already have jumped to 0x4800 if leftover
	 * after that frame was below XU_RX_NOWRAP_MIN. */
	if (start + need > phys_end)
		return 0;

	/* Real datasheet S9.2 behavior: "If ERXHEAD reaches ERXTAIL
	 * while receiving a frame... the packet will be discarded and
	 * the Head Pointer restored" -- rx_wrpos is left unchanged on
	 * drop, matching that exactly. */
	uint32_t free_space = (erxtail >= start) ? (erxtail - start)
	                     : (XU_BUF_RX_SIZE - (start - erxtail));
	if (free_space < need) return 0;

	uint32_t next = start + need;
	uint32_t next_ptr = next;
	/* Leftover too small for another max frame -- point firmware at
	 * ERXST. Do not wait for tail here: after catch-up npp must
	 * already be 0x4800 or the next write has nowhere to go. The
	 * stomp guard above refuses to *write* at 0x4800 until drain. */
	if (phys_end - next < XU_RX_NOWRAP_MIN)
		next_ptr = XU_BUF_RX_OFF;

	uint8_t hdr[8];
	hdr[0] = (uint8_t)next_ptr;       hdr[1] = (uint8_t)(next_ptr >> 8);
	hdr[2] = (uint8_t)framelen;       hdr[3] = (uint8_t)(framelen >> 8);
	/* bytes 4-7: RSV flags (filter-match, CRC-OK, etc) -- real
	 * firmware never reads them (confirmed this session: zero
	 * references to any LATECOL/MAXCOL/DEFER-equivalent RX status
	 * field in roms/xubrt45.mac), so left zero. */
	hdr[4] = hdr[5] = hdr[6] = hdr[7] = 0;

	memcpy((void*)(map + start), hdr, 8);
	memcpy((void*)(map + start + 8), buf, framelen);

	/* Full memory barrier: the frame's bytes above must be visible to an
	 * external reader (the FPGA fabric, reading the same DDR3 through its
	 * own port) before erxhead_count is published below -- otherwise the
	 * shim could observe the new count before the data it counts, and
	 * serve a frame that isn't actually there yet. volatile alone only
	 * constrains the compiler, not the CPU's own store-buffer ordering as
	 * seen by another bus master, and how this window is actually mapped
	 * (normal bufferable memory vs. Strongly-Ordered) isn't something to
	 * assume -- force it explicitly instead. */
	__sync_synchronize();

	rx_wrpos = next_ptr;
	erxhead_count++;
	xu_publish_erxhead();
	return 1;
}

/* xu_poll_rx() below drains the one shared socket into two real,
 * persistent priority queues (rxq_uni/rxq_other, declared earlier
 * alongside g_mac) -- not per-call staging. They carry a backlog across
 * xu_poll() calls: a frame that doesn't fit into the DDR3 ring this pass
 * (because it's still full -- firmware hasn't drained it yet) stays
 * queued and is retried next poll, rather than being read out of the
 * kernel socket and then dropped if it lost the intake-pass race. The
 * kernel BPF filter (ethernet_set_mac_filter, shared with A2065) only
 * narrows to dst==our MAC or broadcast/multicast; it can't reorder or
 * persist a backlog, and tightening it further isn't an option since
 * it's shared, reused (not copied) infrastructure the Amiga core also
 * depends on. Both the ethertype tightening and the unicast-first
 * priority live entirely here instead, matching this project's standing
 * preference for new components at a clean boundary over touching
 * shared/upstream code.
 *
 * XU_RXQ_DEPTH is deliberately much bigger than the DDR3 ring can ever
 * hold at once (real, observed backlogs of 100+ frames on a busy LAN) --
 * it's a software backlog buffer, not a mirror of ring capacity.
 *
 * RX: intake (below) runs unconditionally, regardless of RXEN -- a real
 * ENC424J600's PHY keeps receiving off the wire into the chip's own
 * buffer independent of whether the host has RXEN set; RXEN only gates
 * whether the chip hands frames on to the host's ring. Gating intake
 * itself on RXEN (the bug this comment used to justify, 2026-08-31: real
 * hardware test found ARP replies vanishing during firmware's own
 * pktcnt>6 throttle windows, which clear RXEN -- confirmed via tcpdump
 * that a reply arrived on the wire every single time but was never once
 * read off the kernel socket) defeats the whole point of the
 * rxq_uni/rxq_other priority queues above: they exist specifically to
 * survive RXEN throttling, but a gate on intake meant nothing ever
 * reached them while RXEN was clear, leaving packets sitting in the
 * kernel's own socket buffer instead, silently droppable there with zero
 * log visibility. Only the drain step (into DDR3) is gated on RXEN --
 * firmware's own real init order always sets ERXTAIL before enabling
 * RXEN, so drain gating still guarantees ERXTAIL is validly initialized
 * (not its all-zero reset value) before the free-space math below ever
 * runs, and matches real hardware's own drain-side semantics. */
static void xu_poll_rx(void)
{
	uint8_t buf[XU_RECV_ROOM];

	/* Intake: read whatever's waiting on the one shared socket, up to a
	 * bounded per-call budget (same discipline as before -- Main is
	 * single-threaded), dropping anything that isn't ARP or IP right
	 * here (this old guest speaks nothing else), and pushing what's
	 * left onto its own persistent queue by classification. This step
	 * never touches DDR3 and never blocks on ring space. */
	for (int i = 0; i < XU_RX_BATCH_MAX; i++)
	{
		int n = ethernet_recv_nb(buf, sizeof buf);
		if (n <= 0) break;  /* 0 = socket drained, <0 = error -- stop this pass either way */
		if (n < 14) continue;  /* shorter than a full Ethernet header -- can't classify, drop */
		if (n > (int)XU_MAX_FRAME)
		{
			xu_log("[xu] RX drop: %d-byte frame (max %d)\n", n, XU_MAX_FRAME);
			continue;
		}

		uint16_t ethertype = ((uint16_t)buf[12] << 8) | buf[13];
		if (ethertype != 0x0806 /* ARP */ && ethertype != 0x0800 /* IP */)
			continue;

		int is_unicast_to_us = (memcmp(buf, g_mac, 6) == 0);
		xu_rxq_push(is_unicast_to_us ? &rxq_uni : &rxq_other, buf, (uint32_t)n);
	}

	/* Drain: serve the unicast queue into the DDR3 ring first, every
	 * poll, before the other queue is touched -- so a broadcast
	 * backlog cannot delay a unicast frame that's already waiting.
	 * At most 6 frames per poll (firmware's DEUNA ring). Gated on
	 * RXEN; leftover stays queued. */
	if (!((rd64(XU_STATUS_OFF) >> XU_STATUS_RXEN_BIT) & 1)) return;

	/* Match firmware's 6-slot DEUNA ring: on the RXEN rising edge
	 * do not dump the whole software backlog into DDR in one poll
	 * (that makes PKTCNT jump past 6 and immediately re-throttle). */
	int drain_left = 6;
	uint64_t erxtail = (rd64(XU_STATUS_OFF) >> 48) & 0xFFFF;
	while (drain_left > 0 && rxq_uni.count > 0)
	{
		struct xu_rx_pending *p = &rxq_uni.item[rxq_uni.head];
		if (!xu_rx_enqueue(p->buf, p->len, erxtail)) break;
		rxq_uni.head = (rxq_uni.head + 1) % XU_RXQ_DEPTH;
		rxq_uni.count--;
		drain_left--;
	}

	erxtail = (rd64(XU_STATUS_OFF) >> 48) & 0xFFFF;
	while (drain_left > 0 && rxq_other.count > 0)
	{
		struct xu_rx_pending *p = &rxq_other.item[rxq_other.head];
		if (!xu_rx_enqueue(p->buf, p->len, erxtail)) break;
		rxq_other.head = (rxq_other.head + 1) % XU_RXQ_DEPTH;
		rxq_other.count--;
		drain_left--;
	}
}

/* Caller (user_io.cpp's is_pdp2011() gate) already guarantees the
 * PDP2011 core is loaded whenever this runs -- no need to re-check that
 * here the way earlier versions did with a per-call strcasecmp. */
void xu_poll(void)
{
	static int logged_enabled = 0;

	if (!logged_active)
	{
		logged_active = 1;
		xu_log("[xu] core detected: \"%s\"\n", user_io_get_core_name(0));
	}

	if (!xu_enabled())
	{
		if (!logged_enabled)
		{
			logged_enabled = 1;
			xu_log("[xu] core active but External Ethernet is off\n");
		}
		if (card_up) xu_stop();
		return;
	}
	logged_enabled = 0;

	if (!card_up) xu_start();
	if (!card_up) return;

	uint64_t status = rd64(XU_STATUS_OFF);
	int rstseq = ((status >> XU_STATUS_RSTSEQ_BIT) & 1) + 1;
	if (!rstseq_seen)
		rstseq_seen = rstseq;
	else if (rstseq != rstseq_seen)
	{
		rstseq_seen = rstseq;
		rx_wrpos = XU_BUF_RX_OFF;
		erxhead_count = 0;
		rx_tail_moved = 0;
		rxen_was = 0;
		rx_saw_rxen_clear = 0;
		xu_publish_erxhead();
		xu_log("[xu] chip reset, RX ring restarted at 0x4800\n");
	}

	/* Sample ERXTAIL and RXEN every poll, not only on enqueue.
	 * First-lap drain clears RXEN (firmware throttle), so enqueue
	 * never runs while the interior tails exist. After catch-up
	 * tail is 0x5FFE again -- same as init -- and the wrap write
	 * at 0x4800 would stall forever without this latch. */
	{
		uint32_t erxtail = (uint32_t)((status >> 48) & 0xFFFF);
		int rxen = (int)((status >> XU_STATUS_RXEN_BIT) & 1);
		if (erxtail > XU_BUF_RX_OFF && erxtail < XU_BUF_RX_OFF + XU_BUF_RX_SIZE - 2)
			rx_tail_moved = 1;
		if (rxen_was && !rxen && erxhead_count > 0)
			rx_saw_rxen_clear = 1;
		if (rx_saw_rxen_clear && rxen && !rx_tail_moved)
			rx_tail_moved = 1;
		rxen_was = rxen;
	}

	xu_poll_tx();
	xu_poll_rx();
}
