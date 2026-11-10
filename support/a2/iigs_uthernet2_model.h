// Uthernet II (WIZnet W5100) chip model for the Apple IIgs core -- the host
// half of rtl/uthernet2.sv. Portable: no Main dependencies, so the core's
// Verilator simulation builds the same file.
//
// The FPGA keeps MR, the address pointer and the bus timing; the W5100
// address space lives in a shared 64 KB window (see u2_win_* below). This
// model owns the chip behaviour behind it: soft reset, socket OPEN/CLOSE,
// MAC raw SEND/RECV and moving frames between the network and the socket 0
// buffers. Only MAC raw mode on socket 0 is modelled, which is what
// Marinetti's UthernetII link layer uses (S0_MR = $44); other socket modes
// open as CLOSED.

#ifndef IIGS_UTHERNET2_MODEL_H
#define IIGS_UTHERNET2_MODEL_H

#include <stdint.h>

#define U2_WIN_SIZE   0x10000u
#define U2_CTL_MAGIC  0x8000u
#define U2_CTL_GEN    0x8008u
#define U2_CTL_WPTR   0x8010u
#define U2_CTL_ACK    0x8018u
#define U2_CTL_RING   0x8800u
#define U2_RING_SIZE  256u
#define U2_MAGIC      0x3145325553474949ULL   // "IIGSU2E1"

struct u2_net
{
	void *ctx;
	// send one Ethernet frame (no FCS); return <0 on error
	int (*send)(void *ctx, const uint8_t *frame, int len);
	// receive one frame without blocking; 0 or <0 when there is none
	int (*recv)(void *ctx, uint8_t *buf, int maxlen);
};

// Attach to the shared window. Safe to call again with the same pointer.
void u2m_attach(volatile uint8_t *win);

// One pass: drain the doorbell ring, then move received frames into
// socket 0. Bounded and non-blocking. Returns 1 while the core's card is up.
int u2m_poll(const u2_net *net);

// Socket 0 MAC raw state, for the host side to set up its interface.
// Returns 1 while socket 0 is open in MAC raw mode and fills mac[6] with
// the guest's source address (SHAR) as it was when the socket opened.
int u2m_macraw(uint8_t mac[6]);

// Counters for logging.
struct u2_stats { unsigned long tx, rx, rx_drop_full, rx_drop_filter, resets, opens; };
void u2m_stats(u2_stats *out);

#endif
