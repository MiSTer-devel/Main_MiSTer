// IBM RS/6000 43P 7248 Ethernet bridge (the planar's Am79C970 PCnet, in the FPGA): frames cross a DDR3
// mailbox of 64-bit little-endian words. A copy of support/hp9k/hp9k_enet.cpp with this core's magic, its
// OSD bits, and one addition: the MAC for the PCnet's address PROM (+0x38), made from eth0's.

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <strings.h>

#include "../../user_io.h"
#include "../../shmem.h"
#include "ibmrs6k_enet.h"

extern int  ethernet_open(const char *iface, int promiscuous);
extern void ethernet_close(void);
extern void ethernet_send(const uint8_t *frame, int len);
extern int  ethernet_recv_nb(uint8_t *buf, int maxlen);
extern int  ethernet_set_mac_filter_mcast(const uint8_t *mac);
extern int  ethernet_macvlan_create(const char *parent, const char *name, const uint8_t *mac);
extern void ethernet_macvlan_delete(const char *name);
extern int  a2065_mode_available(int mode);
extern void ethernet_offload_off(const char *iface);
extern void ethernet_offload_on(const char *iface);

#define SB_BASE       0x1FF00000UL   // shared with the A2065, NeXT and Sun bridges
#define SB_SIZE       0x10000UL

#define SB_MAGIC_OFF  0x0000   // FPGA, written last; names the RX ring depth
#define SB_GEN_OFF    0x0008   // FPGA, new at every start-up
#define SB_TXW_OFF    0x0010   // FPGA: frames posted
#define SB_TXR_OFF    0x0018   // ours: frames taken
#define SB_RXW_OFF    0x0020   // ours: frames posted
#define SB_RXR_OFF    0x0028   // FPGA: frames taken
#define SB_MAC_OFF    0x0030   // FPGA: bit 63 valid, bits 47:40 the first byte
#define SB_TX_OFF     0x1000   // slot header bits 10:0 = length
#define SB_RX_OFF     0x5000   // slot header bits 10:0 = length with FCS, 21:16 = LADRF index
#define SB_SLOT       0x800
#define SB_TX_RING    8

#define SB_MAGIC_RS6  0x5253364554483031ULL   // RS6ETH01 (IBMRS6K7248)
#define SB_PMAC_OFF   0x0038   // ours: bit 63 valid, the MAC for the PCnet's address PROM

static uint32_t rx_ring_slots(uint64_t magic)
{
	switch (magic)
	{
	case SB_MAGIC_RS6: return 16;
	default:           return 0;
	}
}

#define MODE_OFF      0
#define MODE_ETH0     1
#define MODE_ETH1     2
#define MODE_MACVLAN  3
#define MODE_TAP      4

#define MACVLAN_NAME  "rs6k0"
#define MAX_FRAME     (SB_SLOT - 8)

// the OSD's Network interface row (IBMRS6K7248.sv: "O[8:6],Network interface,eth0,None,eth1,macvlan,tap0")
static const char *ibmrs6k_net_status()
{
	// the RBF name, not the CONF_STR override; it only changes with a new core
	return !strcasecmp(user_io_get_core_name(1), "IBMRS6K7248") ? "[8:6]" : 0;
}

// the PCnet's own MAC: IBM's OUI (08:00:5A) and the last three bytes of this board's eth0, so it is the
// same at every start and differs between boards
static uint64_t prom_mac(void)
{
	uint8_t m[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
	FILE *f = fopen("/sys/class/net/eth0/address", "r");
	if (f)
	{
		unsigned b[6];
		if (fscanf(f, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6)
			for (int i = 0; i < 6; i++) m[i] = (uint8_t)b[i];
		fclose(f);
	}
	return (1ULL << 63) | (0x08005AULL << 24) | ((uint64_t)m[3] << 16) | ((uint64_t)m[4] << 8) | m[5];
}

static volatile uint8_t *mb = 0;
static int      running = 0;
static int      link_open = 0;
static int      cur_mode = MODE_OFF;
static int      made_macvlan = 0;
static const char *offload_iface = 0;
static uint64_t gen = ~0ULL;
static uint8_t  guest_mac[6];
static int      mac_known = 0;
static uint32_t crc_tab[256];

static inline uint64_t rd64(uint32_t off) { return *(volatile uint64_t *)(mb + off); }
static inline void wr64(uint32_t off, uint64_t v) { *(volatile uint64_t *)(mb + off) = v; }

// IEEE 802.3 CRC-32, bit-reversed (as on the wire)
static uint32_t crc32_le(const uint8_t *p, int n)
{
	uint32_t c = 0xFFFFFFFF;
	while (n--) c = crc_tab[(c ^ *p++) & 0xFF] ^ (c >> 8);
	return c;
}

static int read_guest_mac(uint8_t *out)
{
	uint64_t v = rd64(SB_MAC_OFF);
	if (!(v >> 63)) return 0;
	for (int i = 0; i < 6; i++) out[i] = (v >> (40 - 8 * i)) & 0xFF;
	return 1;
}

static void close_link(void)
{
	if (link_open)
	{
		ethernet_close();
		link_open = 0;
	}
	if (made_macvlan)
	{
		ethernet_macvlan_delete(MACVLAN_NAME);
		made_macvlan = 0;
	}
	if (offload_iface)
	{
		ethernet_offload_on(offload_iface);
		offload_iface = 0;
	}
}

static void open_link(int mode)
{
	const char *iface;
	int promisc = 0;

	close_link();
	switch (mode)
	{
	case MODE_ETH0:
		iface = "eth0";
		promisc = 1;   // BPF-filtered on the guest's MAC
		break;
	case MODE_ETH1:
		iface = "eth1";
		promisc = 1;
		break;
	case MODE_MACVLAN:
		if (!mac_known) return;
		if (!ethernet_macvlan_create("eth0", MACVLAN_NAME, guest_mac)) return;
		made_macvlan = 1;
		iface = MACVLAN_NAME;
		break;
	case MODE_TAP:
		iface = "tap0";
		break;
	default:
		return;
	}

	// GRO and friends would hand the guest frames larger than its NIC takes
	if (mode != MODE_TAP)
	{
		offload_iface = (mode == MODE_ETH1) ? "eth1" : "eth0";
		ethernet_offload_off(offload_iface);
	}

	if (!ethernet_open(iface, promisc)) return;
	link_open = 1;

	uint8_t scratch[MAX_FRAME];
	while (ethernet_recv_nb(scratch, MAX_FRAME) > 0) ;
	if ((mode == MODE_ETH0 || mode == MODE_ETH1) && mac_known) ethernet_set_mac_filter_mcast(guest_mac);
	printf("[ibmrs6k-enet] bridge up on %s (mode %d)\n", iface, mode);
}

void ibmrs6k_enet_start(void)
{
	if (!ibmrs6k_net_status()) return;

	if (!mb)
	{
		mb = (volatile uint8_t *)shmem_map(SB_BASE, SB_SIZE);
		if (!mb)
		{
			printf("[ibmrs6k-enet] shmem_map failed\n");
			return;
		}
		// a stale magic must not look alive
		wr64(SB_MAGIC_OFF, 0);
	}
	for (uint32_t i = 0; i < 256; i++)
	{
		uint32_t c = i;
		for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320 & -(c & 1));
		crc_tab[i] = c;
	}
	running = 1;
	gen = ~0ULL;
	mac_known = 0;
	cur_mode = MODE_OFF;
	printf("[ibmrs6k-enet] armed, waiting for the core's mailbox\n");
}

void ibmrs6k_enet_stop(void)
{
	if (!running) return;
	running = 0;
	close_link();
	printf("[ibmrs6k-enet] stopped\n");
}

static int mode_from_status(void)
{
	// OSD value 0 is eth0 and 1 is Off; the rest equal their MODE_ value
	const char *opt = ibmrs6k_net_status();
	if (!opt) return MODE_OFF;
	int v = (int)user_io_status_get(opt);
	int mode = (v == 0) ? MODE_ETH0 : (v == 1) ? MODE_OFF : v;
	if (mode < 0 || mode > MODE_TAP) mode = MODE_OFF;
	if (!a2065_mode_available(mode)) mode = MODE_OFF;
	return mode;
}

void ibmrs6k_enet_poll(void)
{
	static uint8_t frame[MAX_FRAME + 64];

	if (!running || !mb) return;

	uint32_t rx_ring = rx_ring_slots(rd64(SB_MAGIC_OFF));
	if (!rx_ring) return;

	// new FPGA start-up: its pointers are zero again
	uint64_t g = rd64(SB_GEN_OFF);
	if (g != gen)
	{
		gen = g;
		mac_known = 0;
		printf("[ibmrs6k-enet] core mailbox up (generation %08llX)\n", (unsigned long long)g);
		wr64(SB_PMAC_OFF, prom_mac());
	}

	uint8_t m[6];
	if (read_guest_mac(m) && (!mac_known || memcmp(m, guest_mac, 6)))
	{
		memcpy(guest_mac, m, 6);
		mac_known = 1;
		printf("[ibmrs6k-enet] guest MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
		       m[0], m[1], m[2], m[3], m[4], m[5]);
		if (link_open && (cur_mode == MODE_ETH0 || cur_mode == MODE_ETH1))
			ethernet_set_mac_filter_mcast(guest_mac);
		if (cur_mode == MODE_MACVLAN) cur_mode = -1;
	}

	int mode = mode_from_status();
	if (mode != cur_mode)
	{
		cur_mode = mode;
		if (mode == MODE_OFF) close_link();
		else open_link(mode);
	}
	else if (mode != MODE_OFF && !link_open)
	{
		open_link(mode);
	}

	uint64_t txw = rd64(SB_TXW_OFF);
	uint64_t txr = rd64(SB_TXR_OFF);
	if (txw - txr > SB_TX_RING) txr = txw;           // out of step: resync
	while (txr != txw)
	{
		uint32_t slot = SB_TX_OFF + SB_SLOT * (uint32_t)(txr % SB_TX_RING);
		int len = (int)(rd64(slot) & 0x7FF);
		if (link_open && len >= 14 && len <= MAX_FRAME)
		{
			memcpy(frame, (const void *)(mb + slot + 8), len);
			ethernet_send(frame, len);
		}
		txr++;
	}
	__sync_synchronize();
	wr64(SB_TXR_OFF, txr);

	if (!link_open) return;

	for (;;)
	{
		int len = ethernet_recv_nb(frame, MAX_FRAME);
		if (len <= 0) break;
		if (len < 14) continue;

		uint64_t rxw = rd64(SB_RXW_OFF);
		uint64_t rxr = rd64(SB_RXR_OFF);
		if (rxw - rxr >= rx_ring) continue;          // full: drop, keep draining

		if (len < 60)
		{
			memset(frame + len, 0, 60 - len);
			len = 60;
		}
		if (len > MAX_FRAME - 4) continue;
		uint32_t fcs = ~crc32_le(frame, len);
		for (int k = 0; k < 4; k++) frame[len++] = fcs >> (8 * k);
		uint32_t hash = crc32_le(frame, 6) >> 26;

		uint32_t slot = SB_RX_OFF + SB_SLOT * (uint32_t)(rxw % rx_ring);
		memcpy((void *)(mb + slot + 8), frame, len);
		__sync_synchronize();
		wr64(slot, (uint64_t)len | ((uint64_t)hash << 16));
		__sync_synchronize();
		wr64(SB_RXW_OFF, rxw + 1);
	}
}
