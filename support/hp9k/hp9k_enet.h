#ifndef HP9K_ENET_H
#define HP9K_ENET_H

// Ethernet bridge for the HP 9000 cores: the NIC stays in the FPGA, Main is the wire.
// OSD "Network": 0 eth0 (default), 1 Off, 2 eth1, 3 macvlan on eth0, 4 tap0.

void hp9k_enet_start(void);
void hp9k_enet_stop(void);
void hp9k_enet_poll(void);

#endif
