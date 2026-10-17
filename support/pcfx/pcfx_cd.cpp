#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <algorithm>

#include "../../file_io.h"
#include "../../user_io.h"
#include "../../cd.h"
#include "../chd/mister_chd.h"
#include "pcfx_cd.h"

// CD LBAs are absolute: track 1 data starts at 150

enum { TRK_AUDIO = 0, TRK_MODE1 = 1, TRK_MODE2 = 2 };

struct cd_track
{
	int file;
	int ss;            // bytes per sector in the file
	int type;
	int idx0;          // index 0
	int start;         // index 1
	int end;           // exclusive
	int gap;           // PREGAP, not in the file
	int fbase;         // LBA of the file's first sector
};

struct cd_file
{
	fileTYPE f;
	int base;
	__off64_t off, len;   // sector data within the file (a WAV's data chunk)
	int swap;             // big-endian audio (MOTOROLA)
};

static cd_track  cd_tracks[100];
static cd_file   cd_files[100];
static int       cd_ntracks, cd_nfiles;
static int       cd_present;
static toc_t     chd_toc;
static uint8_t  *chd_hunkbuf;
static int       chd_hunknum = -1;
static fileTYPE  cd_sub;           // CloneCD .sub

#define CD_CACHE_N 6
static uint8_t   cd_cache[CD_CACHE_N * PCFX_CD_BLOCK];
static int       cd_cache_lba = -1;

void pcfx_cd_unload()
{
	for (int i = 0; i < cd_nfiles; i++) FileClose(&cd_files[i].f);
	if (cd_sub.opened()) FileClose(&cd_sub);
	if (chd_toc.chd_f) chd_close(chd_toc.chd_f);
	if (chd_hunkbuf) free(chd_hunkbuf);
	memset(&chd_toc, 0, sizeof(chd_toc));
	memset(cd_tracks, 0, sizeof(cd_tracks));
	chd_hunkbuf = NULL;
	chd_hunknum = -1;
	cd_ntracks = cd_nfiles = 0;
	cd_present = 0;
	cd_cache_lba = -1;
}

// named after the cue, else the first track file
static void cd_open_sub(const char *cue)
{
	char path[1024];
	const char *tries[2] = { cue, cd_nfiles ? cd_files[0].f.path : cue };
	for (int i = 0; i < 2; i++)
	{
		snprintf(path, sizeof(path), "%s", tries[i]);
		char *e = strrchr(path, '.');
		if (!e) continue;
		strcpy(e, ".sub");
		if (FileOpen(&cd_sub, path)) { printf("CD: subcode from %s\n", path); return; }
	}
}

// .sub stores each channel as a 12-byte block; the core wants P..W per byte
static void sub_interleave(const uint8_t *cooked, uint8_t *raw)
{
	for (int k = 0; k < PCFX_CD_SUB; k++)
	{
		uint8_t b = 0;
		for (int ch = 0; ch < 8; ch++) b |= ((cooked[ch * 12 + (k >> 3)] >> (7 - (k & 7))) & 1) << (7 - ch);
		raw[k] = b;
	}
}

static int msf_frames(const char *s)
{
	int m, sec, f;
	if (sscanf(s, "%d:%d:%d", &m, &sec, &f) != 3) return -1;
	return (m * 60 + sec) * 75 + f;
}

static int cd_file_sectors(int fi, int ss)
{
	return (int)(cd_files[fi].len / ss);
}

static uint32_t le32(const uint8_t *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Walks the RIFF chunks to "data", e.g. RIFF/WAVE, "fmt ", "LIST", "data".
// Only CD audio is accepted: 16-bit stereo 44.1 kHz PCM.
static int wav_data(cd_file *cf)
{
	fileTYPE *f = &cf->f;
	uint8_t h[24];
	int fmt_ok = 0;
	if (!FileSeek(f, 0, SEEK_SET) || FileReadAdv(f, h, 12) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return 0;
	for (__off64_t pos = 12; pos + 8 <= f->size;)
	{
		if (!FileSeek(f, pos, SEEK_SET) || FileReadAdv(f, h, 8) != 8) return 0;
		uint32_t len = le32(h + 4);
		if (!memcmp(h, "fmt ", 4))
		{
			if (len < 16 || FileReadAdv(f, h + 8, 16) != 16) return 0;
			int format = h[8] | (h[9] << 8), channels = h[10] | (h[11] << 8), bits = h[22] | (h[23] << 8);
			fmt_ok = (format == 1 || format == 0xFFFE) && channels == 2 && le32(h + 12) == 44100 && bits == 16;
		}
		else if (!memcmp(h, "data", 4))
		{
			if (!fmt_ok) return 0;
			cf->off = pos + 8;
			cf->len = std::min((__off64_t)len, f->size - cf->off);
			return 1;
		}
		pos += 8 + (__off64_t)len + (len & 1);
	}
	return 0;
}

// Compressed audio can't be served as sectors.
static int compressed(const char *type, const char *name)
{
	static const char *const kinds[] = { "MP3", "AIFF", "FLAC", "OGG", "OPUS", "APE", "WV" };
	const char *ext = strrchr(name, '.');
	for (const char *k : kinds)
		if (!strncasecmp(type, k, strlen(k)) || (ext && !strcasecmp(ext + 1, k))) return 1;
	return 0;
}

static void swap_samples(uint8_t *buf)
{
	for (int i = 0; i < PCFX_CD_SECTOR; i += 2) { uint8_t x = buf[i]; buf[i] = buf[i + 1]; buf[i + 1] = x; }
}

static void cd_close_track(int t)
{
	if (t < 0) return;
	cd_track *tr = &cd_tracks[t];
	if (!tr->end) tr->end = tr->fbase + cd_file_sectors(tr->file, tr->ss);
}

static int cd_load_cue(const char *filename)
{
	static char text[64 * 1024];
	char fname[1024];

	memset(text, 0, sizeof(text));
	if (!FileLoad(filename, text, sizeof(text) - 1)) return 0;

	snprintf(fname, sizeof(fname), "%s", filename);
	char *dir_end = strrchr(fname, '/');
	dir_end = dir_end ? dir_end + 1 : fname;
	char *fname_end = fname + sizeof(fname) - 1;

	int pos = 150;
	int t = -1;
	char *line = text;
	while (line && *line)
	{
		char *next = strchr(line, '\n');
		if (next) *next++ = 0;
		while (*line == ' ' || *line == '\t') line++;
		char *e = line + strlen(line);
		while (e > line && (e[-1] == '\r' || e[-1] == ' ')) *--e = 0;

		if (!strncasecmp(line, "FILE ", 5))
		{
			if (t >= 0) { cd_close_track(t); pos = cd_tracks[t].end; }
			const char *p = line + 5;
			while (*p == ' ') p++;
			char *out = dir_end;
			if (*p == '"') { p++; while (*p && *p != '"' && out < fname_end) *out++ = *p++; }
			else while (*p && *p != ' ' && out < fname_end) *out++ = *p++;
			*out = 0;
			if (*p == '"') p++;
			while (*p == ' ') p++;
			cd_file *cf = &cd_files[cd_nfiles];
			if (cd_nfiles >= 100 || !FileOpen(&cf->f, fname))
			{
				printf("CD: cannot open track file %s\n", fname);
				return 0;
			}
			cf->base = pos;
			cf->off = 0;
			cf->len = cf->f.size;
			cf->swap = !strncasecmp(p, "MOTOROLA", 8);
			if (compressed(p, fname) || (!strncasecmp(p, "WAVE", 4) && !wav_data(cf)))
			{
				printf("CD: %s is not raw sectors or 16-bit stereo 44.1 kHz PCM WAV\n", fname);
				FileClose(&cf->f);
				return 0;
			}
			cd_nfiles++;
		}
		else if (!strncasecmp(line, "TRACK ", 6))
		{
			int n = atoi(line + 6);
			if (n != cd_ntracks + 1 || cd_ntracks >= 99 || !cd_nfiles) return 0;
			t = cd_ntracks++;
			cd_track *tr = &cd_tracks[t];
			tr->file = cd_nfiles - 1;
			tr->fbase = cd_files[tr->file].base;
			if (strstr(line, "MODE1/2352"))      { tr->type = TRK_MODE1; tr->ss = 2352; }
			else if (strstr(line, "MODE1/2048")) { tr->type = TRK_MODE1; tr->ss = 2048; }
			else if (strstr(line, "MODE2/2352")) { tr->type = TRK_MODE2; tr->ss = 2352; }
			else if (strstr(line, "MODE2/2336")) { tr->type = TRK_MODE2; tr->ss = 2336; }
			else if (strstr(line, "AUDIO"))      { tr->type = TRK_AUDIO; tr->ss = 2352; }
			else { printf("CD: unsupported track type: %s\n", line); return 0; }
		}
		else if (t >= 0 && !strncasecmp(line, "PREGAP ", 7))
		{
			int g = msf_frames(line + 7);
			if (g > 0) { cd_tracks[t].gap = g; cd_tracks[t].fbase += g; cd_files[cd_tracks[t].file].base += g; }
		}
		else if (t >= 0 && !strncasecmp(line, "INDEX ", 6))
		{
			int idx = atoi(line + 6);
			const char *p = line + 6;
			while (*p && *p != ' ') p++;
			int frames = msf_frames(p);
			if (frames < 0) continue;
			cd_track *tr = &cd_tracks[t];
			int abs = tr->fbase + frames;
			if (idx == 0) tr->idx0 = abs;
			else if (idx == 1)
			{
				tr->start = abs;
				if (!tr->idx0) tr->idx0 = abs - tr->gap;
				if (t > 0 && cd_tracks[t - 1].file == tr->file && !cd_tracks[t - 1].end) cd_tracks[t - 1].end = tr->idx0;
			}
		}
		line = next;
	}
	if (t < 0) return 0;
	cd_close_track(t);
	return 1;
}

static int cd_load_iso(const char *filename)
{
	if (!FileOpen(&cd_files[0].f, filename)) return 0;
	cd_nfiles = 1;
	cd_files[0].base = 150;
	cd_files[0].off = 0;
	cd_files[0].len = cd_files[0].f.size;
	cd_files[0].swap = 0;
	cd_ntracks = 1;
	cd_tracks[0].file = 0;
	cd_tracks[0].fbase = 150;
	cd_tracks[0].ss = (cd_files[0].f.size % 2352) ? 2048 : 2352;
	cd_tracks[0].type = TRK_MODE1;
	cd_tracks[0].idx0 = 150;
	cd_tracks[0].start = 150;
	cd_tracks[0].end = 150 + cd_file_sectors(0, cd_tracks[0].ss);
	return 1;
}

static int cd_load_chd(const char *filename)
{
	if (mister_load_chd(filename, &chd_toc) != CHDERR_NONE) return 0;
	chd_hunkbuf = (uint8_t *)malloc(chd_toc.chd_hunksize);
	if (!chd_hunkbuf) return 0;
	chd_hunknum = -1;
	cd_ntracks = std::min(chd_toc.last, 99);
	for (int i = 0; i < cd_ntracks; i++)
	{
		cd_track_t *ct = &chd_toc.tracks[i];
		cd_track *tr = &cd_tracks[i];
		tr->file = -1;
		tr->ss = ct->sector_size ? ct->sector_size : 2352;
		tr->type = (ct->type == TT_CDDA) ? TRK_AUDIO : (ct->type == TT_MODE2) ? TRK_MODE2 : TRK_MODE1;
		tr->start = ct->start + 150;
		tr->end = ct->end + 150;
		// the loader adds unstored pregaps to the previous track
		int prev_end = i ? chd_toc.tracks[i - 1].end : 0;
		int stored = (ct->start - prev_end) == ct->indexes[1];
		tr->gap = stored ? 0 : ct->indexes[1];
		tr->idx0 = i ? tr->start - ct->indexes[1] : 0;
		if (i && tr->gap) cd_tracks[i - 1].end = tr->idx0;
	}
	return 1;
}

int pcfx_cd_load(const char *filename)
{
	pcfx_cd_unload();
	const char *ext = strrchr(filename, '.');
	int ok = 0;
	if (ext && !strcasecmp(ext, ".chd")) ok = cd_load_chd(filename);
	else if (ext && !strcasecmp(ext, ".cue")) ok = cd_load_cue(filename);
	else ok = cd_load_iso(filename);
	if (!ok) { pcfx_cd_unload(); return 0; }
	if (!chd_toc.chd_f) cd_open_sub(filename);
	cd_present = 1;
	for (int i = 0; i < cd_ntracks; i++)
		printf("CD: track %d type %d ss %d idx0 %d start %d end %d gap %d\n", i + 1,
			cd_tracks[i].type, cd_tracks[i].ss, cd_tracks[i].idx0, cd_tracks[i].start, cd_tracks[i].end, cd_tracks[i].gap);
	return 1;
}

// track starts at 4 + 4t, mode 2 bitmap at 408, index 0 at 512 + 4t
void pcfx_cd_send_toc(int index)
{
	uint8_t rec[1024];
	memset(rec, 0, sizeof(rec));
	if (cd_present)
	{
		rec[0] = 1;
		rec[1] = cd_ntracks;
		rec[2] = 1;
		for (int i = 0; i <= cd_ntracks; i++)
		{
			int t = (i < cd_ntracks) ? i + 1 : 100;
			int lba = (i < cd_ntracks) ? cd_tracks[i].start : cd_tracks[cd_ntracks - 1].end;
			uint8_t *e = rec + 4 + 4 * t;
			e[0] = lba % 75;
			e[1] = (lba / 75) % 60;
			e[2] = lba / 4500;
			e[3] = (i < cd_ntracks && cd_tracks[i].type != TRK_AUDIO) ? 0x04 : 0x00;
			if (i < cd_ntracks && cd_tracks[i].type == TRK_MODE2) rec[408 + t / 8] |= 1 << (t & 7);
			if (i < cd_ntracks)
			{
				int p = cd_tracks[i].idx0;
				uint8_t *g = rec + 512 + 4 * t;
				g[0] = p % 75;
				g[1] = (p / 75) % 60;
				g[2] = p / 4500;
			}
		}
	}
	user_io_set_index(index);
	user_io_set_download(1);
	user_io_file_tx_data(rec, sizeof(rec));
	user_io_set_download(0);
}

static void cd_make_header(uint8_t *buf, int lba, int mode)
{
	static const uint8_t sync[12] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
	memcpy(buf, sync, 12);
	buf[12] = BCD(lba / 4500);
	buf[13] = BCD((lba / 75) % 60);
	buf[14] = BCD(lba % 75);
	buf[15] = mode;
}

static void cd_read_one(uint8_t *buf, int lba)
{
	memset(buf, 0, PCFX_CD_BLOCK);
	if (!cd_present) return;

	int t;
	for (t = 0; t < cd_ntracks; t++) if (lba >= cd_tracks[t].idx0 && lba < cd_tracks[t].end) break;
	if (t == cd_ntracks) return;
	cd_track *tr = &cd_tracks[t];
	if (lba < tr->start && lba >= tr->start - tr->gap) return;

	int ok = 0;
	int off = (tr->ss == 2048) ? 16 : (tr->ss == 2336) ? 16 : 0;
	if (chd_toc.chd_f)
	{
		int chd_lba = lba - 150 + chd_toc.tracks[t].offset;
		ok = chd_lba >= 0 && mister_chd_read_sector(chd_toc.chd_f, chd_lba, off, 0, tr->ss, buf, chd_hunkbuf, &chd_hunknum) == CHDERR_NONE;
		if (ok && tr->type == TRK_AUDIO) swap_samples(buf);
	}
	else
	{
		cd_file *cf = &cd_files[tr->file];
		__off64_t pos = cf->off + (__off64_t)(lba - tr->fbase) * tr->ss;
		ok = FileSeek(&cf->f, pos, SEEK_SET) && FileReadAdv(&cf->f, buf + off, tr->ss);
		if (ok && cf->swap && tr->type == TRK_AUDIO) swap_samples(buf);
	}
	if (!ok) { memset(buf, 0, PCFX_CD_SECTOR); return; }
	if (tr->ss != 2352) cd_make_header(buf, lba, tr->type == TRK_MODE2 ? 2 : 1);

	// subcode follows the sector
	uint8_t cooked[PCFX_CD_SUB];
	uint8_t *sub = buf + PCFX_CD_SECTOR;
	if (chd_toc.chd_f)
	{
		cd_track_t *ct = &chd_toc.tracks[t];
		int chd_lba = lba - 150 + ct->offset;
		if (ct->sbc_type == SUBCODE_RW_RAW)
			mister_chd_read_sector(chd_toc.chd_f, chd_lba, 0, PCFX_CD_SECTOR, PCFX_CD_SUB, sub, chd_hunkbuf, &chd_hunknum);
		else if (ct->sbc_type == SUBCODE_RW &&
		         mister_chd_read_sector(chd_toc.chd_f, chd_lba, 0, PCFX_CD_SECTOR, PCFX_CD_SUB, cooked, chd_hunkbuf, &chd_hunknum) == CHDERR_NONE)
			sub_interleave(cooked, sub);
	}
	else if (cd_sub.opened() && tr->file == 0)
	{
		__off64_t pos = (__off64_t)(lba - tr->fbase) * PCFX_CD_SUB;
		if (FileSeek(&cd_sub, pos, SEEK_SET) && FileReadAdv(&cd_sub, cooked, PCFX_CD_SUB)) sub_interleave(cooked, sub);
	}
}

static void cd_cache_fill(int lba)
{
	for (int i = 0; i < CD_CACHE_N; i++) cd_read_one(cd_cache + i * PCFX_CD_BLOCK, lba + i);
	cd_cache_lba = lba;
}

void pcfx_cd_read(uint8_t *buf, int lba, int cnt)
{
	if (cd_cache_lba < 0 || lba < cd_cache_lba || lba + cnt > cd_cache_lba + CD_CACHE_N) cd_cache_fill(lba);
	memcpy(buf, cd_cache + (lba - cd_cache_lba) * PCFX_CD_BLOCK, cnt * PCFX_CD_BLOCK);
}

// once the cache is used up, so the next request is ready
void pcfx_cd_read_ahead(int lba)
{
	if (lba == cd_cache_lba + CD_CACHE_N) cd_cache_fill(lba);
}
