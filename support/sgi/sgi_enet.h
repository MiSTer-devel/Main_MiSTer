#ifndef SGI_ENET_H
#define SGI_ENET_H

// SGI Indy Ethernet. Sends the machine's address on index 64 at core load,
// before the boot ROMs: boot1.rom if there is one, else 08:00:69 (SGI) and the
// low three bytes of this MiSTer's eth0 (or wlan0). The core seeds it into the
// NVRAM, where the PROM makes `eaddr` of it.
void sgi_enet_start(void);

// The SEEQ and HPC3's Ethernet channels: open the host side the OSD names, move
// frames between it and the descriptor rings in guest RAM.
void sgi_enet_poll(void);
void sgi_enet_stop(void);

#endif
