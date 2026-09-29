#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <linux/if_tun.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <arpa/inet.h>

#include "mac_eth.h"

static int sock_fd = -1;

int mac_eth_iface_fd(void) { return sock_fd; }

static void iface_up(const char *name)
{
	int s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (s < 0) return;
	struct ifreq ifr;
	memset(&ifr, 0, sizeof ifr);
	strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
	if (ioctl(s, SIOCGIFFLAGS, &ifr) == 0 && !(ifr.ifr_flags & IFF_UP))
	{
		ifr.ifr_flags |= IFF_UP;
		ioctl(s, SIOCSIFFLAGS, &ifr);
	}
	close(s);
}

static int open_tap(const char *name)
{
	int fd = open("/dev/net/tun", O_RDWR | O_CLOEXEC | O_NONBLOCK);
	if (fd < 0) { printf("mac_eth: open /dev/net/tun failed\n"); return 0; }

	struct ifreq ifr;
	memset(&ifr, 0, sizeof ifr);
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
	strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
	if (ioctl(fd, TUNSETIFF, &ifr) < 0)
	{
		printf("mac_eth: TUNSETIFF %s failed\n", name);
		close(fd);
		return 0;
	}
	sock_fd = fd;
	iface_up(ifr.ifr_name);
	printf("mac_eth: opened tap %s\n", ifr.ifr_name);
	return 1;
}

static void iface_gro_off(int fd, const char *name, int depth)
{
	struct ethtool_value ev;
	struct ifreq ifr;
	memset(&ifr, 0, sizeof ifr);
	strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
	ev.cmd  = ETHTOOL_SGRO;
	ev.data = 0;
	ifr.ifr_data = (char *)&ev;
	if (ioctl(fd, SIOCETHTOOL, &ifr) == 0)
		printf("mac_eth: GRO off on %s\n", name);

	if (depth >= 2) return;
	char path[128];
	snprintf(path, sizeof path, "/sys/class/net/%s", name);
	DIR *d = opendir(path);
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d)))
		if (!strncmp(e->d_name, "lower_", 6))
			iface_gro_off(fd, e->d_name + 6, depth + 1);
	closedir(d);
}

static int open_raw(const char *name)
{
	struct ifreq ifr;
	struct sockaddr_ll addr;

	sock_fd = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, htons(ETH_P_ALL));
	if (sock_fd < 0) { printf("mac_eth: raw socket failed\n"); return 0; }

	memset(&ifr, 0, sizeof ifr);
	strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
	if (ioctl(sock_fd, SIOCGIFINDEX, &ifr) < 0)
	{
		printf("mac_eth: no such interface %s\n", name);
		close(sock_fd); sock_fd = -1;
		return 0;
	}
	int ifindex = ifr.ifr_ifindex;

	iface_up(name);

	memset(&addr, 0, sizeof addr);
	addr.sll_family   = AF_PACKET;
	addr.sll_protocol = htons(ETH_P_ALL);
	addr.sll_ifindex  = ifindex;
	if (bind(sock_fd, (struct sockaddr *)&addr, sizeof addr) < 0)
	{
		printf("mac_eth: bind %s failed\n", name);
		close(sock_fd); sock_fd = -1;
		return 0;
	}

	struct packet_mreq mreq;
	memset(&mreq, 0, sizeof mreq);
	mreq.mr_ifindex = ifindex;
	mreq.mr_type    = PACKET_MR_PROMISC;
	int rcvbuf = 1 << 20;
	if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUFFORCE, &rcvbuf, sizeof rcvbuf) < 0)
		setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);

	if (setsockopt(sock_fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mreq, sizeof mreq) < 0)
		printf("mac_eth: promisc membership on %s failed\n", name);

	iface_gro_off(sock_fd, name, 0);

	printf("mac_eth: opened raw %s (ifindex %d, promisc)\n", name, ifindex);
	return 1;
}

int mac_eth_iface_open(const char *name)
{
	if (sock_fd >= 0) mac_eth_iface_close();
	if (!strncmp(name, "tap", 3)) return open_tap(name);
	return open_raw(name);
}

void mac_eth_iface_close(void)
{
	if (sock_fd >= 0) close(sock_fd);
	sock_fd = -1;
}

int mac_eth_iface_send(const uint8_t *frame, int len)
{
	if (sock_fd < 0) return -1;
	int n = write(sock_fd, frame, len);
	if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
		printf("mac_eth: send errno=%d\n", errno);
	return n;
}

int mac_eth_iface_recv(uint8_t *buf, int maxlen)
{
	if (sock_fd < 0) return -1;
	int n = read(sock_fd, buf, maxlen);
	if (n < 0) return -1;
	return n;
}

int mac_eth_iface_hwaddr(const char *name, uint8_t mac[6])
{
	int s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (s < 0) return 0;
	struct ifreq ifr;
	memset(&ifr, 0, sizeof ifr);
	strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
	int ok = ioctl(s, SIOCGIFHWADDR, &ifr) == 0;
	close(s);
	if (!ok) return 0;
	memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
	return (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) != 0;
}
