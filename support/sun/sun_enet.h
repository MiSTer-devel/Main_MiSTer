#ifndef SUN_ENET_H
#define SUN_ENET_H

// Ethernet bridge for the Sun cores: the NIC stays in the FPGA, Main is the wire.
// OSD "Network": 0 eth0 (default), 1 Off, 2 eth1, 3 macvlan on eth0, 4 tap0.

void sun_enet_start(void);   // also sends the ID PROM
void sun_enet_stop(void);
void sun_enet_poll(void);

#endif
