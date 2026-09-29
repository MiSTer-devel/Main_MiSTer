// DP83932/DP83934 SONIC model — port of MAME dp83932c.cpp (BSD-3-Clause, Patrick Mackinlay).

#include <string.h>
#include <stdio.h>
#include "mac_sonic.h"

enum
{
	CR = 0x00, DCR, RCR, TCR, IMR, ISR, UTDA, CTDA,
	TPS, TFC, TSA0, TSA1, TFS, URDA, CRDA, CRBA0,
	CRBA1, RBWC0, RBWC1, EOBC, URRA, RSA, REA, RRP,
	RWP, TRBA0, TRBA1, TBWC0, TBWC1, ADDR0, ADDR1, LLFA,
	TTDA, CEP, CAP2, CAP1, CAP0, CE, CDP, CDC,
	SR, WT0, WT1, RSC, CRCT, FAET, MPT, MDT,
	DCR2 = 0x3f
};

#define CR_HTX   0x0001
#define CR_TXP   0x0002
#define CR_RXDIS 0x0004
#define CR_RXEN  0x0008
#define CR_STP   0x0010
#define CR_ST    0x0020
#define CR_RST   0x0080
#define CR_RRRA  0x0100
#define CR_LCAM  0x0200

#define DCR_DW   0x0020
#define DCR_EXBUS 0x8000
#define DCR_LBR  0x2000

#define RCR_PRX  0x0001
#define RCR_LBK  0x0002
#define RCR_FAER 0x0004
#define RCR_CRCR 0x0008
#define RCR_LPKT 0x0040
#define RCR_BC   0x0080
#define RCR_MC   0x0100
#define RCR_LB   0x0600
#define RCR_AMC  0x0800
#define RCR_PRO  0x1000
#define RCR_BRD  0x2000
#define RCR_RNT  0x4000
#define RCR_ERR  0x8000

#define TCR_PTX  0x0001
#define TCR_NCRS 0x0100
#define TCR_CRCI 0x2000
#define TCR_PINT 0x8000
#define TCR_TPC  0xf000
#define TCR_TPS_ 0x07ff
#define TCR_BCM  0x0002

#define ISR_RBE  0x0020
#define ISR_RDE  0x0040
#define ISR_TC   0x0080
#define ISR_TXDN 0x0200
#define ISR_PKTRX 0x0400
#define ISR_PINT 0x0800
#define ISR_LCD  0x1000

static const uint16_t regmask[64] = {
	0x03bf, 0xbfff, 0xfe00, 0xf000, 0x7fff, 0x7fff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xfffe, 0xfffe, 0xfffe,
	0xfffe, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0x000f, 0x0000, 0x0000, 0x0000, 0xffff, 0xfffe, 0x001f,
	0x0000, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0x0000,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
	0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xf017,
};

static const sonic_host_ops *host;
static uint16_t reg[64];
static uint64_t cam[16];
static uint16_t isr_seen;

static int      isr_local;
static uint16_t isr_raised;
static inline void isr_raise(uint16_t bits) { reg[ISR] |= bits; isr_raised |= bits; }

#define ISR_REDELIVER_US 20000

static uint64_t model_us = 1000;
static uint64_t pktrx_last_us, txdn_last_us;

static uint32_t ea_mask = 0x00ffffffu;
static inline uint32_t ea_bus(uint32_t hi, uint32_t lo)
{
	return ((hi << 16) | lo) & ea_mask;
}
#define EA(hi, lo) ea_bus((uint16_t)(hi), (uint16_t)(lo))
#define WIDTH()    ((reg[DCR] & DCR_DW) ? 4 : 2)

void sonic_set_addr_bits(int bits)
{
	if (bits == 24 || bits == 32)
		ea_mask = (bits == 32) ? 0xffffffffu : 0x00ffffffu;
}

#define DA(hi, lo) (EA(hi, lo) & ~(uint32_t)1)

#define TX_PARK(ttda) do { \
	reg[CTDA] = (uint16_t)((ttda) | 1); \
	reg[CR]  &= (uint16_t)~CR_TXP; \
	isr_raise(ISR_TXDN); \
	txdn_last_us = model_us; \
	return; } while (0)
#define TX_ABORT() TX_PARK(reg[TTDA])

static uint32_t crc32_eth(const uint8_t *p, int n)
{
	uint32_t c = 0xFFFFFFFFu;
	for (int i = 0; i < n; i++)
	{
		c ^= p[i];
		for (int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xEDB88320u & (0-(c & 1)));
	}
	return ~c;
}

void sonic_init(const sonic_host_ops *ops)
{
	host = ops;
	memset(reg, 0, sizeof reg);
	reg[SR] = 6;
	sonic_reset();
}

void sonic_reset(void)
{
	isr_seen  = 0xFFFF;
	reg[CR]   = CR_RST | CR_STP | CR_RXDIS;
	reg[DCR] &= (uint16_t)~(DCR_EXBUS | DCR_LBR);
	reg[RCR] &= (uint16_t)~(RCR_RNT | RCR_BRD | RCR_LB);
	reg[TCR] |= TCR_NCRS | TCR_PTX;
	reg[TCR] &= (uint16_t)~(TCR_TPC | TCR_BCM);
	reg[IMR]  = 0;
	reg[ISR]  = 0;
	isr_raised = 0;
	reg[EOBC] = 0x02f8;
	reg[CE]   = 0;
	reg[RSC]  = 0;
	reg[DCR2] = 0;
}

uint16_t sonic_reg(int r) { return reg[r & 0x3f]; }

void sonic_fill_shadows(uint16_t out[64])
{
	isr_seen |= reg[ISR];
	memcpy(out, reg, sizeof reg);
}

int sonic_int_line(void) { return (reg[ISR] & reg[IMR] & 0x7fff) != 0; }

void sonic_set_isr_local(int on) { isr_local = on; }

uint16_t sonic_take_raised(void)
{
	uint16_t r = isr_raised;
	isr_raised = 0;
	return r;
}

static void read_rra(int command)
{
	const int w = WIDTH();
	uint16_t v[4];
	if (host->read_words(EA(reg[URRA], reg[RRP]), v, 4, w)) return;
	reg[CRBA0] = v[0];
	reg[CRBA1] = v[1];
	reg[RBWC0] = v[2];
	reg[RBWC1] = v[3];

	reg[RRP] += 4 * w;
	if (reg[RRP] == reg[REA]) reg[RRP] = reg[RSA];
	if (reg[RRP] == reg[RWP]) isr_raise(ISR_RBE);

	if (command)
		reg[CR] &= (uint16_t)~CR_RRRA;
	else
		reg[RSC] = (uint16_t)((reg[RSC] & 0xff00) + 0x100);
}

static void load_cam(void)
{
	const int w = WIDTH();
	while (reg[CDC])
	{
		uint16_t v[4];
		if (host->read_words(EA(reg[URRA], reg[CDP]), v, 4, w)) return;
		unsigned cep = v[0] & 0xf;
		cam[cep] = ((uint64_t)(uint16_t)((v[1] >> 8) | (v[1] << 8)) << 32)
		         | ((uint64_t)(uint16_t)((v[2] >> 8) | (v[2] << 8)) << 16)
		         | ((uint64_t)(uint16_t)((v[3] >> 8) | (v[3] << 8)) << 0);
		reg[CDP] += 4 * w;
		reg[CDC]--;
	}
	uint16_t ce;
	if (host->read_words(EA(reg[URRA], reg[CDP]), &ce, 1, WIDTH())) return;
	reg[CE] = ce;
	reg[CR] &= (uint16_t)~CR_LCAM;
	isr_raise(ISR_LCD);
}

static int address_filter(const uint8_t *buf)
{
	if (reg[RCR] & RCR_PRO) return 1;

	uint64_t const address =
		((uint64_t)buf[0] << 40) | ((uint64_t)buf[1] << 32) |
		((uint64_t)buf[2] << 24) | ((uint64_t)buf[3] << 16) |
		((uint64_t)buf[4] << 8)  | buf[5];

	if (address == 0xffffffffffffULL && (reg[RCR] & (RCR_AMC | RCR_BRD)))
	{
		reg[RCR] |= RCR_BC;
		return 1;
	}
	if ((address & 0x010000000000ULL) && (reg[RCR] & RCR_AMC))
	{
		reg[RCR] |= RCR_MC;
		return 1;
	}
	for (unsigned i = 0; i < 16; i++)
		if (address == cam[i] && ((reg[CE] >> i) & 1))
			return 1;
	return 0;
}

int sonic_rx_frame(const uint8_t *frame, int len)
{
	uint8_t buf[1524];

	if (!host) return -1;
	if (len <= 0 || len > 1518) return 0;
	if (!(reg[CR] & CR_RXEN) || (reg[ISR] & (ISR_RDE | ISR_RBE))) return -1;

	if (reg[CRDA] & 1)
	{
		uint16_t v;
		if (host->read_words(DA(reg[URDA], reg[LLFA]), &v, 1, WIDTH())) return -1;
		reg[CRDA] = v;
		if (reg[CRDA] & 1) return -1;
		uint16_t zero = 0;
		host->write_words(DA(reg[URDA], reg[LLFA]) + WIDTH(), &zero, 1, WIDTH());
	}

	reg[RCR] &= (uint16_t)~(RCR_MC | RCR_BC | RCR_LPKT | RCR_CRCR | RCR_FAER | RCR_LBK | RCR_PRX);

	if (!address_filter(frame)) return 0;

	memcpy(buf, frame, len);
	uint32_t const fcs = crc32_eth(buf, len);
	buf[len + 0] = (uint8_t)(fcs >> 0);
	buf[len + 1] = (uint8_t)(fcs >> 8);
	buf[len + 2] = (uint8_t)(fcs >> 16);
	buf[len + 3] = (uint8_t)(fcs >> 24);
	int length = len + 4;
	int padded = (reg[DCR] & DCR_DW) ? ((length + 3) & ~3) : length;
	for (int i = length; i < padded; i++) buf[i] = 0xff;

	if (length < 64 && !(reg[RCR] & RCR_RNT)) return 0;
	reg[RCR] |= RCR_PRX;
	if (reg[RCR] & RCR_LB) reg[RCR] |= RCR_LBK;

	reg[TRBA0] = reg[CRBA0];
	reg[TRBA1] = reg[CRBA1];
	reg[TBWC0] = reg[RBWC0];
	reg[TBWC1] = reg[RBWC1];

	uint32_t const rba_reg = ((uint32_t)reg[CRBA1] << 16) | reg[CRBA0];
	if (host->write_bytes(EA(reg[CRBA1], reg[CRBA0]), buf, padded)) return 0;

	uint32_t const crba = rba_reg + padded;
	reg[CRBA1] = (uint16_t)(crba >> 16);
	reg[CRBA0] = (uint16_t)crba;

	uint32_t const rbwc = (((uint32_t)reg[RBWC1] << 16) | reg[RBWC0]) - (padded + 1) / 2;
	reg[RBWC1] = (uint16_t)(rbwc >> 16);
	reg[RBWC0] = (uint16_t)rbwc;
	if (rbwc < reg[EOBC]) reg[RCR] |= RCR_LPKT;

	const int w = WIDTH();
	uint32_t const rda = DA(reg[URDA], reg[CRDA]);
	uint16_t st[5] = { reg[RCR], (uint16_t)length, reg[TRBA0], reg[TRBA1], reg[RSC] };
	if (host->write_words(rda + 1 * w, st + 1, 4, w)) return 0;
	reg[LLFA] = (uint16_t)(reg[CRDA] + 5 * w);
	uint16_t link;
	if (host->read_words(rda + 5 * w, &link, 1, w)) return 0;
	if (!(link & 1))
	{
		uint16_t zero = 0;
		host->write_words(rda + 6 * w, &zero, 1, w);
	}
	if (host->write_words(rda, st, 1, w)) return 0;
	reg[CRDA] = link;

	if (reg[CRDA] & 1)
		isr_raise(ISR_RDE);

	if (rbwc < reg[EOBC])
		read_rra(0);
	else
		reg[RSC] = (uint16_t)((reg[RSC] & 0xff00) | (uint8_t)(reg[RSC] + 1));

	isr_raise(ISR_PKTRX);
	pktrx_last_us = model_us;
	return 1;
}

#define TX_CHAIN_BUDGET 8
#define TX_SEEN_MAX 64
static uint32_t tx_seen[TX_SEEN_MAX];
static int      tx_nseen;
static void sonic_tx_new_chain(void) { tx_nseen = 0; }
static int tx_revisited(uint32_t da)
{
	for (int i = 0; i < tx_nseen; i++)
		if (tx_seen[i] == da) return 1;
	if (tx_nseen < TX_SEEN_MAX) tx_seen[tx_nseen++] = da;
	return tx_nseen >= TX_SEEN_MAX;
}
static void transmit_chain(void)
{
	for (int pkts = 0; pkts < TX_CHAIN_BUDGET; pkts++)
	{
		const int w = WIDTH();
		reg[TTDA] = reg[CTDA];
		uint32_t const tda = DA(reg[UTDA], reg[CTDA]);
		unsigned word = 1;

		if (tx_revisited(tda)) TX_PARK(reg[CTDA]);

		uint16_t const tcr_old = reg[TCR];
		uint16_t hdr[4];
		if (host->read_words(tda, hdr, 4, w)) TX_ABORT();
		if (hdr[0] != 0) TX_PARK(reg[CTDA]);
		reg[TCR] = hdr[1] & TCR_TPC;
		reg[TPS] = hdr[2];
		reg[TFC] = hdr[3];
		word += 3;

		if ((reg[TCR] & TCR_PINT) && !(tcr_old & TCR_PINT))
			isr_raise(ISR_PINT);

		uint8_t buf[1520];
		unsigned length = 0;

		for (unsigned frag = 0; frag < reg[TFC]; frag++)
		{
			uint16_t fr[3];
			if (host->read_words(tda + word * w, fr, 3, w)) TX_ABORT();
			reg[TSA0] = fr[0];
			reg[TSA1] = fr[1];
			reg[TFS]  = fr[2];
			word += 3;

			if (length + reg[TFS] > sizeof buf - 4) TX_ABORT();
			if (host->read_bytes(EA(reg[TSA1], reg[TSA0]), buf + length, reg[TFS])) TX_ABORT();
			length += reg[TFS];
		}

		if (!(reg[TCR] & TCR_CRCI))
		{
			uint32_t const crc = crc32_eth(buf, length);
			buf[length + 0] = (uint8_t)(crc >> 0);
			buf[length + 1] = (uint8_t)(crc >> 8);
			buf[length + 2] = (uint8_t)(crc >> 16);
			buf[length + 3] = (uint8_t)(crc >> 24);
			length += 4;
		}

		reg[CTDA] = (uint16_t)(reg[CTDA] + word * w);

		{
			int plen = (int)length;
			if (!(reg[TCR] & TCR_CRCI)) plen -= 4;
			if (reg[RCR] & RCR_LB)
				sonic_rx_frame(buf, plen);
			else
				host->wire_send(buf, plen);
		}

		reg[TCR] |= TCR_PTX;
		uint16_t st = reg[TCR] & TCR_TPS_;
		if (host->write_words(DA(reg[UTDA], reg[TTDA]), &st, 1, w)) TX_ABORT();

		if (reg[CR] & CR_HTX)
		{
			isr_raise(ISR_TXDN);
			txdn_last_us = model_us;
			reg[CR] &= (uint16_t)~CR_TXP;
			return;
		}

		uint16_t link;
		if (host->read_words(DA(reg[UTDA], reg[CTDA]), &link, 1, w)) TX_ABORT();
		reg[CTDA] = link;
		if (reg[CTDA] & 1)
		{
			isr_raise(ISR_TXDN);
			txdn_last_us = model_us;
			reg[CR]  &= (uint16_t)~CR_TXP;
			return;
		}
	}
}

void sonic_tx_continue(void)
{
	if ((reg[CR] & (CR_TXP | CR_RST)) == CR_TXP) transmit_chain();
}

void sonic_time_tick(unsigned us)
{
	model_us += us;
	if (!(reg[CR] & CR_ST)) return;
	uint32_t wt = ((uint32_t)reg[WT1] << 16) | reg[WT0];
	if (!wt) return;
	uint32_t dec = us * 10;
	if (wt > dec)
	{
		wt -= dec;
	} else
	{
		wt = 0;
		isr_raise(ISR_TC);
	}
	reg[WT0] = (uint16_t)wt;
	reg[WT1] = (uint16_t)(wt >> 16);
}

static void command(uint16_t param)
{
	if (param & CR_HTX)   reg[CR] &= (uint16_t)~CR_TXP;
	if (param & CR_TXP) { reg[CR] &= (uint16_t)~CR_HTX; sonic_tx_new_chain(); transmit_chain(); }
	if (param & CR_RXDIS) reg[CR] &= (uint16_t)~CR_RXEN;
	if (param & CR_RXEN)  reg[CR] &= (uint16_t)~CR_RXDIS;
	if (param & CR_STP)   reg[CR] &= (uint16_t)~CR_ST;
	if (param & CR_ST)    reg[CR] &= (uint16_t)~CR_STP;
	if (param & CR_RRRA)  read_rra(1);
	if (param & CR_LCAM)  load_cam();
}

void sonic_reg_write(int r, uint16_t data)
{
	r &= 0x3f;

	switch (r)
	{
	case CR:
		if (reg[CR] & CR_RST)
		{
			if (!(data & CR_RST))
				reg[CR] &= (uint16_t)~CR_RST;
		} else if (data & CR_RST)
		{
			reg[CR] &= (uint16_t)~(CR_LCAM | CR_RRRA | CR_TXP | CR_HTX);
			reg[CR] |= CR_RST | CR_RXDIS;
		} else
		{
			uint16_t cmd = data & regmask[r];
			if (reg[CR] & CR_TXP) cmd &= (uint16_t)~CR_TXP;
			reg[r] |= data & regmask[r];
			command(cmd);
		}
		break;

	case RCR:
		reg[r] = (uint16_t)((reg[r] & ~regmask[r]) | (data & regmask[r]));
		break;

	case IMR:
		reg[r] = (uint16_t)((reg[r] & ~regmask[r]) | (data & regmask[r]));
		break;

	case ISR:
	{
		if (isr_local)
		{
			uint16_t was = reg[r];
			reg[r] &= (uint16_t)~(data & regmask[r]);
			if (was & data & ISR_RBE) read_rra(0);
			break;
		}
		data &= isr_seen;
		isr_seen &= (uint16_t)~data;
		if ((reg[r] & ISR_RBE) && (data & ISR_RBE))
			read_rra(0);
		uint16_t was = reg[r];
		reg[r] &= (uint16_t)~(data & regmask[r]);
		if ((was & data & ISR_PKTRX) && pktrx_last_us
		    && model_us - pktrx_last_us < ISR_REDELIVER_US)
		{
			reg[r] |= ISR_PKTRX;
			pktrx_last_us = 0;
		}
		if ((was & data & ISR_TXDN) && txdn_last_us
		    && model_us - txdn_last_us < ISR_REDELIVER_US)
		{
			reg[r] |= ISR_TXDN;
			txdn_last_us = 0;
		}
		break;
	}

	case CRCT:
	case FAET:
	case MPT:
		reg[r] = (uint16_t)~data;
		break;

	default:
		if (regmask[r])
			reg[r] = (uint16_t)((reg[r] & ~regmask[r]) | (data & regmask[r]));
		break;
	}
}
