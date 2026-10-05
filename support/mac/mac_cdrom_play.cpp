#include <string.h>
#include <math.h>
#include "mac_cdrom_play.h"

#define L20(x) ((uint32_t)(x) & 0xFFFFFu)

static uint32_t cdb_lba(const uint8_t *c)
{
	if (c[2] != 0 || (c[3] & 0xF0)) return 0xFFFFF;
	return ((uint32_t)(c[3] & 15) << 16) | ((uint32_t)c[4] << 8) | c[5];
}

static uint32_t msf2lba_bcd(uint8_t m, uint8_t s, uint8_t f)
{
	return L20((uint32_t)mac_cd_bcd2bin(m) * 4500u + (uint32_t)mac_cd_bcd2bin(s) * 75u + mac_cd_bcd2bin(f));
}

static uint32_t msf2lba_std(uint8_t m, uint8_t s, uint8_t f)
{
	uint32_t v = (uint32_t)m * 4500u + (uint32_t)s * 75u + f;
	return L20((v >= 150) ? v - 150 : 0);
}

static int toc_n(const mac_cd_play *p)
{
	if (!p->toc) return 1;
	if (p->toc->n <= 0) return 1;
	if (p->toc->n > MAC_CD_MAX_TRACKS) return MAC_CD_MAX_TRACKS;
	return p->toc->n;
}

static uint32_t track_start(const mac_cd_play *p, unsigned k)
{
	int n = toc_n(p);
	if (!p->toc) return 0;
	return L20(p->toc->start[(k < (unsigned)n) ? k : (unsigned)(n - 1)]);
}

static uint32_t leadout(const mac_cd_play *p)
{
	return p->toc ? L20(p->toc->leadout) : 0;
}

static void flush(mac_cd_play *p)
{
	p->fetch = p->cur;
	p->flush_gen++;
}

void mac_cd_play_init(mac_cd_play *p)
{
	const mac_cd_toc *t = p->toc;
	memset(p, 0, sizeof(*p));
	p->toc = t;
	p->state = MAC_CD_ST_IDLE;
	p->ports[0] = 0x01; p->ports[1] = 0xFF;
	p->ports[2] = 0x02; p->ports[3] = 0xFF;
}

void mac_cd_play_set_toc(mac_cd_play *p, const mac_cd_toc *t)
{
	p->toc = t;
	p->state = MAC_CD_ST_IDLE;
	p->scan = 0;
}

void mac_cd_play_stop(mac_cd_play *p)
{
	if (p->state != MAC_CD_ST_IDLE)
	{
		p->state = MAC_CD_ST_IDLE;
		p->scan = 0;
	}
}

static void mode_select(mac_cd_play *p, const uint8_t *list, int list_len)
{
	if (list_len < 4) return;
	unsigned bd = list[3];
	if (bd != 0)
	{
		if (bd + 4 > (unsigned)list_len) return;
		if (list[10] != 0x08 || list[11] != 0x00) return;
	}
	unsigned base = bd ? 12 : 4;
	if (base + 2 > (unsigned)list_len) return;
	if (list[base] != 0x0E) return;
	p->ports[0] = list[base + 8];
	p->ports[1] = list[base + 9];
	p->ports[2] = list[base + 10];
	p->ports[3] = list[base + 11];
}

static void track_range(const mac_cd_play *p, unsigned trk, unsigned trk2, uint32_t *addr, uint32_t *next)
{
	*addr = track_start(p, trk);
	*next = (trk2 >= (unsigned)toc_n(p)) ? leadout(p) : track_start(p, trk2);
}

static void apply_range(mac_cd_play *p, uint32_t addr, uint32_t next)
{
	if (addr == next)
	{
		p->cur = addr;
		flush(p);
	}
	else
	{
		p->cur  = addr;
		p->stop = next;
		flush(p);
		p->state = (addr < next) ? MAC_CD_ST_PLAY : MAC_CD_ST_IDLE;
	}
}

void mac_cd_play_command(mac_cd_play *p, const uint8_t *c, const uint8_t *list, int list_len)
{
	uint8_t op = c[0];
	unsigned form = c[9] >> 6;
	uint32_t addr, next;

	if (op == 0x15) { mode_select(p, list, list_len); return; }

	if (op != 0xCD) p->scan = 0;

	switch (op)
	{
	case 0xCA:
		if (c[1] == 0x10) { if (p->state == MAC_CD_ST_PLAY) p->state = MAC_CD_ST_PAUSE; }
		else if (p->state == MAC_CD_ST_PAUSE) p->state = MAC_CD_ST_PLAY;
		break;

	case 0xCD:
		addr = (form == 0) ? cdb_lba(c) :
		       (form == 1) ? msf2lba_std(c[3], c[4], c[5]) : p->cur;
		p->cur = addr;
		flush(p);
		p->scan = 1;
		p->scan_dir = (c[1] & 0x10) ? 1 : 0;
		if (!p->scan_dir && p->stop <= addr) p->stop = leadout(p);
		p->state = MAC_CD_ST_PLAY;
		break;

	case 0xC8: case 0xC9: case 0xCB:
		if (form == 1)
		{
			addr = (op == 0xC9) ? msf2lba_bcd(c[3], c[4], c[5]) : msf2lba_bcd(c[5], c[6], c[7]);
			next = 0;
		}
		else if (form == 2)
		{
			unsigned t = mac_cd_bcd2bin(c[5]);
			if (t == 0)
			{
				if (op != 0xCB) p->state = MAC_CD_ST_IDLE;
				return;
			}
			unsigned trk  = (t - 1 < 99) ? t - 1 : 98;
			unsigned trk2 = (t < 99) ? t : 99;
			track_range(p, trk, trk2, &addr, &next);
		}
		else
		{
			addr = cdb_lba(c);
			next = 0;
		}
		if (op == 0xCB)
		{
			uint32_t e = (form == 2) ? next : addr;
			p->stop = e;
			if ((p->state == MAC_CD_ST_PLAY || p->state == MAC_CD_ST_PAUSE) && p->cur >= e)
				p->state = MAC_CD_ST_IDLE;
		}
		else if (op == 0xC9)
		{
			if (c[1] & 0x10)
			{
				p->stop = addr;
				if (p->cur < addr) p->state = MAC_CD_ST_PLAY;
			}
			else
			{
				p->cur = addr;
				flush(p);
				p->state = MAC_CD_ST_PLAY;
				if (form == 2) p->stop = leadout(p);
				else if (p->stop <= addr) p->stop = leadout(p);
			}
		}
		else
		{
			p->cur = addr;
			flush(p);
			if (form == 2) p->stop = next;
			else if (p->stop <= addr) p->stop = leadout(p);
			p->state = (c[1] & 0x10) ? MAC_CD_ST_PLAY : MAC_CD_ST_PAUSE;
		}
		break;

	case 0x47:
		addr = (c[3] == 0xFF && c[4] == 0xFF && c[5] == 0xFF) ? p->cur : msf2lba_std(c[3], c[4], c[5]);
		next = msf2lba_std(c[6], c[7], c[8]);
		apply_range(p, addr, next);
		break;

	case 0x48:
		if (c[4] == 0) { p->state = MAC_CD_ST_IDLE; break; }
		{
			unsigned trk  = ((unsigned)c[4] - 1 < 99) ? (unsigned)c[4] - 1 : 98;
			unsigned trk2 = (c[7] < 99) ? (c[7] & 0x7F) : 99;
			track_range(p, trk, trk2, &addr, &next);
			apply_range(p, addr, next);
		}
		break;

	case 0x45: case 0xA5:
	{
		int ff = (c[2] == 0xFF && c[3] == 0xFF && c[4] == 0xFF && c[5] == 0xFF);
		addr = ff ? p->cur : cdb_lba(c);
		uint32_t len = (op == 0x45) ? (((uint32_t)c[7] << 8) | c[8])
		                            : (((uint32_t)c[6] << 24) | ((uint32_t)c[7] << 16) | ((uint32_t)c[8] << 8) | c[9]);
		next = L20(addr + len);
		apply_range(p, addr, next);
		break;
	}

	case 0x4B:
		if (c[8] & 1) { if (p->state == MAC_CD_ST_PAUSE) p->state = MAC_CD_ST_PLAY; }
		else if (p->state == MAC_CD_ST_PLAY) p->state = MAC_CD_ST_PAUSE;
		break;

	case 0x4E: case 0x01: case 0x0B: case 0x2B:
		if (p->state != MAC_CD_ST_IDLE) p->state = MAC_CD_ST_IDLE;
		break;

	default:
		break;
	}
}

int mac_cd_play_frame(mac_cd_play *p, uint32_t *lba)
{
	if (p->state != MAC_CD_ST_PLAY || !p->toc) return 0;
	if (!(p->fetch < p->stop)) return 0;
	*lba = p->fetch;
	p->fetch = L20(p->fetch + 1);

	uint32_t step = p->scan ? 8 : 1;
	if (p->scan && p->scan_dir)
	{
		p->cur = (p->cur > 8) ? p->cur - 8 : 0;
	}
	else if (p->cur + step >= p->stop)
	{
		p->cur   = p->stop;
		p->state = MAC_CD_ST_END;
		p->scan  = 0;
	}
	else p->cur = L20(p->cur + step);
	return 1;
}

uint8_t mac_cd_play_ast(const mac_cd_play *p)
{
	switch (p->state)
	{
	case MAC_CD_ST_PLAY:  return 0x00;
	case MAC_CD_ST_PAUSE: return 0x01;
	case MAC_CD_ST_END:   return 0x03;
	default:              return 0x05;
	}
}

void mac_cd_play_pos(const mac_cd_play *p, mac_cd_pos *o)
{
	uint32_t ref_abs = p->cur;
	uint32_t best_start = 0;
	unsigned best_trk = 0;
	uint8_t  best_ctrl = 0x14;
	if (p->toc)
	{
		int n = toc_n(p);
		for (int k = 0; k < n; k++)
		{
			uint32_t st = L20(p->toc->start[k]);
			if (st <= ref_abs) { best_start = st; best_trk = (unsigned)k; best_ctrl = p->toc->ctrl[k]; }
		}
	}
	o->ast  = mac_cd_play_ast(p);
	o->ctrl = best_ctrl;
	o->trk  = (uint8_t)(best_trk + 1);
	mac_cd_lba2msf(ref_abs, &o->abs_m, &o->abs_s, &o->abs_f);
	mac_cd_lba2msf(L20(ref_abs - best_start), &o->rel_m, &o->rel_s, &o->rel_f);
}

uint16_t mac_cd_vol_gain(uint8_t v)
{
	double g = 32768.0 * pow((double)v / 255.0, 5.0);
	long r = lround(g);
	if (r < 0) r = 0;
	if (r > 32768) r = 32768;
	return (uint16_t)r;
}

void mac_cd_play_scale(const mac_cd_play *p, int16_t *pcm, int stereo_samples)
{
	uint8_t ch0 = p->ports[0], ch1 = p->ports[2];
	int32_t gl = mac_cd_vol_gain(p->ports[1]);
	int32_t gr = mac_cd_vol_gain(p->ports[3]);
	for (int i = 0; i < stereo_samples; i++)
	{
		int32_t l = pcm[2 * i], r = pcm[2 * i + 1];
		int32_t sl = (ch0 == 0x01) ? l : (ch0 == 0x02) ? r : 0;
		int32_t sr = (ch1 == 0x02) ? r : (ch1 == 0x01) ? l : 0;
		pcm[2 * i]     = (int16_t)((sl * gl) >> 15);
		pcm[2 * i + 1] = (int16_t)((sr * gr) >> 15);
	}
}
