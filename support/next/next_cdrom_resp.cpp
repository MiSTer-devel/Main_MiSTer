#include <string.h>
#include "next_cdrom_resp.h"

#define LBW_MASK 0xFFFFFu

void next_cd_lba2msf(uint32_t lba, uint8_t *m, uint8_t *s, uint8_t *f)
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

void next_cd_put_addr(uint8_t *out, uint32_t lba, int msf)
{
	if (msf)
	{
		out[0] = 0;
		next_cd_lba2msf((lba + 150u) & LBW_MASK, out + 1, out + 2, out + 3);
	}
	else
	{
		out[0] = (uint8_t)(lba >> 24); out[1] = (uint8_t)(lba >> 16);
		out[2] = (uint8_t)(lba >> 8);  out[3] = (uint8_t)lba;
	}
}

static int clamp_n(const next_cd_toc *t)
{
	if (t->n <= 0) return 1;
	if (t->n > NEXT_CD_MAX_TRACKS) return NEXT_CD_MAX_TRACKS;
	return t->n;
}

static int toc_fmt0(const next_cd_toc *t, int msf, uint8_t track, uint8_t *out)
{
	int n = clamp_n(t);
	unsigned first = (track == 0 || track == 1) ? 0 : (track == 0xAA) ? (unsigned)n : (track > (unsigned)n) ? (unsigned)n : (unsigned)track - 1;
	int rows = (int)(n - first) + 1;
	int dlen = rows * 8 + 2;
	out[0] = (uint8_t)(dlen >> 8);
	out[1] = (uint8_t)dlen;
	out[2] = 1;
	out[3] = (uint8_t)n;
	uint8_t *r = out + 4;
	for (unsigned k = first; k < (unsigned)n; k++, r += 8)
	{
		r[1] = t->ctrl[k];
		r[2] = (uint8_t)(k + 1);
		next_cd_put_addr(r + 4, t->start[k], msf);
	}
	r[1] = t->ctrl[n - 1];
	r[2] = 0xAA;
	next_cd_put_addr(r + 4, t->leadout, msf);
	return dlen + 2;
}

static int toc_fmt1(const next_cd_toc *t, int msf, uint8_t *out)
{
	out[1] = 10;
	out[2] = 1;
	out[3] = 1;
	out[5] = t->ctrl[0];
	out[6] = 1;
	next_cd_put_addr(out + 8, t->start[0], msf);
	return 12;
}

static void raw_row(uint8_t *r, uint8_t ctrl, uint8_t point, uint32_t lba, int is_lba)
{
	uint8_t m, s, f;
	r[0] = 1;
	r[1] = ctrl;
	r[3] = point;
	if (is_lba) next_cd_lba2msf((lba + 150u) & LBW_MASK, &m, &s, &f);
	else { m = (uint8_t)(lba >> 16); s = (uint8_t)(lba >> 8); f = (uint8_t)lba; }
	r[8] = m; r[9] = s; r[10] = f;
}

static int toc_fmt2(const next_cd_toc *t, uint8_t *out)
{
	int n = clamp_n(t);
	int w = (n > 41) ? 41 : n;
	int rows = w + 3;
	int dlen = rows * 11 + 2;
	out[0] = (uint8_t)(dlen >> 8);
	out[1] = (uint8_t)dlen;
	out[2] = 1;
	out[3] = 1;
	raw_row(out + 4,  t->ctrl[0],     0xA0, ((uint32_t)1 << 16) | ((uint32_t)(next_cd_has_data(t) ? 0 : 0) << 8), 0);
	raw_row(out + 15, t->ctrl[0],     0xA1, (uint32_t)w << 16, 0);
	raw_row(out + 26, t->ctrl[n - 1], 0xA2, t->leadout, 1);
	for (int k = 0; k < w; k++)
		raw_row(out + 37 + 11 * k, t->ctrl[k], (uint8_t)(k + 1), t->start[k], 1);
	return dlen + 2;
}

int next_cd_resp_toc(const next_cd_toc *t, int msf, unsigned fmt, uint8_t track, uint8_t *out)
{
	memset(out, 0, 512);
	if (fmt == 2) return toc_fmt2(t, out);
	if (fmt == 1) return toc_fmt1(t, msf, out);
	return toc_fmt0(t, msf, track, out);
}

static uint8_t ast_std(uint8_t ast)
{
	return (ast == 0) ? 0x11 : (ast == 1) ? 0x12 : (ast == 3) ? 0x13 : 0x15;
}

int next_cd_resp_subch(const next_cd_pos *p, int msf, int subq, uint8_t fmt, uint8_t *out)
{
	memset(out, 0, 512);
	out[1] = ast_std(p->ast);
	if (!subq) return 4;
	if (fmt == 0x02 || fmt == 0x03)
	{
		out[3] = 20;
		out[4] = fmt;
		return 24;
	}
	out[3] = 12;
	out[4] = 0x01;
	out[5] = p->ctrl;
	out[6] = p->trk;
	out[7] = 0x01;
	next_cd_put_addr(out + 8, p->abs_lba, msf);
	if (msf)
	{
		out[12] = 0;
		next_cd_lba2msf(p->rel_lba, out + 13, out + 14, out + 15);
	}
	else next_cd_put_addr(out + 12, p->rel_lba, 0);
	return 16;
}

int next_cd_has_data(const next_cd_toc *t)
{
	int n = clamp_n(t);
	for (int k = 0; k < n; k++) if (t->ctrl[k] & 0x04) return 1;
	return 0;
}
