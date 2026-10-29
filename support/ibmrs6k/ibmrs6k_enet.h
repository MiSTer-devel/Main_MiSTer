// IBM RS/6000 43P 7248 Ethernet: the PCnet in the FPGA, Main is the wire.

#ifndef IBMRS6K_ENET_H
#define IBMRS6K_ENET_H

void ibmrs6k_enet_start(void);
void ibmrs6k_enet_stop(void);
void ibmrs6k_enet_poll(void);

#endif
