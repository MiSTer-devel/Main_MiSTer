// st_mfmt.h - "MFMT" track buffer exchanged between Main and the Atari ST core
//
// Copyright (C) 2026 Manfredi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#ifndef ST_MFMT_H
#define ST_MFMT_H

#include <stdint.h>

#define ST_MFMT_MAGIC            0x544D464DUL
#define ST_MFMT_VERSION          1

#define ST_MFMT_LBA_FLAG         0x80000000UL
#define ST_MFMT_LBA_PART_SHIFT   16
#define ST_MFMT_PART_META        0
#define ST_MFMT_PART_CELLS       1
#define ST_MFMT_LBA(cyl, head, part) \
	(ST_MFMT_LBA_FLAG | ((uint32_t)(part) << ST_MFMT_LBA_PART_SHIFT) | (((uint32_t)(head) & 1) << 8) | ((uint32_t)(cyl) & 0xFF))

#define ST_MFMT_META_BYTES       4096
#define ST_MFMT_CELLS_BYTES      16384
#define ST_MFMT_MAX_CELLS        (ST_MFMT_CELLS_BYTES * 8)

#define ST_MFMT_HDR_OFS          0x0000
#define ST_MFMT_TIMING_OFS       0x0040
#define ST_MFMT_MAX_TIMING       256
#define ST_MFMT_WEAK_OFS         0x0440
#define ST_MFMT_MAX_WEAK         48
#define ST_MFMT_POOL_OFS         0x0500
#define ST_MFMT_MAX_POOL_WORDS   ((ST_MFMT_META_BYTES - ST_MFMT_POOL_OFS) / 2)

#define ST_MFMT_F_FORMATTED      0x0001
#define ST_MFMT_F_WEAK           0x0002
#define ST_MFMT_F_VARTIME        0x0004
#define ST_MFMT_F_HD             0x0008
#define ST_MFMT_F_TRUNCATED      0x0100

#define ST_MFMT_TIME_UNITS_PER_NS  16
#define ST_MFMT_CELL_DD          32000
#define ST_MFMT_CELL_HD          16000

#pragma pack(push, 1)

typedef struct
{
	uint32_t magic;
	uint16_t version;
	uint16_t flags;
	uint32_t cell_count;
	uint16_t timing_count;
	uint16_t weak_count;
	uint8_t  cylinder;
	uint8_t  head;
	uint16_t pool_words;
	uint32_t rev_time_ns;
	uint8_t  reserved[40];
} st_mfmt_header;

typedef struct
{
	uint16_t start_word;
	uint16_t cell_len;
} st_mfmt_timing;

typedef struct
{
	uint16_t start_word;
	uint16_t n_words;
} st_mfmt_weak;

#pragma pack(pop)

#endif
