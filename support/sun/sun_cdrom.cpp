// Sun CD-ROM slot: CUE/CHD/raw-sector images served as a flat disc of 2048-byte sectors.

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include "../../file_io.h"
#include "../../user_io.h"
#include "../../hardware.h"
#include "../chd/mister_chd.h"
#include "sun.h"
#include "sun_cdrom.h"

struct sun_cdrom_state
{
	int      active;
	int      is_chd;

	toc_t    toc;          // disc LBAs, no +150
	uint8_t *hunkbuf;
	int      hunknum;

	int      trk;          // the data track, or -1
	uint32_t data_soff;    // user data offset in its raw sector
	uint64_t sectors;
};

static sun_cdrom_state cd = {};

static uint32_t layout_data_off(int is_mode2, uint32_t sec_size)
{
	if (sec_size == 2048) return 0;
	if (sec_size == 2336) return 8;              // MODE2 form 1, no sync/header
	return is_mode2 ? 24 : 16;                   // raw: sync, header (, subheader)
}

void sun_cdrom_unmount(int index)
{
	if (index != sun_cdrom_slot()) return;
	if (cd.is_chd && cd.toc.chd_f) chd_close(cd.toc.chd_f);
	for (int i = 0; i < cd.toc.last; i++)
		if (cd.toc.tracks[i].f.opened()) FileClose(&cd.toc.tracks[i].f);
	if (cd.hunkbuf) free(cd.hunkbuf);
	memset(&cd, 0, sizeof(cd));
}

uint64_t sun_cdrom_size(int index)
{
	if (!sun_cdrom_active(index)) return 0;
	return cd.sectors * 2048ULL;
}

int sun_cdrom_active(int index)
{
	return index == sun_cdrom_slot() && cd.active;
}

static void log_toc(const char *src)
{
	printf("Sun CD: %s, %d track(s), leadout %d, data track %d (%llu sectors)\n",
	       src, cd.toc.last, cd.toc.end, cd.trk + 1, (unsigned long long)cd.sectors);
	for (int i = 0; i < cd.toc.last; i++)
		printf("Sun CD:   track %2d %s start %6d end %6d raw %4d pregap %d\n",
		       i + 1, cd.toc.tracks[i].type == TT_CDDA ? "audio" : "data ",
		       cd.toc.tracks[i].start, cd.toc.tracks[i].end,
		       cd.toc.tracks[i].sector_size, cd.toc.tracks[i].pregap);
}

static int finish_mount(const char *src)
{
	cd.trk = -1;
	for (int i = 0; i < cd.toc.last; i++)
		if (cd.toc.tracks[i].type != TT_CDDA) { cd.trk = i; break; }
	if (cd.trk < 0)
	{
		// audio-only: reads return zeros, but a non-zero capacity keeps the disc visible
		cd.data_soff = 0;
		cd.sectors   = (uint64_t)cd.toc.end;
	}
	else
	{
		cd.data_soff = layout_data_off(cd.toc.tracks[cd.trk].type == TT_MODE2,
		                               cd.toc.tracks[cd.trk].sector_size);
		cd.sectors   = (uint64_t)(cd.toc.tracks[cd.trk].end - cd.toc.tracks[cd.trk].start);
	}
	log_toc(src);
	return SUN_CDROM_HANDLED;
}

static int mount_chd(const char *name)
{
	memset(&cd.toc, 0, sizeof(cd.toc));
	if (mister_load_chd(name, &cd.toc) != CHDERR_NONE || !cd.toc.chd_f)
	{
		printf("Sun CD: failed to load chd %s\n", name);
		return SUN_CDROM_REJECT;
	}
	cd.hunkbuf = (uint8_t *)malloc(cd.toc.chd_hunksize);
	if (!cd.hunkbuf) return SUN_CDROM_REJECT;
	cd.hunknum = -1;
	cd.is_chd  = 1;
	return finish_mount("chd");
}

static uint32_t msf_field(const char *line)
{
	int m = 0, s = 0, f = 0;
	if (sscanf(line, "%*s %*d %d:%d:%d", &m, &s, &f) != 3) return 0;
	return (uint32_t)(m * 60 + s) * 75 + f;
}

static int mount_cue(const char *name)
{
	fileTYPE cue = {};
	if (!FileOpen(&cue, name)) return SUN_CDROM_REJECT;
	int sz = cue.size > 65535 ? 65535 : (int)cue.size;
	char *txt = (char *)malloc(sz + 1);
	if (!txt) { FileClose(&cue); return SUN_CDROM_REJECT; }
	FileReadAdv(&cue, txt, sz);
	FileClose(&cue);
	txt[sz] = 0;

	toc_t *t = &cd.toc;
	char fpath[1024] = {};
	uint64_t fsize = 0;
	uint32_t file_base = 0;       // disc LBA of the current FILE's frame 0
	uint32_t synth = 0;           // PREGAP frames so far, in no file
	uint32_t pending_pregap = 0;
	int      cur_type = -1, cur_m2 = 0;
	uint32_t cur_ss = 0;
	int      index0 = -1;
	int      prev_in_file = -1;   // last track of the current FILE
	int      bad = 0;

	char *line = strtok(txt, "\r\n");
	while (line && !bad)
	{
		while (*line == ' ' || *line == '\t') line++;

		if (!strncasecmp(line, "FILE", 4))
		{
			// the previous FILE's last track runs to its end
			if (prev_in_file >= 0)
			{
				cd_track_t *p = &t->tracks[prev_in_file];
				uint32_t ffr = (uint32_t)(fsize / p->sector_size);
				uint32_t infile1 = (uint32_t)(p->start + p->offset);
				p->end = p->start + (ffr > infile1 ? ffr - infile1 : 0);
				file_base = p->end - synth;
			}
			char *q1 = strchr(line, '"');
			char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
			if (!q1 || !q2 || (q2 - q1) <= 1 || !strcasestr(q2 + 1, "BINARY"))
			{
				printf("Sun CD: cue FILE unsupported: %s\n", line);
				bad = 1; break;
			}
			*q2 = 0;
			strncpy(fpath, name, sizeof(fpath) - 1);
			char *slash = strrchr(fpath, '/');
			if (slash) strncpy(slash + 1, q1 + 1, sizeof(fpath) - (slash - fpath) - 2);
			else       strncpy(fpath, q1 + 1, sizeof(fpath) - 1);
			fsize = 0;
			prev_in_file = -1;
		}
		else if (!strncasecmp(line, "TRACK", 5))
		{
			if (t->last >= 99) break;
			cur_type = -1; cur_m2 = 0; cur_ss = 0; index0 = -1;
			if      (strcasestr(line, "AUDIO"))      { cur_type = TT_CDDA;  cur_ss = 2352; }
			else if (strcasestr(line, "MODE1/2352")) { cur_type = TT_MODE1; cur_ss = 2352; }
			else if (strcasestr(line, "MODE1/2048")) { cur_type = TT_MODE1; cur_ss = 2048; }
			else if (strcasestr(line, "MODE2/2352")) { cur_type = TT_MODE2; cur_ss = 2352; cur_m2 = 1; }
			else if (strcasestr(line, "MODE2/2336")) { cur_type = TT_MODE2; cur_ss = 2336; cur_m2 = 1; }
			else { printf("Sun CD: cue TRACK unsupported: %s\n", line); bad = 1; }
		}
		else if (!strncasecmp(line, "PREGAP", 6))
		{
			int m = 0, s = 0, f = 0;
			if (sscanf(line, "%*s %d:%d:%d", &m, &s, &f) == 3)
				pending_pregap += (uint32_t)(m * 60 + s) * 75 + f;
		}
		else if (!strncasecmp(line, "INDEX", 5) && cur_type >= 0)
		{
			int idx = 0;
			sscanf(line, "%*s %d", &idx);
			if (idx == 0) index0 = (int)msf_field(line);
			else if (idx == 1)
			{
				uint32_t index1 = msf_field(line);
				cd_track_t *k = &t->tracks[t->last];
				memset(k, 0, sizeof(*k));
				if (!fpath[0] || !FileOpen(&k->f, fpath))
				{
					printf("Sun CD: cue references missing bin: %s\n", fpath[0] ? fpath : "?");
					bad = 1; break;
				}
				if (!fsize) fsize = k->f.size;

				synth += pending_pregap;
				k->type        = (TrackType)cur_type;
				k->sector_size = (int)cur_ss;
				k->start       = (int)(file_base + synth + index1);
				k->offset      = (int)index1 - k->start;   // file frame = disc LBA + offset
				k->pregap      = (int)((index0 >= 0 ? index1 - (uint32_t)index0 : 0) + pending_pregap);
				k->indexes[0]  = (index0 >= 0) ? index0 : (int)index1;
				k->indexes[1]  = (int)index1;
				k->index_num   = 2;
				pending_pregap = 0;

				// previous track in the same FILE ends where this one's gap begins
				if (prev_in_file >= 0)
				{
					cd_track_t *p = &t->tracks[prev_in_file];
					uint32_t bound = (index0 >= 0) ? (uint32_t)index0 : index1;
					uint32_t p_in1 = (uint32_t)(p->start + p->offset);
					p->end = p->start + (bound > p_in1 ? bound - p_in1 : 0);
				}
				prev_in_file = t->last;
				t->last++;
				(void)cur_m2;
			}
		}
		line = strtok(NULL, "\r\n");
	}
	free(txt);

	// the last FILE's last track runs to its end
	if (!bad && prev_in_file >= 0)
	{
		cd_track_t *p = &t->tracks[prev_in_file];
		uint32_t ffr = (uint32_t)(fsize / p->sector_size);
		uint32_t infile1 = (uint32_t)(p->start + p->offset);
		p->end = p->start + (ffr > infile1 ? ffr - infile1 : 0);
	}
	if (bad || !t->last)
	{
		for (int i = 0; i < t->last; i++)
			if (t->tracks[i].f.opened()) FileClose(&t->tracks[i].f);
		memset(&cd.toc, 0, sizeof(cd.toc));
		if (!bad) printf("Sun CD: cue parse found no tracks (%s)\n", name);
		return SUN_CDROM_REJECT;
	}
	t->end = t->tracks[t->last - 1].end;
	return finish_mount("cue");
}

static int probe_pvd(fileTYPE *f, uint32_t sec_size, uint32_t off)
{
	uint8_t pvd[8];
	if (!FileSeek(f, (uint64_t)16 * sec_size + off, SEEK_SET)) return 0;
	if (FileReadAdv(f, pvd, sizeof(pvd)) != sizeof(pvd)) return 0;
	if (pvd[0] == 1 && !memcmp(pvd + 1, "CD001", 5)) return 1;
	if (!memcmp(pvd, "\x01\x43\x44\x52\x4f\x4d", 6)) return 1;  // "\1CDROM"
	return 0;
}

static int mount_raw(const char *name)
{
	cd_track_t *k = &cd.toc.tracks[0];
	if (!FileOpen(&k->f, name)) return SUN_CDROM_REJECT;

	static const struct { uint32_t ss, off; int m2; } cand[] = {
		{ 2048,  0, 0 },   // flat
		{ 2352, 16, 0 },   // MODE1 raw
		{ 2336,  8, 1 },   // MODE2 form 1
		{ 2352, 24, 1 },   // MODE2 raw
	};
	int found = -1;
	for (unsigned i = 0; i < sizeof(cand) / sizeof(cand[0]); i++)
	{
		if (probe_pvd(&k->f, cand[i].ss, cand[i].off)) { found = (int)i; break; }
	}

	if (found < 0)
	{
		// no PVD (a UFS-only disc): raw 2352 if it starts with a sync pattern, else flat
		static const uint8_t sync[12] =
			{ 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0 };
		uint8_t hdr[12] = {};
		FileSeek(&k->f, 0, SEEK_SET);
		FileReadAdv(&k->f, hdr, sizeof(hdr));
		if (!memcmp(hdr, sync, sizeof(sync))) found = 1;
	}

	if (found <= 0)
	{
		FileClose(&k->f);
		return SUN_CDROM_PASSTHRU;
	}

	k->type        = cand[found].m2 ? TT_MODE2 : TT_MODE1;
	k->sector_size = (int)cand[found].ss;
	k->start       = 0;
	k->offset      = 0;
	k->end         = (int)(k->f.size / k->sector_size);
	k->indexes[0]  = k->indexes[1] = 0;
	k->index_num   = 2;
	cd.toc.last    = 1;
	cd.toc.end     = k->end;
	return finish_mount("raw image");
}

int sun_cdrom_mount(int index, const char *name)
{
	if (index != sun_cdrom_slot()) return SUN_CDROM_PASSTHRU;
	sun_cdrom_unmount(index);

	int len = strlen(name);
	int r;
	if (len > 4 && !strcasecmp(name + len - 4, ".chd"))      r = mount_chd(name);
	else if (len > 4 && !strcasecmp(name + len - 4, ".cue")) r = mount_cue(name);
	else                                                     r = mount_raw(name);

	if (r == SUN_CDROM_HANDLED) cd.active = 1;
	else sun_cdrom_unmount(index);
	return r;
}

void sun_cdrom_fill(int index, uint64_t lba, uint8_t *buf, int sz)
{
	memset(buf, 0, sz);
	if (index != sun_cdrom_slot() || !cd.active) return;
	if (sz < 512 || (sz & 511) || sz > UIO_BUFFER_SIZE) return;
	if (cd.trk < 0) return;
	cd_track_t *k = &cd.toc.tracks[cd.trk];

	uint64_t byte_pos = lba * 512ULL;
	for (int pos = 0; pos < sz; )
	{
		uint64_t cd_lba = byte_pos >> 11;
		uint32_t off    = byte_pos & 2047;
		int      n      = 2048 - off;
		if (n > sz - pos) n = sz - pos;

		if (cd_lba >= cd.sectors) break;      // past the end: zeros

		if (cd.is_chd)
		{
			int chd_lba = (int)cd_lba + k->start + k->offset;
			if (mister_chd_read_sector(cd.toc.chd_f, chd_lba, 0, cd.data_soff + off,
			                           n, buf + pos, cd.hunkbuf, &cd.hunknum) != CHDERR_NONE)
				memset(buf + pos, 0, n);
		}
		else
		{
			diskled_on();
			uint64_t src_frame = (uint64_t)((int)cd_lba + k->start + k->offset);
			if (FileSeek(&k->f, src_frame * k->sector_size + cd.data_soff + off, SEEK_SET))
				FileReadAdv(&k->f, buf + pos, n);
		}
		pos      += n;
		byte_pos += n;
	}
}
