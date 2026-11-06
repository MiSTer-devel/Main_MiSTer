#ifndef B3_ETH_H
#define B3_ETH_H

// AT&T UNIX PC (3B1) Ethernet card

char is_3b1();
void b3_eth_start(void);
void b3_eth_stop(void);
void b3_eth_poll(void);

#endif
