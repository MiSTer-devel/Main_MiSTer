#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <algorithm>
#include <string>
#include <vector>

#include "../../file_io.h"
#include "../../user_io.h"
#include "../../spi.h"
#include "../../hardware.h"
#include "../../menu.h"
#include "../../cd.h"
#include "../chd/mister_chd.h"
#include "../arcade/mra_loader.h"
#include "marty.h"
#include "marty_db.h"

#define UIO_MARTY_GET_REQ 0x70
#define UIO_MARTY_ACK_REQ 0x71

// OSD options the module reads back
#define OPT_CARD_AUTO   "[26]"     // 0 = automount an IC card named after the CD
#define OPT_FDD_AUTO    "[52]"     // 0 = mount a floppy beside the CD that shares its name
#define OPT_TWO_FDD     "[31]"     // 1 = the second drive is fitted
#define OPT_SETTINGS_DB "[5]"      // 0 = set the machine up from the settings database
#define OPT_CD_RESET    "[51]"     // 0 = a CD mounted while the core runs resets it
#define OPT_RAM         "[9:8]"
#define OPT_SPEED       "[50:49]"
#define OPT_PAD1        "[12:10]"
#define OPT_PAD2        "[46:44]"
#define OPT_MACHINE     "[27]"

#define CARD_KB 4096               // every card is the largest TICMFMT formats
// the two Towns system disks as 512-byte blocks: Fujitsu M2611SA (1334
// cylinders x 2 heads x 34 sectors, sold as 40 MB) and M2612SA (4 heads,
// sold as 80/90 MB)
static const uint32_t hdd_blocks[] = { 1334 * 2 * 34, 1334 * 4 * 34 };

static char cd_name[256];         // CD image name without path or extension

// Savestates belong to the media the machine runs from, not to the core, so
// the .ss files are named after it: the CD when a disc is in, else the
// floppy, else the hard disk. Kept per slot and re-evaluated on every mount
// and eject, because ejecting a CD hands the machine back to the floppy or
// the disk it was booted from.
static char ss_media[3][1024];    // 0 CD, 1 floppy, 2 hard disk
static int  cmos_chosen;          // a CMOS image came from the config or the OSD

// The CMOS lives here as one 8 KB copy: the core saves it as sixteen
// blocks every time the OSD opens, and sixteen synchronous card writes
// would hold the menu for a second or two. Blocks land in the copy and
// the file is written once, shortly after the last one.
#define CMOS_SIZE 8192
static uint8_t  cmos_ram[CMOS_SIZE];
static char     cmos_path[1024];
static int      cmos_dirty;
static unsigned long cmos_flush_at;

static void cmos_flush()
{
	fileTYPE f;
	if (!FileOpenEx(&f, cmos_path, O_CREAT | O_RDWR)) { printf("Marty: cannot write %s\n", cmos_path); return; }
	if (!FileWriteAdv(&f, cmos_ram, CMOS_SIZE)) printf("Marty: cannot write %s\n", cmos_path);
	else if (f.filp) { fflush(f.filp); fsync(fileno(f.filp)); }
	FileClose(&f);
	cmos_dirty = 0;
}

void marty_read_cmos(uint8_t *buf, uint32_t lba, int cnt)
{
	for (int i = 0; i < cnt; i++, lba++)
	{
		if (lba < CMOS_SIZE / 512) memcpy(buf + i * 512, cmos_ram + lba * 512, 512);
		else memset(buf + i * 512, 0, 512);
	}
}

void marty_write_cmos(const uint8_t *buf, uint32_t lba)
{
	if (lba >= CMOS_SIZE / 512) return;
	memcpy(cmos_ram + lba * 512, buf, 512);
	cmos_dirty = 1;
	cmos_flush_at = GetTimer(300);
}

// show the image on the OSD line and keep it for the next start, the way
// a selection in the file browser is kept
static void remember(int index, const char *path)
{
	char cfg[64], buf[1024];
	StoreIdx_S(index, path);
	sprintf(cfg, "%s.s%d", user_io_get_core_name(), index);
	memset(buf, 0, sizeof(buf));
	strncpy(buf, path, sizeof(buf) - 1);
	FileSaveConfig(cfg, buf, sizeof(buf));
}

// ---------------------------------------------------------------------------
// CD-ROM: absolute LBA space (0 = 00:00:00, first track data at 150), raw
// 2352-byte sectors from CUE/BIN, CHD or ISO.

enum { TRK_AUDIO = 0, TRK_MODE1 = 1, TRK_MODE2 = 2 };

struct cd_track
{
	int file;          // index into cd_files
	int ss;            // bytes per sector in the file: 2352, 2336 or 2048
	int type;
	int idx0;          // absolute LBA where the pregap (index 0) starts
	int start;         // absolute LBA of index 1
	int end;           // exclusive
	int gap;           // sectors before start that are not in the file (PREGAP)
	int fbase;         // absolute LBA of the file's first sector as this track sees it
};

struct cd_file
{
	fileTYPE f;
	int base;          // absolute LBA of the file's first sector
};

static cd_track  cd_tracks[100];
static cd_file   cd_files[100];
static int       cd_ntracks, cd_nfiles;
static int       cd_present;
static toc_t     chd_toc;
static uint8_t  *chd_hunkbuf;
static int       chd_hunknum = -1;
static fileTYPE  cd_sub;           // CloneCD .sub beside a cue: 96 bytes per sector, P..W as 12-byte blocks

static void cd_unload()
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
}

// the .sub with the cue's name, else with the first track file's name
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
		if (FileOpen(&cd_sub, path)) { printf("Marty: subcode from %s\n", path); return; }
	}
}

// the drive hands subcode out one byte per frame with P in bit 7 down to
// W in bit 0; dumps keep each channel as its own 12-byte block
static void sub_interleave(const uint8_t *cooked, uint8_t *raw)
{
	for (int k = 0; k < MARTY_CD_SUB; k++)
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
	return (int)(cd_files[fi].f.size / ss);
}

// close the track before a new file or the end of the cue
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

	int pos = 150;         // absolute LBA where the next file starts
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
			if (cd_nfiles >= 100 || !FileOpen(&cd_files[cd_nfiles].f, fname))
			{
				printf("Marty: cannot open track file %s\n", fname);
				return 0;
			}
			cd_files[cd_nfiles].base = pos;
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
			else { printf("Marty: unsupported track type: %s\n", line); return 0; }
		}
		else if (t >= 0 && !strncasecmp(line, "PREGAP ", 7))
		{
			int g = msf_frames(line + 7);
			// a gap shifts this and later tracks of the file, never the ones already mapped
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
		// a pregap the CHD does not store was added to the previous track's
		// end by the loader; it belongs to this track and reads as silence
		int prev_end = i ? chd_toc.tracks[i - 1].end : 0;
		int stored = (ct->start - prev_end) == ct->indexes[1];
		tr->gap = stored ? 0 : ct->indexes[1];
		tr->idx0 = i ? tr->start - ct->indexes[1] : 0;
		if (i && tr->gap) cd_tracks[i - 1].end = tr->idx0;
	}
	return 1;
}

static int cd_load(const char *filename)
{
	cd_unload();
	const char *ext = strrchr(filename, '.');
	int ok = 0;
	if (ext && !strcasecmp(ext, ".chd")) ok = cd_load_chd(filename);
	else if (ext && !strcasecmp(ext, ".cue")) ok = cd_load_cue(filename);
	else ok = cd_load_iso(filename);
	if (!ok) { cd_unload(); return 0; }
	if (!chd_toc.chd_f) cd_open_sub(filename);
	cd_present = 1;
	for (int i = 0; i < cd_ntracks; i++)
		printf("Marty: track %d type %d ss %d idx0 %d start %d end %d gap %d\n", i + 1,
			cd_tracks[i].type, cd_tracks[i].ss, cd_tracks[i].idx0, cd_tracks[i].start, cd_tracks[i].end, cd_tracks[i].gap);
	return 1;
}

// track starts at 4 + 4t, mode-2 bitmap at 408, pause (index 0) starts at 512 + 4t
static void cd_send_toc()
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
	user_io_set_index(MARTY_TOC_INDEX);
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
	memset(buf, 0, MARTY_CD_BLOCK);
	if (!cd_present) return;

	int t;
	for (t = 0; t < cd_ntracks; t++) if (lba >= cd_tracks[t].idx0 && lba < cd_tracks[t].end) break;
	if (t == cd_ntracks) return;
	cd_track *tr = &cd_tracks[t];
	if (lba < tr->start && lba >= tr->start - tr->gap) return;   // pregap not in the image

	int ok = 0;
	int off = (tr->ss == 2048) ? 16 : (tr->ss == 2336) ? 16 : 0;
	if (chd_toc.chd_f)
	{
		int chd_lba = lba - 150 + chd_toc.tracks[t].offset;
		ok = mister_chd_read_sector(chd_toc.chd_f, chd_lba, off, 0, tr->ss, buf, chd_hunkbuf, &chd_hunknum) == CHDERR_NONE;
		if (ok && tr->type == TRK_AUDIO)
			for (int i = 0; i < MARTY_CD_SECTOR; i += 2) { uint8_t x = buf[i]; buf[i] = buf[i + 1]; buf[i + 1] = x; }
	}
	else
	{
		fileTYPE *f = &cd_files[tr->file].f;
		__off64_t pos = (__off64_t)(lba - tr->fbase) * tr->ss;
		ok = FileSeek(f, pos, SEEK_SET) && FileReadAdv(f, buf + off, tr->ss);
	}
	if (!ok) { memset(buf, 0, MARTY_CD_SECTOR); return; }
	if (tr->ss != 2352) cd_make_header(buf, lba, tr->type == TRK_MODE2 ? 2 : 1);

	// the sector's subcode after it, when the image carries any
	uint8_t cooked[MARTY_CD_SUB];
	uint8_t *sub = buf + MARTY_CD_SECTOR;
	if (chd_toc.chd_f)
	{
		cd_track_t *ct = &chd_toc.tracks[t];
		int chd_lba = lba - 150 + ct->offset;
		if (ct->sbc_type == SUBCODE_RW_RAW)
			mister_chd_read_sector(chd_toc.chd_f, chd_lba, 0, MARTY_CD_SECTOR, MARTY_CD_SUB, sub, chd_hunkbuf, &chd_hunknum);
		else if (ct->sbc_type == SUBCODE_RW &&
		         mister_chd_read_sector(chd_toc.chd_f, chd_lba, 0, MARTY_CD_SECTOR, MARTY_CD_SUB, cooked, chd_hunkbuf, &chd_hunknum) == CHDERR_NONE)
			sub_interleave(cooked, sub);
	}
	else if (cd_sub.opened() && tr->file == 0)
	{
		__off64_t pos = (__off64_t)(lba - tr->fbase) * MARTY_CD_SUB;
		if (FileSeek(&cd_sub, pos, SEEK_SET) && FileReadAdv(&cd_sub, cooked, MARTY_CD_SUB)) sub_interleave(cooked, sub);
	}
}

void marty_read_cd(uint8_t *buf, int lba, int cnt)
{
	for (int i = 0; i < cnt; i++) cd_read_one(buf + i * MARTY_CD_BLOCK, lba + i);
}

// ---------------------------------------------------------------------------
// Floppy: D88/D77 held in memory, raw images read in place; both served as
// 16 KB track records.

#define D88_HDR      0x2B0
#define D88_TRACKS   164
#define REC_TABLE    4
#define REC_DATA     256
#define REC_MAX_SEC  32
#define REC_SLOTS    0x3FC0   // 32 x 16-bit slot lengths, sent when the sectors overlap

struct fd_image
{
	int      present;
	int      d88;
	int      wp;
	int      hd;             // 2HD data rate
	uint8_t *buf;            // D88: the whole file
	uint32_t size;
	fileTYPE f;              // raw image
	int      cyls, spt, n;   // raw geometry
};

static fd_image fds[2];   // drive 0 (S1) and drive 1 (S5)
static fd_image *fd_of(int index) { return &fds[index == MARTY_SLOT_FDD2]; }

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void wr16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

static void fd_unload(fd_image *fd)
{
	if (fd->buf) free(fd->buf);
	if (fd->f.opened()) FileClose(&fd->f);
	memset((void *)fd, 0, sizeof(*fd));
}

static int fd_raw_geometry(fd_image *fd, uint32_t size)
{
	switch (size)
	{
	case 1261568: fd->cyls = 77; fd->spt = 8;  fd->n = 3; fd->hd = 1; return 1;   // 1232 KB
	case 1228800: fd->cyls = 80; fd->spt = 15; fd->n = 2; fd->hd = 1; return 1;   // 1.2 MB
	case 1474560: fd->cyls = 80; fd->spt = 18; fd->n = 2; fd->hd = 1; return 1;   // 1.44 MB
	case 737280:  fd->cyls = 80; fd->spt = 9;  fd->n = 2; fd->hd = 0; return 1;   // 720 KB
	case 655360:  fd->cyls = 80; fd->spt = 8;  fd->n = 2; fd->hd = 0; return 1;   // 640 KB
	}
	// 1232 KB dumps also come short (unused cylinders dropped) or with odd
	// filler after the last cylinder; anything near that size is one
	if (size >= 1000 * 1024 && size < 1300 * 1024) { fd->cyls = 77; fd->spt = 8; fd->n = 3; fd->hd = 1; return 1; }
	return 0;
}

static int fd_load(fd_image *fd, const char *filename)
{
	fd_unload(fd);
	fd->wp = !FileCanWrite(filename);
	if (!FileOpenEx(&fd->f, filename, fd->wp ? O_RDONLY : (O_RDWR | O_SYNC))) return 0;

	const char *ext = strrchr(filename, '.');
	int d88 = ext && (!strcasecmp(ext, ".d88") || !strcasecmp(ext, ".d77"));
	if (d88)
	{
		if (fd->f.size < D88_HDR || fd->f.size > (16 << 20)) { fd_unload(fd); return 0; }
		fd->size = fd->f.size;
		fd->buf = (uint8_t *)malloc(fd->size);
		if (!fd->buf || !FileReadAdv(&fd->f, fd->buf, fd->size)) { fd_unload(fd); return 0; }
		// only the first disk of a multi-disk file
		uint32_t disk_size = rd32(fd->buf + 0x1C);
		if (disk_size && disk_size < fd->size) fd->size = disk_size;
		fd->d88 = 1;
		fd->wp |= (fd->buf[0x1A] & 0x10) != 0;
		fd->hd = fd->buf[0x1B] == 0x20;
	}
	else if (!fd_raw_geometry(fd, fd->f.size))
	{
		printf("Marty: unknown floppy image size %lld\n", (long long)fd->f.size);
		fd_unload(fd);
		return 0;
	}
	fd->present = 1;
	return 1;
}

static uint32_t d88_track_offset(const fd_image *fd, uint32_t trk)
{
	if (trk >= D88_TRACKS) return 0;
	uint32_t off = rd32(fd->buf + 0x20 + 4 * trk);
	return (off >= D88_HDR && off < fd->size) ? off : 0;
}

// byte length of the D88 track at off, walking its sector headers
static uint32_t d88_track_len(const fd_image *fd, uint32_t off)
{
	if (!off) return 0;
	uint32_t p = off, count = 0, n = 0;
	while (p + 16 <= fd->size)
	{
		if (!n) n = rd16(fd->buf + p + 4);
		p += 16 + rd16(fd->buf + p + 14);
		if (++count >= n || !n) break;
	}
	return (p > fd->size ? fd->size : p) - off;
}

// Slot lengths for a track that does not fit at the drive's standard
// pitch (62 + size + GAP3 per sector), which is how protected disks put
// their extra sectors on a track: a sector whose data carries the next
// sector's ID pattern overlaps it there; then the gaps shrink, then the
// data slots of bad-CRC sectors, then everything. Returns 0 when the
// standard pitch fits and no table is needed.
static int rec_slot_table(const uint8_t *rec, int n, uint16_t *pitch)
{
	const uint32_t track_len = (rec[1] & 1) ? 10416 : 6250;
	uint8_t embedded[REC_MAX_SEC] = {0};
	uint32_t total = 146, std_total = 146;
	for (int i = 0; i < n; i++)
	{
		const uint8_t *e = rec + REC_TABLE + 8 * i;
		int nn = e[3] > 3 ? 3 : e[3];
		uint32_t size = 128u << nn;
		uint32_t gap3 = nn == 3 ? 116 : nn == 2 ? ((rec[1] & 1) ? 84 : 54) : nn == 1 ? 34 : 22;
		pitch[i] = 62 + size + gap3;
		std_total += pitch[i];
		if (i + 1 < n)
		{
			const uint8_t *x = e + 8, *data = rec + rd16(e + 6);
			uint8_t pat[8] = {0xA1, 0xA1, 0xA1, 0xFE, x[0], x[1], x[2], x[3]};
			for (uint32_t k = 12; k + 8 <= size; k++)
				if (!memcmp(data + k, pat, 8)) { pitch[i] = k + 48; embedded[i] = 1; break; }
		}
		total += pitch[i];
	}
	if (std_total <= track_len) return 0;
	for (int rule = 0; rule < 3 && total > track_len; rule++)
	{
		for (int moved = 1; moved && total > track_len;)
		{
			moved = 0;
			for (int i = 0; i < n && total > track_len; i++)
			{
				const uint8_t *e = rec + REC_TABLE + 8 * i;
				if (embedded[i]) continue;
				uint32_t floor = rule == 0 ? 62 + (128u << (e[3] > 3 ? 3 : e[3])) : 62 + 16;
				if (rule == 1 && e[4] == 0) continue;
				if (pitch[i] > floor) { pitch[i]--; total--; moved = 1; }
			}
		}
	}
	return 1;
}

void marty_read_track(int index, uint8_t *rec, uint32_t lba)
{
	fd_image *fd = fd_of(index);
	memset(rec, 0, MARTY_TRACK_REC);
	if (!fd->present) { rec[1] = 2; return; }
	rec[1] = (fd->hd ? 1 : 0) | (fd->wp ? 4 : 0);

	int count = 0;
	uint32_t data = REC_DATA;
	if (fd->d88)
	{
		uint32_t p = d88_track_offset(fd, lba), n = 0;
		if (!p) { rec[1] |= 2; return; }
		while (p + 16 <= fd->size && count < REC_MAX_SEC)
		{
			const uint8_t *h = fd->buf + p;
			if (!n) n = rd16(h + 4);
			uint32_t len = rd16(h + 14);
			uint32_t sz = 128u << (h[3] > 3 ? 3 : h[3]);
			if (data + sz > MARTY_TRACK_REC) break;
			uint8_t *e = rec + REC_TABLE + 8 * count;
			e[0] = h[0]; e[1] = h[1]; e[2] = h[2]; e[3] = h[3];
			e[4] = h[8];
			e[5] = h[6];
			wr16(e + 6, data);
			uint32_t avail = (p + 16 + len <= fd->size) ? len : (fd->size - p - 16);
			memcpy(rec + data, h + 16, avail < sz ? avail : sz);
			data += sz;
			count++;
			p += 16 + len;
			if (count >= (int)n) break;
		}
		uint16_t pitch[REC_MAX_SEC];
		if (count && rec_slot_table(rec, count, pitch))
		{
			rec[1] |= 8;
			for (int i = 0; i < count; i++) wr16(rec + REC_SLOTS + 2 * i, pitch[i]);
		}
	}
	else
	{
		uint32_t cyl = lba >> 1, head = lba & 1;
		if (cyl >= (uint32_t)fd->cyls) { rec[1] |= 2; return; }
		uint32_t sz = 128u << fd->n;
		FileSeek(&fd->f, (__off64_t)(cyl * 2 + head) * fd->spt * sz, SEEK_SET);
		for (int r = 1; r <= fd->spt; r++)
		{
			uint8_t *e = rec + REC_TABLE + 8 * count;
			e[0] = cyl; e[1] = head; e[2] = r; e[3] = fd->n;
			wr16(e + 6, data);
			if (!FileReadAdv(&fd->f, rec + data, sz)) memset(rec + data, 0, sz);   // short image
			data += sz;
			count++;
		}
	}
	rec[0] = count;
}

// rebuild one D88 track from a record and put it back in the file
static void d88_write_track(fd_image *fd, const uint8_t *rec, uint32_t trk)
{
	if (trk >= D88_TRACKS) return;
	int count = rec[0] > REC_MAX_SEC ? REC_MAX_SEC : rec[0];
	uint32_t new_len = 0;
	for (int i = 0; i < count; i++) new_len += 16 + (128u << (rec[REC_TABLE + 8 * i + 3] & 3));

	uint32_t old_off = d88_track_offset(fd, trk);
	uint32_t old_len = d88_track_len(fd, old_off);
	uint8_t *trk_buf = (uint8_t *)malloc(new_len ? new_len : 1);
	if (!trk_buf) return;
	uint32_t p = 0;
	for (int i = 0; i < count; i++)
	{
		const uint8_t *e = rec + REC_TABLE + 8 * i;
		uint32_t sz = 128u << (e[3] & 3);
		uint8_t *h = trk_buf + p;
		memset(h, 0, 16);
		h[0] = e[0]; h[1] = e[1]; h[2] = e[2]; h[3] = e[3] & 3;
		wr16(h + 4, count);
		h[6] = e[5];
		h[7] = (e[4] == 0x10) ? 0x10 : 0;
		h[8] = e[4];
		wr16(h + 14, sz);
		uint32_t src = rd16(e + 6);
		if (src + sz <= MARTY_TRACK_REC) memcpy(h + 16, rec + src, sz);
		p += 16 + sz;
	}

	if (old_off && old_len == new_len)
	{
		memcpy(fd->buf + old_off, trk_buf, new_len);
		if (!FileSeek(&fd->f, old_off, SEEK_SET) || !FileWriteAdv(&fd->f, fd->buf + old_off, new_len))
			printf("Marty: floppy write failed, track %u\n", trk);
	}
	else
	{
		// lay the disk out again in track order; the file shrinks or grows
		uint32_t total = D88_HDR;
		for (uint32_t t = 0; t < D88_TRACKS; t++)
			total += (t == trk) ? new_len : d88_track_len(fd, d88_track_offset(fd, t));
		uint8_t *nb = (uint8_t *)malloc(total);
		if (nb)
		{
			memcpy(nb, fd->buf, D88_HDR);
			uint32_t out = D88_HDR;
			for (uint32_t t = 0; t < D88_TRACKS; t++)
			{
				uint32_t off = (t == trk) ? 0 : d88_track_offset(fd, t);
				uint32_t len = (t == trk) ? new_len : d88_track_len(fd, off);
				const uint8_t *src = (t == trk) ? trk_buf : fd->buf + off;
				if (!len) { wr32(nb + 0x20 + 4 * t, 0); continue; }
				memcpy(nb + out, src, len);
				wr32(nb + 0x20 + 4 * t, out);
				out += len;
			}
			wr32(nb + 0x1C, out);
			free(fd->buf);
			fd->buf = nb;
			fd->size = out;
			if (!FileSeek(&fd->f, 0, SEEK_SET) || !FileWriteAdv(&fd->f, fd->buf, fd->size))
				printf("Marty: floppy write failed, track %u\n", trk);
			else if (fd->f.filp) ftruncate(fileno(fd->f.filp), fd->size);
		}
	}
	free(trk_buf);
}

void marty_write_track(int index, const uint8_t *rec, uint32_t lba)
{
	fd_image *fd = fd_of(index);
	if (!fd->present || fd->wp) return;
	if (fd->d88) { d88_write_track(fd, rec, lba); return; }

	// raw image: sectors land by their ID, nothing else can change
	uint32_t cyl = lba >> 1, head = lba & 1;
	if (cyl >= (uint32_t)fd->cyls) return;
	uint32_t sz = 128u << fd->n;
	int count = rec[0] > REC_MAX_SEC ? REC_MAX_SEC : rec[0];
	for (int i = 0; i < count; i++)
	{
		const uint8_t *e = rec + REC_TABLE + 8 * i;
		if (e[0] != cyl || e[1] != head || e[3] != fd->n || e[2] < 1 || e[2] > fd->spt) continue;
		uint32_t src = rd16(e + 6);
		if (src + sz > MARTY_TRACK_REC) continue;
		if (!FileSeek(&fd->f, ((__off64_t)(cyl * 2 + head) * fd->spt + e[2] - 1) * sz, SEEK_SET) ||
		    !FileWriteAdv(&fd->f, (void *)(rec + src), sz))
			printf("Marty: floppy write failed, track %u\n", lba);
	}
}

// ---------------------------------------------------------------------------
// Blank images, formatted the way TOWNS OS FORMAT and TICMFMT leave them:
// an "IPL4" boot sector whose entry returns to the ROM, the BPB, empty
// FATs and root directory.

struct fat_geom { int bps, spc, resv, fats, root, total, media, spf, spt, heads; };

static void fat_boot_sector(uint8_t *b, const fat_geom &g)
{
	memset(b, 0, g.bps);
	memcpy(b, "IPL4\xEB\x58", 6);   // jmp short 0x5E
	wr16(b + 0x0B, g.bps);
	b[0x0D] = g.spc;
	wr16(b + 0x0E, g.resv);
	b[0x10] = g.fats;
	wr16(b + 0x11, g.root);
	wr16(b + 0x13, g.total);
	b[0x15] = g.media;
	wr16(b + 0x16, g.spf);
	wr16(b + 0x18, g.spt);
	wr16(b + 0x1A, g.heads);
	b[0x5E] = 0xCB;   // retf
}

// one formatted volume of g.bps * g.total bytes
static void fat_volume(uint8_t *img, const fat_geom &g, int fat16)
{
	memset(img, 0, (size_t)g.bps * g.total);
	fat_boot_sector(img, g);
	for (int f = 0; f < g.fats; f++)
	{
		uint8_t *at = img + (size_t)(g.resv + f * g.spf) * g.bps;
		at[0] = g.media; at[1] = 0xFF; at[2] = 0xFF;
		if (fat16) at[3] = 0xFF;
	}
}

// the Towns floppy: 2HD 1232 KB as TOWNS OS formats it
struct fd_format { uint32_t size; int cyls; fat_geom g; };
static const fd_format fd_1232k = { 1261568, 77, { 1024, 1, 1, 2, 192, 1232, 0xFE, 2, 8, 2 } };

// write the raw image as a D88 with one sector per header
static int fd_save_d88(const char *path, const uint8_t *img, const fd_format *fmt)
{
	uint32_t ss = fmt->g.bps;
	int n = fmt->g.bps == 1024 ? 3 : 2;
	uint32_t tracks = fmt->cyls * 2;
	uint32_t total = D88_HDR + tracks * fmt->g.spt * (16 + ss);
	uint8_t *d = (uint8_t *)calloc(total, 1);
	if (!d) return 0;
	d[0x1B] = (fmt->g.media == 0xF9) ? 0x10 : 0x20;
	wr32(d + 0x1C, total);
	uint32_t out = D88_HDR;
	const uint8_t *src = img;
	for (uint32_t t = 0; t < tracks; t++)
	{
		wr32(d + 0x20 + 4 * t, out);
		for (int r = 1; r <= fmt->g.spt; r++)
		{
			uint8_t *h = d + out;
			h[0] = t >> 1; h[1] = t & 1; h[2] = r; h[3] = n;
			wr16(h + 4, fmt->g.spt);
			wr16(h + 14, ss);
			memcpy(h + 16, src, ss);
			src += ss;
			out += 16 + ss;
		}
	}
	int ok = FileSave(path, d, total);
	free(d);
	return ok;
}

// IC card as TICMFMT formats it: FAT12, 512-byte sectors, 256 KB steps
static std::vector<uint8_t> card_blank;
static const std::vector<uint8_t> &card_image(uint32_t kb)
{
	int total = kb * 2;
	int clusters = (total - 1 - 16) / 2;
	int spf = ((clusters + 2) * 3 / 2 + 511) / 512;
	fat_geom g = { 512, 2, 1, 2, kb >= 2048 ? 256 : 128, total, 0xF8, spf, 8, 2 };
	card_blank.resize((size_t)kb * 1024);
	fat_volume(card_blank.data(), g, 0);
	return card_blank;
}

// SCSI disk of the given block count: blank IPL block, the Fujitsu
// partition table at block 1, one MS-DOS partition (2048-byte sectors,
// FAT16) from block 3 to the end
static int hdd_save(const char *path, uint32_t total)
{
	uint32_t part_blocks = ((total - 3) / 4) * 4;
	fat_geom g = { 2048, 2, 1, 2, 1024, (int)(part_blocks / 4), 0xFE, 0, 0, 0 };
	// FAT sectors for the clusters the partition holds
	int clusters = (g.total - g.resv - g.root * 32 / g.bps) / g.spc;
	g.spf = ((clusters + 2) * 2 + g.bps - 1) / g.bps;
	fileTYPE f;
	if (!FileOpenEx(&f, path, O_CREAT | O_RDWR | O_TRUNC)) return 0;

	uint8_t head[3 * 512];
	memset(head, 0, sizeof(head));
	uint8_t *pt = head + 512;
	memcpy(pt, "\x95\x78\x8E\x6D\x92\xCA", 6);   // the maker's name in Shift-JIS
	wr32(pt + 6, 3); wr32(pt + 10, total - 2); wr16(pt + 14, 512);
	for (int i = 0; i < 10; i++) memset(pt + 32 + i * 48 + 16, ' ', 32);
	pt[33] = 0x01;
	wr32(pt + 34, 3); wr32(pt + 38, part_blocks);
	memcpy(pt + 48, "MS-DOS", 6); memcpy(pt + 64, "DATA", 4);
	int ok = FileWriteAdv(&f, head, sizeof(head));

	// the partition, then zeros to the end of the image
	static uint8_t chunk[1 << 20];
	uint32_t hdr = (g.resv + g.fats * g.spf) * g.bps + g.root * 32;
	uint32_t left = (uint32_t)g.bps * g.total;
	int first = 1;
	while (ok && left)
	{
		uint32_t n = left > sizeof(chunk) ? sizeof(chunk) : left;
		memset(chunk, 0, n);
		if (first)
		{
			// the header fits the first chunk: boot sector and FATs
			std::vector<uint8_t> vol(hdr);
			fat_boot_sector(vol.data(), g);
			for (int k = 0; k < g.fats; k++)
			{
				uint8_t *at = vol.data() + (size_t)(g.resv + k * g.spf) * g.bps;
				at[0] = g.media; at[1] = 0xFF; at[2] = 0xFF; at[3] = 0xFF;
			}
			memcpy(chunk, vol.data(), hdr < n ? hdr : n);
			first = 0;
		}
		ok = FileWriteAdv(&f, chunk, n);
		left -= n;
	}
	for (uint32_t pad = (total - 3) * 512 - (uint32_t)g.bps * g.total; ok && pad; )
	{
		uint32_t n = pad > sizeof(chunk) ? sizeof(chunk) : pad;
		memset(chunk, 0, n);
		ok = FileWriteAdv(&f, chunk, n);
		pad -= n;
	}
	FileClose(&f);
	return ok;
}

// <dir>/<cd name or blank>[_n].<ext>, never an existing file. A floppy goes
// beside the mounted CD, else into the games folder; a disk goes where the
// slot's last image came from, else the games folder; cards stay in saves.
static void new_image_path(char *out, size_t size, const char *ext, int slot)
{
	char dir[1024];
	const char *last = (slot == MARTY_SLOT_HDD || slot == MARTY_SLOT_HDD2) ? GetIdx_S(slot) : ((slot == MARTY_SLOT_FDD || slot == MARTY_SLOT_FDD2) && cd_name[0]) ? GetIdx_S(MARTY_SLOT_CD) : "";
	const char *cut = strrchr(last, '/');
	if (cut) { size_t n = cut - last; if (n >= sizeof(dir)) n = sizeof(dir) - 1; memcpy(dir, last, n); dir[n] = 0; }
	else if (slot == MARTY_SLOT_CARD) sprintf(dir, "%s/%s", SAVE_DIR, CoreName2);
	else strcpy(dir, HomeDir());
	FileCreatePath(dir);
	char base[300];
	snprintf(base, sizeof(base), "%s%s", cd_name[0] ? cd_name : "blank", (slot == MARTY_SLOT_FDD2 || slot == MARTY_SLOT_HDD2) ? "_1" : "");
	for (int n = 1; n < 100; n++)
	{
		if (n == 1) snprintf(out, size, "%s/%s.%s", dir, base, ext);
		else snprintf(out, size, "%s/%s_%d.%s", dir, base, n, ext);
		if (!FileExists(out)) return;
	}
}

static void card_path(char *out)
{
	sprintf(out, "%s/%s/%s.icm", SAVE_DIR, CoreName2, cd_name);
}

static void mount_card_for_cd()
{
	if (!cd_name[0] || user_io_status_get(OPT_CARD_AUTO)) return;
	char path[1024];
	card_path(path);
	user_io_set_index(MARTY_SLOT_CARD);
	if (FileExists(path)) user_io_file_mount(path, MARTY_SLOT_CARD);
	else
	{
		card_image(CARD_KB);
		user_io_file_mount(path, MARTY_SLOT_CARD, 1, CARD_KB * 1024);
	}
	StoreIdx_S(MARTY_SLOT_CARD, path);
}

// a floppy beside the CD with the CD's name, any of the floppy formats;
// <name>_1 goes in the second drive when it is fitted
static const char *fdd_exts[] = { "d88", "D88", "d77", "D77", "hdm", "HDM", "bin", "BIN" };

// judged by size the way fd_load does, so a single-file cue's own .bin is skipped
static int fd_size_ok(const char *path)
{
	struct stat64 *st = getPathStat(path);
	if (!st || !S_ISREG(st->st_mode) || st->st_size > (16 << 20)) return 0;
	const char *ext = strrchr(path, '.');
	if (ext && (!strcasecmp(ext, ".d88") || !strcasecmp(ext, ".d77"))) return st->st_size >= D88_HDR;
	fd_image probe;
	return fd_raw_geometry(&probe, (uint32_t)st->st_size);
}

// mounts <folder of cd_path>/<cd name>[_1].<ext> in the slot when there is one;
// 1 mounted, 0 none there, -1 one there that did not load
static int mount_cd_floppy(const char *cd_path, int slot)
{
	if (!cd_present || !cd_name[0]) return 0;
	const char *cut = strrchr(cd_path, '/');
	int dir = cut ? (int)(cut - cd_path) : 0;
	char path[1200];
	for (const char *e : fdd_exts)
	{
		snprintf(path, sizeof(path), "%.*s%s%s%s.%s", dir, cd_path, dir ? "/" : "", cd_name, slot == MARTY_SLOT_FDD2 ? "_1" : "", e);
		if (!fd_size_ok(path)) continue;
		if (fd_of(slot)->present && !strcmp(GetIdx_S(slot), path)) return 1;
		marty_set_image(slot, path);
		remember(slot, path);
		return fd_of(slot)->present ? 1 : -1;
	}
	return 0;
}

// cd_path is the image being mounted; the stored slot path can still be the last one
static void mount_fdd_for_cd(const char *cd_path)
{
	if (user_io_status_get(OPT_FDD_AUTO)) return;
	mount_cd_floppy(cd_path, MARTY_SLOT_FDD);
	if (user_io_status_get(OPT_TWO_FDD)) mount_cd_floppy(cd_path, MARTY_SLOT_FDD2);
}

// what a promised image reads as before its first write
void marty_fill_blank(int index, uint8_t *buf, uint32_t lba, int cnt)
{
	memset(buf, 0, cnt * 512);
	if (index != MARTY_SLOT_CARD) return;
	size_t at = (size_t)lba * 512, n = (size_t)cnt * 512;
	if (at < card_blank.size()) memcpy(buf, card_blank.data() + at, std::min(n, card_blank.size() - at));
}

// the same image, made whole on the first write
const uint8_t *marty_blank_image(int index, uint32_t *size)
{
	if (index == MARTY_SLOT_CARD) { *size = card_blank.size(); return card_blank.data(); }
	*size = 0;
	return NULL;
}

// one request bit: 0 new floppy, 1 new card, 2 new disk, 3 eject CD,
// 4 eject floppy, 5 eject disk, 6 eject floppy 2, 7 new floppy 2,
// 8 eject disk 2, 9 new disk 2
static int save_blank_floppy(const char *path)
{
	const fd_format *fmt = &fd_1232k;
	uint8_t *img = (uint8_t *)malloc(fmt->size);
	if (!img) return 0;
	fat_volume(img, fmt->g, 0);
	int ok = fd_save_d88(path, img, fmt);
	free(img);
	return ok;
}

static void create_image(int bit, int size_code)
{
	char path[1024], msg[1100];
	if (bit == 0 || bit == 7)
	{
		int slot = (bit == 0) ? MARTY_SLOT_FDD : MARTY_SLOT_FDD2;
		new_image_path(path, sizeof(path), "d88", slot);
		if (!save_blank_floppy(path)) { Info("Floppy image not created", 3000); return; }
		snprintf(msg, sizeof(msg), "New floppy mounted:\n%s", path);
		Info(msg, 3000);
		marty_set_image(slot, path);
		remember(slot, path);
	}
	else if (bit == 1)
	{
		new_image_path(path, sizeof(path), "icm", MARTY_SLOT_CARD);
		const std::vector<uint8_t> &img = card_image(CARD_KB);
		if (!FileSave(path, (void *)img.data(), img.size())) { Info("IC card not created", 3000); return; }
		snprintf(msg, sizeof(msg), "New IC card mounted:\n%s", path);
		Info(msg, 3000);
		marty_set_image(MARTY_SLOT_CARD, path);
		remember(MARTY_SLOT_CARD, path);
	}
	else if (bit == 2 || bit == 9)
	{
		int slot = (bit == 2) ? MARTY_SLOT_HDD : MARTY_SLOT_HDD2;
		new_image_path(path, sizeof(path), "vhd", slot);
		Info("Creating hard disk image...", 60000);
		if (!hdd_save(path, hdd_blocks[size_code & 1])) { Info("Hard disk not created", 3000); return; }
		snprintf(msg, sizeof(msg), "New hard disk mounted:\n%s", path);
		Info(msg, 3000);
		marty_set_image(slot, path);
		remember(slot, path);
	}
	else if (bit >= 3 && bit <= 8)
	{
		int slot = (bit == 3) ? MARTY_SLOT_CD : (bit == 4) ? MARTY_SLOT_FDD : (bit == 5) ? MARTY_SLOT_HDD : (bit == 6) ? MARTY_SLOT_FDD2 : MARTY_SLOT_HDD2;
		marty_set_image(slot, "");
		remember(slot, "");
		Info(bit == 3 ? "CD ejected" : bit == 4 ? "Floppy ejected" : bit == 5 ? "Hard disk ejected" : bit == 6 ? "Floppy 2 ejected" : "Hard disk 2 ejected", 2000);
	}
}

// ---------------------------------------------------------------------------

int marty_block_size(int index, int wire_size)
{
	if (index == MARTY_SLOT_CD) return MARTY_CD_BLOCK;
	if (index == MARTY_SLOT_FDD || index == MARTY_SLOT_FDD2) return MARTY_TRACK_REC;
	return wire_size;
}

static void ss_remember(int which, const char *filename)
{
	snprintf(ss_media[which], sizeof(ss_media[which]), "%s", filename ? filename : "");
}

static void ss_update_key()
{
	const char *name = ss_media[0][0] ? ss_media[0] : ss_media[1][0] ? ss_media[1] : ss_media[2];
	if (name[0]) process_ss(name);
}

// ---------------------------------------------------------------------------
// Settings database: a mounted CD, or a floppy with no CD, names a title and
// the machine is set up for it. The pass at core start runs from marty_init,
// once the saved options are in and reset is held. A machine change while
// the core runs resets it; a disc the database does not know leaves a
// running machine alone, as it may be the next disc of the game in it.

static int  db_ready;
static int  db_busy;
static char db_notice[512];

// the primary volume descriptor of the first data track, NULL when none
static const uint8_t *db_cd_pvd()
{
	if (!cd_present || cd_tracks[0].type == TRK_AUDIO) return NULL;
	static uint8_t buf[MARTY_CD_BLOCK];
	cd_read_one(buf, cd_tracks[0].start + 16);
	for (int off : { 16, 24 })   // mode 1 user data, mode 2 form 1
		if (!memcmp(buf + off, "\x01" "CD001", 6)) return buf + off;
	return NULL;
}

static std::string db_fd_key(const char *path)
{
	static uint8_t head[16384];
	fileTYPE f;
	if (!FileOpen(&f, path, 1)) return "";
	int n = (f.size < (__off64_t)sizeof(head)) ? (int)f.size : (int)sizeof(head);
	int ok = n > 0 && FileReadAdv(&f, head, n) == n;
	uint64_t size = f.size;
	FileClose(&f);
	return ok ? mdb_fd_key(head, n, size) : "";
}

// 2 when the media's own key names the title, 1 when its file name does,
// 0 when nothing does, -1 with nothing mounted
static int db_lookup(const uint8_t *pvd, mdb_settings *s, std::string *title)
{
	const char *path = cd_present ? ss_media[0] : fds[0].present ? ss_media[1] : "";
	if (!path[0]) return -1;
	std::string key = cd_present ? (pvd ? mdb_cd_key(pvd) : "") : db_fd_key(path);
	if (mdb_find(key, s, title)) return 2;

	char name[512];
	const char *p = strrchr(path, '/');
	snprintf(name, sizeof(name), "%s", p ? p + 1 : path);
	char *dot = strrchr(name, '.');
	if (dot && dot > name) *dot = 0;
	return (mdb_find(mdb_exact_key(name), s, title) || mdb_find(mdb_loose_key(name), s, title) ||
	        mdb_find(mdb_japanese_key(name), s, title)) ? 1 : 0;
}

// a notice line holds 30 characters
static void db_say(std::string &out, int &col, const std::string &item)
{
	if (col && col + 2 + (int)item.size() > 30) { out += "\n"; col = 0; }
	else if (col) { out += ", "; col += 2; }
	out += item;
	col += item.size();
}

// the save disk sits beside the CD under its name; an existing one is used
static const char *db_save_disk()
{
	if (!cd_present || fds[0].present || !cd_name[0]) return 0;
	int found = mount_cd_floppy(ss_media[0], MARTY_SLOT_FDD);
	if (found) return found > 0 ? "Save disk mounted" : "Save disk did not load";
	const char *cut = strrchr(ss_media[0], '/');
	int dir = cut ? (int)(cut - ss_media[0]) : 0;
	char path[1200];
	snprintf(path, sizeof(path), "%.*s%s%s.d88", dir, ss_media[0], dir ? "/" : "", cd_name);
	if (FileExists(path)) return "Save disk did not load";   // never over a disk
	if (!save_blank_floppy(path)) return "Save disk not created";
	marty_set_image(MARTY_SLOT_FDD, path);
	remember(MARTY_SLOT_FDD, path);
	return "Save disk created";
}

// what the last lookup found, for db_disks; how is -2 when there is nothing to do
static struct
{
	int how = -2, year = 0;
	mdb_settings s;
	std::string title;
} db_hit;

// Sets the machine up for the mounted media, before the core hears of it.
// Returns 1 when a setting that needs a reset changed while the core runs.
// fresh: a reset is coming anyway, so unknown media gets stock settings as
// it does at start.
static int db_settings(int fresh)
{
	db_hit.how = -2;
	if (!db_ready || db_busy || user_io_status_get(OPT_SETTINGS_DB)) return 0;

	char path[1024], user[1024];
	snprintf(path, sizeof(path), "%s/%s/marty_db.tsv", getRootDir(), HomeDir());
	snprintf(user, sizeof(user), "%s/%s/marty_db_user.tsv", getRootDir(), HomeDir());
	mdb_load(path, user);

	int starting = user_io_status_get("[0]");
	mdb_settings s, stock;
	mdb_defaults(&stock);
	std::string title;
	const uint8_t *pvd = db_cd_pvd();
	int how = db_lookup(pvd, &s, &title);
	if (how <= 0 && !starting && !fresh) return 0;
	if (how <= 0) s = stock;

	// the disc's own creation year, else the title's release year
	int year = pvd ? mdb_cd_year(pvd) : 0;
	if (!year) year = s.year;
	if (!s.speed_given) mdb_date_speed(year, &s.speed);

	// speed switches live, so it alone does not need a reset
	int machine = user_io_status_get(OPT_RAM) != s.ram || user_io_status_get(OPT_MACHINE) != s.machine ||
	              user_io_status_get(OPT_TWO_FDD) != s.fdd;
	user_io_status_set(OPT_PAD1, s.pad1);
	user_io_status_set(OPT_PAD2, s.pad2);
	user_io_status_set(OPT_RAM, s.ram);
	user_io_status_set(OPT_SPEED, s.speed);
	user_io_status_set(OPT_MACHINE, s.machine);
	user_io_status_set(OPT_TWO_FDD, s.fdd);

	db_hit.how = how;
	db_hit.year = year;
	db_hit.s = s;
	db_hit.title = title;
	return machine && !starting;
}

static void core_reset()
{
	user_io_status_set("[0]", 1);
	user_io_status_set("[0]", 0);
}

// Mounts the floppy the title needs and leaves the notice, once the core has the media.
static void db_disks(int reset)
{
	if (db_hit.how == -2) return;
	db_busy = 1;
	int how = db_hit.how, year = db_hit.year;
	const mdb_settings &s = db_hit.s;
	const std::string &title = db_hit.title;
	mdb_settings stock;
	mdb_defaults(&stock);

	static const char *ram[] = { "2 MB", "4 MB", "6 MB", "8 MB" };
	static const char *speed[] = { "Original", "Plus", "Great Scott" };
	static const char *pad[] = { "Marty pad", "6-button pad", "mouse", "analog stick", "analog pad", "nothing", "Capcom pad", "Towns pad" };
	std::string msg = "Unknown disc";
	if (how > 0)
	{
		msg.clear();
		for (char c : title) if (!(c & 0x80) && msg.size() < 30) msg += c;   // the OSD font is ASCII
	}
	int col = 30;
	if (s.ram != stock.ram) db_say(msg, col, ram[s.ram]);
	if (s.speed != stock.speed) db_say(msg, col, speed[s.speed]);
	if (s.pad1 != stock.pad1) db_say(msg, col, std::string("port 1 ") + pad[s.pad1]);
	if (s.pad2 != stock.pad2) db_say(msg, col, std::string("port 2 ") + pad[s.pad2]);
	if (s.machine != stock.machine) db_say(msg, col, s.machine ? "Marty ID" : "FM Towns ID");
	if (s.fdd != stock.fdd) db_say(msg, col, "2 drives");
	if (col == 30) db_say(msg, col, "stock settings");
	if (how == 1) db_say(msg, col, "(name match)");

	if (s.boot)
	{
		// the CD's own floppy also replaces one left from another title
		int found = mount_cd_floppy(ss_media[0], MARTY_SLOT_FDD);
		if (found) msg += found > 0 ? "\nBoot floppy mounted" : "\nBoot floppy did not load";
		else if (!fds[0].present) msg += "\nNeeds a boot floppy";
	}
	else if (s.save)
	{
		const char *disk = db_save_disk();
		if (disk) msg += std::string("\n") + disk;
	}
	if (how >= 0) snprintf(db_notice, sizeof(db_notice), "%s", msg.c_str());
	for (char &c : msg) if (c == '\n') c = '|';
	printf("Marty: settings (%s, %d%s): %s\n", how == 2 ? "media key" : how == 1 ? "name" : how == 0 ? "unknown" : "no media",
	       year, reset ? ", reset" : "", msg.c_str());
	db_hit.how = -2;
	db_busy = 0;
}

static void db_media_changed()
{
	int reset = db_settings(0);
	if (reset) core_reset();
	db_disks(reset);
}

void marty_set_image(int index, const char *filename)
{
	int has = filename && filename[0];
	switch (index)
	{
	case MARTY_SLOT_CD:
	{
		cd_unload();
		cd_name[0] = 0;
		if (has && cd_load(filename))
		{
			const char *p = strrchr(filename, '/');
			strncpy(cd_name, p ? p + 1 : filename, sizeof(cd_name) - 1);
			char *e = strrchr(cd_name, '.');
			if (e) *e = 0;
		}
		ss_remember(0, cd_present ? filename : "");

		// a reset clears what the core was sent, so it goes first
		int reset = cd_present && !user_io_status_get("[0]") && !user_io_status_get(OPT_CD_RESET);
		if (db_settings(reset)) reset = 1;
		if (reset)
		{
			printf("Marty: reset before the CD mount\n");
			core_reset();
		}

		user_io_set_index(MARTY_SLOT_CD);
		user_io_file_mount(cd_present ? filename : "", MARTY_SLOT_CD);
		cd_send_toc();
		if (cd_present) { mount_card_for_cd(); mount_fdd_for_cd(filename); }
		ss_update_key();
		db_disks(reset);
		break;
	}

	case MARTY_SLOT_FDD:
	case MARTY_SLOT_FDD2:
	{
		fd_image *fd = fd_of(index);
		fd_unload(fd);
		if (has) fd_load(fd, filename);
		user_io_set_index(index);
		user_io_file_mount(fd->present ? filename : "", index);
		if (index == MARTY_SLOT_FDD)
		{
			ss_remember(1, fd->present ? filename : "");
			ss_update_key();
			if (!cd_present) db_media_changed();   // with a CD in, the CD decides
		}
		break;
	}

	case MARTY_SLOT_CMOS:
		// the copy follows the chosen file; a missing one starts blank
		if (cmos_dirty) cmos_flush();
		cmos_chosen = has;
		memset(cmos_ram, 0, CMOS_SIZE);
		if (has) { strncpy(cmos_path, filename, sizeof(cmos_path) - 1); FileLoad(cmos_path, cmos_ram, CMOS_SIZE); }
		user_io_set_index(MARTY_SLOT_CMOS);
		user_io_file_mount(has ? filename : "", MARTY_SLOT_CMOS, has, CMOS_SIZE);
		break;

	case MARTY_SLOT_HDD:
		user_io_set_index(index);
		user_io_file_mount(has ? filename : "", index);
		ss_remember(2, has ? filename : "");
		ss_update_key();
		break;

	default:
		user_io_set_index(index);
		user_io_file_mount(has ? filename : "", index);
		break;
	}
}

// A CD named by an MGL goes in before the machine leaves reset, so the ROM
// finds it at power-on. Mounted once the ROM is probing, it counts as a
// disc inserted during the boot and the ROM does not boot from it.
void marty_mgl_premount()
{
	mgl_struct *mgl = mgl_get();
	int kept = 0;
	for (int i = 0; i < mgl->count; i++)
	{
		mgl_item_struct *it = &mgl->item[i];
		if (it->action == MGL_ACTION_LOAD && it->type == 'S' && it->index == MARTY_SLOT_CD)
		{
			char path[1024];
			if (it->path[0] == '/') snprintf(path, sizeof(path), "%s", it->path);
			else snprintf(path, sizeof(path), "%s/%s", HomeDir(), it->path);
			printf("Marty: MGL CD mounted before reset: %s\n", path);
			marty_set_image(MARTY_SLOT_CD, path);
			StoreIdx_S(MARTY_SLOT_CD, path);
			continue;
		}
		if (kept != i) mgl->item[kept] = *it;
		kept++;
	}
	mgl->count = kept;
}

void marty_init()
{
	// the images the config remounted went in before the saved options did
	db_ready = 1;
	db_media_changed();

	char path[1024];
	// the mask ROMs from the core folder; the OSD entries stay for a
	// core folder without them
	sprintf(path, "%s/mrom.m36", HomeDir());
	if (FileExists(path)) user_io_file_tx(path, 1);
	sprintf(path, "%s/mrom.m37", HomeDir());
	if (FileExists(path)) user_io_file_tx(path, 2);

	sprintf(path, "%s/%s", SAVE_DIR, CoreName2);
	FileCreatePath(path);
	// the default CMOS image unless the config or the OSD named one
	if (cmos_chosen) return;
	sprintf(path, "%s/%s/cmos.sav", SAVE_DIR, CoreName2);
	marty_set_image(MARTY_SLOT_CMOS, path);
	cmos_chosen = 0;
	StoreIdx_S(MARTY_SLOT_CMOS, path);
}

void marty_poll()
{
	static unsigned long timer = 0;
	if (timer && !CheckTimer(timer)) return;
	timer = GetTimer(100);

	if (cmos_dirty && CheckTimer(cmos_flush_at)) cmos_flush();
	if (db_notice[0] && !user_io_osd_is_visible())
	{
		Info(db_notice, 4000);
		db_notice[0] = 0;
	}

	// bits 9:0 requests, bit 15 the disk size code
	uint16_t req = spi_uio_cmd(UIO_MARTY_GET_REQ);
	if (!(req & 0x3FF)) return;

	for (int bit = 0; bit < 10; bit++)
		if (req & (1 << bit)) create_image(bit, req >> 15);
	spi_uio_cmd16(UIO_MARTY_ACK_REQ, req & 0x3FF);
}
