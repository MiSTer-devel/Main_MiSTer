// Ethernet bridge daemon for the NeXT core -- see next_enet.h.
//
// DDR3 mailbox layout (ARM physical 0x1FF00000, the same window the
// A2065 uses; the cores are mutually exclusive).  All slots are 64-bit
// little-endian words; frame byte i sits at slot byte 8+i:
//   +0x0000  MAGIC     0x4E58544554483031 ("NXTETH01"), FPGA-written
//   +0x0008  TX_WPTR   FPGA increments per transmitted frame
//   +0x0010  RX_WPTR   this daemon increments per delivered frame
//   +0x0018  RX_RPTR   FPGA increments per consumed frame
//   +0x0020  GUEST_MAC bit63 valid, bits[47:0] station address
//   +0x0800  TX slots: 4 x 2048 bytes (u64 len header, then frame)
//   +0x2800  RX slots: 4 x 2048 bytes

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../../user_io.h"
#include "../../shmem.h"
#include "../../menu.h"
#include "next_enet.h"

// shared host-network layer from the A2065 module
extern int  ethernet_open(const char *iface, int promiscuous);
extern void ethernet_close(void);
extern void ethernet_send(const uint8_t *frame, int len);
extern int  ethernet_recv_nb(uint8_t *buf, int maxlen);
extern int  ethernet_set_mac_filter(const uint8_t *mac);
extern int  ethernet_macvlan_create(const char *parent, const char *name, const uint8_t *mac);
extern void ethernet_macvlan_delete(const char *name);
extern int  a2065_mode_available(int mode);

#define NB_BASE        0x1FF00000UL
#define NB_SIZE        0x10000UL

#define NB_MAGIC_OFF   0x0000
#define NB_TXWPTR_OFF  0x0008
#define NB_RXWPTR_OFF  0x0010
#define NB_RXRPTR_OFF  0x0018
#define NB_MAC_OFF     0x0020
#define NB_TXSLOT_OFF  0x0800
#define NB_RXSLOT_OFF  0x2800
#define NB_SLOT_SIZE   0x800
#define NB_RING        4

#define NB_MAGIC       0x4E58544554483031ULL

#define NB_MODE_OFF     0
#define NB_MODE_ETH0    1
#define NB_MODE_ETH1    2
#define NB_MODE_MACVLAN 3
#define NB_MODE_TAP     4

#define MACVLAN_NAME   "next0"
#define MAX_FRAME      1600

static volatile uint8_t *mb = 0;
static int      running = 0;      // start() called for this core
static int      link_open = 0;    // host interface open
static int      cur_mode = NB_MODE_OFF;
static int      made_macvlan = 0;
static uint64_t tx_rd = 0;        // local TX ring read index
static uint8_t  guest_mac[6];
static int      mac_known = 0;

static inline uint64_t rd64(uint32_t off)
{
	return *(volatile uint64_t *)(mb + off);
}

static inline void wr64(uint32_t off, uint64_t v)
{
	*(volatile uint64_t *)(mb + off) = v;
}

static int read_guest_mac(uint8_t *out)
{
	uint64_t v = rd64(NB_MAC_OFF);
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
}

static void open_link(int mode)
{
	const char *iface = "eth0";
	int promisc = 0;

	close_link();

	switch (mode)
	{
	case NB_MODE_ETH0:
		iface = "eth0";
		promisc = 1;   // shared NIC: promiscuous plus the BPF MAC filter
		break;
	case NB_MODE_ETH1:
		iface = "eth1";
		break;
	case NB_MODE_MACVLAN:
		if (!mac_known) return;    // needs the guest MAC to create the child
		if (!ethernet_macvlan_create("eth0", MACVLAN_NAME, guest_mac)) return;
		made_macvlan = 1;
		iface = MACVLAN_NAME;
		break;
	case NB_MODE_TAP:
		iface = "tap0";
		break;
	default:
		return;
	}

	if (!ethernet_open(iface, promisc)) return;
	link_open = 1;

	if (mode == NB_MODE_ETH0 && mac_known)
		ethernet_set_mac_filter(guest_mac);

	printf("[next-enet] bridge up on %s (mode %d)\n", iface, mode);
}

void next_enet_start(void)
{
	if (!mb)
	{
		mb = (volatile uint8_t *)shmem_map(NB_BASE, NB_SIZE);
		if (!mb)
		{
			printf("[next-enet] shmem_map failed\n");
			return;
		}
	}
	running = 1;
	tx_rd = 0;
	mac_known = 0;
	cur_mode = NB_MODE_OFF;
	printf("[next-enet] armed, waiting for the core mailbox\n");
}

void next_enet_stop(void)
{
	if (!running) return;
	running = 0;
	close_link();
	printf("[next-enet] stopped\n");
}

static int mode_from_status(void)
{
	int mode = (int)user_io_status_get(NEXT_ENET_STATUS_OPT);
	if (mode < 0 || mode > NB_MODE_TAP) mode = NB_MODE_OFF;
	if (!a2065_mode_available(mode)) mode = NB_MODE_OFF;
	return mode;
}

void next_enet_poll(void)
{
	static uint8_t frame[MAX_FRAME];

	if (!running || !mb) return;
	if (rd64(NB_MAGIC_OFF) != NB_MAGIC) return;   // core not up yet

	// track the guest MAC published by the FPGA
	uint8_t m[6];
	if (read_guest_mac(m) && (!mac_known || memcmp(m, guest_mac, 6)))
	{
		memcpy(guest_mac, m, 6);
		mac_known = 1;
		printf("[next-enet] guest MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
		       m[0], m[1], m[2], m[3], m[4], m[5]);
		if (link_open && cur_mode == NB_MODE_ETH0)
			ethernet_set_mac_filter(guest_mac);
	}

	// follow the OSD selection
	int mode = mode_from_status();
	if (mode != cur_mode)
	{
		cur_mode = mode;
		if (mode == NB_MODE_OFF) close_link();
		else open_link(mode);
	}
	else if (mode != NB_MODE_OFF && !link_open)
	{
		// retry (macvlan waits for the MAC to become known)
		open_link(mode);
	}

	if (!link_open)
	{
		// with no wire, still consume the guest's TX frames
		tx_rd = rd64(NB_TXWPTR_OFF);
		return;
	}

	// drain guest transmissions
	uint64_t wptr = rd64(NB_TXWPTR_OFF);
	if (wptr - tx_rd > NB_RING) tx_rd = wptr - NB_RING;
	while (tx_rd != wptr)
	{
		uint32_t slot = NB_TXSLOT_OFF + NB_SLOT_SIZE * (uint32_t)(tx_rd & (NB_RING - 1));
		int len = (int)(rd64(slot) & 0x7FF);
		if (len > 0 && len <= MAX_FRAME)
		{
			memcpy(frame, (const void *)(mb + slot + 8), len);
			ethernet_send(frame, len);
		}
		tx_rd++;
	}

	// deliver received frames while the FPGA ring has room
	for (int budget = 0; budget < NB_RING; budget++)
	{
		uint64_t rxw = rd64(NB_RXWPTR_OFF);
		uint64_t rxr = rd64(NB_RXRPTR_OFF);
		if (rxw - rxr >= NB_RING) break;   // ring full, FPGA still draining

		int len = ethernet_recv_nb(frame, MAX_FRAME);
		if (len <= 0) break;
		if (len < 14) continue;

		uint32_t slot = NB_RXSLOT_OFF + NB_SLOT_SIZE * (uint32_t)(rxw & (NB_RING - 1));
		memcpy((void *)(mb + slot + 8), frame, len);
		__sync_synchronize();
		wr64(slot, (uint64_t)len);
		__sync_synchronize();
		wr64(NB_RXWPTR_OFF, rxw + 1);
	}
}
