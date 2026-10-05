#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "mac_eth.h"
#include "mac_eth_q8.h"
#include "mac_sonic.h"


static q8_mailbox mbx;
static uint8_t    seq;
static int        stale;

static struct
{
	uint32_t ga, len, xoff;
	int      wr;
	uint8_t *dst;
} q[ETH_Q8_MAX_OPS];
static int      nq;
static uint32_t xnext;

#define AHEAD_BYTES 256
static uint8_t  ahead[AHEAD_BYTES];
static uint32_t ahead_ga, ahead_len;

static inline uint64_t now_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000ull + ts.tv_nsec / 1000;
}

void q8_init(const q8_mailbox *m)
{
	mbx = *m;
	seq = (uint8_t)*mbx.cmd;
	stale = 0;
	nq = 0;
	xnext = 0;
	ahead_len = 0;
}

void q8_begin(void) { ahead_len = 0; }

static uint32_t span_of(uint32_t ga, uint32_t len) { return ((ga & 3) + len + 3) & ~3u; }

#define SPIN_US 1500
static int run(void)
{
	if (!nq) return 0;
	if (stale)
	{
		uint64_t t0 = now_us();
		while ((uint8_t)*mbx.stat != seq && now_us() - t0 < 2000000)
		{
			if (mbx.idle) mbx.idle();
			usleep(50);
		}
		if ((uint8_t)*mbx.stat != seq)
		{
			nq = 0;
			xnext = 0;
			return -1;
		}
		stale = 0;
	}
	for (int i = 0; i < nq; i++)
		mbx.ops[i] = ((uint64_t)q[i].ga << 32) | ((uint64_t)q[i].len << 16) | (q[i].wr ? 1 : 0);
	if (++seq == 0) seq = 1;
	__sync_synchronize();
	*mbx.cmd = ((uint64_t)nq << 8) | seq;
	__sync_synchronize();

	int rc = -1;
	uint64_t t0 = now_us();
	for (;;)
	{
		if ((uint8_t)*mbx.stat == seq) { rc = 0; break; }
		if (mbx.idle) mbx.idle();
		uint64_t el = now_us() - t0;
		if (el > 250000)
		{
			printf("mac_eth: DMA timeout (seq %u, %d ops, first %08X+%u)\n", seq, nq, q[0].ga, q[0].len);
			stale = 1;
			break;
		}
		if (el > SPIN_US) usleep(50);
	}
	if (!rc) for (int i = 0; i < nq; i++)
		if (!q[i].wr && q[i].dst)
			memcpy(q[i].dst, (const void *)(mbx.xfer + q[i].xoff + (q[i].ga & 3)), q[i].len);
	nq = 0;
	xnext = 0;
	return rc;
}

int q8_flush(void) { return run(); }

static int bad(uint32_t ga, uint32_t len)
{
	if (len && ga < ETH_Q8_RAM_TOP && len <= ETH_Q8_RAM_TOP - ga && span_of(ga, len) <= ETH_Q8_XFER_SIZE)
		return 0;
	return 1;
}

static int enqueue(uint32_t ga, uint32_t len, int wr, const uint8_t *src, uint8_t *dst)
{
	uint32_t span = span_of(ga, len);
	if (nq == ETH_Q8_MAX_OPS || xnext + span > ETH_Q8_XFER_SIZE)
		if (run()) return -1;
	q[nq].ga = ga; q[nq].len = len; q[nq].xoff = xnext; q[nq].wr = wr; q[nq].dst = dst;
	if (wr) memcpy((void *)(mbx.xfer + xnext + (ga & 3)), src, len);
	nq++;
	xnext = (xnext + span + 7) & ~7u;
	return 0;
}

static int wr(uint32_t ga, const uint8_t *src, uint32_t len)
{
	if (bad(ga, len)) return -1;
	ahead_len = 0;
	return enqueue(ga, len, 1, src, 0);
}

static int rd(uint32_t ga, uint8_t *dst, uint32_t len, int want_ahead)
{
	if (ahead_len && ga >= ahead_ga && ga - ahead_ga + len <= ahead_len)
	{
		memcpy(dst, ahead + (ga - ahead_ga), len);
		return 0;
	}
	if (bad(ga, len)) return -1;
	if (want_ahead && len <= AHEAD_BYTES)
	{
		uint32_t n = AHEAD_BYTES;
		if (n > ETH_Q8_RAM_TOP - ga) n = ETH_Q8_RAM_TOP - ga;
		ahead_len = 0;
		if (enqueue(ga, n, 0, 0, ahead) || run()) return -1;
		ahead_ga = ga; ahead_len = n;
		memcpy(dst, ahead, len);
		return 0;
	}
	if (enqueue(ga, len, 0, 0, dst)) return -1;
	return run();
}

int q8_read_words(uint32_t ga, uint16_t *v, int n, int stride)
{
	uint8_t b[64 * 4];
	if (n <= 0 || n > 64) return -1;
	if (rd(ga, b, (uint32_t)n * stride, 1)) return -1;
	for (int i = 0; i < n; i++)
	{
		const uint8_t *p = b + i * stride + (stride == 4 ? 2 : 0);
		v[i] = (uint16_t)((p[0] << 8) | p[1]);
	}
	return 0;
}

int q8_write_words(uint32_t ga, const uint16_t *v, int n, int stride)
{
	uint8_t b[64 * 4];
	if (n <= 0 || n > 64) return -1;
	memset(b, 0, (size_t)n * stride);
	for (int i = 0; i < n; i++)
	{
		uint8_t *p = b + i * stride + (stride == 4 ? 2 : 0);
		p[0] = (uint8_t)(v[i] >> 8);
		p[1] = (uint8_t)v[i];
	}
	return wr(ga, b, (uint32_t)n * stride);
}

int q8_read_bytes(uint32_t ga, uint8_t *b, int n)  { return n > 0 ? rd(ga, b, (uint32_t)n, 0) : 0; }
int q8_write_bytes(uint32_t ga, const uint8_t *b, int n) { return n > 0 ? wr(ga, b, (uint32_t)n) : 0; }

static uint16_t isr_seq;
static uint16_t isr_unposted;
static uint16_t isr_bit_seq[16];

void q8_isr_reset(void)
{
	isr_seq = (uint16_t)*mbx.isr_set;
	isr_unposted = 0;
	for (int b = 0; b < 16; b++) isr_bit_seq[b] = isr_seq;
	sonic_take_raised();
}

void q8_isr_post(uint16_t applied)
{
	isr_unposted |= sonic_take_raised();
	if (!isr_unposted || (uint16_t)*mbx.isr_ack != isr_seq) return;
	isr_seq++;
	for (int b = 0; b < 16; b++)
	{
		if (isr_unposted & (1 << b)) isr_bit_seq[b] = isr_seq;
		else if ((int16_t)(isr_seq - isr_bit_seq[b]) > 0x3000) isr_bit_seq[b] = (uint16_t)(isr_seq - 0x3000);
	}
	*mbx.isr_set = ((uint64_t)applied << 48) | ((uint64_t)isr_unposted << 16) | isr_seq;
	__sync_synchronize();
	isr_unposted = 0;
}

uint16_t q8_isr_qualify(uint16_t data, uint16_t seen)
{
	uint16_t ok = 0;
	isr_unposted |= sonic_take_raised();
	for (int b = 0; b < 15; b++)
	{
		if (!(data & (1 << b))) continue;
		if (!(isr_unposted & (1 << b)) && (int16_t)(seen - isr_bit_seq[b]) >= 0) ok |= (uint16_t)(1 << b);
	}
	return ok;
}
