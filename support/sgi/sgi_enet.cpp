// SGI Indy Ethernet: the SEEQ 80C03 and HPC3's Ethernet DMA, on the core's register window (UIO 0x70).

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../../user_io.h"
#include "../../file_io.h"
#include "../../spi.h"
#include "../../shmem.h"
#include "sgi.h"
#include "sgi_enet.h"

extern int  ethernet_read_iface_mac(const char *iface, uint8_t *out);
extern int  ethernet_open(const char *iface, int promiscuous);
extern void ethernet_close(void);
extern void ethernet_send(const uint8_t *frame, int len);
extern int  ethernet_recv_nb(uint8_t *buf, int maxlen);
extern int  ethernet_set_mac_filter_mcast(const uint8_t *mac);
extern int  ethernet_macvlan_create(const char *parent, const char *name, const uint8_t *mac);
extern void ethernet_macvlan_delete(const char *name);
extern int  a2065_mode_available(int mode);
extern void ethernet_offload_off(const char *iface);
extern void ethernet_offload_on(const char *iface);

#define MAC_INDEX     64

#define UIO_SGI_ENET  0x70
#define OP_READ       1
#define OP_WRITE      2
#define OP_INVAL      3

#define IX_TX_NBDP    0x17
#define IX_CRBDP      0x18
#define IX_CPFXBDP    0x1A
#define IX_SEEQ       0x40
#define IX_STATE      0x100
#define IX_RX_STAT    0x101
#define IX_TX_STAT    0x102
#define IX_STOP       0x103
#define IX_MISC       0x104
#define IX_LIVE       0x105

#define ST_RX_ACTIVE  (1u << 16)
#define ST_TX_ACTIVE  (1u << 17)
#define ST_LOOPBACK   (1u << 19)
#define ST_RESET      (1u << 20)
#define ST_PRESENT(s) (((s) >> 24) == 0xEC)

#define D_EOX         0x80000000u
#define D_EOXP        0x40000000u
#define D_XIE         0x20000000u
#define D_TXD         0x00008000u
#define D_ROWN        0x00004000u
#define D_COUNT       0x00003FFFu

#define RX_GOOD_END   0x30
#define TX_SUCCESS    0x08

#define RAM_PHYS      0x08000000u
#define RAM_ARM       0x38000000u
#define RAM_LEN       0x08000000u

#define MODE_OFF      0
#define MODE_ETH0     1
#define MODE_ETH1     2
#define MODE_MACVLAN  3
#define MODE_TAP      4
#define MACVLAN_NAME  "sgi0"
#define NET_STATUS    "[26:24]"

#define MAX_FRAME     1536
#define RXQ_LEN       32

static volatile uint8_t *ram = 0;
static int      running = 0;
static int      link_open = 0;
static int      cur_mode = -1;
static int      made_macvlan = 0;
static const char *offload_iface = 0;
static uint8_t  mac[6];
static uint8_t  rxq[RXQ_LEN][MAX_FRAME];
static int      rxq_len[RXQ_LEN];
static int      rxq_head = 0, rxq_n = 0;
static int      seeq_cnt = -1;
static uint32_t rx_cmd;
static uint8_t  sta[6];

static void win_read(uint16_t idx, int n, uint32_t *out)
{
	spi_uio_cmd_cont(UIO_SGI_ENET);
	spi_w(OP_READ);
	spi_w(idx);
	spi_w(n);
	for (int i = 0; i < n; i++)
	{
		uint32_t lo = spi_w(0);
		uint32_t hi = spi_w(0);
		out[i] = lo | (hi << 16);
	}
	DisableIO();
}

static void win_write(uint16_t idx, uint16_t seq, uint32_t v)
{
	spi_uio_cmd_cont(UIO_SGI_ENET);
	spi_w(OP_WRITE);
	spi_w(idx);
	spi_w(seq);
	spi_w(v);
	spi_w(v >> 16);
	DisableIO();
}

static void win_misc(int irq)
{
	win_write(IX_MISC, 0, (irq ? 1 : 0) | (link_open ? 0 : 2));
}

static void win_inval(void)
{
	__sync_synchronize();
	spi_uio_cmd_cont(UIO_SGI_ENET);
	spi_w(OP_INVAL);
	DisableIO();
}

static inline int in_ram(uint32_t pa, uint32_t n) { return pa >= RAM_PHYS && pa - RAM_PHYS <= RAM_LEN - n; }
static inline uint32_t phys(uint32_t a) { return a & 0x1FFFFFFF; }
static inline uint32_t g_rd32(uint32_t pa) { return *(volatile uint32_t *)(ram + ((pa - RAM_PHYS) ^ 4)); }
static inline void g_wr16(uint32_t pa, uint16_t v) { *(volatile uint16_t *)(ram + ((pa - RAM_PHYS) ^ 6)) = v; }
static inline uint8_t g_rd8(uint32_t pa) { return ram[(pa - RAM_PHYS) ^ 7]; }
static inline void g_wr8(uint32_t pa, uint8_t v) { ram[(pa - RAM_PHYS) ^ 7] = v; }

static void g_copy_in(uint32_t pa, const uint8_t *src, int n)
{
	while (n && (pa & 7)) { g_wr8(pa++, *src++); n--; }
	for (; n >= 8; n -= 8, pa += 8, src += 8)
	{
		uint64_t v;
		memcpy(&v, src, 8);
		*(volatile uint64_t *)(ram + (pa - RAM_PHYS)) = __builtin_bswap64(v);
	}
	while (n--) g_wr8(pa++, *src++);
}

static void g_copy_out(uint8_t *dst, uint32_t pa, int n)
{
	while (n && (pa & 7)) { *dst++ = g_rd8(pa++); n--; }
	for (; n >= 8; n -= 8, pa += 8, dst += 8)
	{
		uint64_t v = __builtin_bswap64(*(volatile uint64_t *)(ram + (pa - RAM_PHYS)));
		memcpy(dst, &v, 8);
	}
	while (n--) *dst++ = g_rd8(pa++);
}

static void rxq_push(const uint8_t *frame, int len)
{
	if (rxq_n == RXQ_LEN) return;
	int t = (rxq_head + rxq_n++) % RXQ_LEN;
	memcpy(rxq[t], frame, len);
	rxq_len[t] = len;
}

static void send_mac(void)
{
	static const uint8_t def[6] = { 0x08, 0x00, 0x69, 0x12, 0x34, 0x56 };
	uint8_t hw[6];

	memcpy(mac, def, 6);
	char *path = user_io_make_filepath(HomeDir(), "boot1.rom");
	if (FileLoad(path, 0, 0) >= 6) FileLoad(path, mac, sizeof(mac));
	else if (ethernet_read_iface_mac("eth0", hw) || ethernet_read_iface_mac("wlan0", hw)) memcpy(mac + 3, hw + 3, 3);
	else return;

	user_io_set_index(MAC_INDEX);
	user_io_set_download(1);
	user_io_file_tx_data(mac, sizeof(mac));
	user_io_set_download(0);
	printf("[sgi-enet] MAC %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void close_link(void)
{
	if (link_open)
	{
		ethernet_close();
		link_open = 0;
	}
	if (made_macvlan)
	{
		ethernet_macvlan_delete(MACVLAN_NAME);
		made_macvlan = 0;
	}
	if (offload_iface)
	{
		ethernet_offload_on(offload_iface);
		offload_iface = 0;
	}
	rxq_n = 0;
}

static void open_link(int mode)
{
	const char *iface;
	int promisc = 0;

	close_link();
	switch (mode)
	{
	case MODE_ETH0:
		iface = "eth0";
		promisc = 1;
		break;
	case MODE_ETH1:
		iface = "eth1";
		promisc = 1;
		break;
	case MODE_MACVLAN:
		if (!ethernet_macvlan_create("eth0", MACVLAN_NAME, mac)) return;
		made_macvlan = 1;
		iface = MACVLAN_NAME;
		break;
	case MODE_TAP:
		iface = "tap0";
		break;
	default:
		return;
	}

	if (mode != MODE_TAP)
	{
		offload_iface = (mode == MODE_ETH1) ? "eth1" : "eth0";
		ethernet_offload_off(offload_iface);
	}

	if (!ethernet_open(iface, promisc)) return;
	link_open = 1;

	uint8_t scratch[MAX_FRAME];
	while (ethernet_recv_nb(scratch, MAX_FRAME) > 0) ;
	if (mode == MODE_ETH0 || mode == MODE_ETH1) ethernet_set_mac_filter_mcast(mac);
	printf("[sgi-enet] link up on %s\n", iface);
}

static int mode_from_status(void)
{
	int v = (int)user_io_status_get(NET_STATUS);
	int mode = (v == 0) ? MODE_ETH0 : (v == 1) ? MODE_OFF : v;
	if (mode < 0 || mode > MODE_TAP) mode = MODE_OFF;
	if (!a2065_mode_available(mode)) mode = MODE_OFF;
	return mode;
}

static int rx_match(const uint8_t *f)
{
	static const uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	int mode = rx_cmd & 0xC0;
	if (!mode) return 0;
	if (mode == 0x40 || (mode == 0xC0 && (f[0] & 1))) return 1;
	return !memcmp(f, bcast, 6) || !memcmp(f, sta, 6);
}

static void tx_run(uint16_t seq, int loopback)
{
	static uint8_t frame[MAX_FRAME];
	uint32_t p[2];
	win_read(IX_CPFXBDP, 2, p);
	uint32_t cpfx = p[0], ppfx = p[1];
	uint32_t d = cpfx, next = 0;
	int xie = 0, packets = 0, eox = 0;

	for (int guard = 0; guard < 256 && !eox; guard++)
	{
		uint32_t first = d;
		int len = 0, sent = 0;

		for (int k = 0; k < 64; k++)
		{
			uint32_t pd = phys(d);
			if (!in_ram(pd, 16))
			{
				win_write(IX_STOP, seq, 2);
				return;
			}
			uint32_t w1 = g_rd32(pd + 4);
			__sync_synchronize();
			uint32_t buf = phys(g_rd32(pd));
			next = g_rd32(pd + 8);
			int n = w1 & D_COUNT;
			if (d == first && (w1 & D_TXD)) sent = 1;
			if (w1 & D_XIE) xie = 1;
			if (n && len + n <= MAX_FRAME && in_ram(buf, n))
			{
				g_copy_out(frame + len, buf, n);
				len += n;
			}
			eox = (w1 & D_EOX) != 0;
			if (eox || (n && (w1 & D_EOXP))) break;
			d = next;
		}

		if (len)
		{
			if (!sent && len >= 14)
			{
				if (loopback) rxq_push(frame, len);
				else if (link_open) ethernet_send(frame, len);
			}
			g_wr16(phys(first) + 6, D_TXD);
			packets++;
		}
		if (eox) break;

		d = next;
		ppfx = cpfx;
		cpfx = d;
	}

	if (!packets && !eox) return;
	win_inval();
	win_write(IX_CPFXBDP, seq, cpfx);
	win_write(IX_CPFXBDP + 1, seq, ppfx);
	if (eox)
	{
		win_write(IX_TX_NBDP, seq, next);
		win_write(IX_STOP, seq, 2);
	}
	if (packets) win_write(IX_TX_STAT, 0, TX_SUCCESS);
	if (xie) win_misc(1);
}

static void rx_run(uint16_t seq)
{
	static uint8_t img[MAX_FRAME + 3];
	uint32_t wb_addr[RXQ_LEN];
	uint16_t wb_val[RXQ_LEN];
	int nwb = 0, xie = 0, stop = 0;
	uint32_t d, st;

	win_read(IX_CRBDP, 1, &d);
	while (rxq_n)
	{
		uint32_t pd = phys(d);
		if (!in_ram(pd, 16))
		{
			stop = 1;
			break;
		}
		// OWN before the buffer address: the driver sets OWN last
		uint32_t w1 = g_rd32(pd + 4);
		if (!(w1 & D_ROWN))
		{
			if (w1 & D_EOX) stop = 1;
			break;
		}
		__sync_synchronize();
		uint32_t buf = phys(g_rd32(pd)), next = g_rd32(pd + 8);

		win_read(IX_STATE, 1, &st);
		if (((st ^ seq) & 0xFF) || !(st & ST_RX_ACTIVE) || (st & ST_RESET)) return;

		int len = rxq_len[rxq_head];
		int cap = w1 & D_COUNT;
		memcpy(img + 2, rxq[rxq_head], len);
		rxq_head = (rxq_head + 1) % RXQ_LEN;
		rxq_n--;
		if (len + 3 > cap || !in_ram(buf, len + 3)) continue;

		img[0] = img[1] = 0;
		img[2 + len] = RX_GOOD_END;
		g_copy_in(buf, img, len + 3);
		wb_addr[nwb] = pd + 6;
		wb_val[nwb++] = (uint16_t)(cap - (len + 3));
		if (w1 & D_XIE) xie = 1;

		if (w1 & D_EOX)
		{
			stop = 1;
			break;
		}
		d = next;
	}

	if (!nwb && !stop) return;
	if (nwb)
	{
		win_read(IX_STATE, 1, &st);
		if (((st ^ seq) & 0xFF) || (st & ST_RESET)) return;
		win_inval();
		for (int i = 0; i < nwb; i++) g_wr16(wb_addr[i], wb_val[i]);
		win_inval();
	}
	win_write(IX_CRBDP, seq, d);
	if (stop) win_write(IX_STOP, seq, 1);
	if (nwb) win_write(IX_RX_STAT, 0, RX_GOOD_END);
	if (xie) win_misc(1);
}

void sgi_enet_start(void)
{
	send_mac();

	if (!ram && !(ram = (volatile uint8_t *)shmem_map(RAM_ARM, RAM_LEN))) return;
	running = 1;
	cur_mode = -1;
	seeq_cnt = -1;
	rxq_n = 0;
}

void sgi_enet_stop(void)
{
	if (!running) return;
	running = 0;
	close_link();
}

void sgi_enet_poll(void)
{
	static uint8_t frame[MAX_FRAME];
	uint32_t st;

	if (!running) return;

	win_read(IX_STATE, 1, &st);
	if (!ST_PRESENT(st))
	{
		if (link_open) close_link();
		cur_mode = -1;
		return;
	}
	uint16_t seq = st & 0xFFFF;

	int mode = mode_from_status();
	if (mode != cur_mode)
	{
		cur_mode = mode;
		if (mode == MODE_OFF) close_link();
		else open_link(mode);
		win_misc(0);
	}
	if (st & ST_RESET)
	{
		rxq_n = 0;
		return;
	}

	if (st & ST_TX_ACTIVE) tx_run(seq, (st & ST_LOOPBACK) != 0);

	if (link_open && !(st & ST_LOOPBACK))
	{
		int sc = (st >> 21) & 7;
		if (seeq_cnt != sc)
		{
			uint32_t w[6];
			win_read(IX_LIVE, 1, w);
			rx_cmd = (w[0] >> 16) & 0xFF;
			win_read(IX_SEEQ, 6, w);
			for (int i = 0; i < 6; i++) sta[i] = w[i] & 0xFF;
			seeq_cnt = sc;
		}

		int len;
		while ((len = ethernet_recv_nb(frame, MAX_FRAME)) > 0)
		{
			if (len < 14 || !(st & ST_RX_ACTIVE) || !rx_match(frame)) continue;
			if (len < 60)
			{
				memset(frame + len, 0, 60 - len);
				len = 60;
			}
			rxq_push(frame, len);
		}
	}

	if ((st & ST_RX_ACTIVE) && rxq_n) rx_run(seq);
}
