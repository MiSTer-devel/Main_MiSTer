// st_fdd.cpp - Atari ST floppy images served to the core as MFMT tracks
//
// Copyright (C) 2026 Manfredi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <map>
#include <vector>

#include "../../hardware.h"
#include "../../user_io.h"
#include "../../spi.h"
#include "../../file_io.h"
#include "st_fdd.h"
#include "st_ipf.h"
#include "st_mfmt.h"
#include "st_stx.h"

#define ST_FDD_SLOTS     2
#define ST_FDD_MAX_FILE  (4 * 1024 * 1024)

struct StFddTrack
{
	std::vector<uint8_t> meta, cells;
};

struct StFddSlot
{
	StxImage *stx = nullptr;
	std::map<int, StFddTrack> tracks;
};

static StFddSlot slots[ST_FDD_SLOTS];

static void st_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void st_log(const char *fmt, ...)
{
	char line[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	printf("STX: %s\n", line);
}

static bool has_ext(const char *name, const char *ext)
{
	size_t n = strlen(name), e = strlen(ext);
	return n > e && name[n - e - 1] == '.' && !strcasecmp(name + n - e, ext);
}

void st_fdd_unmount(int index)
{
	if (index < 0 || index >= ST_FDD_SLOTS) return;
	if (slots[index].stx) st_log("drive %c: ejected", 'A' + index);
	delete slots[index].stx;
	slots[index].stx = nullptr;
	slots[index].tracks.clear();
}

int st_fdd_mount_hook(int index, const char *name, fileTYPE *f, int *writable)
{
	if (!is_st() || index < 0 || index >= ST_FDD_SLOTS) return 1;
	st_fdd_unmount(index);

	if (has_ext(name, "ipf"))
	{
		char tmp[64], err[256] = "";
		snprintf(tmp, sizeof(tmp), "/tmp/st_ipf_%d.scp", index);
		if (!st_ipf_to_scp(getFullPath(name), tmp, err, sizeof(err)))
		{
			st_log("%s: %s", name, err);
			return 0;
		}
		if (!FileOpenEx(f, tmp, O_RDONLY, 0, 0))
		{
			st_log("%s: cannot open %s", name, tmp);
			return 0;
		}
		const char *base = strrchr(name, '/');
		snprintf(f->name, sizeof(f->name), "%s", base ? base + 1 : name);
		*writable = 0;
		st_log("drive %c: %s as SCP (%lld bytes)", 'A' + index, name, (long long)f->size);
		return 1;
	}

	if (has_ext(name, "scp"))
	{
		st_log("drive %c: %s (%lld bytes)", 'A' + index, name, (long long)f->size);
		return 1;
	}

	if (!has_ext(name, "stx")) return 1;

	if (f->size <= 0 || f->size > ST_FDD_MAX_FILE)
	{
		st_log("%s: bad size %lld", name, (long long)f->size);
		return 0;
	}

	size_t n = (size_t)f->size;
	uint8_t *buf = (uint8_t *)malloc(n);
	if (!buf) return 0;
	FileSeek(f, 0, SEEK_SET);
	size_t got = 0;
	while (got < n)
	{
		int r = FileReadAdv(f, buf + got, n - got);
		if (r <= 0) break;
		got += r;
	}
	FileSeek(f, 0, SEEK_SET);

	StxImage *img = new StxImage();
	bool ok = got == n && img->parse(buf, n);
	free(buf);
	if (!ok)
	{
		st_log("%s: %s", name, got == n ? img->error() : "read error");
		delete img;
		return 0;
	}

	slots[index].stx = img;
	*writable = 0;
	st_log("drive %c: %s, %d cylinders, %d heads, revision %d (Main built " __DATE__ " " __TIME__ ")", 'A' + index, name, img->cylinders(), img->heads(), img->revision());
	return 1;
}

uint64_t st_fdd_size(int index, uint64_t size)
{
	if (index < 0 || index >= ST_FDD_SLOTS || !slots[index].stx || !is_st()) return size;
	return size | (ST_FDD_TYPE_MFMT << ST_FDD_SIZE_TYPE_SHIFT);
}

static const StFddTrack &build(StFddSlot &s, int cyl, int head)
{
	int key = cyl * 2 + head;
	auto it = s.tracks.find(key);
	if (it != s.tracks.end()) return it->second;
	StFddTrack &t = s.tracks[key];
	t.meta.resize(ST_MFMT_META_BYTES);
	t.cells.resize(ST_MFMT_CELLS_BYTES);
	TrackBuilder tb;
	StxBuildReport rep;
	stx_build_track(*s.stx, cyl, head, tb, &rep);
	tb.encode((uint8_t)cyl, (uint8_t)head, t.meta.data(), t.cells.data());
	if (rep.jumps || rep.splice_cells)
		st_log("track %d.%d rebuilt with %d phase jump(s), splice +%d cells", cyl, head, rep.jumps, rep.splice_cells);
	return t;
}

int st_fdd_sd_service(int disk, int op, uint64_t lba, int sz, int ack)
{
	if (!is_st() || disk < 0 || disk >= ST_FDD_SLOTS || !slots[disk].stx) return 0;
	StFddSlot &s = slots[disk];

	static uint8_t buf[ST_MFMT_CELLS_BYTES];
	if (sz > (int)sizeof(buf)) sz = sizeof(buf);

	if (op == 2)
	{
		st_log("drive %c: write to lba %llx dropped", 'A' + disk, (unsigned long long)lba);
		EnableIO();
		spi_w(UIO_SECTOR_WR | ack);
		spi_block_read(buf, user_io_get_width(), sz);
		DisableIO();
		return 1;
	}
	if (!(op & 1)) return -1;

	memset(buf, 0, sz);
	if (lba & ST_MFMT_LBA_FLAG)
	{
		int cyl = lba & 0xFF;
		int head = (lba >> 8) & 1;
		int part = (lba >> ST_MFMT_LBA_PART_SHIFT) & 3;
		diskled_on();
		unsigned long t0 = GetTimer(0);
		bool cached = s.tracks.count(cyl * 2 + head) != 0;
		const StFddTrack &t = build(s, cyl, head);
		if (part == ST_MFMT_PART_META)
			st_log("drive %c: track %d.%d %s (%lu ms)", 'A' + disk, cyl, head, cached ? "cached" : "built", GetTimer(0) - t0);
		if (part == ST_MFMT_PART_META) memcpy(buf, t.meta.data(), sz < ST_MFMT_META_BYTES ? sz : ST_MFMT_META_BYTES);
		else if (part == ST_MFMT_PART_CELLS) memcpy(buf, t.cells.data(), sz);
	}
	else st_log("drive %c: sector request lba %llu (not a track)", 'A' + disk, (unsigned long long)lba);

	EnableIO();
	spi_w(UIO_SECTOR_RD | ack);
	spi_block_write(buf, user_io_get_width(), sz);
	DisableIO();
	return 1;
}
