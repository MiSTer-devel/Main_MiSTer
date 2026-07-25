// A2065 ZorroII Ethernet card — host half.
//
// See minimig_a2065.h for the architecture summary. This file owns the
// interface selection persisted from the OSD, the DDR3 mailbox mapping, and
// the per-pass work: draining the register doorbell and moving received frames
// into the Amiga's ring.
//
// Everything here runs in Main's single thread, driven by a2065_poll() from
// the poll loop. Nothing blocks and nothing is unbounded.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <fcntl.h>

#include "../../shmem.h"
#include "../../user_io.h"
#include "../../file_io.h"

#include "minimig_a2065.h"
#include "minimig_a2065_types.h"
#include "minimig_a2065_debug.h"
#include "minimig_a2065_ddr3.h"
#include "minimig_a2065_boardram.h"

extern void registers_reset(void);
extern void registers_set_boardram(volatile uint8_t *ram);
extern void registers_set_fakemac(const uint8_t *mac);
extern void registers_get_fakemac(uint8_t *out);
extern void registers_set_on_interrupt(void (*fn)(void));
extern void registers_set_on_transmit(int (*fn)(void));
extern uint16_t registers_csr0(void);
extern uint16_t registers_csr(int n);
extern uint16_t registers_mode(void);
extern void chip_wput(uint8_t reg_offset, uint16_t v);
extern int  do_transmit(void);
extern void gotfunc(const uint8_t *data, int len);
extern int  rings_rx_has_space(void);
extern void mac_set_addresses(const uint8_t *fake, const uint8_t *real);
extern int  ethernet_open(const char *iface, int promiscuous);
extern void ethernet_close(void);
extern int  ethernet_recv(uint8_t *buf, int maxlen);
extern int  ethernet_recv_nb(uint8_t *buf, int maxlen);
extern void ethernet_get_mac(uint8_t *mac_out);
extern int  ethernet_read_iface_mac(const char *iface, uint8_t *out);
extern int  ethernet_set_mac_filter(const uint8_t *mac);
extern void ethernet_clear_filter(void);
extern void ethernet_packet_stats(unsigned long *packets, unsigned long *drops);
extern void rings_get_rx_stats(unsigned long *deliv, unsigned long *rxoff, unsigned long *ringfull);
extern void mac_get_real(uint8_t *out);
extern int  ethernet_iface_present(const char *iface);
extern int  ethernet_iface_carrier(const char *iface);
extern void ethernet_offload_off(const char *iface);
extern void ethernet_offload_on(const char *iface);
extern int  ethernet_set_iface_mac(const char *iface, const uint8_t *mac);
extern int  ethernet_macvlan_available(const char *parent);
extern int  ethernet_macvlan_create(const char *parent, const char *name, const uint8_t *mac);
extern void ethernet_macvlan_delete(const char *name);
extern void ethernet_dhcpcd_deny(const char *iface);

// Delivery mode.
//   DIRECT  dedicated NIC carries the Amiga MAC, non-promiscuous, no filter
//   BPF     NIC shared with MiSTer: promiscuous plus a cBPF filter so
//           off-target traffic is dropped before the userspace copy
//   MACVLAN macvlan child of the onboard NIC, carrying the Amiga MAC
//   TAP     tap device, host-side routing/bridging
enum { MODE_DIRECT = 0, MODE_BPF = 1, MODE_MACVLAN = 2, MODE_TAP = 3 };

// Name of the macvlan child created for MODE_MACVLAN.
#define MACVLAN_NAME "ve-amiga"

// The selection is kept in core status bits (A2065_STATUS_OPT). Minimig is
// excluded from the generic status-word config load, and the mm_configTYPE
// blob can't grow (its loader does an exact sizeof() check), so the field is
// persisted next to the numbered Minimig config slots instead.
static char *a2065_cfg_name(int num)
{
	static char name[128];
	if (num) sprintf(name, "%s%d.a2065", user_io_get_core_name(), num);
	else     sprintf(name, "%s.a2065",   user_io_get_core_name());
	return name;
}

static volatile uint8_t *map = NULL;
static int card_up = 0;

static const char *iface_name = "eth0";
static const char *parent_name = "eth0";
static int mode = 0;
static int created_macvlan = 0;
static int prom_active = 0;     // BPF filter detached for Amiga-side MODE_PROM

// Set while a batch of received frames is being written into the ring, to
// suppress the per-frame CSR-shadow push and interrupt; one INT2 is raised for
// the whole batch instead.
static int rx_batching = 0;

int a2065_eth1_present(void)
{
	return access("/sys/class/net/eth1", F_OK) == 0;
}

// Probe results, resolved once and cached. The macvlan probe shells out to
// `ip link add`, far too costly to repeat on every OSD redraw.
static int tap_avail = -1;
static int macvlan_avail = -1;

// Is this mode usable on this box? tun/tap and macvlan may be built in (=y),
// built as modules (=m) or absent entirely, so both are probed for the
// capability itself rather than for a loaded module — on a kernel with
// CONFIG_TUN=y there is no module to find, yet tap works fine.
int a2065_mode_available(int mode)
{
	switch (mode)
	{
	case A2065_ETH1:
		return a2065_eth1_present();

	case A2065_TAP:
		if (tap_avail < 0)
		{
			// Opening the node proves the driver is there; it creates no
			// interface until TUNSETIFF, so this is free of side effects.
			int fd = open("/dev/net/tun", O_RDWR);
			tap_avail = (fd >= 0);
			if (fd >= 0) close(fd);
		}
		return tap_avail;

	case A2065_MACVLAN:
		if (macvlan_avail < 0) macvlan_avail = ethernet_macvlan_available("eth0");
		return macvlan_avail;

	default:
		// OFF always, and eth0 is the DE10-Nano's onboard NIC — always there.
		return 1;
	}
}

const char *a2065_iface_msg(int mode)
{
	switch (mode)
	{
	case A2065_ETH0:    return "eth0";
	case A2065_ETH1:    return "eth1";
	case A2065_MACVLAN: return "macvlan";
	case A2065_TAP:     return "tap0";
	default:            return "OFF";
	}
}

int a2065_get_iface(void)
{
	int mode = (int)user_io_status_get(A2065_STATUS_OPT);
	if (mode >= A2065_MODES) mode = A2065_OFF;

	// A selection that this box cannot support (second NIC unplugged, no tun
	// driver, no macvlan) falls back to off — never to eth0, since moving the
	// Amiga onto the NIC carrying MiSTer's own address should always be a
	// deliberate choice.
	if (!a2065_mode_available(mode)) mode = A2065_OFF;

	return mode;
}

void a2065_cfg_save(int num)
{
	uint8_t mode = (uint8_t)a2065_get_iface();
	FileSaveConfig(a2065_cfg_name(num), &mode, sizeof(mode));
}

void a2065_cfg_load(int num)
{
	// No stored selection for this slot means the card has never been enabled
	// here, so leave it off. Opting in is explicit: the card only comes up
	// once a mode has been chosen in the OSD and the config slot saved.
	uint8_t mode = A2065_OFF;

	uint8_t saved;
	if (FileLoadConfig(a2065_cfg_name(num), &saved, sizeof(saved)) && saved < A2065_MODES)
		mode = saved;

	// A stored mode this box can no longer support falls back to off rather
	// than quietly moving the Amiga onto MiSTer's own NIC.
	if (!a2065_mode_available(mode)) mode = A2065_OFF;

	user_io_status_set(A2065_STATUS_OPT, mode);
}

static uint64_t rd64(unsigned long off)
{
	uint64_t val;
	memcpy(&val, (void *)(map + off), 8);
	return val;
}

static void wr64(unsigned long off, uint64_t val)
{
	memcpy((void *)(map + off), &val, 8);
}

static void push_csr_shadow(void)
{
	uint16_t c0 = registers_csr0();
	uint16_t c1 = registers_csr(1);
	uint16_t c2 = registers_csr(2);
	uint16_t c3 = registers_csr(3);

	uint64_t packed = (uint64_t)c0
	                | ((uint64_t)c1 << 16)
	                | ((uint64_t)c2 << 32)
	                | ((uint64_t)c3 << 48);
	wr64(DDR3_CSR_OFF, packed);
}

static void update_int_state(void)
{
	uint16_t csr0 = registers_csr0();

	wr64(DDR3_INT_OFF, ((csr0 & CSR0_INTR) && (csr0 & CSR0_INEA)) ? 1 : 0);
}

static void on_interrupt_cb(void)
{
	if (rx_batching) return;   // defer until the whole RX batch is delivered
	push_csr_shadow();
	update_int_state();
}

static int on_transmit_cb(void)
{
	int r = do_transmit();
	push_csr_shadow();
	update_int_state();
	return r;
}

// Move whatever the socket already has into the Amiga's receive ring.
//
// Non-blocking, and bounded: this runs from Main's poll loop, which is
// single-threaded and also services the core, the UI and input, so it must
// hand control back promptly rather than sit on the socket. Anything left
// over stays in the kernel's buffer and is picked up next time round.
//
// Frames are taken in a batch with the per-frame interrupt suppressed, and
// one INT2 raised at the end: a server's TSO pair then lands together and the
// Amiga acknowledges it immediately instead of arming its delayed-ACK timer.
#define RX_BATCH_MAX 64

static void a2065_drain_rx(void)
{
	static uint8_t rxbuf[MAX_PACKET_SIZE];
	int taken = 0;

	// Backpressure: with no free descriptor, leave the frames in the kernel
	// buffer rather than overrunning the ring. Overruns used to clear RXON via
	// RX_OFLO and cascade into a stall; letting the buffer hold the burst is
	// the same zero-window flow control that made eth1 work.
	if (!rings_rx_has_space()) return;

	// Internal loopback disconnects the LANCE from the wire on real hardware.
	// gotfunc skips its destination filter in that mode, so live segment
	// traffic would otherwise flood the ring and turn RXON off, failing the
	// internal-loopback diagnostic.
	int loop = (registers_mode() & MODE_LOOP) != 0;

	rx_batching = 1;
	while (taken < RX_BATCH_MAX && (loop || rings_rx_has_space()))
	{
		int len = ethernet_recv_nb(rxbuf, sizeof rxbuf);
		if (len <= 0) break;          // socket empty (0) or error (-1)
		if (!loop) gotfunc(rxbuf, len);
		taken++;
	}
	rx_batching = 0;

	if (taken)
	{
		push_csr_shadow();
		update_int_state();
	}
}

static void write_mbx_mac(const uint8_t *fakemac)
{
	uint64_t val = 1;
	val |= ((uint64_t)fakemac[2]) << 32;
	val |= ((uint64_t)fakemac[3]) << 40;
	val |= ((uint64_t)fakemac[4]) << 48;
	val |= ((uint64_t)fakemac[5]) << 56;
	wr64(DDR3_MAC_OFF, val);
}

static int mac_low_is_zero(const uint8_t *m) { return (m[3] | m[4] | m[5]) == 0; }

// Last-resort unique low half when no NIC MAC is available. Seeds from the
// hostname plus pid/time so two boards never collide and never emit an
// all-zero MAC.
static void derive_unique_low(uint8_t *m)
{
	char host[64] = {};
	gethostname(host, sizeof host - 1);
	uint32_t h = 2166136261u;                       // FNV-1a
	for (const char *p = host; *p; ++p) h = (h ^ (uint8_t)*p) * 16777619u;
	h ^= (uint32_t)getpid();
	h ^= (uint32_t)time(NULL);
	m[3] = (h >> 16) & 0xff;
	m[4] = (h >> 8)  & 0xff;
	m[5] =  h        & 0xff;
	if (mac_low_is_zero(m)) m[5] = 0x01;            // never all-zero
}

// Resolve the wire-side MAC low bytes robustly so two cards on one LAN never
// collide. Order: selected iface MAC -> onboard eth0 (unique per board,
// stable) -> hostname/pid/time hash. The on-wire source is realmac (the
// fakemac->realmac munge in mungepacket), so a unique non-zero low half here
// is what stops the Amiga emitting 00:80:10:00:00:00. The OUI is always
// forced to Commodore 00:80:10.
static void a2065_set_default_mac(const char *iface)
{
	uint8_t realmac[6] = {}, fakemac[6];
	const char *src = iface;

	ethernet_get_mac(realmac);

	if (mac_low_is_zero(realmac))
	{
		// Selected iface had no usable MAC (e.g. USB NIC late or down). Try
		// the onboard eth0 — present and unique per DE10-Nano even when eth1
		// isn't.
		uint8_t fb[6];
		if (ethernet_read_iface_mac("eth0", fb))
		{
			memcpy(realmac, fb, 6);
			src = "eth0";
		}
		else
		{
			derive_unique_low(realmac);
			src = "hostname-hash";
		}
	}

	fakemac[0] = COMMODORE_OUI0; fakemac[1] = COMMODORE_OUI1; fakemac[2] = COMMODORE_OUI2;
	fakemac[3] = realmac[3]; fakemac[4] = realmac[4]; fakemac[5] = realmac[5];

	realmac[0] = COMMODORE_OUI0; realmac[1] = COMMODORE_OUI1; realmac[2] = COMMODORE_OUI2;

	mac_set_addresses(fakemac, realmac);
	registers_set_fakemac(fakemac);

	printf("A2065: MAC low bytes from %s -> wire %02X:%02X:%02X:%02X:%02X:%02X\n",
		src, realmac[0], realmac[1], realmac[2], realmac[3], realmac[4], realmac[5]);
}

// Build the Amiga's wire MAC from a NIC's low three bytes, under the
// Commodore OUI, so two boards on one LAN never collide.
static void amiga_mac_from(const uint8_t *src, uint8_t *out)
{
	out[0] = COMMODORE_OUI0; out[1] = COMMODORE_OUI1; out[2] = COMMODORE_OUI2;
	out[3] = src[3]; out[4] = src[4]; out[5] = src[5];
}

// Put the chosen interface into a known-good state for its delivery mode.
// Offload is forced either way rather than trusting whatever it was left at:
// on a dedicated NIC (direct) offload OFF measurably raises doorbell
// round-trip time, while on a shared NIC (bpf/macvlan) offload ON produces GRO
// super-frames far larger than the Amiga LANCE can accept.
//
// Returns 1 if a macvlan child was created here and must be torn down on stop.
static int a2065_setup_iface(const char *iface, const char *parent, int mode)
{
	if (mode == MODE_TAP) return 0;        // tap is created by ethernet_open()

	if (mode == MODE_DIRECT)
	{
		ethernet_offload_on(parent);

		// Reassign the dedicated NIC's MAC to the Amiga wire MAC so a
		// non-promiscuous socket receives the Amiga's frames.
		uint8_t orig[6], amac[6];
		if (ethernet_read_iface_mac(iface, orig))
		{
			amiga_mac_from(orig, amac);
			ethernet_set_iface_mac(iface, amac);
		}
		return 0;
	}

	ethernet_offload_off(parent);

	if (mode == MODE_MACVLAN)
	{
		// Give the Amiga its own address on the LAN without a second NIC, and
		// without disturbing the parent's. Deny dhcpcd first so it can't lease
		// the Amiga's MAC out from under us.
		uint8_t pmac[6], amac[6];
		if (!ethernet_read_iface_mac(parent, pmac)) return 0;
		amiga_mac_from(pmac, amac);
		ethernet_dhcpcd_deny(iface);
		if (ethernet_macvlan_create(parent, iface, amac)) return 1;
		printf("A2065: macvlan create failed for %s\n", iface);
	}

	return 0;
}

// Bring the card up: map the mailbox, configure the interface, open the socket.
// Runs to completion in the caller's context — there is no thread here. Main is
// single-threaded by design, and a background thread would compete with it for
// the one CPU it has, adding input lag or making it miss core requests.
static int a2065_open(void)
{
	int sel = a2065_get_iface();

	switch (sel)
	{
	case A2065_ETH1:    iface_name = "eth1";       parent_name = "eth1"; mode = MODE_DIRECT;  break;
	case A2065_MACVLAN: iface_name = MACVLAN_NAME; parent_name = "eth0"; mode = MODE_MACVLAN; break;
	case A2065_TAP:     iface_name = "tap0";       parent_name = "tap0"; mode = MODE_TAP;     break;
	default:            iface_name = "eth0";       parent_name = "eth0"; mode = MODE_BPF;     break;
	}

	map = (volatile uint8_t *)shmem_map(DDR3_FLAT_BASE, DDR3_FLAT_WINDOW_SIZE);
	if (!map)
	{
		printf("A2065: cannot map DDR3 mailbox\n");
		return 0;
	}

	boardram = map + DDR3_BRAM_OFF;

	wr64(DDR3_CMD_OFF, 0);
	wr64(DDR3_CSR_OFF, 0);
	wr64(DDR3_INT_OFF, 0);

	registers_reset();
	registers_set_boardram(boardram);
	registers_set_on_interrupt(on_interrupt_cb);
	registers_set_on_transmit(on_transmit_cb);

	// Interface setup happens before the socket is opened so the NIC already
	// carries the Amiga's MAC when a2065_set_default_mac() reads it back.
	created_macvlan = a2065_setup_iface(iface_name, parent_name, mode);

	int promiscuous = (mode == MODE_BPF);   // others own the Amiga MAC outright
	if (!ethernet_open(iface_name, promiscuous))
		printf("A2065: failed to open %s, continuing without network\n", iface_name);

	a2065_set_default_mac(iface_name);

	if (mode == MODE_BPF)
	{
		uint8_t realmac[6];
		mac_get_real(realmac);
		ethernet_set_mac_filter(realmac);
		prom_active = 0;
	}

	{
		uint8_t fakemac[6];
		registers_get_fakemac(fakemac);
		write_mbx_mac(fakemac);
	}

	push_csr_shadow();
	update_int_state();

	printf("A2065: running on %s (%s)\n", iface_name, a2065_iface_msg(sel));
	return 1;
}

// One pass of the card's work, called from Main's poll loop.
//
// Two jobs: drain any register write the Amiga has posted through the doorbell,
// and move received frames into its ring. Both are bounded and non-blocking, so
// the cost per call stays small and predictable.
//
// A register write is DTACK-stretched by the FPGA until the doorbell is
// drained, so the Amiga is held off for as long as it takes to get back here.
// That is the reason this must not sit behind anything slow.
void a2065_poll(void)
{
	if (!card_up) return;

	uint64_t cmd = rd64(DDR3_CMD_OFF);
	if (cmd & DDR3_CMD_PENDING_BIT)
	{
		uint8_t  rap_v = (cmd >> DDR3_CMD_RAP_SHIFT) & DDR3_CMD_RAP_MASK;
		uint16_t data  = (cmd >> DDR3_CMD_DATA_SHIFT) & DDR3_CMD_DATA_MASK;

		chip_wput(A2065_RAP_OFF, rap_v);
		chip_wput(A2065_RDP_OFF, data);

		wr64(DDR3_CMD_OFF, 0);
		__sync_synchronize();

		push_csr_shadow();
		update_int_state();

		// When the Amiga puts the LANCE into promiscuous mode (a sniffer), drop
		// the kernel filter so it sees every frame, and re-attach on clear.
		// SO_ATTACH_FILTER replaces atomically. Only meaningful in BPF mode —
		// the others have no filter.
		if (mode == MODE_BPF)
		{
			int want_prom = (registers_mode() & MODE_PROM) != 0;
			if (want_prom != prom_active)
			{
				if (want_prom)
				{
					ethernet_clear_filter();
				}
				else
				{
					uint8_t realmac[6];
					mac_get_real(realmac);
					ethernet_set_mac_filter(realmac);
				}
				prom_active = want_prom;
			}
		}
	}

	a2065_drain_rx();
}

void a2065_stop(void)
{
	if (!card_up) return;

	card_up = 0;
	wr64(DDR3_INT_OFF, 0);
	ethernet_close();

	// Tear down only what we created. Offload settings and a direct-mode MAC
	// reassignment are deliberately left in place.
	if (created_macvlan) ethernet_macvlan_delete(iface_name);
	created_macvlan = 0;

	if (map)
	{
		shmem_unmap((void *)map, DDR3_FLAT_WINDOW_SIZE);
		map = NULL;
	}

	printf("A2065: stopped\n");
}

void a2065_start(void)
{
	// Already up: this is a Minimig reset, so put the LANCE back to its
	// power-on state rather than tearing the interface down and back up.
	if (card_up)
	{
		registers_reset();
		push_csr_shadow();
		update_int_state();
		return;
	}

	if (a2065_get_iface() == A2065_OFF) return;

	if (a2065_open()) card_up = 1;
	else a2065_stop();
}

void a2065_set_iface(int mode_sel)
{
	if (mode_sel < A2065_OFF || mode_sel >= A2065_MODES) mode_sel = A2065_OFF;
	// Clamp an unsupported mode (the OSD skips these, so this is a safety net
	// for a stale saved value or a scripted status write).
	if (!a2065_mode_available(mode_sel)) mode_sel = A2065_OFF;

	a2065_stop();
	user_io_status_set(A2065_STATUS_OPT, mode_sel);
	a2065_start();
}
