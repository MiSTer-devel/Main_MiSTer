// st_stx.h - Pasti (STX) image reader and STX -> MFMT track conversion
//
// Copyright (C) 2026 Manfredi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#ifndef ST_STX_H
#define ST_STX_H

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include "st_trackbuild.h"

#define STX_SECT_VARIABLE_TIME  0x01
#define STX_SECT_CRC_ERROR      0x08
#define STX_SECT_RNF            0x10
#define STX_SECT_DELETED        0x20
#define STX_SECT_FUZZY          0x80

#define STX_TRK_SECTOR_BLOCK    0x0001
#define STX_TRK_TRACK_IMAGE     0x0040
#define STX_TRK_IMAGE_SYNC      0x0080

struct StxSector
{
	uint32_t data_offset;
	uint16_t bit_position;
	uint16_t read_time;
	uint8_t  id[6];
	uint8_t  status;
	int      size;
	const uint8_t *data;
	const uint8_t *fuzzy;
	const uint8_t *timing;
};

struct StxTrack
{
	uint8_t  cylinder;
	uint8_t  head;
	uint16_t flags;
	uint16_t mfm_size;
	bool     simple;
	std::vector<StxSector> sectors;
	const uint8_t *image;
	int      image_size;
	int      image_sync;
	int      length;
	bool     bitpos_time;
};

class StxImage
{
public:
	bool parse(const uint8_t *buf, size_t len);
	void clear();

	const char *error() const { return m_error; }
	int revision() const { return m_revision; }
	int heads() const { return m_heads; }
	int cylinders() const { return m_cylinders; }
	const std::vector<StxTrack> &tracks() const { return m_tracks; }
	const StxTrack *find(int cyl, int head) const;

private:
	bool parse_track(const uint8_t *p, const uint8_t *end, StxTrack &t);
	bool in(const uint8_t *p, size_t n) const;

	std::vector<uint8_t> m_file;
	std::vector<StxTrack> m_tracks;
	int m_track_index[256][2];
	int m_revision = 0;
	int m_heads = 0;
	int m_cylinders = 0;
	char m_error[128] = {};
};

struct StxBuildReport
{
	int sectors = 0;
	int headers_found_in_image = 0;
	int data_found_in_image = 0;
	int image_offset = 0;
	int overlaps = 0;
	int overlap_mismatch = 0;
	bool image_ignored = false;
	int gap1_from_image = 0;
	int splice_cells = 0;
	int jumps = 0;
	int forced_cells = 0;
	bool unformatted = false;
	bool used_image = false;
	int bad_fields = 0;
};

double stx_bitpos_bytes(const StxTrack &t, int bit_position);
double stx_bitpos_ns(const StxTrack &t, int bit_position);

bool stx_build_track(const StxImage &img, int cyl, int head, TrackBuilder &tb, StxBuildReport *rep = nullptr);

#endif
