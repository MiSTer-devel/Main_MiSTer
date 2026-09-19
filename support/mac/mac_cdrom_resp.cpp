#include <string.h>
#include "mac_cdrom_resp.h"

#define LBW_MASK 0xFFFFFu

uint8_t mac_cd_bin2bcd(uint8_t v)
{
	unsigned t = ((unsigned)v * 205u >> 11) & 0x1F;
	unsigned u = ((unsigned)v - t * 10u) & 0xFF;
	return (uint8_t)(((t & 15) << 4) | (u & 15));
}

uint8_t mac_cd_bcd2bin(uint8_t b)
{
	return (uint8_t)(((b >> 4) * 10u + (b & 15)) & 0xFF);
}

void mac_cd_lba2msf(uint32_t lba, uint8_t *m, uint8_t *s, uint8_t *f)
{
	uint32_t v = lba & LBW_MASK;
	unsigned mm;
	if (v >= 99u * 4500u) { mm = 99; v -= 99u * 4500u; }
	else                  { mm = v / 4500u; v %= 4500u; }
	unsigned st = v / 75u;
	v -= st * 75u;
	*m = (uint8_t)mm;
	*s = (uint8_t)(st & 0x7F);
	*f = (uint8_t)(v & 0xFF);
}

static int clamp_n(const mac_cd_toc *t)
{
	if (t->n <= 0) return 1;
	if (t->n > MAC_CD_MAX_TRACKS) return MAC_CD_MAX_TRACKS;
	return t->n;
}

int mac_cd_resp_inquiry(uint8_t *out)
{
	static const uint8_t inq[54] = {
		0x05, 0x80, 0x02, 0x02, 0x31, 0x00, 0x00, 0x00,
		'S', 'O', 'N', 'Y', ' ', ' ', ' ', ' ',
		'C', 'D', '-', 'R', 'O', 'M', ' ', 'C',
		'D', 'U', '-', '8', '0', '0', '4', ' ',
		'1', '.', '9', 'a', 0x00, 0x00, 0x00, 0xd0,
		0x90, 0x27, 0x3e, 0x01, 0x04, 0x91, 0x00, 0x18,
		0x06, 0xf0, 0xfe, 0x00, 0x00, 0x00
	};
	memset(out, 0, 512);
	memcpy(out, inq, sizeof(inq));
	return 54;
}

int mac_cd_resp_mode_sense(uint8_t page, uint32_t last_lba, const uint8_t ports[4], uint8_t *out)
{
	memset(out, 0, 512);
	int len = (page == 0x30) ? 36 : (page == 0x0E) ? 28 : (page == 0x2A) ? 38 : 12;
	out[0]  = (uint8_t)(len - 1);
	out[2]  = 0x80;
	out[3]  = 8;
	out[5]  = (uint8_t)(last_lba >> 16);
	out[6]  = (uint8_t)(last_lba >> 8);
	out[7]  = (uint8_t)last_lba;
	out[10] = 0x08;
	if (page == 0x30)
	{
		out[12] = 0x30;
		memcpy(out + 14, "APPLE COMPUTER, INC   ", 22);
	}
	else if (page == 0x0E)
	{
		out[12] = 0x0E; out[13] = 0x0E; out[14] = 0x04;
		out[18] = 75;   out[19] = 75;
		out[20] = ports[0]; out[21] = ports[1];
		out[22] = ports[2]; out[23] = ports[3];
	}
	else if (page == 0x2A)
	{
		out[12] = 0x2A; out[13] = 0x18; out[16] = 0x71;
		out[18] = 0x28; out[19] = 0x03; out[22] = 0x01;
	}
	return len;
}

void mac_cd_table_c1(const mac_cd_toc *t, uint8_t *tab)
{
	int n = clamp_n(t);
	uint8_t m, s, f;
	memset(tab, 0, 404);
	tab[0] = 0x01;
	tab[1] = mac_cd_bin2bcd((uint8_t)n);
	mac_cd_lba2msf(t->leadout, &m, &s, &f);
	tab[4] = mac_cd_bin2bcd(m); tab[5] = mac_cd_bin2bcd(s); tab[6] = mac_cd_bin2bcd(f);
	for (int k = 0; k < 99; k++)
	{
		int i = (k < n) ? k : n - 1;
		mac_cd_lba2msf(t->start[i], &m, &s, &f);
		uint8_t *e = tab + 8 + 4 * k;
		e[0] = t->ctrl[i];
		e[1] = mac_cd_bin2bcd(m); e[2] = mac_cd_bin2bcd(s); e[3] = mac_cd_bin2bcd(f);
	}
}

int mac_cd_resp_toc_c1(const mac_cd_toc *t, uint8_t cdb9, uint8_t cdb5, uint8_t *out)
{
	uint8_t tab[404];
	mac_cd_table_c1(t, tab);
	memset(out, 0, 512);

	unsigned trk = mac_cd_bcd2bin(cdb5);
	unsigned k   = (trk == 0) ? 0 : (trk > 99) ? 98 : trk - 1;
	unsigned base = ((cdb9 >> 6) == 1) ? 4 : ((cdb9 >> 6) == 2) ? (8 + 4 * k) : 0;
	int len = (cdb9 & 0x80) ? 400 : 4;
	for (int i = 0; i < len; i++)
	{
		unsigned raw  = base + (unsigned)i;
		unsigned addr = (raw < 404) ? raw : (400 + (raw & 3));
		out[i] = tab[addr];
	}
	return len;
}

int mac_cd_table_43(const mac_cd_toc *t, uint8_t *tab)
{
	int n = clamp_n(t);
	int nreal = (n > 60) ? 60 : n;
	int dlen = (nreal + 1) * 8 + 2;
	uint8_t m, s, f;
	memset(tab, 0, 512);
	tab[0] = (uint8_t)(dlen >> 8);
	tab[1] = (uint8_t)dlen;
	tab[2] = 0x01;
	tab[3] = (uint8_t)n;
	uint8_t ctrl = 0x14;
	for (int k = 0; k < nreal; k++)
	{
		uint8_t *r = tab + 4 + 8 * k;
		ctrl = t->ctrl[k];
		mac_cd_lba2msf((t->start[k] + 150u) & LBW_MASK, &m, &s, &f);
		r[1] = ctrl; r[2] = (uint8_t)(k + 1);
		r[5] = m; r[6] = s; r[7] = f;
	}
	uint8_t *r = tab + 4 + 8 * nreal;
	mac_cd_lba2msf((t->leadout + 150u) & LBW_MASK, &m, &s, &f);
	r[1] = ctrl; r[2] = 0xAA;
	r[5] = m; r[6] = s; r[7] = f;
	return dlen + 2;
}

int mac_cd_table_2(const mac_cd_toc *t, uint8_t *tab)
{
	int n = clamp_n(t);
	int w = (n > 41) ? 41 : n;
	int rows = w + 3;
	int dlen = rows * 11 + 2;
	uint8_t m, s, f;
	memset(tab, 0, 512);
	tab[0] = (uint8_t)(dlen >> 8);
	tab[1] = (uint8_t)dlen;
	tab[2] = 0x01;
	tab[3] = 0x01;
	uint8_t fctrl = t->ctrl[0];

	uint8_t *a0 = tab + 4;
	a0[0] = 0x01; a0[1] = fctrl; a0[3] = 0xA0; a0[8] = 0x01;
	uint8_t *a1 = tab + 15;
	a1[0] = 0x01; a1[1] = fctrl; a1[3] = 0xA1; a1[8] = mac_cd_bin2bcd((uint8_t)w);
	uint8_t *a2 = tab + 26;
	mac_cd_lba2msf((t->leadout + 150u) & LBW_MASK, &m, &s, &f);
	a2[0] = 0x01; a2[1] = fctrl; a2[3] = 0xA2;
	a2[8] = mac_cd_bin2bcd(m); a2[9] = mac_cd_bin2bcd(s); a2[10] = mac_cd_bin2bcd(f);

	uint8_t fm = 0, fs = 0, ff = 0;
	for (int k = 0; k < w; k++)
	{
		uint8_t *r = tab + 37 + 11 * k;
		mac_cd_lba2msf((t->start[k] + 150u) & LBW_MASK, &m, &s, &f);
		r[0] = 0x01; r[1] = t->ctrl[k]; r[3] = (uint8_t)(k + 1);
		r[8] = mac_cd_bin2bcd(m); r[9] = mac_cd_bin2bcd(s); r[10] = mac_cd_bin2bcd(f);
		if (k == 0) { fm = m; fs = s; ff = f; }
	}

	uint8_t *sp = tab + 496;
	sp[1] = 0x0A; sp[2] = 0x01; sp[3] = 0x01; sp[5] = fctrl; sp[6] = 0x01;
	sp[9] = fm; sp[10] = fs; sp[11] = ff;
	return dlen + 2;
}

int mac_cd_resp_toc_43(const mac_cd_toc *t, uint8_t cdb9, uint8_t cdb6, uint8_t *out)
{
	uint8_t tab[512];
	memset(out, 0, 512);
	unsigned fmt = cdb9 >> 6;
	if (fmt == 2)
	{
		int len = mac_cd_table_2(t, tab);
		memcpy(out, tab, len);
	}
	else if (fmt == 1)
	{
		mac_cd_table_2(t, tab);
		memcpy(out, tab + 496, 12);
	}
	else
	{
		int len = mac_cd_table_43(t, tab);
		unsigned nreal = (unsigned)(len - 14) / 8 + 1;
		unsigned soff  = (cdb6 == 0x00 || cdb6 == 0x01) ? 0 :
		                 (cdb6 == 0xAA) ? nreal :
		                 (cdb6 > nreal) ? nreal : (unsigned)cdb6 - 1;
		unsigned flen = (1 + nreal - soff) * 8 + 2;
		unsigned tot  = flen + 2;
		out[0] = (uint8_t)(flen >> 8);
		out[1] = (uint8_t)flen;
		out[2] = tab[2];
		out[3] = tab[3];
		for (unsigned i = 4; i < tot && i < 512; i++)
		{
			unsigned addr = 4 + soff * 8 + (i - 4);
			out[i] = (addr < 512) ? tab[addr] : 0;
		}
	}
	return 512;
}

static uint8_t ast_std(uint8_t ast)
{
	return (ast == 0) ? 0x11 : (ast == 1) ? 0x12 : 0x13;
}

int mac_cd_resp_subch(const mac_cd_pos *p, uint8_t cdb3, uint8_t cdb6, uint8_t *out)
{
	memset(out, 0, 512);
	out[1] = ast_std(p->ast);
	if (cdb3 == 0x02)      { out[3] = 20; out[4] = 0x02; }
	else if (cdb3 == 0x03) { out[3] = 20; out[4] = 0x03; out[6] = cdb6; }
	else
	{
		out[3] = 12; out[4] = 0x01;
		out[5] = p->ctrl; out[6] = p->trk; out[7] = 0x01;
		out[9] = p->abs_m; out[10] = p->abs_s; out[11] = p->abs_f;
		out[13] = p->rel_m; out[14] = p->rel_s; out[15] = p->rel_f;
	}
	return 64;
}

int mac_cd_resp_subq(const mac_cd_pos *p, uint8_t *out)
{
	memset(out, 0, 512);
	out[0] = p->ctrl;
	out[1] = mac_cd_bin2bcd(p->trk);
	out[2] = 0x01;
	out[3] = mac_cd_bin2bcd(p->rel_m); out[4] = mac_cd_bin2bcd(p->rel_s); out[5] = mac_cd_bin2bcd(p->rel_f);
	out[6] = mac_cd_bin2bcd(p->abs_m); out[7] = mac_cd_bin2bcd(p->abs_s); out[8] = mac_cd_bin2bcd(p->abs_f);
	return 9;
}

int mac_cd_resp_astat(const mac_cd_pos *p, uint8_t cdb3, uint8_t *out)
{
	memset(out, 0, 512);
	out[0] = (cdb3 == 1) ? 0xFF : p->ast;
	out[1] = (cdb3 == 1) ? 0xFF : 0x00;
	out[2] = p->ctrl;
	out[3] = mac_cd_bin2bcd(p->abs_m); out[4] = mac_cd_bin2bcd(p->abs_s); out[5] = mac_cd_bin2bcd(p->abs_f);
	return 6;
}

int mac_cd_has_data(const mac_cd_toc *t)
{
	int n = clamp_n(t);
	int w = (n > 41) ? 41 : n;
	for (int k = 0; k < w; k++) if (t->ctrl[k] & 0x04) return 1;
	return 0;
}

void mac_cd_build_blob(const mac_cd_toc *t, uint8_t *b)
{
	int n = clamp_n(t);
	int has_data = 0, has_audio = 0;
	memset(b, 0, 1024);
	memcpy(b, "MCDA", 4);
	b[4] = 2;
	b[5] = 1;
	b[6] = (uint8_t)n;
	b[7] = (uint8_t)(t->data_trk + 1);
	b[8]  = (uint8_t)t->leadout;         b[9]  = (uint8_t)(t->leadout >> 8);
	b[10] = (uint8_t)(t->leadout >> 16); b[11] = (uint8_t)(t->leadout >> 24);
	for (int i = 0; i < n; i++)
	{
		uint8_t *e = b + 16 + 8 * i;
		e[0] = t->ctrl[i];
		e[2] = (uint8_t)t->start[i];         e[3] = (uint8_t)(t->start[i] >> 8);
		e[4] = (uint8_t)(t->start[i] >> 16); e[5] = (uint8_t)(t->start[i] >> 24);
		uint32_t pg = (t->pregap[i] > 0xFFFF) ? 0xFFFF : t->pregap[i];
		e[6] = (uint8_t)pg; e[7] = (uint8_t)(pg >> 8);
		if (t->ctrl[i] & 0x04) has_data = 1; else has_audio = 1;
	}
	b[12] = (uint8_t)((has_data ? 1 : 0) | (has_audio ? 2 : 0));
}
