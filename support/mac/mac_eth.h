#ifndef MAC_ETH_H
#define MAC_ETH_H

#include <stdint.h>

#define ETH_DDR_BASE   0x1FF00000UL
#define ETH_WIN_SIZE   0x21000UL

#define ETH_LC_OFF_XFER 0x00000UL
#define ETH_LC_OFF_ROM  0x10000UL
#define ETH_LC_CTRL     0x20000UL
#define ETH_LC_WIN_SIZE 0x21000UL
#define ETH_MAGIC_LC    0x4D634C4345544832ULL

#define ETH_CTL_MAGIC   0x000UL
#define ETH_CTL_WPTR    0x008UL
#define ETH_CTL_SHAD    0x010UL
#define ETH_CTL_INT     0x090UL
#define ETH_CTL_MACPROM 0x098UL
#define ETH_CTL_GEO     0x0A0UL
#define ETH_CTL_RPTR    0x0A8UL
#define ETH_CTL_DMACMD  0x0B0UL
#define ETH_CTL_DMASTAT 0x0B8UL
#define ETH_CTL_RING    0x800UL
#define ETH_RING_ENTRIES 256

#define ETH_Q8_OFF_XFER 0x0000UL
#define ETH_Q8_XFER_SIZE 0x4000UL
#define ETH_Q8_CTRL     0x4000UL
#define ETH_Q8_WIN_SIZE 0x5000UL
#define ETH_MAGIC_Q8    0x4D63513845544834ULL
#define ETH_Q8_ISRSET   0x090UL
#define ETH_Q8_ISRACK   0x098UL
#define ETH_Q8_MACPROM  0x0A0UL
#define ETH_Q8_GEO      0x0A8UL
#define ETH_Q8_PTRS     0x0B0UL
#define ETH_Q8_DMACMD   0x0B8UL
#define ETH_Q8_DMASTAT  0x0C0UL
#define ETH_Q8_OPS      0x100UL
#define ETH_Q8_MAX_OPS  8
#define ETH_Q8_RAM_TOP  0x08000000UL

#define ETH_TAG_REG_WR 0
#define ETH_TAG_RESET  1

void mac_eth_poll(void);

int  mac_eth_iface_open(const char *name);
void mac_eth_iface_close(void);
int  mac_eth_iface_send(const uint8_t *frame, int len);
int  mac_eth_iface_recv(uint8_t *buf, int maxlen);
int  mac_eth_iface_fd(void);
int  mac_eth_iface_hwaddr(const char *name, uint8_t mac[6]);

#endif
