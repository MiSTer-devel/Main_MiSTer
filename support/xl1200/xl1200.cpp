#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>

#include "../../user_io.h"
#include "../../shmem.h"
#include "../../hardware.h"
#include "xl1200.h"

extern int  ethernet_open(const char *iface, int promiscuous);
extern void ethernet_close(void);
extern void ethernet_send(const uint8_t *frame, int len);
extern int  ethernet_recv_nb(uint8_t *buf, int maxlen);
extern int  ethernet_set_mac_filter(const uint8_t *mac);
extern int  ethernet_macvlan_create(const char *parent, const char *name, const uint8_t *mac);
extern void ethernet_macvlan_delete(const char *name);
extern void ethernet_offload_off(const char *iface);
extern void ethernet_offload_on(const char *iface);
extern int  a2065_mode_available(int mode);

#define SB_BASE    0x1FF00000UL
#define SB_SIZE    0x10000UL
#define SB_MAGIC   0x0000
#define SB_GEN     0x0008
#define SB_TXW     0x0010
#define SB_TXR     0x0018
#define SB_RXW     0x0020
#define SB_RXR     0x0028
#define SB_MAC     0x0030
#define SB_TX      0x1000
#define SB_RX      0x5000
#define SB_SLOT    0x800
#define TX_RING    8
#define RX_RING    16
#define MAX_FRAME  (SB_SLOT - 8)
#define XL_MAGIC   0x584C455448303031ULL

#define MODE_OFF     0
#define MODE_ETH0    1
#define MODE_ETH1    2
#define MODE_MACVLAN 3
#define MODE_TAP     4

static volatile uint8_t *mb = 0;
static int running = 0;
static int link_open = 0;
static int cur_mode = MODE_OFF;
static int made_macvlan = 0;
static const char *offload_iface = 0;
static uint64_t gen = ~0ULL;
static uint8_t guest_mac[6];
static int mac_known = 0;
static uint8_t frame[MAX_FRAME];
static uint32_t crc_tab[256];

static uint64_t rd64(uint32_t off) { return *(volatile uint64_t *)(mb + off); }
static void wr64(uint32_t off, uint64_t v) { *(volatile uint64_t *)(mb + off) = v; }

static uint32_t crc32_le(const uint8_t *p, int n)
{
	uint32_t c = 0xFFFFFFFF;
	while (n--) c = crc_tab[(c ^ *p++) & 0xFF] ^ (c >> 8);
	return c;
}

char is_xl1200()
{
	static int type = 0;
	if (!type && user_io_get_core_name(1)[0])
	{
		type = !strcasecmp(user_io_get_core_name(1), "XL1200") ? 1 : 2;
		if (type == 1) core_poll = xl1200_enet_poll;
	}
	return type == 1;
}

static void close_link()
{
	if (link_open) ethernet_close();
	if (made_macvlan) ethernet_macvlan_delete("xl0");
	if (offload_iface) ethernet_offload_on(offload_iface);
	link_open = 0;
	made_macvlan = 0;
	offload_iface = 0;
}

static void open_link(int mode)
{
	int wired = (mode == MODE_ETH0 || mode == MODE_ETH1);
	const char *iface = (mode == MODE_ETH1) ? "eth1" : (mode == MODE_TAP) ? "tap0" : "eth0";

	close_link();
	if (mode == MODE_MACVLAN)
	{
		if (!mac_known || !ethernet_macvlan_create("eth0", "xl0", guest_mac)) return;
		made_macvlan = 1;
	}

	if (mode != MODE_TAP)
	{
		offload_iface = iface;
		ethernet_offload_off(iface);
	}

	if (made_macvlan) iface = "xl0";
	if (!ethernet_open(iface, wired)) return;
	link_open = 1;

	while (ethernet_recv_nb(frame, MAX_FRAME) > 0);
	if (wired && mac_known) ethernet_set_mac_filter(guest_mac);
	printf("[xl1200-enet] bridge up on %s\n", iface);
}

void xl1200_enet_start()
{
	if (!mb)
	{
		mb = (volatile uint8_t *)shmem_map(SB_BASE, SB_SIZE);
		if (!mb)
		{
			printf("[xl1200-enet] shmem_map failed\n");
			return;
		}
		wr64(SB_MAGIC, 0);
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
}

void xl1200_enet_stop()
{
	if (!running) return;
	running = 0;
	close_link();
}

void xl1200_enet_poll()
{
	static unsigned long rtc_timer = 0;
	if (!rtc_timer || CheckTimer(rtc_timer))
	{
		rtc_timer = GetTimer(60000);
		user_io_send_rtc(2);
	}

	if (!running || rd64(SB_MAGIC) != XL_MAGIC) return;

	uint64_t g = rd64(SB_GEN);
	if (g != gen)
	{
		gen = g;
		mac_known = 0;
	}

	uint64_t m = rd64(SB_MAC);
	if (m >> 63)
	{
		uint8_t mac[6];
		for (int i = 0; i < 6; i++) mac[i] = m >> (40 - 8 * i);
		if (!mac_known || memcmp(mac, guest_mac, 6))
		{
			memcpy(guest_mac, mac, 6);
			mac_known = 1;
			if (link_open && (cur_mode == MODE_ETH0 || cur_mode == MODE_ETH1)) ethernet_set_mac_filter(guest_mac);
			if (cur_mode == MODE_MACVLAN) cur_mode = -1;
		}
	}

	int v = (int)user_io_status_get("[10:8]");
	int mode = (v == 0) ? MODE_ETH0 : (v == 1) ? MODE_OFF : v;
	if (mode > MODE_TAP || !a2065_mode_available(mode)) mode = MODE_OFF;

	if (mode != cur_mode)
	{
		cur_mode = mode;
		if (mode == MODE_OFF) close_link();
		else open_link(mode);
	}

	uint64_t txw = rd64(SB_TXW);
	uint64_t txr = rd64(SB_TXR);
	if (txw - txr > TX_RING) txr = txw;
	for (; txr != txw; txr++)
	{
		uint32_t slot = SB_TX + SB_SLOT * (uint32_t)(txr % TX_RING);
		int len = rd64(slot) & 0x7FF;
		if (link_open && len >= 14 && len <= MAX_FRAME)
		{
			memcpy(frame, (const void *)(mb + slot + 8), len);
			ethernet_send(frame, len);
		}
	}
	__sync_synchronize();
	wr64(SB_TXR, txr);

	if (!link_open) return;

	for (int n = 0; n < RX_RING; n++)
	{
		int len = ethernet_recv_nb(frame, MAX_FRAME);
		if (len <= 0) break;

		uint64_t rxw = rd64(SB_RXW);
		if (len < 14 || len > MAX_FRAME - 4 || rxw - rd64(SB_RXR) >= RX_RING) continue;

		if (len < 60)
		{
			memset(frame + len, 0, 60 - len);
			len = 60;
		}
		uint32_t fcs = ~crc32_le(frame, len);
		for (int k = 0; k < 4; k++) frame[len++] = fcs >> (8 * k);
		uint32_t hash = crc32_le(frame, 6) >> 26;

		uint32_t slot = SB_RX + SB_SLOT * (uint32_t)(rxw % RX_RING);
		memcpy((void *)(mb + slot + 8), frame, len);
		__sync_synchronize();
		wr64(slot, len | (hash << 16));
		__sync_synchronize();
		wr64(SB_RXW, rxw + 1);
	}
}
