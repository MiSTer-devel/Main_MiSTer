#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>

#include "../../user_io.h"
#include "../../menu.h"
#include "../../shmem.h"
#include "../../hardware.h"
#include "mac.h"
#include "mac_eth.h"
#include "mac_sonic.h"
#include "mac_eth_declrom.h"
#include "mac_eth_q8.h"

enum { CARD_LC, CARD_Q8 };
static int card_kind;

static volatile uint8_t *win;
static uint32_t ctrl_base;
static int      card_up;
static uint32_t rptr;
static uint8_t  dma_seq;

static inline uint64_t now_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000ull + ts.tv_nsec / 1000;
}

static uint8_t  guest_mac[6];
static char     ifname[64] = "eth0";

static volatile uint64_t *w64(uint32_t off)
{
	return (volatile uint64_t *)(win + off);
}

static volatile uint64_t *ctl(uint32_t rel)
{
	return w64(ctrl_base + rel);
}

#define ETH_OPT_IFACE  "[37:36]"
#define ETH_OPT_MACSUF "[35:32]"
static const char *const iface_names[4] = { "eth0", "tap0", "macvlan", "eth1" };

#define Q8_OPT_ENABLE  "[6]"
#define Q8_OPT_IFACE   "[8:7]"
static const char *const q8_iface_names[4] = { "eth0", "eth1", "wlan0", "tap0" };

#define MAC_OUI_0   0x08
#define MAC_OUI_1   0x00
#define MAC_OUI_2   0x07
#define MAC_FAMILY  0x4D
#define MAC_CORE_LC 0x4C
#define MAC_CORE_Q8 0x51

#define ADDR_BITS_DEFAULT 24
#define DECLROM_NAME      "ethernet.rom"
static int addr_bits = ADDR_BITS_DEFAULT;

static int core_is_maclc(void)
{
	return !strcasecmp(user_io_get_core_name(0), "maclc")
	    || !strcasecmp(user_io_get_core_name(1), "maclc");
}

static int core_is_q800(void)
{
	return !strcasecmp(user_io_get_core_name(0), "macquadra800")
	    || !strcasecmp(user_io_get_core_name(1), "macquadra800");
}

static void q8_config(void)
{
	snprintf(ifname, sizeof ifname, "%s", q8_iface_names[user_io_status_get(Q8_OPT_IFACE) & 3]);
	guest_mac[0] = MAC_OUI_0; guest_mac[1] = MAC_OUI_1; guest_mac[2] = MAC_OUI_2;
	guest_mac[3] = MAC_FAMILY; guest_mac[4] = MAC_CORE_Q8; guest_mac[5] = 0;

	const char *const from[3] = { ifname, "eth0", "wlan0" };
	uint8_t hw[6];
	for (int i = strncmp(ifname, "tap", 3) ? 0 : 1; i < 3; i++)
		if (mac_eth_iface_hwaddr(from[i], hw))
		{
			guest_mac[3] = hw[3]; guest_mac[4] = hw[4]; guest_mac[5] = hw[5];
			return;
		}
	printf("mac_eth: no interface with a hardware address - fixed MAC\n");
}

static void load_config(void)
{
	if (card_kind == CARD_Q8) { q8_config(); return; }

	snprintf(ifname, sizeof ifname, "%s", iface_names[user_io_status_get(ETH_OPT_IFACE) & 3]);
	guest_mac[0] = MAC_OUI_0; guest_mac[1] = MAC_OUI_1; guest_mac[2] = MAC_OUI_2;
	guest_mac[3] = MAC_FAMILY;
	guest_mac[4] = MAC_CORE_LC;
	guest_mac[5] = (uint8_t)(user_io_status_get(ETH_OPT_MACSUF) & 0xF);
	addr_bits = ADDR_BITS_DEFAULT;

	FILE *f = fopen(getFullPath(user_io_make_filepath(HomeDir(), "eth.cfg")), "r");
	if (!f) return;
	char line[160];
	while (fgets(line, sizeof line, f))
	{
		line[strcspn(line, "\r\n")] = 0;
		if (!strncasecmp(line, "iface=", 6) && line[6])
			snprintf(ifname, sizeof ifname, "%s", line + 6);
		else if (!strncasecmp(line, "mac=", 4))
		{
			unsigned m[6];
			if (sscanf(line + 4, "%x:%x:%x:%x:%x:%x",
			           &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6)
				for (int i = 0; i < 6; i++) guest_mac[i] = (uint8_t)m[i];
		}
		else if (!strncasecmp(line, "macbyte=", 8))
		{
			unsigned b;
			if (sscanf(line + 8, "%x", &b) == 1) guest_mac[4] = (uint8_t)b;
		}
		else if (!strncasecmp(line, "addrbits=", 9))
		{
			int b = atoi(line + 9);
			if (b == 24 || b == 32) addr_bits = b;
			else printf("mac_eth: addrbits=%s ignored (24 or 32)\n", line + 9);
		}
	}
	fclose(f);
}

#define DMA_SPIN_US 1500

static void ring_slurp(void);

#define SONIC_CR        0x00
#define SONIC_CR_TXP    0x0002
#define SONIC_ISR       0x05
static int dma_rpc(uint32_t gaddr, uint32_t len, int wr)
{
	if (!card_up) return -1;
	if (++dma_seq == 0) dma_seq = 1;

	if (gaddr > 0x00ffffffu)
	{
		if (addr_bits > 24)
		{
			static int warned;
			if (!warned++)
				printf("mac_eth: guest address %08X exceeds the mailbox's 24-bit field\n", gaddr);
			return -1;
		}
		gaddr &= 0x00ffffffu;
	}

	*ctl(ETH_CTL_DMACMD) = ((uint64_t)len << 40) | ((uint64_t)gaddr << 16)
	                     | ((uint64_t)(wr ? 1 : 0) << 8) | dma_seq;
	__sync_synchronize();

	uint64_t t0 = now_us();
	int rc = -1;
	for (;;)
	{
		uint64_t s = *ctl(ETH_CTL_DMASTAT);
		if ((s & 0xff) == dma_seq) { rc = (s & 0x100) ? -1 : 0; break; }
		ring_slurp();
		uint64_t el = now_us() - t0;
		if (el > 250000)
		{
			printf("mac_eth: DMA-RPC timeout (seq %u addr 0x%x len %u)\n",
			       dma_seq, gaddr, len);
			break;
		}
		if (el > DMA_SPIN_US) usleep(50);
	}
	return rc;
}

static int rpc_read(uint32_t ga, uint8_t *dst, uint32_t len)
{
	if (!len) return 0;
	uint32_t a0  = ga & ~1u;
	uint32_t off = ga - a0;
	uint32_t n   = (off + len + 1) & ~1u;
	if (n > 0xFFFE) return -1;
	if (dma_rpc(a0, n, 0)) return -1;
	memcpy(dst, (const void *)(win + ETH_LC_OFF_XFER + off), len);
	return 0;
}

static int rpc_write(uint32_t ga, const uint8_t *src, uint32_t len)
{
	if (!len) return 0;
	uint32_t a0  = ga & ~1u;
	uint32_t off = ga - a0;
	uint32_t n   = (off + len + 1) & ~1u;
	if (n > 0xFFFE) return -1;
	if (off || (n != off + len))
	{
		if (dma_rpc(a0, n, 0)) return -1;
	}
	memcpy((void *)(win + ETH_LC_OFF_XFER + off), src, len);
	return dma_rpc(a0, n, 1);
}

static int gw_read_words(uint32_t ga, uint16_t *v, int n, int stride)
{
	uint8_t b[64 * 4];
	if (n <= 0 || n > 64) return -1;
	if (rpc_read(ga, b, (uint32_t)n * stride)) return -1;
	for (int i = 0; i < n; i++)
	{
		const uint8_t *p = b + i * stride + (stride == 4 ? 2 : 0);
		v[i] = (uint16_t)((p[0] << 8) | p[1]);
	}
	return 0;
}

static int gw_write_words(uint32_t ga, const uint16_t *v, int n, int stride)
{
	uint8_t b[64 * 4];
	if (n <= 0 || n > 64) return -1;
	memset(b, 0, (size_t)n * stride);
	for (int i = 0; i < n; i++)
	{
		uint8_t *p = b + i * stride + (stride == 4 ? 2 : 0);
		p[0] = (uint8_t)(v[i] >> 8);
		p[1] = (uint8_t)v[i];
	}
	return rpc_write(ga, b, (uint32_t)n * stride);
}

static int gb_read(uint32_t ga, uint8_t *b, int n)  { return rpc_read(ga, b, (uint32_t)n); }
static int gb_write(uint32_t ga, const uint8_t *b, int n) { return rpc_write(ga, b, (uint32_t)n); }

static const sonic_host_ops lc_host_ops = {
	gw_read_words, gw_write_words, gb_read, gb_write, mac_eth_iface_send
};

static const sonic_host_ops q8_host_ops = {
	q8_read_words, q8_write_words, q8_read_bytes, q8_write_bytes, mac_eth_iface_send
};

static uint32_t declrom_crc(const uint8_t *p, uint32_t n, uint32_t crc_off)
{
	uint32_t s = 0;
	for (uint32_t i = 0; i < n; i++)
	{
		uint32_t b = (i >= crc_off && i < crc_off + 4) ? 0 : p[i];
		s = ((s << 1) | (s >> 31)) + b;
	}
	return s;
}

static int declrom_valid(const uint8_t *d, uint32_t size, int quiet, uint8_t want_lanes)
{
	if (size < 20) return 0;
	uint32_t length = ((uint32_t)d[size - 16] << 24) | ((uint32_t)d[size - 15] << 16)
	                | ((uint32_t)d[size - 14] << 8)  | d[size - 13];
	uint32_t crc    = ((uint32_t)d[size - 12] << 24) | ((uint32_t)d[size - 11] << 16)
	                | ((uint32_t)d[size - 10] << 8)  | d[size - 9];
	uint32_t testpat = ((uint32_t)d[size - 6] << 24) | ((uint32_t)d[size - 5] << 16)
	                 | ((uint32_t)d[size - 4] << 8)  | d[size - 3];
	uint8_t bytelanes = d[size - 1];
	if (testpat != 0x5A932BC7 || length > size || length < 20)
	{
		if (!quiet) printf("mac_eth: declROM format block bad (testPattern %08X)\n",
		                   testpat);
		return 0;
	}
	if (bytelanes != want_lanes)
	{
		if (!quiet) printf("mac_eth: declROM byteLanes %02X, this card needs %02X\n",
		                   bytelanes, want_lanes);
		return 0;
	}
	const uint8_t *span = d + size - length;
	uint32_t computed = declrom_crc(span, length, length - 12);
	if (computed != crc)
	{
		if (!quiet) printf("mac_eth: declROM CRC %08X != stored %08X\n", computed, crc);
		return 0;
	}
	return 1;
}

static uint8_t  declrom[MAC_ETH_DECLROM_SIZE];
static uint32_t declrom_len;

static int load_declrom(int quiet)
{
	uint8_t want_lanes = 0x0F;
	declrom_len = 0;

	char path[1200];
	snprintf(path, sizeof path, "%s", getFullPath(user_io_make_filepath(HomeDir(), DECLROM_NAME)));
	FILE *f = fopen(path, "rb");
	if (f)
	{
		size_t n = fread(declrom, 1, sizeof declrom, f);
		int over = (fgetc(f) != EOF);
		fclose(f);
		if (over || !n)
		{
			if (!quiet) printf("mac_eth: %s is not a %u-byte declROM\n",
			                   path, (unsigned)sizeof declrom);
			return 0;
		}
		if (!declrom_valid(declrom, (uint32_t)n, quiet, want_lanes))
		{
			if (!quiet) printf("mac_eth: %s failed validation - card stays down\n", path);
			return 0;
		}
		declrom_len = (uint32_t)n;
		if (!quiet) printf("mac_eth: declROM %s (%u bytes)\n", path, declrom_len);
		return 1;
	}

	if (!core_is_maclc()) return 0;

	const uint8_t *d = mac_eth_declrom_data;
	unsigned nspans = sizeof mac_eth_declrom_spans / sizeof mac_eth_declrom_spans[0];
	memset(declrom, 0, sizeof declrom);
	for (unsigned s = 0; s < nspans; s++)
		for (uint16_t i = 0; i < mac_eth_declrom_spans[s].len; i++)
			declrom[mac_eth_declrom_spans[s].off + i] = *d++;
	declrom_len = MAC_ETH_DECLROM_SIZE;
	return 1;
}

static void stage_declrom(void)
{
	volatile uint8_t *rom = win + ETH_LC_OFF_ROM;
	for (uint32_t i = 0; i < 0x10000; i++) rom[i] = 0;
	uint32_t off = MAC_ETH_DECLROM_WINDOW_OFF + (MAC_ETH_DECLROM_SIZE - declrom_len);
	for (uint32_t i = 0; i < declrom_len; i++) rom[off + i] = declrom[i];
}

static uint8_t swizzle(uint8_t x)
{
	uint8_t lo = (uint8_t)(((x & 1) << 3) | ((x & 2) << 1) | ((x & 4) >> 1) | ((x & 8) >> 3));
	uint8_t hi = (uint8_t)((((x >> 4) & 1) << 3) | (((x >> 4) & 2) << 1) |
	                       (((x >> 4) & 4) >> 1) | (((x >> 4) & 8) >> 3));
	return (uint8_t)((lo << 4) | hi);
}

static void stage_macprom(void)
{
	uint8_t prom[8];
	uint8_t x = 0;
	for (int i = 0; i < 6; i++)
	{
		prom[i] = swizzle(guest_mac[i]);
		x ^= prom[i];
	}
	prom[6] = 0;
	prom[7] = (uint8_t)(x ^ 0xff);
	uint64_t v = 0;
	for (int i = 0; i < 8; i++) v |= (uint64_t)prom[i] << (8 * i);
	*ctl(card_kind == CARD_Q8 ? ETH_Q8_MACPROM : ETH_CTL_MACPROM) = v;
}

static uint32_t q8_aptr;

static void push_state(void)
{
	if (card_kind == CARD_Q8) q8_flush();

	uint16_t r[64];
	sonic_fill_shadows(r);
	for (int n = 0; n < 16; n++)
	{
		uint64_t v = 0;
		for (int k = 0; k < 4; k++)
			v |= (uint64_t)r[4 * n + k] << (16 * k);
		*ctl(ETH_CTL_SHAD + 8 * n) = v;
	}
	__sync_synchronize();
	if (card_kind == CARD_Q8)
	{
		*ctl(ETH_Q8_PTRS) = ((uint64_t)q8_aptr << 32) | rptr;
		q8_isr_post((uint16_t)q8_aptr);
	}
	else *ctl(ETH_CTL_INT) = sonic_int_line() ? 1 : 0;
}

static void model_enter(void) { if (card_kind == CARD_Q8) q8_begin(); }

#define STASH_DEPTH 1024
static uint64_t ring_stash[STASH_DEPTH];
static uint32_t ring_stash_idx[STASH_DEPTH];
static int stash_head, stash_count;

static void publish_rptr(void)
{
	if (card_kind == CARD_Q8) *ctl(ETH_Q8_PTRS) = ((uint64_t)q8_aptr << 32) | rptr;
	else *ctl(ETH_CTL_RPTR) = rptr;
}

static void ring_slurp(void)
{
	uint32_t wp = (uint32_t)*ctl(ETH_CTL_WPTR);
	if (wp == rptr) return;
	if (wp < rptr)
	{
		printf("mac_eth: wptr regressed (%u < %u) — FPGA reset, resync\n", wp, rptr);
		rptr = 0;
		q8_aptr = 0;
		stash_head = stash_count = 0;
	}
	if (wp - rptr > ETH_RING_ENTRIES) rptr = wp - ETH_RING_ENTRIES;
	int guard = ETH_RING_ENTRIES;
	while (rptr != wp && guard--)
	{
		uint64_t e = *ctl(ETH_CTL_RING + 8 * (rptr & (ETH_RING_ENTRIES - 1)));
		if (stash_count < STASH_DEPTH)
		{
			ring_stash_idx[(stash_head + stash_count) % STASH_DEPTH] = rptr;
			ring_stash[(stash_head + stash_count++) % STASH_DEPTH] = e;
		}
		rptr++;
	}
	publish_rptr();
}

#define DRAIN_BUDGET 256
static void drain_ring(void)
{
	ring_slurp();
	int budget = DRAIN_BUDGET;
	while (stash_count && budget--)
	{
		uint64_t e = ring_stash[stash_head];
		uint32_t applied_idx = ring_stash_idx[stash_head] + 1;
		stash_head = (stash_head + 1) % STASH_DEPTH;
		stash_count--;
		if (!(e & 1)) { q8_aptr = applied_idx; continue; }
		int tag  = (int)(e >> 1) & 7;
		int r    = (int)(e >> 4) & 0x3f;
		int data = (int)(e >> 16) & 0xffff;
		model_enter();
		switch (tag)
		{
		case ETH_TAG_REG_WR:
			if (card_kind == CARD_Q8 && r == SONIC_ISR)
				data = q8_isr_qualify((uint16_t)data, (uint16_t)(e >> 32));
			sonic_reg_write(r, (uint16_t)data);
			break;
		case ETH_TAG_RESET:
			sonic_reset();
			if (card_kind == CARD_Q8) q8_isr_reset();
			break;
		}
		q8_aptr = applied_idx;
		push_state();
	}
}

#define SEL_NONE 0xFFFFFFFFu
static uint32_t sel_cur = SEL_NONE;
static uint32_t fail_announced = SEL_NONE;

static uint32_t sel_snapshot(void)
{
	if (core_is_q800()) return 0x10000 | (user_io_status_get(Q8_OPT_IFACE) & 3);
	return (user_io_status_get(ETH_OPT_IFACE) & 3) |
	       ((user_io_status_get(ETH_OPT_MACSUF) & 0xF) << 8);
}

static void announce_up(void)
{
	char msg[96];
	snprintf(msg, sizeof msg,
	         "Ethernet: %s\n%02X:%02X:%02X:%02X:%02X:%02X\n"
	         "restart the Mac to apply",
	         ifname, guest_mac[0], guest_mac[1], guest_mac[2],
	         guest_mac[3], guest_mac[4], guest_mac[5]);
	Info(msg, 3000);
}

static void announce_down(const char *what)
{
	if (fail_announced == sel_cur) return;
	fail_announced = sel_cur;
	char msg[80];
	snprintf(msg, sizeof msg, "Ethernet: %s", what);
	Info(msg, 4000);
}

static void card_start(void)
{
	card_kind = core_is_q800() ? CARD_Q8 : CARD_LC;
	ctrl_base = (card_kind == CARD_Q8) ? ETH_Q8_CTRL : ETH_LC_CTRL;
	load_config();
	sel_cur = sel_snapshot();

	int quiet = (fail_announced == sel_cur);
	if (card_kind == CARD_LC && !load_declrom(quiet))
	{
		announce_down("no declaration ROM");
		return;
	}
	if (!mac_eth_iface_open(ifname))
	{
		char msg[64];
		snprintf(msg, sizeof msg, "%s unavailable", ifname);
		announce_down(msg);
		printf("mac_eth: iface %s unavailable - card stays down\n", ifname);
		return;
	}
	fail_announced = SEL_NONE;

	sonic_init(card_kind == CARD_Q8 ? &q8_host_ops : &lc_host_ops);
	sonic_set_addr_bits(card_kind == CARD_Q8 ? 32 : addr_bits);
	sonic_set_isr_local(card_kind == CARD_Q8);

	rptr = (uint32_t)*ctl(ETH_CTL_WPTR);
	q8_aptr = rptr;
	publish_rptr();
	stash_head = stash_count = 0;
	if (card_kind == CARD_LC)
	{
		dma_seq = 0;
		*ctl(ETH_CTL_DMACMD)  = 0;
		*ctl(ETH_CTL_DMASTAT) = 0;
		stage_declrom();
	}
	else
	{
		q8_mailbox m = { win + ETH_Q8_OFF_XFER, ctl(ETH_Q8_OPS), ctl(ETH_Q8_DMACMD),
		                 ctl(ETH_Q8_DMASTAT), ctl(ETH_Q8_ISRSET), ctl(ETH_Q8_ISRACK), ring_slurp };
		q8_init(&m);
		q8_isr_reset();
	}

	stage_macprom();
	push_state();
	*ctl(card_kind == CARD_Q8 ? ETH_Q8_GEO : ETH_CTL_GEO) = (card_kind == CARD_Q8) ? 4 : 2;
	__sync_synchronize();
	*ctl(ETH_CTL_MAGIC) = (card_kind == CARD_Q8) ? ETH_MAGIC_Q8 : ETH_MAGIC_LC;
	card_up = 1;
	printf("mac_eth: %s up (iface %s, MAC %02X:%02X:%02X:%02X:%02X:%02X)\n",
	       card_kind == CARD_Q8 ? "Quadra 800 SONIC" : "LC PDS card", ifname,
	       guest_mac[0], guest_mac[1], guest_mac[2],
	       guest_mac[3], guest_mac[4], guest_mac[5]);
}

#define RXQ_DEPTH      64
#define RXQ_MAX_AGE_US 2000000ULL
static struct { uint8_t buf[1600]; int len; uint64_t t; } rxq[RXQ_DEPTH];
static int rxq_head, rxq_count;

static void rxq_reset(void) { rxq_head = rxq_count = 0; }

static void rxq_push(const uint8_t *f, int n)
{
	if (n > (int)sizeof rxq[0].buf || rxq_count == RXQ_DEPTH) return;
	int slot = (rxq_head + rxq_count) % RXQ_DEPTH;
	memcpy(rxq[slot].buf, f, n);
	rxq[slot].len = n;
	rxq[slot].t = now_us();
	rxq_count++;
}

static void rxq_flush(void)
{
	uint64_t now = now_us();
	while (rxq_count)
	{
		if (now - rxq[rxq_head].t <= RXQ_MAX_AGE_US)
		{
			model_enter();
			if (sonic_rx_frame(rxq[rxq_head].buf, rxq[rxq_head].len) < 0) return;
		}
		rxq_head = (rxq_head + 1) % RXQ_DEPTH;
		rxq_count--;
	}
}

static void card_stop(void)
{
	if (!card_up) return;
	*ctl(ETH_CTL_MAGIC) = 0;
	mac_eth_iface_close();
	card_up = 0;
	rxq_reset();
	printf("mac_eth: card down\n");
}

static int core_has_card(void)
{
	if (!is_mac_scsi_family()) return 0;
	if (core_is_maclc()) return 1;
	if (core_is_q800()) return user_io_status_get(Q8_OPT_ENABLE) & 1;
	return FileExists(user_io_make_filepath(HomeDir(), DECLROM_NAME));
}

void mac_eth_poll(void)
{
	static unsigned long pace_timer, name_timer;
	static int mapped;

	static int purged;
	if (!purged)
	{
		purged = 1;
		volatile uint8_t *w = (volatile uint8_t *)shmem_map(ETH_DDR_BASE, ETH_WIN_SIZE);
		if (w)
		{
			*(volatile uint64_t *)(w + ETH_LC_CTRL + ETH_CTL_MAGIC) = 0;
			*(volatile uint64_t *)(w + ETH_Q8_CTRL + ETH_CTL_MAGIC) = 0;
			shmem_unmap((void *)w, ETH_WIN_SIZE);
		}
	}

	if (!(card_up && card_kind == CARD_Q8))
	{
		if (!CheckTimer(pace_timer)) return;
		pace_timer = GetTimer(1);
	}

	if (CheckTimer(name_timer))
	{
		name_timer = GetTimer(1000);
		int want = core_has_card();
		if (want && !card_up)
		{
			if (!mapped)
			{
				win = (volatile uint8_t *)shmem_map(ETH_DDR_BASE, ETH_WIN_SIZE);
				mapped = (win != NULL);
			}
			if (mapped) card_start();
		}
		else if (!want && card_up) card_stop();
		else if (want && card_up && sel_snapshot() != sel_cur)
		{
			card_stop();
			card_start();
			if (card_up) announce_up();
		}
	}

	if (!card_up) return;

	drain_ring();
	model_enter();
	sonic_tx_continue();
	for (int spins = 32; (sonic_reg(SONIC_CR) & SONIC_CR_TXP) && spins; spins--)
	{
		drain_ring();
		model_enter();
		sonic_tx_continue();
	}

	{
		static uint64_t tick_last;
		uint64_t nowu = now_us();
		if (tick_last && nowu > tick_last)
			sonic_time_tick((unsigned)(nowu - tick_last));
		tick_last = nowu;
	}

	rxq_flush();
	uint8_t frame[2048];
	uint64_t rx_t0 = now_us();
	while (now_us() - rx_t0 < 1000)
	{
		int n = mac_eth_iface_recv(frame, sizeof frame);
		if (n <= 0) break;
		static int jumbo_warned;
		if (n > 1518 && !jumbo_warned++)
			printf("mac_eth: %d-byte frame off the tap - receive offload leaking (GRO?)\n", n);
		int unicast_ours = !(frame[0] & 1) && !memcmp(frame, guest_mac, 6);
		if (unicast_ours && rxq_count) { rxq_push(frame, n); continue; }
		model_enter();
		if (sonic_rx_frame(frame, n) < 0 && unicast_ours) rxq_push(frame, n);
	}

	push_state();
}
