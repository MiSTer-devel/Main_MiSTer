#ifndef NEXT_ENET_H
#define NEXT_ENET_H

// Ethernet bridge for the NeXT core.
//
// The core implements the MB8795 ethernet controller and its DMA
// channels in the FPGA (the boot ROM's power-on tests exercise them
// there); this module is the wire behind it.  The FPGA exchanges raw
// frames with this daemon through a DDR3 shared-memory mailbox: a TX
// ring of frames the guest transmitted and an RX ring of frames to
// deliver, plus the guest's station address for the kernel-side
// packet filter.  The host network machinery (AF_PACKET raw sockets,
// the cBPF MAC filter, macvlan and tap setup) is shared with the
// Minimig A2065 module (support/minimig/minimig_a2065_ethernet.cpp);
// only one core runs at a time, so sharing is safe.
//
// Interface selection mirrors the A2065 modes and lives in core
// status bits [54:52] (the "Network" option in the core's OSD):
//   0 Off, 1 eth0 (shared, BPF filtered), 2 eth1 (dedicated),
//   3 macvlan child of eth0, 4 tap0.

#define NEXT_ENET_STATUS_OPT "[54:52]"

void next_enet_start(void);
void next_enet_stop(void);
void next_enet_poll(void);

#endif
