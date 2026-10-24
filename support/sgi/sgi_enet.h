#ifndef SGI_ENET_H
#define SGI_ENET_H

// Ethernet for the SGI Indy: the SEEQ 80C03 and HPC3's Ethernet DMA run here, the core keeps a register window.
// OSD "Network interface": 0 eth0 (default), 1 None, 2 eth1, 3 macvlan on eth0, 4 tap0.

void sgi_enet_start(void);
void sgi_enet_stop(void);
void sgi_enet_poll(void);

#endif
