// Konami Firebeat arcade core: CD-ROM CHDs in the image slots, served as their raw .bin followed by
// a TOC block (the core's rtl/board/fb_cd_slot.sv reads it); a DVD CHD (Pop'n Music's data DVD) as
// its 2048-byte sectors back to back (rtl/spu/fb_dvd_slot.sv).

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include "../../file_io.h"
#include "../../user_io.h"
#include "../../spi.h"
#include "../chd/mister_chd.h"
#include "firebeat.h"

#define FB_SLOTS     4
#define FB_FRAME     2352
#define FB_DVD_TAG   CHD_MAKE_TAG('D','V','D',' ')   // chdman createdvd's metadata

struct fb_chd
{
	int       active;
	int       dvd;            // a DVD: data_bytes of sectors, no TOC block
	toc_t     toc;
	uint8_t  *hunkbuf;
	int       hunknum;
	uint64_t  frames;         // frames in the raw image: every track's, back to back
	uint64_t  first[100];     // raw-image frame each track starts at
	uint64_t  data_bytes;     // the frames' bytes, padded to a 512-byte block
	uint8_t   tocblk[1024];   // then the TOC block
};

static fb_chd slot[FB_SLOTS] = {};

char is_firebeat()
{
	// the core's own name (CONF_STR), not the .mra's setname: Firebeat and its test
	// revision FirebeatCD
	return !strncasecmp(user_io_get_core_name(1), "Firebeat", 8);
}

void firebeat_unmount(int index)
{
	if (index < 0 || index >= FB_SLOTS) return;
	fb_chd *s = &slot[index];
	if (s->toc.chd_f) chd_close(s->toc.chd_f);
	if (s->hunkbuf) free(s->hunkbuf);
	memset(s, 0, sizeof(*s));
}

int firebeat_mount_hook(int index, const char *name, fileTYPE *f, int *writable)
{
	if (!is_firebeat()) return 1;
	sd_service = firebeat_sd_service;
	firebeat_unmount(index);

	int len = strlen(name);
	if (index >= FB_SLOTS || len < 4 || strcasecmp(name + len - 4, ".chd")) return 1;

	fb_chd *s = &slot[index];
	if (mister_load_chd(name, &s->toc) != CHDERR_NONE || !s->toc.chd_f)
	{
		printf("Firebeat: cannot open %s as a CD CHD\n", name);
		FileClose(f);
		return 0;
	}
	s->hunkbuf = (uint8_t *)malloc(s->toc.chd_hunksize);
	if (!s->hunkbuf)
	{
		firebeat_unmount(index);
		FileClose(f);
		return 0;
	}
	s->hunknum = -1;

	if (s->toc.last == 0 &&
	    chd_get_metadata(s->toc.chd_f, FB_DVD_TAG, 0, NULL, 0, NULL, NULL, NULL) == CHDERR_NONE)
	{
		s->dvd = 1;
		s->data_bytes = chd_get_header(s->toc.chd_f)->logicalbytes;
		s->active = 1;
		*writable = 0;
		f->size = (int64_t)s->data_bytes;
		printf("Firebeat: %s in slot %d, a DVD of %llu bytes\n", name, index, (unsigned long long)s->data_bytes);
		return 1;
	}

	s->frames = 0;
	for (int i = 0; i < s->toc.last; i++)
	{
		cd_track_t *k = &s->toc.tracks[i];
		if (k->sector_size != FB_FRAME && !(k->sector_size == 2048 && k->type == TT_MODE1))
		{
			printf("Firebeat: track %d of %s is neither 2352-byte raw nor MODE1/2048, not served\n", i + 1, name);
			firebeat_unmount(index);
			FileClose(f);
			return 0;
		}
		s->first[i] = s->frames;
		s->frames += (uint64_t)(k->end - k->start);
	}
	// The TOC block: "FBTC", the track count, version 1, then for each track and the lead-out its
	// start frame (little-endian), bit 0 of byte 4 for audio, byte 5 the control flags. The frames
	// served are MAME's logical frames for a CHD without pregaps (cdrom_file: logframeofs equals
	// physframeofs then), which the Firebeat discs are.
	memset(s->tocblk, 0, sizeof(s->tocblk));
	memcpy(s->tocblk, "FBTC", 4);
	s->tocblk[4] = (uint8_t)s->toc.last;
	s->tocblk[5] = 1;
	for (int i = 0; i <= s->toc.last; i++)
	{
		uint32_t st = (uint32_t)(i < s->toc.last ? s->first[i] : s->frames);
		uint8_t *e = s->tocblk + 8 + 8 * i;
		e[0] = st; e[1] = st >> 8; e[2] = st >> 16; e[3] = st >> 24;
		e[4] = (i < s->toc.last && s->toc.tracks[i].type == TT_CDDA) ? 1 : 0;
	}
	s->data_bytes = ((s->frames * FB_FRAME + 511) / 512) * 512;

	s->active = 1;
	*writable = 0;
	f->size = (int64_t)(s->data_bytes + sizeof(s->tocblk));
	printf("Firebeat: %s in slot %d, %d track(s), %llu frames, %llu bytes as a raw image\n", name, index,
	       s->toc.last, (unsigned long long)s->frames, (unsigned long long)f->size);
	return 1;
}

// bytes [pos, pos + n) of the raw image, within one frame
static void fill_frame(fb_chd *s, uint64_t frame, uint32_t off, int n, uint8_t *dst)
{
	int t = 0;
	while (t + 1 < s->toc.last && frame >= s->first[t + 1]) t++;
	cd_track_t *k = &s->toc.tracks[t];
	int chd_lba = (int)(frame - s->first[t]) + k->start + k->offset;

	if (k->sector_size == 2048)
	{
		// MODE1/2048 track: the frame rebuilt with its sync and header around the user data. EDC
		// and ECC are left zero; the core reads only bytes 16-2063.
		uint8_t fr[FB_FRAME] = {};
		memset(fr + 1, 0xff, 10);
		uint32_t a = (uint32_t)frame + 150;
		uint32_t mm = a / (75 * 60), ss = (a / 75) % 60, ff = a % 75;
		fr[12] = (uint8_t)((mm / 10) << 4 | mm % 10);
		fr[13] = (uint8_t)((ss / 10) << 4 | ss % 10);
		fr[14] = (uint8_t)((ff / 10) << 4 | ff % 10);
		fr[15] = 1;
		if (mister_chd_read_sector(s->toc.chd_f, chd_lba, 16, 0, 2048, fr, s->hunkbuf, &s->hunknum) != CHDERR_NONE)
			memset(fr + 16, 0, 2048);
		memcpy(dst, fr + off, n);
		return;
	}

	if (k->type != TT_CDDA)
	{
		if (mister_chd_read_sector(s->toc.chd_f, chd_lba, 0, off, n, dst, s->hunkbuf, &s->hunknum) != CHDERR_NONE)
			memset(dst, 0, n);
		return;
	}

	// CHD stores CD audio byte-swapped; the .bin has it little-endian
	uint8_t fr[FB_FRAME];
	if (mister_chd_read_sector(s->toc.chd_f, chd_lba, 0, 0, FB_FRAME, fr, s->hunkbuf, &s->hunknum) != CHDERR_NONE)
	{
		memset(dst, 0, n);
		return;
	}
	for (int i = 0; i < FB_FRAME; i += 2)
	{
		uint8_t b = fr[i]; fr[i] = fr[i + 1]; fr[i + 1] = b;
	}
	memcpy(dst, fr + off, n);
}

int firebeat_sd_service(int disk, fileTYPE *, int op, uint64_t lba, int sz, int ack)
{
	if (!op || !is_firebeat() || disk < 0 || disk >= FB_SLOTS || !slot[disk].active) return 0;

	static uint8_t buf[UIO_BUFFER_SIZE];
	if (!(op & 1) || sz > (int)sizeof(buf)) return -1;      // read-only

	fb_chd *s = &slot[disk];
	diskled_on();
	uint64_t pos = lba * 512ULL;
	for (int done = 0; done < sz && s->dvd; )
	{
		uint32_t hb = s->toc.chd_hunksize;
		int h = (int)(pos / hb);
		uint32_t off = (uint32_t)(pos % hb);
		int n = (int)(hb - off);
		if (n > sz - done) n = sz - done;
		if (pos >= s->data_bytes) memset(buf + done, 0, n);
		else if (h != s->hunknum && chd_read(s->toc.chd_f, h, s->hunkbuf) != CHDERR_NONE) memset(buf + done, 0, n);
		else { s->hunknum = h; memcpy(buf + done, s->hunkbuf + off, n); }
		done += n;
		pos += n;
	}
	for (int done = 0; done < sz && !s->dvd; )
	{
		uint64_t frame = pos / FB_FRAME;
		uint32_t off = (uint32_t)(pos % FB_FRAME);
		int n = FB_FRAME - off;
		if (n > sz - done) n = sz - done;
		if (pos >= s->data_bytes)
		{
			uint64_t o = pos - s->data_bytes;
			n = sz - done;
			for (int i = 0; i < n; i++) buf[done + i] = (o + i < sizeof(s->tocblk)) ? s->tocblk[o + i] : 0;
		}
		else if (frame >= s->frames) { n = (int)(s->data_bytes - pos); if (n > sz - done) n = sz - done; memset(buf + done, 0, n); }
		else fill_frame(s, frame, off, n, buf + done);
		done += n;
		pos += n;
	}

	EnableIO();
	spi_w(UIO_SECTOR_RD | ack);
	spi_block_write(buf, user_io_get_width(), sz);
	DisableIO();
	return 1;
}
