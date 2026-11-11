// st_trackbuild.h - build MFMT tracks from a byte level description
//
// Copyright (C) 2026 Manfredi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#ifndef ST_TRACKBUILD_H
#define ST_TRACKBUILD_H

#include <stdint.h>
#include <vector>
#include <utility>
#include "st_mfmt.h"

class TrackBuilder
{
public:
	enum Mark : uint8_t
	{
		MARK_NONE = 0,
		MARK_A1   = 1,
		MARK_C2   = 2,
	};

	struct Byte
	{
		uint8_t value;
		uint8_t mark;
		uint8_t weak;
		uint8_t owner;
		float   time_ns;
	};

	TrackBuilder() {}

	void reset(int len, uint8_t fill = 0x4E);

	int  size() const { return (int)m_bytes.size(); }
	Byte &at(int pos) { return m_bytes[wrap(pos)]; }
	const Byte &at(int pos) const { return m_bytes[wrap(pos)]; }

	void put(int pos, uint8_t value, uint8_t owner = 0, uint8_t mark = MARK_NONE);
	void fill(int pos, int count, uint8_t value, uint8_t owner = 0);
	void set_time(int pos, int count, float time_ns_per_byte);
	void set_weak(int pos, uint8_t weak_mask);

	void set_unformatted() { m_formatted = false; }
	void set_hd(bool hd) { m_hd = hd; }

	void set_extra_cells(int k) { m_extra = (k < 0) ? 0 : (k > 15 ? 15 : k); }
	int extra_cells() const { return m_extra; }

	void add_jump(int pos, int k);
	void remove_jump(int pos);
	void clear_jumps() { m_jumps.clear(); }
	int jumps() const { return (int)m_jumps.size(); }

	void set_cell(int cell, int bit) { m_cell_set.emplace_back(cell, (uint8_t)(bit & 1)); }
	void set_weak_cell(int cell) { m_cell_set.emplace_back(cell, (uint8_t)2); }
	void clear_cells() { m_cell_set.clear(); }
	int forced_cells() const { return (int)m_cell_set.size(); }

	struct Cells
	{
		std::vector<uint8_t> bit, weak;
		std::vector<float> ns;
		std::vector<int> byte_cell;
	};
	void build_cells(Cells &out) const;

	void set_revolution_ns(double ns) { m_rev_ns = ns; }

	bool encode(uint8_t cyl, uint8_t head, uint8_t *meta, uint8_t *cells) const;

	static void encode_noise(uint8_t cyl, uint8_t head, uint8_t *meta, uint8_t *cells, bool hd = false);

	int wrap(int pos) const
	{
		int n = (int)m_bytes.size();
		if (!n) return 0;
		pos %= n;
		return pos < 0 ? pos + n : pos;
	}

private:
	std::vector<Byte> m_bytes;
	bool   m_formatted = true;
	bool   m_hd = false;
	double m_rev_ns = 200000000.0;
	int    m_extra = 0;
	std::vector<std::pair<int, int>> m_jumps;
	std::vector<std::pair<int, uint8_t>> m_cell_set;
	double free_byte_ns() const;
};

uint16_t st_crc16(uint16_t crc, uint8_t value);
uint16_t st_crc16_buf(uint16_t crc, const uint8_t *data, int len);

#endif
