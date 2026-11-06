// 3B1 Ethernet bridge: frames cross a DDR3 mailbox of 64-bit little-endian words (see rtl/ethernet/eth_ddr_mbox.sv in the core).
//
//   word 0 magic "B31ETH01"
//   word 1 TX descriptor: [31:0] sequence, [47:32] length
//   word 2 TX ack: sequence taken
//   word 3 RX descriptor: [31:0] sequence, [47:32] length
//   word 4 RX ack: sequence taken
//   word 5 station address, first byte on the wire in bits 47:40
//   word 6 counters (FPGA): [15:0] sent, [31:16] received, [47:32] dropped
//   words 0x200.. TX frame bytes, 0x400.. RX frame bytes

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <strings.h>

#include "../../user_io.h"
#include "../../shmem.h"
#include "3b1_eth.h"

// host network layer
extern int  ethernet_open(const char *iface, int promiscuous);
extern void ethernet_close(void);
extern void ethernet_send(const uint8_t *frame, int len);
extern int  ethernet_recv_nb(uint8_t *buf, int maxlen);
extern int  ethernet_set_mac_filter(const uint8_t *mac);
extern int  ethernet_macvlan_create(const char *parent, const char *name, const uint8_t *mac);
extern void ethernet_macvlan_delete(const char *name);
extern int  a2065_mode_available(int mode);
extern void ethernet_offload_off(const char *iface);
extern void ethernet_offload_on(const char *iface);

#define MB_BASE       0x1FF00000UL   // shared with the A2065, NeXT, Sun and HP 9000 bridges
#define MB_SIZE       0x10000UL
#define W_MAGIC       0
#define W_TXD         1
#define W_TXA         2
#define W_RXD         3
#define W_RXA         4
#define W_MAC         5
#define W_CNT         6
#define W_TXF         0x200
#define W_RXF         0x400
#define MB_MAGIC      0x4231314554483031ULL   // "B31ETH01" (the FPGA writes it as the 64-bit word 4231_3145_5448_3031)
#define MAX_FRAME     1536

#define OPT_NET       "[20:19]"   // OSD "Ethernet": 0 eth0, 1 Off, 2 macvlan
#define MODE_OFF      0
#define MODE_ETH0     1
#define MODE_MACVLAN  3
#define MACVLAN_NAME  "b3eth0"

static volatile uint64_t *mb = 0;
static int      running = 0;
static int      link_open = 0;
static int      cur_mode = MODE_OFF;
static int      made_macvlan = 0;
static int      offload_off = 0;
static uint8_t  guest_mac[6];
static int      mac_known = 0;
static uint32_t tx_seen = 0, rx_pub = 0;
static unsigned long n_tx = 0, n_rx = 0, n_bad = 0;

static inline uint64_t rd(unsigned w) { return mb[w]; }
static inline void wr(unsigned w, uint64_t v) { mb[w] = v; }

static int is_core_named(const char *n)
{
	size_t len = strlen(n);
	return !strncasecmp(user_io_get_core_name(0), n, len) || !strncasecmp(user_io_get_core_name(1), n, len);
}

char is_3b1()
{
	return is_core_named("3B1");
}

static void close_link(void)
{
	if (link_open) { ethernet_close(); link_open = 0; }
	if (made_macvlan) { ethernet_macvlan_delete(MACVLAN_NAME); made_macvlan = 0; }
	if (offload_off) { ethernet_offload_on("eth0"); offload_off = 0; }
}

static void open_link(int mode)
{
	const char *iface;
	int promisc = 0;

	close_link();
	if (mode == MODE_ETH0)
	{
		iface = "eth0";
		promisc = 1;
	}
	else if (mode == MODE_MACVLAN)
	{
		if (!mac_known) return;
		if (!ethernet_macvlan_create("eth0", MACVLAN_NAME, guest_mac)) return;
		made_macvlan = 1;
		iface = MACVLAN_NAME;
	}
	else return;

	ethernet_offload_off("eth0");
	offload_off = 1;

	if (!ethernet_open(iface, promisc)) return;
	link_open = 1;

	uint8_t scratch[MAX_FRAME + 64];
	while (ethernet_recv_nb(scratch, sizeof scratch) > 0) ;
	if (mode == MODE_ETH0 && mac_known) ethernet_set_mac_filter(guest_mac);
	printf("[3b1-eth] bridge up on %s\n", iface);
}

static int mode_from_status(void)
{
	int v = (int)user_io_status_get(OPT_NET);
	int mode = (v == 0) ? MODE_ETH0 : (v == 2) ? MODE_MACVLAN : MODE_OFF;
	if (!a2065_mode_available(mode)) mode = MODE_OFF;
	return mode;
}

void b3_eth_start(void)
{
	if (!mb)
	{
		mb = (volatile uint64_t *)shmem_map(MB_BASE, MB_SIZE);
		if (!mb) { printf("[3b1-eth] shmem_map failed\n"); return; }
	}
	wr(W_MAGIC, 0);
	running = 1;
	mac_known = 0;
	cur_mode = -1;
	tx_seen = rx_pub = 0;
	printf("[3b1-eth] armed, waiting for the core's mailbox\n");
}

void b3_eth_stop(void)
{
	if (!running) return;
	running = 0;
	close_link();
	printf("[3b1-eth] stopped (%lu frames out, %lu in, %lu bad)\n", n_tx, n_rx, n_bad);
}

void b3_eth_poll(void)
{
	static uint8_t frame[MAX_FRAME + 64];

	if (!running || !mb) return;

	// the FPGA writes the magic after every reset
	if (rd(W_MAGIC) == MB_MAGIC)
	{
		uint64_t m = rd(W_MAC);
		for (int i = 0; i < 6; i++) guest_mac[i] = (m >> (40 - 8 * i)) & 0xFF;
		mac_known = 1;
		tx_seen = rx_pub = 0;
		wr(W_MAGIC, 0);
		printf("[3b1-eth] core mailbox up, 3B1 MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
		       guest_mac[0], guest_mac[1], guest_mac[2], guest_mac[3], guest_mac[4], guest_mac[5]);
		if (link_open && cur_mode == MODE_ETH0) ethernet_set_mac_filter(guest_mac);
		if (cur_mode == MODE_MACVLAN) cur_mode = -1;
	}
	if (!mac_known) return;

	int mode = mode_from_status();
	if (mode != cur_mode)
	{
		cur_mode = mode;
		if (mode == MODE_OFF) close_link(); else open_link(mode);
	}
	else if (mode != MODE_OFF && !link_open) open_link(mode);

	// TX: 3B1 -> network
	uint64_t d = rd(W_TXD);
	uint32_t seq = (uint32_t)d;
	if (seq != tx_seen)
	{
		int len = (int)((d >> 32) & 0xFFFF);
		__sync_synchronize();
		if (link_open && len >= 14 && len <= MAX_FRAME)
		{
			for (int i = 0; i < (len + 7) / 8; i++)
			{
				uint64_t w = rd(W_TXF + i);
				memcpy(frame + i * 8, &w, 8);
			}
			ethernet_send(frame, len);
			n_tx++;
		}
		else n_bad++;
		tx_seen = seq;
		__sync_synchronize();
		wr(W_TXA, seq);
	}

	// RX: network -> 3B1, one frame at a time
	if (link_open && (uint32_t)rd(W_RXA) == rx_pub)
	{
		int n = ethernet_recv_nb(frame, sizeof frame);
		if (n >= 14 && n <= MAX_FRAME)
		{
			for (int i = 0; i < (n + 7) / 8; i++)
			{
				uint64_t w = 0;
				memcpy(&w, frame + i * 8, (n - i * 8 >= 8) ? 8 : (n - i * 8));
				wr(W_RXF + i, w);
			}
			__sync_synchronize();
			rx_pub++;
			wr(W_RXD, ((uint64_t)n << 32) | rx_pub);
			n_rx++;
		}
	}
}
