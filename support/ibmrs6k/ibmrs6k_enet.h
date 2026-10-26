#ifndef IBMRS6K_ENET_H
#define IBMRS6K_ENET_H

// Ethernet bridge for the IBM RS/6000 43P 7248 core: the PCnet stays in the FPGA, Main is the wire.
// OSD "Network interface" [8:6]: 0 eth0 (default), 1 None, 2 eth1, 3 macvlan on eth0, 4 tap0.

void ibmrs6k_enet_start(void);
void ibmrs6k_enet_stop(void);
void ibmrs6k_enet_poll(void);

#endif
