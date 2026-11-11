// st_trackbuild.cpp - build MFMT tracks from a byte level description
//
// Copyright (C) 2026 Manfredi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include "st_trackbuild.h"

uint16_t st_crc16(uint16_t crc, uint8_t value)
{
	crc ^= (uint16_t)value << 8;
	for (int i = 0; i < 8; i++) crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
	return crc;
}

uint16_t st_crc16_buf(uint16_t crc, const uint8_t *data, int len)
{
	while (len--) crc = st_crc16(crc, *data++);
	return crc;
}

void TrackBuilder::reset(int len, uint8_t fill_value)
{
	if (len < 1) len = 1;
	m_bytes.assign(len, Byte{ fill_value, MARK_NONE, 0, 0, 0.0f });
	m_formatted = true;
	m_hd = false;
	m_rev_ns = 200000000.0;
	m_extra = 0;
	m_jumps.clear();
}

void TrackBuilder::put(int pos, uint8_t value, uint8_t owner, uint8_t mark)
{
	if (m_bytes.empty()) return;
	Byte &b = at(pos);
	b.value = value;
	b.mark = mark;
	b.owner = owner;
}

void TrackBuilder::fill(int pos, int count, uint8_t value, uint8_t owner)
{
	for (int i = 0; i < count; i++) put(pos + i, value, owner);
}

void TrackBuilder::set_time(int pos, int count, float time_ns_per_byte)
{
	if (m_bytes.empty()) return;
	for (int i = 0; i < count; i++) at(pos + i).time_ns = time_ns_per_byte;
}

void TrackBuilder::set_weak(int pos, uint8_t weak_mask)
{
	if (m_bytes.empty()) return;
	at(pos).weak = weak_mask;
}

static uint16_t mfm_encode(uint8_t value, int &prev_bit)
{
	uint16_t w = 0;
	for (int i = 7; i >= 0; i--)
	{
		int d = (value >> i) & 1;
		int c = (!prev_bit && !d) ? 1 : 0;
		w = (w << 2) | (c << 1) | d;
		prev_bit = d;
	}
	return w;
}

static uint16_t weak_to_cells(uint8_t weak)
{
	uint16_t w = 0;
	for (int k = 0; k < 8; k++) if (weak & (0x80 >> k)) w |= 1 << (14 - 2 * k);
	return w;
}

struct TimingRun
{
	uint32_t start;
	uint32_t words;
	double   cell;
};

static void merge_runs(std::vector<TimingRun> &runs, size_t max_runs)
{
	double threshold = 1.0;
	while (runs.size() > max_runs)
	{
		std::vector<TimingRun> out;
		for (const TimingRun &r : runs)
		{
			if (!out.empty() && fabs(out.back().cell - r.cell) <= threshold)
			{
				TimingRun &o = out.back();
				double total = o.cell * o.words + r.cell * r.words;
				o.words += r.words;
				o.cell = total / o.words;
			}
			else out.push_back(r);
		}
		runs.swap(out);
		threshold *= 2.0;
	}
}

void TrackBuilder::add_jump(int pos, int k)
{
	if (!k) return;
	if (k > 15) k = 15;
	if (k < -15) k = -15;
	remove_jump(pos);
	m_jumps.push_back(std::make_pair(pos, k));
	std::sort(m_jumps.begin(), m_jumps.end());
}

void TrackBuilder::remove_jump(int pos)
{
	for (size_t i = 0; i < m_jumps.size(); i++)
		if (m_jumps[i].first == pos) { m_jumps.erase(m_jumps.begin() + i); return; }
}

double TrackBuilder::free_byte_ns() const
{
	const double nominal_byte = m_hd ? 16000.0 : 32000.0;
	double fixed_ns = 0;
	int free_bytes = 0;
	for (const Byte &b : m_bytes)
	{
		if (b.time_ns > 0) fixed_ns += b.time_ns;
		else free_bytes++;
	}
	double free_byte = nominal_byte;
	if (free_bytes) free_byte = (m_rev_ns - fixed_ns) / free_bytes;
	if (free_byte < nominal_byte * 0.85) free_byte = nominal_byte * 0.85;
	if (free_byte > nominal_byte * 1.15) free_byte = nominal_byte * 1.15;
	return free_byte;
}

void TrackBuilder::build_cells(Cells &out) const
{
	int n = (int)m_bytes.size();
	if (n > ST_MFMT_MAX_CELLS / 16 - 16) n = ST_MFMT_MAX_CELLS / 16 - 16;
	out.bit.clear(); out.weak.clear(); out.ns.clear();
	out.byte_cell.assign(n, 0);
	out.bit.reserve(n * 16 + 64); out.weak.reserve(n * 16 + 64); out.ns.reserve(n * 16 + 64);
	double free_byte = free_byte_ns();

	const Byte &last = m_bytes[n - 1];
	int prev = (last.mark == MARK_C2) ? 0 : (last.value & 1);
	size_t j = 0;

	for (int i = 0; i < n; i++)
	{
		const Byte &b = m_bytes[i];
		float cell_ns = (float)((b.time_ns > 0 ? b.time_ns : free_byte) / 16.0);

		while (j < m_jumps.size() && m_jumps[j].first < i) j++;
		if (j < m_jumps.size() && m_jumps[j].first == i && out.bit.size() > 16)
		{
			int k = m_jumps[j].second;
			if (k > 0)
			{
				size_t from = out.bit.size() - 16;
				for (int e = 0; e < k; e++)
				{
					out.bit.push_back(out.bit[from + (e % 16)]);
					out.weak.push_back(0);
					out.ns.push_back(cell_ns);
				}
			}
			else
			{
				for (int e = 0; e < -k; e++) { out.bit.pop_back(); out.weak.pop_back(); out.ns.pop_back(); }
			}
			prev = out.bit.back();
		}

		out.byte_cell[i] = (int)out.bit.size();
		uint16_t w;
		if (b.mark == MARK_A1) { w = 0x4489; prev = 1; }
		else if (b.mark == MARK_C2) { w = 0x5224; prev = 0; }
		else w = mfm_encode(b.value, prev);
		uint16_t wm = weak_to_cells(b.weak);
		for (int c = 15; c >= 0; c--)
		{
			out.bit.push_back((w >> c) & 1);
			out.weak.push_back((wm >> c) & 1);
			out.ns.push_back(cell_ns);
		}
	}

	if (m_extra)
	{
		float cell_ns = out.ns.back();
		for (int e = 0; e < m_extra; e++)
		{
			out.bit.push_back(out.bit[16 - m_extra + e]);
			out.weak.push_back(0);
			out.ns.push_back(cell_ns);
		}
		if (!m_bytes[0].mark)
			out.bit[0] = (!out.bit.back() && !out.bit[1]) ? 1 : 0;
	}

	for (auto &f : m_cell_set)
		if (f.first >= 0 && f.first < (int)out.bit.size())
		{
			if (f.second == 2) out.weak[f.first] = 1;
			else out.bit[f.first] = f.second;
		}
}

bool TrackBuilder::encode(uint8_t cyl, uint8_t head, uint8_t *meta, uint8_t *cells) const
{
	memset(meta, 0, ST_MFMT_META_BYTES);
	memset(cells, 0, ST_MFMT_CELLS_BYTES);

	st_mfmt_header *hdr = (st_mfmt_header *)(meta + ST_MFMT_HDR_OFS);
	st_mfmt_timing *tim = (st_mfmt_timing *)(meta + ST_MFMT_TIMING_OFS);
	st_mfmt_weak *weak = (st_mfmt_weak *)(meta + ST_MFMT_WEAK_OFS);
	uint16_t *pool = (uint16_t *)(meta + ST_MFMT_POOL_OFS);
	uint16_t *cw = (uint16_t *)cells;

	hdr->magic = ST_MFMT_MAGIC;
	hdr->version = ST_MFMT_VERSION;
	hdr->cylinder = cyl;
	hdr->head = head;

	if (!m_formatted || m_bytes.empty())
	{
		encode_noise(cyl, head, meta, cells, m_hd);
		return true;
	}

	uint16_t flags = ST_MFMT_F_FORMATTED | (m_hd ? ST_MFMT_F_HD : 0);
	if ((int)m_bytes.size() > ST_MFMT_MAX_CELLS / 16 - 16) flags |= ST_MFMT_F_TRUNCATED;

	Cells cs;
	build_cells(cs);
	int total = (int)cs.bit.size();
	int nwords = (total + 15) / 16;

	for (int c = 0; c < total; c++) if (cs.bit[c]) cw[c >> 4] |= 0x8000 >> (c & 15);

	std::vector<TimingRun> runs;
	double rev = 0;
	for (int c = 0; c < total; c++) rev += cs.ns[c];
	for (int w = 0; w < nwords; w++)
	{
		double cell = cs.ns[w * 16] * 16.0;
		if (!runs.empty() && fabs(runs.back().cell - cell) < 0.5) runs.back().words++;
		else runs.push_back(TimingRun{ (uint32_t)w, 1, cell });
	}
	merge_runs(runs, ST_MFMT_MAX_TIMING);
	for (size_t i = 0; i < runs.size(); i++)
	{
		double c = floor(runs[i].cell + 0.5);
		if (c < 1) c = 1;
		if (c > 65535) c = 65535;
		tim[i].start_word = (uint16_t)runs[i].start;
		tim[i].cell_len = (uint16_t)c;
	}
	hdr->timing_count = (uint16_t)runs.size();
	if (runs.size() > 1) flags |= ST_MFMT_F_VARTIME;
	hdr->rev_time_ns = (uint32_t)(rev + 0.5);

	std::vector<uint16_t> wmask(nwords, 0);
	for (int c = 0; c < total; c++) if (cs.weak[c]) wmask[c >> 4] |= 0x8000 >> (c & 15);
	struct Seg { int start, words; };
	std::vector<Seg> segs;
	for (int w = 0; w < nwords; w++)
	{
		if (!wmask[w]) continue;
		if (!segs.empty() && segs.back().start + segs.back().words == w) segs.back().words++;
		else segs.push_back(Seg{ w, 1 });
	}
	while (segs.size() > ST_MFMT_MAX_WEAK)
	{
		size_t best = 0;
		int best_gap = 1 << 30;
		for (size_t s = 0; s + 1 < segs.size(); s++)
		{
			int gap = segs[s + 1].start - (segs[s].start + segs[s].words);
			if (gap < best_gap) { best_gap = gap; best = s; }
		}
		segs[best].words = segs[best + 1].start + segs[best + 1].words - segs[best].start;
		segs.erase(segs.begin() + best + 1);
	}
	int pool_used = 0;
	int nweak = 0;
	for (const Seg &s : segs)
	{
		int words = s.words;
		if (pool_used + words > ST_MFMT_MAX_POOL_WORDS)
		{
			words = ST_MFMT_MAX_POOL_WORDS - pool_used;
			flags |= ST_MFMT_F_TRUNCATED;
		}
		if (words <= 0) break;
		weak[nweak].start_word = (uint16_t)s.start;
		weak[nweak].n_words = (uint16_t)words;
		for (int i = 0; i < words; i++) pool[pool_used + i] = wmask[s.start + i];
		pool_used += words;
		nweak++;
	}
	hdr->weak_count = (uint16_t)nweak;
	hdr->pool_words = (uint16_t)pool_used;
	if (nweak) flags |= ST_MFMT_F_WEAK;

	hdr->cell_count = (uint32_t)total;
	hdr->flags = flags;
	return true;
}

void TrackBuilder::encode_noise(uint8_t cyl, uint8_t head, uint8_t *meta, uint8_t *cells, bool hd)
{
	memset(meta, 0, ST_MFMT_META_BYTES);
	memset(cells, 0, ST_MFMT_CELLS_BYTES);

	st_mfmt_header *hdr = (st_mfmt_header *)(meta + ST_MFMT_HDR_OFS);
	st_mfmt_timing *tim = (st_mfmt_timing *)(meta + ST_MFMT_TIMING_OFS);

	hdr->magic = ST_MFMT_MAGIC;
	hdr->version = ST_MFMT_VERSION;
	hdr->cylinder = cyl;
	hdr->head = head;
	hdr->flags = hd ? ST_MFMT_F_HD : 0;
	hdr->cell_count = hd ? 200000 : 100000;
	hdr->timing_count = 1;
	hdr->rev_time_ns = 200000000;
	tim[0].start_word = 0;
	tim[0].cell_len = hd ? ST_MFMT_CELL_HD : ST_MFMT_CELL_DD;
}
