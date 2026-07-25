#ifndef MINIMIG_A2065_H
#define MINIMIG_A2065_H

// A2065 ZorroII Ethernet card support for the Minimig core.
//
// The FPGA side implements ZorroII autoconfig, the boardram window and the
// LANCE CSR register file; this module is the host half — the AMD Am7990
// LANCE controller state machine, the TX/RX descriptor ring walker and the
// bridge to a host network interface. It runs in Main's single thread, driven
// by a2065_poll() from the poll loop, and is started when the Minimig core
// boots and stopped when it unloads.
//
// FPGA <-> host communication is a DDR3 shared-memory mailbox: register
// writes raise a doorbell in a DDR3 CMD slot that a2065_poll() drains, and CSR
// reads are served from a DDR3 shadow it keeps up to date.

// Interface selection, shown on the Minimig "System" OSD page.
//
//   OFF      card idle, nothing polled
//   eth0     onboard NIC, shared with MiSTer itself (promiscuous + cBPF filter)
//   eth1     dedicated second NIC (carries the Amiga MAC, no filter)
//   macvlan  macvlan child of eth0, so the Amiga gets its own MAC on the LAN
//            without a second NIC and without disturbing MiSTer's own address
//   tap0     tap device on a private subnet, reached by host-side routing or
//            NAT. Needs tun/tap in the kernel (CONFIG_TUN), and covers the two
//            cases the others cannot: local-only access between the Amiga and
//            the MiSTer, and getting out over WiFi — a WiFi station cannot emit
//            frames bearing a second source MAC, so the Amiga cannot hold its
//            own address on the wireless network the way macvlan gives it one
//            on wired eth0.
#define A2065_OFF     0
#define A2065_ETH0    1
#define A2065_ETH1    2
#define A2065_MACVLAN 3
#define A2065_TAP     4
#define A2065_MODES   5

// The live selection lives in core status bits so it travels the same path as
// every other core option. Minimig is excluded from the generic status-word
// config load (user_io.cpp), so a2065_cfg_save()/a2065_cfg_load() persist just
// this field alongside the numbered Minimig config slots.
#define A2065_STATUS_OPT "[54:52]"

int  a2065_get_iface(void);
void a2065_set_iface(int mode);
const char *a2065_iface_msg(int mode);
int  a2065_eth1_present(void);

// Whether this box can actually support a mode: a second NIC for eth1, a tun
// driver for tap0, macvlan support for macvlan. Probed once and cached, so the
// OSD can skip modes that cannot work here. OFF and eth0 are always available.
int  a2065_mode_available(int mode);

// Persistence, called from minimig_cfg_save()/minimig_cfg_load().
void a2065_cfg_save(int num);
void a2065_cfg_load(int num);

// Lifecycle. Both are idempotent: a2065_start() on an already-running card
// just resets the LANCE state, and a2065_stop() on a stopped card is a nop.
void a2065_start(void);
void a2065_stop(void);

// One pass of the card's work — drain the register doorbell and move any
// received frames into the Amiga's ring. Bounded and non-blocking; call it
// from the poll loop. Does nothing when the card is off.
void a2065_poll(void);

#endif
