/*
 * ethernet.cpp — AF_PACKET raw socket for A2065 daemon
 *
 * Replaces Amiberry's WinPcap/libpcap layer.
 * Sends and receives raw Ethernet frames via Linux AF_PACKET socket.
 *
 * Stub implementation for non-Linux builds (macOS native testing).
 */

#include "minimig_a2065_types.h"
#include "minimig_a2065_debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <linux/if_tun.h>
#include <linux/filter.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>

static int sock_fd = -1;
static int is_tap = 0;
static uint8_t host_mac[6];

void ethernet_iface_up(const char *iface);  /* forward decl for open_tap */

int ethernet_is_tap(void) { return is_tap; }

static int open_tap(const char *iface)
{
    int fd = open("/dev/net/tun", O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror("[a2065] open /dev/net/tun"); return 0; }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    if (ioctl(fd, TUNSETIFF, &ifr) < 0) {
        perror("[a2065] TUNSETIFF");
        close(fd);
        return 0;
    }

    sock_fd = fd;
    is_tap = 1;

    /* Read the tap interface's MAC via a temporary socket. */
    memset(host_mac, 0, 6);
    int s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (s >= 0) {
        struct ifreq m;
        memset(&m, 0, sizeof m);
        strncpy(m.ifr_name, ifr.ifr_name, IFNAMSIZ - 1);
        if (ioctl(s, SIOCGIFHWADDR, &m) == 0)
            memcpy(host_mac, m.ifr_hwaddr.sa_data, 6);
        close(s);
    }

    ethernet_iface_up(ifr.ifr_name);

    LOG("[a2065] Opened tap %s MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
            ifr.ifr_name,
            host_mac[0], host_mac[1], host_mac[2],
            host_mac[3], host_mac[4], host_mac[5]);
    return 1;
}

int ethernet_open(const char *iface, int promiscuous)
{
    if (!strncmp(iface, "tap", 3)) return open_tap(iface);

    struct ifreq ifr;
    struct sockaddr_ll addr;

    is_tap = 0;
    /* SOCK_CLOEXEC matters here: Main spawns helper processes, and without it
     * every one of them inherits this raw socket. The socket then outlives the
     * card being switched off — it keeps its promiscuous-mode membership and
     * keeps queueing every frame on the interface, with nobody to read it. */
    sock_fd = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, htons(ETH_P_ALL));
    if (sock_fd < 0) {
        perror("[a2065] socket");
        return 0;
    }

    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';
    if (ioctl(sock_fd, SIOCGIFINDEX, &ifr) < 0) {
        perror("[a2065] SIOCGIFINDEX");
        close(sock_fd); sock_fd = -1;
        return 0;
    }
    int ifindex = ifr.ifr_ifindex;

    memset(host_mac, 0, 6);
    if (ioctl(sock_fd, SIOCGIFHWADDR, &ifr) == 0)
        memcpy(host_mac, ifr.ifr_hwaddr.sa_data, 6);
    else
        perror("[a2065] SIOCGIFHWADDR");

    /* Bring the interface up. The A2065's wire side (eth1) is often admin-down
     * after boot; raw send/recv fail with "Network is down" until IFF_UP is set. */
    if (ioctl(sock_fd, SIOCGIFFLAGS, &ifr) == 0) {
        if (!(ifr.ifr_flags & IFF_UP)) {
            ifr.ifr_flags |= IFF_UP;
            if (ioctl(sock_fd, SIOCSIFFLAGS, &ifr) < 0)
                perror("[a2065] SIOCSIFFLAGS (up)");
            else
                LOG("[a2065] Brought %s up\n", iface);
        }
    }

    memset(&addr, 0, sizeof addr);
    addr.sll_family   = AF_PACKET;
    addr.sll_protocol = htons(ETH_P_ALL);
    addr.sll_ifindex  = ifindex;
    if (bind(sock_fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("[a2065] bind");
        close(sock_fd); sock_fd = -1;
        return 0;
    }

    if (promiscuous) {
        struct packet_mreq mreq = {};
        mreq.mr_ifindex = ifindex;
        mreq.mr_type    = PACKET_MR_PROMISC;
        setsockopt(sock_fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mreq, sizeof mreq);
    }

    /* Enlarge the kernel socket receive buffer. The default (rmem_max ~180KB on
     * MiSTer) overflows when the rx_thread can't drain a line-rate TCP burst
     * fast enough, dropping segments → retransmit spiral → collapsed throughput.
     * SO_RCVBUFFORCE bypasses rmem_max (we run as root). */
    int rcvbuf = 4 * 1024 * 1024;
    if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUFFORCE, &rcvbuf, sizeof rcvbuf) < 0) {
        /* fall back to the capped, non-privileged setter */
        if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf) < 0)
            perror("[a2065] SO_RCVBUF");
    }
    int got = 0; socklen_t glen = sizeof got;
    if (getsockopt(sock_fd, SOL_SOCKET, SO_RCVBUF, &got, &glen) == 0)
        LOG("[a2065] socket rcvbuf = %d bytes\n", got);

    LOG("[a2065] Opened %s idx=%d MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
            iface, ifindex,
            host_mac[0], host_mac[1], host_mac[2],
            host_mac[3], host_mac[4], host_mac[5]);
    return 1;
}

/* Read and reset the kernel AF_PACKET stats (tp_packets seen, tp_drops dropped
 * for lack of socket buffer). PACKET_STATISTICS clears the counters on read.
 * Not available on tap devices (no SOL_PACKET). */
void ethernet_packet_stats(unsigned long *packets, unsigned long *drops)
{
    *packets = *drops = 0;
    if (sock_fd < 0 || is_tap) return;
    struct tpacket_stats st = {};
    socklen_t len = sizeof st;
    if (getsockopt(sock_fd, SOL_PACKET, PACKET_STATISTICS, &st, &len) == 0) {
        *packets = st.tp_packets;
        *drops   = st.tp_drops;
    }
}

void ethernet_close(void)
{
    if (sock_fd >= 0) { close(sock_fd); sock_fd = -1; }
}

void ethernet_send(const uint8_t *frame, int len)
{
    if (sock_fd < 0 || len <= 0) return;
    ssize_t sent = is_tap ? write(sock_fd, frame, (size_t)len)
                          : send(sock_fd, frame, (size_t)len, 0);
    if (sent < 0) perror("[a2065] send");
}

int ethernet_recv(uint8_t *buf, int maxlen)
{
    if (sock_fd < 0) return -1;
    ssize_t n = is_tap ? read(sock_fd, buf, (size_t)maxlen)
                       : recv(sock_fd, buf, (size_t)maxlen, 0);
    if (n < 0) {
        if (errno != EINTR) perror("[a2065] recv");
        return -1;
    }
    return (int)n;
}

/* Non-blocking receive for batch draining. Returns the frame length (>0),
 * 0 when the socket queue is empty (EAGAIN — drain complete), or -1 on error.
 * Lets the rx_thread pull every queued frame into the RX ring in one pass so
 * a TSO pair lands together and the Amiga issues an immediate (2-segment) ACK. */
int ethernet_recv_nb(uint8_t *buf, int maxlen)
{
    if (sock_fd < 0) return -1;
    if (is_tap) {
        struct pollfd pfd = { sock_fd, POLLIN, 0 };
        int pr = poll(&pfd, 1, 0);
        if (pr <= 0) return 0;
        ssize_t n = read(sock_fd, buf, (size_t)maxlen);
        if (n < 0) { if (errno != EINTR) perror("[a2065] tap recv_nb"); return -1; }
        return (int)n;
    }
    ssize_t n = recv(sock_fd, buf, (size_t)maxlen, MSG_DONTWAIT);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        if (errno != EINTR) perror("[a2065] recv_nb");
        return -1;
    }
    return (int)n;
}

void ethernet_get_mac(uint8_t *mac_out)
{
    memcpy(mac_out, host_mac, 6);
}

/* Read any interface's hardware MAC without disturbing the open socket.
 * Returns 1 if a non-zero low half (bytes 3..5) was obtained, else 0. */
int ethernet_read_iface_mac(const char *iface, uint8_t *out)
{
    memset(out, 0, 6);
    int fd = socket(AF_PACKET, SOCK_DGRAM | SOCK_CLOEXEC, htons(ETH_P_ALL));
    if (fd < 0) return 0;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    int ok = 0;
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) == 0) {
        memcpy(out, ifr.ifr_hwaddr.sa_data, 6);
        ok = (out[3] | out[4] | out[5]) != 0;
    }
    close(fd);
    return ok;
}

/*
 * Attach a classic-BPF filter that runs in the kernel before the copy to
 * userspace, so the daemon only wakes for frames the Amiga cares about.
 * Required for the BPF eth0-sharing mode: eth0 is promiscuous (so frames for
 * the Amiga's distinct MAC reach the kernel), and this filter does the same
 * selection gotfunc() does in software — accept dst == mac or broadcast; drop
 * our own outgoing frames AND non-broadcast multicast. gotfunc() re-filters,
 * so the bind()/attach startup race is harmless.
 *
 * Why drop multicast in the kernel: on a shared host segment multicast is the
 * dominant noise (mDNS/PTP/IPv6-ND were ~75% of frames in measurement).
 * gotfunc() already rejects it via the LADRF hash (LADRF=0 → all multicast
 * dropped), but only after a userspace copy, an rx-thread wakeup and a
 * registers_lock hold per frame — which starves the doorbell CMD loop and
 * collapses TCP throughput. Filtering it here removes that load entirely.
 * Trade-off: if the Amiga ever joins a multicast group it won't be delivered
 * under BPF mode — acceptable for v1 (AmigaOS/Roadshow rarely joins groups);
 * MODE_PROM detaches the filter for sniffers.
 *
 * 11-instruction program (interpreted; CONFIG_BPF_JIT off on stock MiSTer):
 */
int ethernet_set_mac_filter(const uint8_t *mac)
{
    if (sock_fd < 0 || is_tap) return 0;

    /* cBPF BPF_ABS loads are big-endian, so pack the MAC the same way. */
    uint32_t macword = ((uint32_t)mac[0] << 24) | ((uint32_t)mac[1] << 16)
                     | ((uint32_t)mac[2] << 8)  |  (uint32_t)mac[3];
    uint32_t machalf = ((uint32_t)mac[4] << 8)  |  (uint32_t)mac[5];

    struct sock_filter code[] = {
        /* 0: drop frames the host itself transmitted (loopback echo) */
        BPF_STMT(BPF_LD  | BPF_B   | BPF_ABS, (uint32_t)(SKF_AD_OFF + SKF_AD_PKTTYPE)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,   PACKET_OUTGOING, 8, 0),
        /* 2: load dst[0..3]; branch broadcast vs realmac */
        BPF_STMT(BPF_LD  | BPF_W   | BPF_ABS, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,   0xFFFFFFFFu, 0, 2),
        /* 4: dst[0..3]==FFFFFFFF → accept iff dst[4..5]==FFFF (broadcast) */
        BPF_STMT(BPF_LD  | BPF_H   | BPF_ABS, 4),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,   0xFFFFu, 3, 4),
        /* 6: dst[0..3]==macword (A still holds dst[0..3]); else drop */
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,   macword, 0, 3),
        /* 7: dst[4..5]==machalf → accept; else drop */
        BPF_STMT(BPF_LD  | BPF_H   | BPF_ABS, 4),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,   machalf, 0, 1),
        /* 9: accept (whole frame) */
        BPF_STMT(BPF_RET | BPF_K,             0xFFFFFFFFu),
        /* 10: drop */
        BPF_STMT(BPF_RET | BPF_K,             0),
    };

    struct sock_fprog prog;
    prog.len    = sizeof code / sizeof code[0];
    prog.filter = code;

    if (setsockopt(sock_fd, SOL_SOCKET, SO_ATTACH_FILTER, &prog, sizeof prog) < 0) {
        perror("[a2065] SO_ATTACH_FILTER");
        return 0;
    }

    LOG("[a2065] BPF filter attached: dst=%02X:%02X:%02X:%02X:%02X:%02X + bcast/mcast\n",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return 1;
}

void ethernet_clear_filter(void)
{
    if (sock_fd < 0 || is_tap) return;
    int dummy = 0;  /* SO_DETACH_FILTER ignores optval but needs a valid ptr */
    if (setsockopt(sock_fd, SOL_SOCKET, SO_DETACH_FILTER, &dummy, sizeof dummy) < 0)
        perror("[a2065] SO_DETACH_FILTER");
    else
        LOG("[a2065] BPF filter detached\n");
}

/* ── Network-management helpers (combined daemon auto-setup) ───────────────
 * These mutate host network state (interface flags, MAC, macvlan, dhcpcd) and
 * are gated by the caller's --no-net-setup. Shell-out (v1) for readability and
 * parity with the manually validated commands. */

static int run_cmd(const char *cmd)
{
    LOG("[a2065] exec: %s\n", cmd);
    int rc = system(cmd);
    if (rc != 0) LOG("[a2065] exec rc=%d: %s\n", rc, cmd);
    return rc;
}

int ethernet_iface_present(const char *iface)
{
    return if_nametoindex(iface) != 0;
}

void ethernet_iface_up(const char *iface)
{
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    struct ifreq ifr; memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) == 0 && !(ifr.ifr_flags & IFF_UP)) {
        ifr.ifr_flags |= IFF_UP;
        ioctl(fd, SIOCSIFFLAGS, &ifr);
    }
    close(fd);
}

/* 1 iff the interface exists and reports carrier (link up). Brings it up first
 * so /carrier is meaningful. */
int ethernet_iface_carrier(const char *iface)
{
    if (!ethernet_iface_present(iface)) return 0;
    ethernet_iface_up(iface);
    char path[128];
    snprintf(path, sizeof path, "/sys/class/net/%s/carrier", iface);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int c = 0;
    if (fscanf(f, "%d", &c) != 1) c = 0;
    fclose(f);
    return c == 1;
}

void ethernet_offload_off(const char *iface)
{
    char cmd[160];
    snprintf(cmd, sizeof cmd,
             "ethtool -K %s gro off lro off gso off tso off 2>/dev/null", iface);
    run_cmd(cmd);
}

void ethernet_offload_on(const char *iface)
{
    char cmd[160];
    snprintf(cmd, sizeof cmd,
             "ethtool -K %s gro on lro on gso on tso on 2>/dev/null", iface);
    run_cmd(cmd);
}

/* Set an interface's hardware MAC (direct mode). Bounces the link down/up. */
int ethernet_set_iface_mac(const char *iface, const uint8_t *mac)
{
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return 0;
    struct ifreq ifr;

    memset(&ifr, 0, sizeof ifr); strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) == 0) {       /* down */
        ifr.ifr_flags &= ~IFF_UP;
        ioctl(fd, SIOCSIFFLAGS, &ifr);
    }
    memset(&ifr, 0, sizeof ifr); strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
    memcpy(ifr.ifr_hwaddr.sa_data, mac, 6);
    int ok = ioctl(fd, SIOCSIFHWADDR, &ifr) == 0;
    if (!ok) perror("[a2065] SIOCSIFHWADDR");

    memset(&ifr, 0, sizeof ifr); strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) == 0) {       /* up */
        ifr.ifr_flags |= IFF_UP;
        ioctl(fd, SIOCSIFFLAGS, &ifr);
    }
    close(fd);
    if (ok)
        LOG("[a2065] set %s MAC %02X:%02X:%02X:%02X:%02X:%02X\n", iface,
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return ok;
}

/* Probe whether macvlan is usable by creating + deleting a throwaway child. */
int ethernet_macvlan_available(const char *parent)
{
    char cmd[160];
    snprintf(cmd, sizeof cmd,
             "ip link add a2065probe link %s type macvlan mode bridge 2>/dev/null", parent);
    if (run_cmd(cmd) != 0) return 0;
    run_cmd("ip link del a2065probe 2>/dev/null");
    return 1;
}

int ethernet_macvlan_create(const char *parent, const char *name, const uint8_t *mac)
{
    char cmd[200], m[18];
    snprintf(m, sizeof m, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(cmd, sizeof cmd, "ip link del %s 2>/dev/null", name); run_cmd(cmd);
    snprintf(cmd, sizeof cmd,
             "ip link add %s link %s address %s type macvlan mode bridge",
             name, parent, m);
    if (run_cmd(cmd) != 0) return 0;
    snprintf(cmd, sizeof cmd, "ip link set %s up", name); run_cmd(cmd);
    snprintf(cmd, sizeof cmd, "ip addr flush dev %s", name); run_cmd(cmd);
    return 1;
}

void ethernet_macvlan_delete(const char *name)
{
    char cmd[80];
    snprintf(cmd, sizeof cmd, "ip link del %s 2>/dev/null", name);
    run_cmd(cmd);
}

/* Stop dhcpcd contending for the Amiga's MAC on <iface>: add denyinterfaces to
 * the global section of /etc/dhcpcd.conf (idempotent), restart dhcpcd
 * (persistent keeps eth0's lease), and flush any address already on <iface>. */
void ethernet_dhcpcd_deny(const char *iface)
{
    const char *cfg = "/etc/dhcpcd.conf";
    char cmd[512];

    snprintf(cmd, sizeof cmd,
             "grep '^denyinterfaces' %s 2>/dev/null | grep -qw %s", cfg, iface);
    if (run_cmd(cmd) == 0) {
        LOG("[a2065] dhcpcd already denies %s\n", iface);
    } else {
        snprintf(cmd, sizeof cmd,
            "if grep -q '^denyinterfaces ' %s 2>/dev/null; then "
            "sed -i '0,/^denyinterfaces /s//denyinterfaces %s /' %s; "
            "else printf 'denyinterfaces %s\\n' >> %s; fi",
            cfg, iface, cfg, iface, cfg);
        run_cmd(cmd);
        LOG("[a2065] added denyinterfaces %s to %s; restarting dhcpcd\n", iface, cfg);
        run_cmd("killall dhcpcd 2>/dev/null; dhcpcd 2>/dev/null &");
        usleep(300000);
    }
    snprintf(cmd, sizeof cmd, "ip addr flush dev %s 2>/dev/null", iface);
    run_cmd(cmd);
}

#else

int ethernet_is_tap(void) { return 0; }

int ethernet_open(const char *iface, int promiscuous)
{
    (void)iface; (void)promiscuous;
    LOG("[a2065] ethernet: stub (non-Linux build)\n");
    return 1;
}

void ethernet_close(void) {}

void ethernet_send(const uint8_t *frame, int len)
{
    (void)frame; (void)len;
}

int ethernet_recv(uint8_t *buf, int maxlen)
{
    (void)buf; (void)maxlen;
    return -1;
}

int ethernet_recv_nb(uint8_t *buf, int maxlen)
{
    (void)buf; (void)maxlen;
    return 0;
}

void ethernet_get_mac(uint8_t *mac_out)
{
    memset(mac_out, 0, 6);
}

int ethernet_read_iface_mac(const char *iface, uint8_t *out)
{
    (void)iface;
    memset(out, 0, 6);
    return 0;
}

int ethernet_set_mac_filter(const uint8_t *mac) { (void)mac; return 1; }

void ethernet_clear_filter(void) {}

void ethernet_packet_stats(unsigned long *packets, unsigned long *drops)
{
    *packets = *drops = 0;
}

int  ethernet_iface_present(const char *iface) { (void)iface; return 0; }
void ethernet_iface_up(const char *iface) { (void)iface; }
int  ethernet_iface_carrier(const char *iface) { (void)iface; return 0; }
void ethernet_offload_off(const char *iface) { (void)iface; }
void ethernet_offload_on(const char *iface) { (void)iface; }
int  ethernet_set_iface_mac(const char *iface, const uint8_t *mac) { (void)iface; (void)mac; return 0; }
int  ethernet_macvlan_available(const char *parent) { (void)parent; return 0; }
int  ethernet_macvlan_create(const char *parent, const char *name, const uint8_t *mac)
{ (void)parent; (void)name; (void)mac; return 0; }
void ethernet_macvlan_delete(const char *name) { (void)name; }
void ethernet_dhcpcd_deny(const char *iface) { (void)iface; }

#endif
