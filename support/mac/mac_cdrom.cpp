// Mac CD-ROM slot: CUE/CHD/raw-sector image translation to the flat
// 2048-byte-sector virtual disc, plus the TOC/audio windows (see mac_cdrom.h).

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../../file_io.h"
#include "../../user_io.h"
#include "../../hardware.h"
#include "../chd/mister_chd.h"
#include "mac.h"

struct mac_cdrom_state
{
	int      active;       // MAC_CDROM_HANDLED mount live on this slot
	int      is_chd;

	toc_t    toc;          // full track table, disc-LBA space (no +150)
	uint8_t *hunkbuf;      // chd hunk cache
	int      hunknum;

	int      trk;          // index of the served data track in toc.tracks[]
	uint32_t data_soff;    // user-data byte offset inside the data track's raw sector
	uint64_t sectors;      // virtual 2048-byte sector count (data track length)

	uint8_t  tocblob[MAC_CDROM_TOC_BLKS * 512];   // staged TOC blob (see mac_cdrom.h)

	// one-frame audio cache: the RTL reads a 2352-byte frame as five
	// 512-byte blocks; decode/seek once per frame, not per block
	uint8_t  aframe[2352];
	int64_t  aframe_lba;
};

static mac_cdrom_state cd = {};

static char     rp_path[1024];
static uint32_t rp_timer;
static int      rp_armed;
static int      rp_firing;      // reentrancy guard: the repulse's own mount
static uint32_t rp_data_reads;  // data-window fills since attach

static uint32_t layout_data_off(int is_mode2, uint32_t sec_size)
{
	if (sec_size == 2048) return 0;
	if (sec_size == 2336) return 8;              // MODE2 form1 (no sync/header)
	return is_mode2 ? 24 : 16;                   // 2352 raw: sync+header(+subheader)
}

void mac_cdrom_unmount(int index)
{
	if (index != mac_cdrom_slot()) return;
	if (cd.is_chd && cd.toc.chd_f) chd_close(cd.toc.chd_f);
	for (int i = 0; i < cd.toc.last; i++)
		if (cd.toc.tracks[i].f.opened()) FileClose(&cd.toc.tracks[i].f);
	if (cd.hunkbuf) free(cd.hunkbuf);
	memset(&cd, 0, sizeof(cd));
	cd.aframe_lba = -1;
}

uint64_t mac_cdrom_size(int index)
{
	if (!mac_cdrom_active(index)) return 0;
	return cd.sectors * 2048ULL;
}

int mac_cdrom_active(int index)
{
	return index == mac_cdrom_slot() && cd.active;
}

// ---- TOC blob ------------------

static void build_toc_blob(void)
{
	uint8_t *b = cd.tocblob;
	memset(b, 0, sizeof(cd.tocblob));
	memcpy(b, "MCDA", 4);
	b[4] = 1;                                  // version
	b[5] = 1;                                  // first track
	b[6] = (uint8_t)cd.toc.last;               // last track
	b[7] = (uint8_t)(cd.trk + 1);              // data track number (1-based)
	uint32_t leadout = (uint32_t)cd.toc.end;
	b[8]  = (uint8_t)leadout;       b[9]  = (uint8_t)(leadout >> 8);
	b[10] = (uint8_t)(leadout >> 16); b[11] = (uint8_t)(leadout >> 24);
	for (int i = 0; i < cd.toc.last && i < 99; i++)
	{
		uint8_t *e = b + 16 + 8 * i;
		e[0] = (cd.toc.tracks[i].type == TT_CDDA) ? 0x10 : 0x14;  // ctrl/adr
		uint32_t s = (uint32_t)cd.toc.tracks[i].start;
		e[2] = (uint8_t)s; e[3] = (uint8_t)(s >> 8);
		e[4] = (uint8_t)(s >> 16); e[5] = (uint8_t)(s >> 24);
		uint32_t pg = (uint32_t)cd.toc.tracks[i].pregap;
		if (pg > 0xffff) pg = 0xffff;
		e[6] = (uint8_t)pg; e[7] = (uint8_t)(pg >> 8);
	}
}

static void log_toc(const char *src)
{
	printf("Mac CD: %s, %d track(s), leadout %d, data track %d (%llu sectors)\n",
	       src, cd.toc.last, cd.toc.end, cd.trk + 1, (unsigned long long)cd.sectors);
	for (int i = 0; i < cd.toc.last; i++)
		printf("Mac CD:   track %2d %s start %6d end %6d raw %4d pregap %d\n",
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
		// Audio-only disc: still a valid mount (the core CHECKs data READs).
		// Capacity must stay non-zero — scsi.v gates presence on |img_blocks.
		cd.data_soff = 0;
		cd.sectors   = (uint64_t)cd.toc.end;
	}
	else
	{
		cd.data_soff = layout_data_off(cd.toc.tracks[cd.trk].type == TT_MODE2,
		                               cd.toc.tracks[cd.trk].sector_size);
		cd.sectors   = (uint64_t)(cd.toc.tracks[cd.trk].end - cd.toc.tracks[cd.trk].start);
	}
	cd.aframe_lba = -1;
	build_toc_blob();
	log_toc(src);
	return MAC_CDROM_HANDLED;
}

// ---- chd ------------------

static int mount_chd(const char *name)
{
	memset(&cd.toc, 0, sizeof(cd.toc));
	if (mister_load_chd(name, &cd.toc) != CHDERR_NONE || !cd.toc.chd_f)
	{
		printf("Mac CD: failed to load chd %s\n", name);
		return MAC_CDROM_REJECT;
	}
	cd.hunkbuf = (uint8_t *)malloc(cd.toc.chd_hunksize);
	if (!cd.hunkbuf) return MAC_CDROM_REJECT;
	cd.hunknum = -1;
	cd.is_chd  = 1;
	return finish_mount("chd");
}

// ---- cue + bin ------------------

static uint32_t msf_field(const char *line)
{
	int m = 0, s = 0, f = 0;
	if (sscanf(line, "%*s %*d %d:%d:%d", &m, &s, &f) != 3) return 0;
	return (uint32_t)(m * 60 + s) * 75 + f;
}

static int mount_cue(const char *name)
{
	fileTYPE cue = {};
	if (!FileOpen(&cue, name)) return MAC_CDROM_REJECT;
	int sz = cue.size > 65535 ? 65535 : (int)cue.size;
	char *txt = (char *)malloc(sz + 1);
	if (!txt) { FileClose(&cue); return MAC_CDROM_REJECT; }
	FileReadAdv(&cue, txt, sz);
	FileClose(&cue);
	txt[sz] = 0;

	toc_t *t = &cd.toc;
	char fpath[1024] = {};        // current FILE, resolved next to the cue
	uint64_t fsize = 0;           // its byte size (from the first track's open)
	uint32_t file_base = 0;       // disc LBA of the current FILE's frame 0
	uint32_t synth = 0;           // accumulated synthesized (PREGAP cmd) frames
	uint32_t pending_pregap = 0;
	int      cur_type = -1, cur_m2 = 0;
	uint32_t cur_ss = 0;
	int      index0 = -1;
	int      prev_in_file = -1;   // toc index of the last finalized track of this FILE
	int      bad = 0;

	char *line = strtok(txt, "\r\n");
	while (line && !bad)
	{
		while (*line == ' ' || *line == '\t') line++;

		if (!strncasecmp(line, "FILE", 4))
		{
			// close out the previous FILE: its last track runs to file end
			if (prev_in_file >= 0)
			{
				cd_track_t *p = &t->tracks[prev_in_file];
				uint32_t ffr = (uint32_t)(fsize / p->sector_size);
				uint32_t infile1 = (uint32_t)(p->start + p->offset); // in-file frame of its INDEX 01
				p->end = p->start + (ffr > infile1 ? ffr - infile1 : 0);
				file_base = p->end - synth;   // next file starts here (disc = file frames + synth)
			}
			char *q1 = strchr(line, '"');
			char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
			if (!q1 || !q2 || (q2 - q1) <= 1 || !strcasestr(q2 + 1, "BINARY"))
			{
				printf("Mac CD: cue FILE unsupported: %s\n", line);
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
			else { printf("Mac CD: cue TRACK unsupported: %s\n", line); bad = 1; }
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
					printf("Mac CD: cue references missing bin: %s\n", fpath[0] ? fpath : "?");
					bad = 1; break;
				}
				if (!fsize) fsize = k->f.size;

				synth += pending_pregap;
				k->type        = (TrackType)cur_type;
				k->sector_size = (int)cur_ss;
				k->start       = (int)(file_base + synth + index1);
				k->offset      = (int)index1 - k->start;   // source_frame = disc_lba + offset
				k->pregap      = (int)((index0 >= 0 ? index1 - (uint32_t)index0 : 0) + pending_pregap);
				k->indexes[0]  = (index0 >= 0) ? index0 : (int)index1;
				k->indexes[1]  = (int)index1;
				k->index_num   = 2;
				pending_pregap = 0;

				// previous track in the same FILE ends where this one's gap begins
				if (prev_in_file >= 0)
				{
					cd_track_t *p = &t->tracks[prev_in_file];
					uint32_t bound = (index0 >= 0) ? (uint32_t)index0 : index1;  // in-file frames
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

	// close out the final FILE
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
		if (!bad) printf("Mac CD: cue parse found no tracks (%s)\n", name);
		return MAC_CDROM_REJECT;
	}
	t->end = t->tracks[t->last - 1].end;
	return finish_mount("cue");
}

// ---- raw bin/iso probe -------------------------------------------------------

static int probe_pvd(fileTYPE *f, uint32_t sec_size, uint32_t off)
{
	uint8_t pvd[8];
	if (!FileSeek(f, (uint64_t)16 * sec_size + off, SEEK_SET)) return 0;
	if (FileReadAdv(f, pvd, sizeof(pvd)) != sizeof(pvd)) return 0;
	// ISO9660 PVD or High Sierra SVD magic
	if (pvd[0] == 1 && !memcmp(pvd + 1, "CD001", 5)) return 1;
	if (!memcmp(pvd, "\x01\x43\x44\x52\x4f\x4d", 6)) return 1;  // "\1CDROM" (HSF at +8 handled loosely)
	return 0;
}

static int mount_raw(const char *name)
{
	cd_track_t *k = &cd.toc.tracks[0];
	if (!FileOpen(&k->f, name)) return MAC_CDROM_REJECT;

	static const struct { uint32_t ss, off; int m2; } cand[] = {
		{ 2048,  0, 0 },   // flat user data (ISO/TOAST/2048-bin)
		{ 2352, 16, 0 },   // MODE1 raw
		{ 2336,  8, 1 },   // MODE2 form1 (no sync)
		{ 2352, 24, 1 },   // MODE2 raw
	};
	int found = -1;
	for (unsigned i = 0; i < sizeof(cand) / sizeof(cand[0]); i++)
	{
		if (probe_pvd(&k->f, cand[i].ss, cand[i].off)) { found = (int)i; break; }
	}

	if (found < 0)
	{
		// No PVD (pure-HFS disc). Sync pattern at byte 0 => raw 2352;
		// otherwise treat as flat 2048 (passthrough).
		static const uint8_t sync[12] =
			{ 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0 };
		uint8_t hdr[12] = {};
		FileSeek(&k->f, 0, SEEK_SET);
		FileReadAdv(&k->f, hdr, sizeof(hdr));
		if (!memcmp(hdr, sync, sizeof(sync))) found = 1;
	}

	if (found <= 0)
	{
		// flat 2048: the generic sd_image path serves it byte-for-byte
		FileClose(&k->f);
		return MAC_CDROM_PASSTHRU;
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

// ---- entry points ------------------------------------------------------------

int mac_cdrom_mount(int index, const char *name)
{
	if (index != mac_cdrom_slot()) return MAC_CDROM_PASSTHRU;
	mac_cdrom_unmount(index);

	int len = strlen(name);
	int r;
	if (len > 4 && !strcasecmp(name + len - 4, ".chd"))      r = mount_chd(name);
	else if (len > 4 && !strcasecmp(name + len - 4, ".cue")) r = mount_cue(name);
	else                                                     r = mount_raw(name);

	if (r == MAC_CDROM_HANDLED) cd.active = 1;
	else mac_cdrom_unmount(index);

	// Arm the boot repulse for HANDLED mounts; its own remount must not re-arm.
	if (!rp_firing)
	{
		if (r == MAC_CDROM_HANDLED && name && *name)
		{
			strncpy(rp_path, name, sizeof(rp_path) - 1);
			rp_path[sizeof(rp_path) - 1] = 0;
			rp_timer = GetTimer(60000);
			rp_armed = 1;
			rp_data_reads = 0;
		}
		else rp_armed = 0;
	}
	return r;
}

// One-shot boot repulse: re-fire the mount ~60 s after attach unless the guest
// read the disc — the Apple CD driver misses the single early attach pulse.
void mac_cdrom_poll(void)
{
	if (!rp_armed || !CheckTimer(rp_timer)) return;
	rp_armed = 0;
	if (!cd.active || !rp_path[0]) return;
	if (rp_data_reads > 8)
	{
		printf("Mac CD: boot repulse skipped (%u data reads — guest mounted it)\n", rp_data_reads);
		return;
	}
	printf("Mac CD: boot repulse — re-inserting %s (%u data reads)\n",
	       rp_path, rp_data_reads);
	rp_firing = 1;
	user_io_file_mount(rp_path, (unsigned char)mac_cdrom_slot(), 0, 0);
	rp_firing = 0;
}

static int audio_frame(uint32_t disc_lba)
{
	if (cd.aframe_lba == (int64_t)disc_lba) return 1;

	int ti = -1;
	for (int i = 0; i < cd.toc.last; i++)
	{
		if (cd.toc.tracks[i].type == TT_CDDA &&
		    (int)disc_lba >= cd.toc.tracks[i].start && (int)disc_lba < cd.toc.tracks[i].end)
			{ ti = i; break; }
	}
	if (ti < 0) return 0;
	cd_track_t *k = &cd.toc.tracks[ti];

	if (cd.is_chd)
	{
		if (mister_chd_read_sector(cd.toc.chd_f, (int)disc_lba + k->offset, 0, 0,
		                           2352, cd.aframe, cd.hunkbuf, &cd.hunknum) != CHDERR_NONE)
			return 0;
		// CHD stores CD-DA byteswapped; the window contract is bin byte order
		for (int i = 0; i < 2352; i += 2)
		{
			uint8_t x = cd.aframe[i];
			cd.aframe[i] = cd.aframe[i + 1];
			cd.aframe[i + 1] = x;
		}
	}
	else
	{
		diskled_on();
		if (!FileSeek(&k->f, (uint64_t)((int)disc_lba + k->offset) * k->sector_size, SEEK_SET))
			return 0;
		if (FileReadAdv(&k->f, cd.aframe, 2352) != 2352) return 0;
	}
	cd.aframe_lba = disc_lba;
	return 1;
}

void mac_cdrom_fill(int index, uint64_t lba, uint8_t *buf, int sz)
{
	memset(buf, 0, sz);
	if (index != mac_cdrom_slot() || !cd.active || (sz != 512 && sz != 2352)) return;

	// data-window reads = the guest genuinely reading (boot-repulse skip signal)
	if (lba < MAC_CDROM_AUDIO_BLK) rp_data_reads++;

	// whole-frame CD-DA (user_io sets sz=2352): lba = MAC_CDROM_AUDIO_BLK + disc_lba
	if (sz == 2352)
	{
		if (lba >= MAC_CDROM_AUDIO_BLK && lba < MAC_CDROM_TOC_BLK &&
		    audio_frame((uint32_t)(lba - MAC_CDROM_AUDIO_BLK)))
			memcpy(buf, cd.aframe, 2352);
		return;
	}

	// TOC blob window
	if (lba >= MAC_CDROM_TOC_BLK && lba < MAC_CDROM_TOC_BLK + MAC_CDROM_TOC_BLKS)
	{
		memcpy(buf, cd.tocblob + (lba - MAC_CDROM_TOC_BLK) * 512, 512);
		return;
	}

	// raw audio window: 5 blocks per disc sector (2352 bytes + 208 pad)
	if (lba >= MAC_CDROM_AUDIO_BLK && lba < MAC_CDROM_TOC_BLK)
	{
		uint64_t rel  = lba - MAC_CDROM_AUDIO_BLK;
		uint32_t dlba = (uint32_t)(rel / 5);
		uint32_t part = (uint32_t)(rel % 5);
		if (!audio_frame(dlba)) return;          // silence
		uint32_t off = part * 512;
		if (off < 2352) memcpy(buf, cd.aframe + off, (2352 - off) < 512 ? (2352 - off) : 512);
		return;
	}

	// data window: de-headered 2048-byte sectors of the data track
	uint64_t byte_pos = lba * 512ULL;
	uint64_t cd_lba   = byte_pos >> 11;
	uint32_t off      = byte_pos & 2047;   // 512-blocks never straddle a sector

	if (cd_lba >= cd.sectors) return;      // past leadout: zeros

	// Audio-only disc: no data track exists, so there is nothing to serve
	if (cd.trk < 0) return;

	cd_track_t *k = &cd.toc.tracks[cd.trk];
	if (cd.is_chd)
	{
		int chd_lba = (int)cd_lba + k->start + k->offset;
		if (mister_chd_read_sector(cd.toc.chd_f, chd_lba, 0, cd.data_soff + off,
		                           sz, buf, cd.hunkbuf, &cd.hunknum) != CHDERR_NONE)
			memset(buf, 0, sz);
	}
	else
	{
		diskled_on();
		uint64_t src_frame = (uint64_t)((int)cd_lba + k->start + k->offset);
		if (FileSeek(&k->f, src_frame * k->sector_size + cd.data_soff + off, SEEK_SET))
			FileReadAdv(&k->f, buf, sz);
	}
}
