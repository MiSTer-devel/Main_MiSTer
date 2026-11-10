// Apple IIgs core: Uthernet II (W5100) Ethernet card, Main side.
//
// The FPGA half is rtl/uthernet2.sv in the Apple-IIgs core; the chip model
// is iigs_uthernet2_model.cpp. This file maps the shared DDR3 window, follows
// the OSD settings and bridges socket 0's MAC raw frames to a host interface
// through the A2065 network layer (raw socket + BPF filter, macvlan or tap).
//
// OSD (System page): status [28:27] Uthernet II Off / Slot 3 / Slot 4,
//                    status [30:29] Network eth0 / macvlan / tap0 / eth1.
//
//   eth0     onboard NIC shared with MiSTer, promiscuous with a filter on the
//            IIgs MAC. The IIgs is a separate host on the LAN (wired only).
//   macvlan  a macvlan child of eth0 carrying the IIgs MAC (wired only).
//   tap0     a tap device; the MiSTer side must route or bridge it. This is
//            the only mode in which the IIgs and the MiSTer can talk.
//   eth1     a second (USB) NIC.

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>

#include "../../user_io.h"
#include "../../shmem.h"
#include "iigs_uthernet2.h"
#include "iigs_uthernet2_model.h"

// host network layer, from the A2065 module
extern int  ethernet_open(const char *iface, int promiscuous);
extern void ethernet_close(void);
extern void ethernet_send(const uint8_t *frame, int len);
extern int  ethernet_recv_nb(uint8_t *buf, int maxlen);
extern int  ethernet_set_mac_filter(const uint8_t *mac);
extern int  ethernet_macvlan_create(const char *parent, const char *name, const uint8_t *mac);
extern void ethernet_macvlan_delete(const char *name);
extern void ethernet_dhcpcd_deny(const char *iface);
extern void ethernet_offload_off(const char *iface);
extern void ethernet_offload_on(const char *iface);
extern int  a2065_mode_available(int mode);

#define U2_DDR_BASE   0x1FF00000UL   // shared with the other cores' Ethernet bridges

#define OPT_SLOT      "[28:27]"
#define OPT_NET       "[30:29]"

// OSD order of the Network option
enum { NET_ETH0 = 0, NET_MACVLAN = 1, NET_TAP = 2, NET_ETH1 = 3 };
// a2065_mode_available() numbering
enum { A2065_ETH0 = 1, A2065_ETH1 = 2, A2065_MACVLAN = 3, A2065_TAP = 4 };

#define MACVLAN_NAME  "iigs0"

static volatile uint8_t *win;
static int link_open;
static int link_mode = -1;
static uint8_t link_mac[6];
static int made_macvlan;
static const char *offload_iface;
static int card_up;

static int net_send(void *, const uint8_t *frame, int len)
{
	if (!link_open) return -1;
	ethernet_send(frame, len);
	return len;
}

static int net_recv(void *, uint8_t *buf, int maxlen)
{
	return link_open ? ethernet_recv_nb(buf, maxlen) : 0;
}

static const u2_net net = { 0, net_send, net_recv };

static void close_link(void)
{
	if (link_open)
	{
		ethernet_close();
		link_open = 0;
		printf("IIgs Uthernet II: network closed\n");
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
	link_mode = -1;
}

static int a2065_mode(int mode)
{
	switch (mode)
	{
	case NET_MACVLAN: return A2065_MACVLAN;
	case NET_TAP:     return A2065_TAP;
	case NET_ETH1:    return A2065_ETH1;
	default:          return A2065_ETH0;
	}
}

static void open_link(int mode, const uint8_t *mac)
{
	close_link();
	if (!a2065_mode_available(a2065_mode(mode)))
	{
		printf("IIgs Uthernet II: network mode %d not available on this MiSTer\n", mode);
		link_mode = mode;   // do not retry every pass
		memcpy(link_mac, mac, 6);
		return;
	}

	const char *iface = "eth0";
	int promisc = 0;
	switch (mode)
	{
	case NET_ETH1:
		iface = "eth1";
		promisc = 1;
		break;
	case NET_MACVLAN:
		if (!ethernet_macvlan_create("eth0", MACVLAN_NAME, mac)) return;
		made_macvlan = 1;
		ethernet_dhcpcd_deny(MACVLAN_NAME);   // the IIgs runs its own DHCP on this MAC
		iface = MACVLAN_NAME;
		break;
	case NET_TAP:
		iface = "tap0";
		break;
	default:
		iface = "eth0";
		promisc = 1;
		break;
	}

	// GRO would hand the IIgs frames larger than a W5100 takes
	if (mode != NET_TAP)
	{
		offload_iface = (mode == NET_ETH1) ? "eth1" : "eth0";
		ethernet_offload_off(offload_iface);
	}

	link_mode = mode;
	memcpy(link_mac, mac, 6);
	if (!ethernet_open(iface, promisc)) return;
	link_open = 1;

	uint8_t scratch[2048];
	while (ethernet_recv_nb(scratch, sizeof scratch) > 0) {}
	if (promisc) ethernet_set_mac_filter(mac);
	printf("IIgs Uthernet II: network up on %s, IIgs MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
	       iface, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void iigs_uthernet2_poll(void)
{
	const char *core = user_io_get_core_name();
	int slot = (core && !strcasecmp(core, "Apple-IIgs")) ? (int)(user_io_status_get(OPT_SLOT) & 3) : 0;
	if (!slot)
	{
		if (card_up)
		{
			close_link();
			card_up = 0;
		}
		return;
	}

	if (!win)
	{
		win = (volatile uint8_t *)shmem_map(U2_DDR_BASE, U2_WIN_SIZE);
		if (!win) return;
		u2m_attach(win);
	}

	if (!u2m_poll(&net)) return;
	if (!card_up)
	{
		card_up = 1;
		printf("IIgs Uthernet II: card up in slot %d\n", slot == 1 ? 3 : 4);
	}

	uint8_t mac[6];
	if (u2m_macraw(mac))
	{
		int mode = (int)(user_io_status_get(OPT_NET) & 3);
		if (mode != link_mode || memcmp(mac, link_mac, 6)) open_link(mode, mac);
	}
	else if (link_open)
	{
		// socket closed: drop what arrives so it is not stale on the next open
		uint8_t scratch[2048];
		for (int i = 0; i < 64 && ethernet_recv_nb(scratch, sizeof scratch) > 0; i++) {}
	}
}
