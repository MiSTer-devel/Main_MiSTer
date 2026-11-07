// Uthernet II (W5100) chip model -- see iigs_uthernet2_model.h.
//
// W5100 facts used here (W5100 datasheet v1.2.8, Uthernet II manual 2018,
// cross-checked with AppleWin's Uthernet2.cpp):
//   * registers are big-endian; Sn_TX_RD/WR and Sn_RX_RD are free-running
//     16-bit counters, masked by the buffer size to address the buffer;
//   * RMSR/TMSR give each socket 1/2/4/8 KB, packed from $6000 / $4000;
//   * OPEN zeroes the socket pointers; Sn_CR reads 0 when a command is done;
//   * a MAC raw RX frame is stored as a 2-byte big-endian length that counts
//     itself, then the frame without FCS; TX data is the bare frame;
//   * with S0_MR bit 6 (MF) set only frames for SHAR or broadcast arrive.

#include <string.h>
#include "iigs_uthernet2_model.h"

#define SOCK_BASE(n)  (0x0400u + 0x100u * (n))
#define SN_MR      0x00
#define SN_CR      0x01
#define SN_IR      0x02
#define SN_SR      0x03
#define SN_DHAR    0x06
#define SN_TTL     0x16
#define SN_TX_FSR  0x20
#define SN_TX_RD   0x22
#define SN_TX_WR   0x24
#define SN_RX_RSR  0x26
#define SN_RX_RD   0x28

#define CR_OPEN    0x01
#define CR_DISCON  0x08
#define CR_CLOSE   0x10
#define CR_SEND    0x20
#define CR_SENDMAC 0x21
#define CR_RECV    0x40

#define SR_CLOSED  0x00
#define SR_MACRAW  0x42

#define MAX_FRAME  1514

static volatile uint8_t *w;
static uint64_t last_gen = ~0ULL;
static uint32_t ring_rd;
static int      macraw_open;
static int      mac_filter;
static uint8_t  open_mac[6];
static uint16_t rx_wr, rx_rd;          // socket 0 RX counters
static u2_stats st;

static inline uint8_t rd8(uint32_t a) { return w[a]; }
static inline void wr8(uint32_t a, uint8_t v) { w[a] = v; }
static inline uint16_t rd16(uint32_t a) { return (uint16_t)((w[a] << 8) | w[a + 1]); }
// One 16-bit store, so the FPGA never sees half an update (all W5100 16-bit
// registers are at even addresses; the window is little-endian).
static inline void wr16(uint32_t a, uint16_t v)
{
	*(volatile uint16_t *)(w + a) = (uint16_t)((v >> 8) | (v << 8));
}
static inline uint64_t rd64(uint32_t a) { return *(volatile uint64_t *)(w + a); }
static inline void wr64(uint32_t a, uint64_t v) { *(volatile uint64_t *)(w + a) = v; }

static void buf_geom(int tx, int n, uint16_t *base, uint16_t *size)
{
	uint8_t v = rd8(tx ? 0x001B : 0x001A);
	uint32_t b = tx ? 0x4000 : 0x6000, end = b + 0x2000;
	for (int i = 0; i <= n; i++)
	{
		uint32_t sz = 1u << (10 + ((v >> (2 * i)) & 3));
		if (b + sz > end) sz = (b < end) ? end - b : 0;
		if (i == n) { *base = (uint16_t)b; *size = (uint16_t)sz; return; }
		b += sz;
	}
}

static void sock_reset_ptrs(int n)
{
	uint32_t s = SOCK_BASE(n);
	uint16_t base, size;
	buf_geom(1, n, &base, &size);
	wr16(s + SN_TX_RD, 0);
	wr16(s + SN_TX_WR, 0);
	wr16(s + SN_RX_RD, 0);
	wr16(s + SN_RX_RSR, 0);
	wr16(s + SN_TX_FSR, size);
	if (n == 0) rx_wr = rx_rd = 0;
}

static void chip_reset(void)
{
	// common registers (MR at $0000 belongs to the FPGA)
	for (uint32_t a = 0x0001; a < 0x0030; a++) wr8(a, 0);
	wr8(0x0017, 0x07); wr8(0x0018, 0xD0);   // RTR = 2000 (200 ms)
	wr8(0x0019, 0x08);                       // RCR
	wr8(0x001A, 0x55);                       // RMSR: 2 KB each
	wr8(0x001B, 0x55);                       // TMSR
	wr8(0x0028, 0x28);                       // PTIMER
	for (int n = 0; n < 4; n++)
	{
		uint32_t s = SOCK_BASE(n);
		for (uint32_t a = 0; a < 0x30; a++) wr8(s + a, 0);
		for (int k = 0; k < 6; k++) wr8(s + SN_DHAR + k, 0xFF);
		wr8(s + SN_TTL, 0x80);
		sock_reset_ptrs(n);
	}
	macraw_open = 0;
	st.resets++;
}

static void do_send(const u2_net *net)
{
	uint32_t s = SOCK_BASE(0);
	uint16_t base, size;
	buf_geom(1, 0, &base, &size);
	if (!size) return;
	uint16_t mask = size - 1;
	uint16_t rd = rd16(s + SN_TX_RD), wrp = rd16(s + SN_TX_WR);
	uint16_t len = (uint16_t)(wrp - rd);
	if (len && len <= size && len <= MAX_FRAME)
	{
		uint8_t frame[MAX_FRAME];
		for (uint16_t i = 0; i < len; i++) frame[i] = rd8(base + ((rd + i) & mask));
		if (net && net->send) net->send(net->ctx, frame, len);
		st.tx++;
	}
	wr16(s + SN_TX_RD, wrp);
	wr16(s + SN_TX_FSR, size);
}

static void do_cr(int n, uint8_t cmd, const u2_net *net)
{
	uint32_t s = SOCK_BASE(n);
	switch (cmd)
	{
	case CR_OPEN:
		sock_reset_ptrs(n);
		if (n == 0 && (rd8(s + SN_MR) & 0x0F) == 0x04)
		{
			wr8(s + SN_SR, SR_MACRAW);
			mac_filter = (rd8(s + SN_MR) & 0x40) != 0;
			for (int k = 0; k < 6; k++) open_mac[k] = rd8(0x0009 + k);
			macraw_open = 1;
			st.opens++;
		}
		else
			wr8(s + SN_SR, SR_CLOSED);   // TCP/UDP/IP raw are not modelled
		break;
	case CR_CLOSE:
	case CR_DISCON:
		wr8(s + SN_SR, SR_CLOSED);
		if (n == 0) macraw_open = 0;
		break;
	case CR_SEND:
	case CR_SENDMAC:
		if (n == 0 && macraw_open) do_send(net);
		break;
	case CR_RECV:
		if (n == 0)
		{
			rx_rd = rd16(s + SN_RX_RD);
			wr16(s + SN_RX_RSR, (uint16_t)(rx_wr - rx_rd));
		}
		break;
	default:
		break;
	}
	wr8(s + SN_CR, 0);
}

static void do_rx(const u2_net *net)
{
	if (!macraw_open || !net || !net->recv) return;
	uint16_t base, size;
	buf_geom(0, 0, &base, &size);
	if (!size) return;
	uint16_t mask = size - 1;
	uint8_t frame[2048];
	for (int budget = 16; budget; budget--)
	{
		int len = net->recv(net->ctx, frame, sizeof frame);
		if (len <= 0) break;
		if (len < 14 || len > MAX_FRAME) continue;
		if (mac_filter)
		{
			static const uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
			if (memcmp(frame, open_mac, 6) && memcmp(frame, bcast, 6)) { st.rx_drop_filter++; continue; }
		}
		uint16_t used = (uint16_t)(rx_wr - rx_rd);
		uint16_t need = (uint16_t)(len + 2);
		if ((uint32_t)used + need > size) { st.rx_drop_full++; continue; }
		uint16_t p = rx_wr;
		wr8(base + (p & mask), (uint8_t)(need >> 8)); p++;
		wr8(base + (p & mask), (uint8_t)need); p++;
		for (int i = 0; i < len; i++, p++) wr8(base + (p & mask), frame[i]);
		rx_wr = p;
		__sync_synchronize();
		wr16(SOCK_BASE(0) + SN_RX_RSR, (uint16_t)(rx_wr - rx_rd));
		st.rx++;
	}
}

void u2m_attach(volatile uint8_t *win)
{
	if (w != win) { w = win; last_gen = ~0ULL; }
}

int u2m_poll(const u2_net *net)
{
	if (!w || rd64(U2_CTL_MAGIC) != U2_MAGIC) return 0;

	uint64_t g = rd64(U2_CTL_GEN);
	if (g != last_gen)
	{
		// core reset: the FPGA's doorbell count restarted at 0
		last_gen = g;
		ring_rd = 0;
		chip_reset();
		wr64(U2_CTL_ACK, 0);
	}

	uint32_t wptr = (uint32_t)rd64(U2_CTL_WPTR);
	if (wptr - ring_rd > U2_RING_SIZE) ring_rd = wptr;   // lost step: resync
	int did = 0;
	while (ring_rd != wptr)
	{
		uint64_t e = rd64(U2_CTL_RING + 8 * (ring_rd % U2_RING_SIZE));
		if ((uint32_t)(e >> 32) != ring_rd) break;     // entry not landed yet
		uint8_t  val  = (uint8_t)e;
		uint16_t addr = (uint16_t)(e >> 8);
		uint8_t  kind = (uint8_t)(e >> 24);
		if (kind == 1) chip_reset();
		else if (addr >= 0x0400 && addr < 0x0800 && (addr & 0xFF) == SN_CR)
			do_cr((addr >> 8) - 4, val, net);
		ring_rd++;
		did = 1;
	}
	if (did)
	{
		__sync_synchronize();
		wr64(U2_CTL_ACK, ring_rd);
	}

	do_rx(net);
	return 1;
}

int u2m_macraw(uint8_t mac[6])
{
	if (!w || !macraw_open) return 0;
	memcpy(mac, open_mac, 6);
	return 1;
}

void u2m_stats(u2_stats *out) { *out = st; }
