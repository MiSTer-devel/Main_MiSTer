// Atari Falcon030 core: HPS side of the SCSI targets -- see falcon_scsi.h.

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdint.h>

#include "../../user_io.h"
#include "../../spi.h"
#include "../../file_io.h"
#include "falcon_scsi.h"

// ---------------------------------------------------------------------------
// state

#define FCD_MAX_TRACKS 99

struct fcd_track
{
	int      file;       // index into cd_files[]
	int      audio;
	uint32_t start;      // disc LBA of INDEX 01
	uint32_t base;       // disc LBA of frame 0 of its file
	uint32_t fend;       // disc LBA where its file ends
	uint32_t ss;         // sector size in the file
	uint32_t doff;       // user data offset inside a sector
};

struct fcd_disc
{
	int       active;     // translated image (cue / raw bin) on the CD slot
	int       ntrk;
	fcd_track trk[FCD_MAX_TRACKS];
	int       nfiles;
	uint32_t  leadout;
};

static fcd_disc        cd;
static fileTYPE        cd_files[FCD_MAX_TRACKS];
static fileTYPE       *unit_img[FALCON_SCSI_UNITS];
static int             unit_ro[FALCON_SCSI_UNITS];
static falcon_cd_audio audio[FALCON_SCSI_UNITS];

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint32_t get16(const uint8_t *p) { return ((uint32_t)p[0] << 8) | p[1]; }
static uint32_t get32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static int slot_unit(int index)
{
	int u = index - FALCON_SCSI_SLOT0;
	return (u >= 0 && u < FALCON_SCSI_UNITS) ? u : -1;
}

static uint64_t image_size(int unit)
{
	fileTYPE *f = unit_img[unit];
	return f ? (uint64_t)f->size : 0;
}

static void cd_close(void)
{
	for (int i = 0; i < cd.nfiles; i++)
		if (cd_files[i].opened()) FileClose(&cd_files[i]);
	memset(&cd, 0, sizeof(cd));
}

// ---------------------------------------------------------------------------
// CD image translation (cue + bin, raw MODE1/2352 bin)

static uint32_t msf_frames(const char *s)
{
	int m = 0, sec = 0, f = 0;
	if (sscanf(s, "%d:%d:%d", &m, &sec, &f) != 3) return 0;
	return (uint32_t)(m * 60 + sec) * 75 + f;
}

static const char *skip_ws(const char *p)
{
	while (*p == ' ' || *p == '\t') p++;
	return p;
}

static uint32_t data_offset(int mode2, uint32_t ss)
{
	if (ss == 2048) return 0;
	if (ss == 2336) return 8;
	return mode2 ? 24 : 16;
}

// close out the current FILE: its frame count gives the next file's base
static uint32_t file_frames(int file, uint32_t ss)
{
	if (file < 0 || !ss) return 0;
	return (uint32_t)(cd_files[file].size / ss);
}

static int mount_cue(const char *name)
{
	fileTYPE cue;
	if (!FileOpen(&cue, name)) return 0;
	int sz = cue.size > 65535 ? 65535 : (int)cue.size;
	char *txt = (char *)malloc(sz + 1);
	if (!txt) { FileClose(&cue); return 0; }
	sz = FileReadAdv(&cue, txt, sz);
	FileClose(&cue);
	if (sz < 0) sz = 0;
	txt[sz] = 0;

	int      file = -1;
	uint32_t file_ss = 0;       // sector size of the current FILE's first track
	uint32_t base = 0;          // disc LBA of the current FILE's frame 0
	uint32_t synth = 0;         // PREGAP (not in the file) frames so far
	int      cur_audio = -1, cur_mode2 = 0;
	uint32_t cur_ss = 0;
	int      bad = 0;

	char *save = 0;
	for (char *line = strtok_r(txt, "\r\n", &save); line && !bad; line = strtok_r(0, "\r\n", &save))
	{
		const char *p = skip_ws(line);
		if (!strncasecmp(p, "FILE", 4))
		{
			const char *q1 = strchr(p, '"');
			const char *q2 = q1 ? strchr(q1 + 1, '"') : 0;
			if (!q1 || !q2 || q2 == q1 + 1 || strcasestr(q2 + 1, "WAVE") || strcasestr(q2 + 1, "MP3"))
			{
				printf("Falcon CD: unsupported cue FILE: %s\n", p);
				bad = 1;
				break;
			}
			if (cd.nfiles >= FCD_MAX_TRACKS) { bad = 1; break; }
			if (file >= 0) base += file_frames(file, file_ss);
			char path[1024];
			const char *slash = strrchr(name, '/');
			int dl = slash ? (int)(slash - name) + 1 : 0;
			snprintf(path, sizeof(path), "%.*s%.*s", dl, name, (int)(q2 - q1 - 1), q1 + 1);
			file = cd.nfiles;
			if (!FileOpen(&cd_files[file], path))
			{
				printf("Falcon CD: cue references a missing file: %s\n", path);
				bad = 1;
				break;
			}
			cd.nfiles++;
			file_ss = 0;
		}
		else if (!strncasecmp(p, "TRACK", 5))
		{
			cur_audio = -1;
			if (strcasestr(p, "AUDIO"))           { cur_audio = 1; cur_ss = 2352; cur_mode2 = 0; }
			else if (strcasestr(p, "MODE1/2352")) { cur_audio = 0; cur_ss = 2352; cur_mode2 = 0; }
			else if (strcasestr(p, "MODE1/2048")) { cur_audio = 0; cur_ss = 2048; cur_mode2 = 0; }
			else if (strcasestr(p, "MODE2/2352")) { cur_audio = 0; cur_ss = 2352; cur_mode2 = 1; }
			else if (strcasestr(p, "MODE2/2336")) { cur_audio = 0; cur_ss = 2336; cur_mode2 = 1; }
			else
			{
				printf("Falcon CD: unsupported cue TRACK: %s\n", p);
				bad = 1;
			}
			if (file >= 0 && !file_ss) file_ss = cur_ss;
		}
		else if (!strncasecmp(p, "PREGAP", 6))
		{
			synth += msf_frames(skip_ws(p + 6));
		}
		else if (!strncasecmp(p, "INDEX", 5) && cur_audio >= 0 && file >= 0)
		{
			int idx = -1;
			char msf[32] = {};
			if (sscanf(p + 5, "%d %31s", &idx, msf) != 2) continue;
			if (idx != 1) continue;
			if (cd.ntrk >= FCD_MAX_TRACKS) break;
			fcd_track *t = &cd.trk[cd.ntrk++];
			t->file  = file;
			t->audio = cur_audio;
			t->ss    = cur_ss;
			t->doff  = data_offset(cur_mode2, cur_ss);
			t->base  = base + synth;
			t->start = t->base + msf_frames(msf);
		}
	}
	free(txt);

	if (bad || !cd.ntrk)
	{
		if (!bad) printf("Falcon CD: no tracks in %s\n", name);
		cd_close();
		return 0;
	}
	// file ends and the lead-out
	for (int i = 0; i < cd.ntrk; i++)
		cd.trk[i].fend = cd.trk[i].base + file_frames(cd.trk[i].file, cd.trk[i].ss);
	cd.leadout = cd.trk[cd.ntrk - 1].fend;
	cd.active = 1;
	printf("Falcon CD: %s, %d track(s), lead-out %u\n", name, cd.ntrk, cd.leadout);
	return 1;
}

// a .bin without a cue: raw MODE1/MODE2 2352-byte sectors (sync pattern)
static int mount_raw_bin(const char *name)
{
	static const uint8_t sync[12] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
	uint8_t hdr[16];
	if (!FileOpen(&cd_files[0], name)) return 0;
	cd.nfiles = 1;
	if (cd_files[0].size % 2352 || FileReadAdv(&cd_files[0], hdr, 16) != 16 || memcmp(hdr, sync, 12))
	{
		cd_close();
		return 0;
	}
	fcd_track *t = &cd.trk[0];
	t->file  = 0;
	t->audio = 0;
	t->ss    = 2352;
	t->doff  = data_offset(hdr[15] == 2, 2352);
	t->base  = 0;
	t->start = 0;
	t->fend  = (uint32_t)(cd_files[0].size / 2352);
	cd.ntrk = 1;
	cd.leadout = t->fend;
	cd.active = 1;
	printf("Falcon CD: raw bin %s, %u sectors\n", name, cd.leadout);
	return 1;
}

int falcon_scsi_mount_hook(int index, const char *name, fileTYPE *f, int *writable)
{
	if (!is_falcon()) return 1;
	int unit = slot_unit(index);
	if (unit < 0) return 1;

	unit_img[unit] = f;
	unit_ro[unit] = !*writable;
	if (unit != FALCON_SCSI_CD_ID) return 1;

	cd_close();
	memset(&audio[unit], 0, sizeof(audio[unit]));
	audio[unit].status = 0x15;
	*writable = 0;

	int len = strlen(name);
	if (len > 4 && !strcasecmp(name + len - 4, ".cue"))
	{
		if (!mount_cue(name))
		{
			FileClose(f);
			unit_img[unit] = 0;
			return 0;
		}
		f->size = (__off64_t)cd.leadout * 2048;
	}
	else if (len > 4 && !strcasecmp(name + len - 4, ".bin") && mount_raw_bin(name))
	{
		f->size = (__off64_t)cd.leadout * 2048;
	}
	return 1;
}

void falcon_scsi_unmount(int index)
{
	if (!is_falcon()) return;
	int unit = slot_unit(index);
	if (unit < 0) return;
	if (unit == FALCON_SCSI_CD_ID) cd_close();
	unit_img[unit] = 0;
}

int falcon_scsi_cd_fill(int unit, uint32_t lba, uint8_t *blk)
{
	if (unit != FALCON_SCSI_CD_ID || !cd.active) return 0;
	memset(blk, 0, 512);
	uint32_t sec = lba >> 2;
	int i = cd.ntrk - 1;
	while (i > 0 && cd.trk[i].start > sec) i--;
	const fcd_track *t = &cd.trk[i];
	if (sec >= cd.leadout || t->audio || sec < t->base || sec >= t->fend) return 1;
	uint64_t off = (uint64_t)(sec - t->base) * t->ss + t->doff + (lba & 3) * 512;
	if (FileSeek(&cd_files[t->file], off, SEEK_SET))
		FileReadAdv(&cd_files[t->file], blk, 512);
	return 1;
}

// track table of the CD-ROM: the translated disc, else one data track
// covering a flat 2048-byte image
static int disc_tracks(int unit, fcd_track *out, uint32_t *leadout)
{
	if (cd.active)
	{
		memcpy(out, cd.trk, sizeof(fcd_track) * cd.ntrk);
		*leadout = cd.leadout;
		return cd.ntrk;
	}
	uint64_t sz = image_size(unit);
	if (!sz) return 0;
	memset(out, 0, sizeof(fcd_track));
	out[0].ss = 2048;
	*leadout = (uint32_t)(sz / 2048);
	return 1;
}

// ---------------------------------------------------------------------------
// responses

static void put_addr(uint8_t *p, uint32_t lba, int msf)
{
	if (msf)
	{
		uint32_t a = lba + 150;
		p[0] = 0;
		p[1] = (uint8_t)(a / (60 * 75));
		p[2] = (uint8_t)((a / 75) % 60);
		p[3] = (uint8_t)(a % 75);
	}
	else put32(p, lba);
}

static int resp_inquiry(int unit, int flags, int page, uint8_t *b)
{
	int cd_unit = (unit == FALCON_SCSI_CD_ID);
	uint8_t type = (flags & 8) ? 0x7F : (cd_unit ? 0x05 : 0x00);
	if (flags & 1)
	{
		// vital product data: supported pages, unit serial number
		b[0] = type;
		b[1] = (uint8_t)page;
		if (page == 0x00)
		{
			b[3] = 2;
			b[4] = 0x00;
			b[5] = 0x80;
			return 6;
		}
		if (page == 0x80)
		{
			b[3] = 8;
			memcpy(b + 4, "FALCON0", 7);
			b[11] = (uint8_t)('0' + unit);
			return 12;
		}
		return 0;
	}
	b[0] = type;
	b[1] = cd_unit ? 0x80 : 0x00;      // removable medium
	b[2] = 0x02;                       // SCSI-2
	b[3] = 0x02;                       // response data format
	b[4] = 31;
	memcpy(b + 8,  "MiSTer  ", 8);
	memcpy(b + 16, cd_unit ? "Falcon SCSI CD  " : "Falcon SCSI HD  ", 16);
	memcpy(b + 32, "M1.0", 4);
	return 36;
}

// mode pages; pc = page control (0 current, 1 changeable, 2 default)
static int mode_page(int unit, int page, int pc, uint8_t *p)
{
	int cd_unit = (unit == FALCON_SCSI_CD_ID);
	uint64_t blocks = image_size(unit) / (cd_unit ? 2048 : 512);
	int n = 0;
	switch (page)
	{
	case 0x01:      // read/write error recovery
		n = 12;
		p[0] = 0x01; p[1] = 10;
		break;
	case 0x03:      // format device (hard disk)
		if (cd_unit) return 0;
		n = 24;
		p[0] = 0x03; p[1] = 22;
		if (pc != 1)
		{
			put16(p + 10, 63);         // sectors per track
			put16(p + 12, 512);        // bytes per sector
		}
		break;
	case 0x04:      // rigid disk geometry (Hatari HDC_CmdModeSense0x04)
		if (cd_unit) return 0;
		n = 24;
		p[0] = 0x04; p[1] = 22;
		if (pc != 1)
		{
			p[2] = (uint8_t)(blocks >> 23);
			p[3] = (uint8_t)(blocks >> 15);
			p[4] = (uint8_t)(blocks >> 7);
			p[5] = 128;                // heads
			p[20] = 0x1C;              // 7200 rpm
			p[21] = 0x20;
		}
		break;
	case 0x08:      // caching
		if (cd_unit) return 0;
		n = 20;
		p[0] = 0x08; p[1] = 18;
		break;
	case 0x0E:      // CD audio control
		if (!cd_unit) return 0;
		n = 16;
		p[0] = 0x0E; p[1] = 14;
		if (pc != 1)
		{
			p[2] = 0x04;               // immediate
			p[8] = 0x01; p[9] = 0xFF;  // port 0: channel 0, volume
			p[10] = 0x02; p[11] = 0xFF;// port 1: channel 1, volume
		}
		break;
	case 0x2A:      // CD capabilities and mechanical status
		if (!cd_unit) return 0;
		n = 20;
		p[0] = 0x2A; p[1] = 18;
		if (pc != 1)
		{
			p[4] = 0x01;               // audio play
			p[6] = 0x29;               // lock, eject, tray loader
			put16(p + 8, 706);         // 4x
			put16(p + 10, 256);        // volume levels
			put16(p + 12, 64);         // buffer KB
			put16(p + 14, 706);
		}
		break;
	default:
		return 0;
	}
	return n;
}

static int resp_mode_sense(int unit, int ten, int dbd, int arg, uint8_t *b)
{
	int pc = arg >> 6, page = arg & 0x3F;
	int cd_unit = (unit == FALCON_SCSI_CD_ID);
	uint64_t blocks = image_size(unit) / (cd_unit ? 2048 : 512);
	if (pc == 3) return 0;                              // saved values: not supported

	// Hatari HDC_CmdModeSense0x00: vendor page 0 for AHDI's HDX (hard disks)
	if (page == 0x00 && !cd_unit && !ten)
	{
		b[1] = 14;
		b[3] = 8;
		b[5] = (uint8_t)(blocks >> 16);
		b[6] = (uint8_t)(blocks >> 8);
		b[7] = (uint8_t)blocks;
		b[10] = 2;
		return 16;
	}

	int hl = ten ? 8 : 4;
	int n = hl;
	if (!dbd)
	{
		uint8_t *d = b + hl;
		uint32_t nb = blocks > 0xFFFFFF ? 0xFFFFFF : (uint32_t)blocks;
		d[1] = (uint8_t)(nb >> 16);
		d[2] = (uint8_t)(nb >> 8);
		d[3] = (uint8_t)nb;
		d[6] = cd_unit ? 0x08 : 0x02;                   // block length 2048 / 512
		n += 8;
	}
	if (page == 0x3F)
	{
		static const int hd_pages[] = { 0x01, 0x03, 0x04, 0x08 };
		static const int cd_pages[] = { 0x01, 0x0E, 0x2A };
		const int *pl = cd_unit ? cd_pages : hd_pages;
		int np = cd_unit ? 3 : 4;
		for (int i = 0; i < np; i++) n += mode_page(unit, pl[i], pc, b + n);
	}
	else
	{
		int pn = mode_page(unit, page, pc, b + n);
		if (!pn) return 0;
		n += pn;
	}
	uint8_t devspec = (!cd_unit && unit_ro[unit]) ? 0x80 : 0x00;   // write protected
	if (ten)
	{
		put16(b, n - 2);
		b[2] = cd_unit ? 0x01 : 0x00;
		b[3] = devspec;
		b[7] = dbd ? 0 : 8;
	}
	else
	{
		b[0] = (uint8_t)(n - 1);
		b[1] = cd_unit ? 0x01 : 0x00;
		b[2] = devspec;
		b[3] = dbd ? 0 : 8;
	}
	return n;
}

static int resp_read_toc(int unit, int flags, int start, uint8_t *b)
{
	static fcd_track t[FCD_MAX_TRACKS];
	uint32_t leadout = 0;
	int nt = disc_tracks(unit, t, &leadout);
	int msf = flags & 1, fmt = (flags >> 1) & 3;
	if (!nt) return 0;

	if (fmt == 0)
	{
		if (start > nt && start != 0xAA) return 0;
		int n = 4;
		for (int i = 0; i < nt; i++)
		{
			if (i + 1 < start) continue;
			uint8_t *d = b + n;
			d[1] = t[i].audio ? 0x10 : 0x14;
			d[2] = (uint8_t)(i + 1);
			put_addr(d + 4, t[i].start, msf);
			n += 8;
		}
		uint8_t *d = b + n;
		d[1] = 0x14;
		d[2] = 0xAA;
		put_addr(d + 4, leadout, msf);
		n += 8;
		put16(b, n - 2);
		b[2] = 1;
		b[3] = (uint8_t)nt;
		return n;
	}
	if (fmt == 1)
	{
		// session information: one session, first track of it
		put16(b, 10);
		b[2] = 1;
		b[3] = 1;
		b[5] = t[0].audio ? 0x10 : 0x14;
		b[6] = 1;
		put_addr(b + 8, t[0].start, msf);
		return 12;
	}
	return 0;
}

static int resp_sub_channel(int unit, int flags, int fmt, uint8_t *b)
{
	static fcd_track t[FCD_MAX_TRACKS];
	uint32_t leadout = 0;
	int nt = disc_tracks(unit, t, &leadout);
	falcon_cd_audio *a = &audio[unit];
	int msf = flags & 1;
	b[1] = (uint8_t)(a->status ? a->status : 0x15);
	if (a->status == 0x13) a->status = 0x15;            // completion is reported once
	if (!(flags & 2)) return 4;
	uint8_t *d = b + 4;
	int n = 0;
	if (fmt == 1)
	{
		int ti = 0;
		while (ti + 1 < nt && t[ti + 1].start <= a->pos) ti++;
		d[0] = 0x01;
		d[1] = nt ? (t[ti].audio ? 0x10 : 0x14) : 0x14;
		d[2] = (uint8_t)(ti + 1);
		d[3] = 1;
		put_addr(d + 4, a->pos, msf);
		uint32_t rel = nt && a->pos >= t[ti].start ? a->pos - t[ti].start : 0;
		if (msf)
		{
			d[8] = 0;
			d[9] = (uint8_t)(rel / (60 * 75));
			d[10] = (uint8_t)((rel / 75) % 60);
			d[11] = (uint8_t)(rel % 75);
		}
		else put32(d + 8, rel);
		n = 12;
	}
	else if (fmt == 2 || fmt == 3)
	{
		d[0] = (uint8_t)fmt;                            // no MCN / ISRC (valid bits 0)
		n = 20;
	}
	else return 0;
	put16(b + 2, n);
	return 4 + n;
}

int falcon_scsi_window_read(int unit, uint32_t lba, uint8_t *blk)
{
	int flags = (lba >> 16) & 0x0F, op = (lba >> 8) & 0xFF, arg = lba & 0xFF;
	int cd_unit = (unit == FALCON_SCSI_CD_ID);
	int len = 0;
	memset(blk, 0, 512);
	if (unit < 0 || unit >= FALCON_SCSI_UNITS) return 0;

	switch (op)
	{
	case 0x12:
		len = resp_inquiry(unit, flags, arg, blk);
		break;
	case 0x25:
	{
		uint64_t blocks = image_size(unit) / (cd_unit ? 2048 : 512);
		if (!blocks) break;
		put32(blk, (uint32_t)(blocks > 0xFFFFFFFFull ? 0xFFFFFFFF : blocks - 1));
		put32(blk + 4, cd_unit ? 2048 : 512);
		len = 8;
		break;
	}
	case 0x1A:
	case 0x5A:
		len = resp_mode_sense(unit, op == 0x5A, flags & 1, arg, blk);
		break;
	case 0x43:
		if (cd_unit) len = resp_read_toc(unit, flags, arg, blk);
		break;
	case 0x42:
		if (cd_unit) len = resp_sub_channel(unit, flags, arg, blk);
		break;
	default:
		break;
	}
	if (len > 496) len = 496;
	blk[510] = (uint8_t)(len >> 8);
	blk[511] = (uint8_t)len;
	return len;
}

static uint32_t cdb_msf(const uint8_t *p)
{
	uint32_t f = ((uint32_t)p[0] * 60 + p[1]) * 75 + p[2];
	return f >= 150 ? f - 150 : 0;
}

void falcon_scsi_window_write(int unit, uint32_t lba, const uint8_t *blk)
{
	if (unit < 0 || unit >= FALCON_SCSI_UNITS) return;
	const uint8_t *cdb = blk + 496;
	falcon_cd_audio *a = &audio[unit];
	memcpy(a->last_cmd, cdb, 10);
	(void)lba;

	switch (cdb[0])
	{
	case 0x15:      // MODE SELECT(6)
	case 0x55:      // MODE SELECT(10)
	{
		int pl = (cdb[0] == 0x15) ? cdb[4] : (int)get16(cdb + 7);
		if (pl > 496) pl = 496;
		a->mode_sel_len = pl;
		memcpy(a->mode_sel, blk, pl < (int)sizeof(a->mode_sel) ? pl : (int)sizeof(a->mode_sel));
		break;
	}
	case 0x45:      // PLAY AUDIO(10)
	case 0xA5:      // PLAY AUDIO(12)
	{
		uint32_t start = get32(cdb + 2);
		uint32_t n = (cdb[0] == 0x45) ? get16(cdb + 7) : get32(cdb + 6);
		if (!n) break;
		a->pos = start;
		a->end = start + n;
		a->status = 0x11;
		break;
	}
	case 0x47:      // PLAY AUDIO MSF
		a->pos = cdb_msf(cdb + 3);
		a->end = cdb_msf(cdb + 6);
		if (a->end > a->pos) a->status = 0x11;
		break;
	case 0x48:      // PLAY AUDIO TRACK INDEX
	{
		static fcd_track t[FCD_MAX_TRACKS];
		uint32_t leadout = 0;
		int nt = disc_tracks(unit, t, &leadout);
		int st = cdb[4], et = cdb[7];
		if (st < 1 || st > nt) break;
		if (et < st) et = st;
		a->pos = t[st - 1].start;
		a->end = (et < nt) ? t[et].start : leadout;
		a->status = 0x11;
		break;
	}
	case 0x4B:      // PAUSE / RESUME
		if (a->status == 0x11 || a->status == 0x12) a->status = (cdb[8] & 1) ? 0x11 : 0x12;
		break;
	case 0x4E:      // STOP PLAY / SCAN
		a->status = 0x15;
		break;
	default:
		break;
	}
}

const falcon_cd_audio *falcon_scsi_audio(int unit)
{
	return (unit >= 0 && unit < FALCON_SCSI_UNITS) ? &audio[unit] : 0;
}

// ---------------------------------------------------------------------------
// sector service (called from the user_io sd request loop)

int falcon_sd_service(int disk, int op, uint32_t lba, int sz, int ack)
{
	static uint8_t blk[512];
	if (!is_falcon()) return 0;
	int unit = slot_unit(disk);
	if (unit < 0 || sz != 512) return 0;

	uint32_t win = lba >> 24;
	if (win == FALCON_WIN_RESP || win == FALCON_WIN_CMD)
	{
		if (op & 1)
		{
			if (win == FALCON_WIN_RESP) falcon_scsi_window_read(unit, lba, blk);
			else memset(blk, 0, sizeof(blk));
			EnableIO();
			spi_w(UIO_SECTOR_RD | ack);
			spi_block_write(blk, user_io_get_width(), sz);
			DisableIO();
		}
		else if (op == 2)
		{
			EnableIO();
			spi_w(UIO_SECTOR_WR | ack);
			spi_block_read(blk, user_io_get_width(), sz);
			DisableIO();
			if (win == FALCON_WIN_CMD) falcon_scsi_window_write(unit, lba, blk);
		}
		else return -1;
		return 1;
	}

	if (unit == FALCON_SCSI_CD_ID && cd.active)
	{
		if (op & 1)
		{
			falcon_scsi_cd_fill(unit, lba, blk);
			EnableIO();
			spi_w(UIO_SECTOR_RD | ack);
			spi_block_write(blk, user_io_get_width(), sz);
			DisableIO();
		}
		else if (op == 2)
		{
			// the core never writes the CD-ROM; complete the transfer
			EnableIO();
			spi_w(UIO_SECTOR_WR | ack);
			spi_block_read(blk, user_io_get_width(), sz);
			DisableIO();
		}
		else return -1;
		return 1;
	}
	return 0;
}
