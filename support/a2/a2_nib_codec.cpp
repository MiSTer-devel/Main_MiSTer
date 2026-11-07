// Apple II / TK2000 5.25" track codec -- see a2_nib_codec.h.
//
// The track layout (48-byte gap, address field, 5-byte gap, data field,
// 416 bytes per sector, 16 sectors = 6656 bytes) and the physical sector
// order on the track match the original on-the-fly nibblizer in Main_MiSTer
// (support/a2/dsk2nib_lib.cpp), so the bytes the core sees are unchanged
// for DOS-order images.
#include <string.h>
#include "a2_nib_codec.h"

#define GAP1_LEN 48
#define GAP2_LEN 5
#define NIB_SECTOR_LEN 416

// DOS 3.3 order: image sector holding address-field sector N.
static const uint8_t dos_order[16] = { 0x0,0x7,0xE,0x6,0xD,0x5,0xC,0x4,0xB,0x3,0xA,0x2,0x9,0x1,0x8,0xF };
// ProDOS order (.po): image sector holding address-field sector N.
static const uint8_t prodos_order[16] = { 0x0,0x8,0x1,0x9,0x2,0xA,0x3,0xB,0x4,0xC,0x5,0xD,0x6,0xE,0x7,0xF };
// Address-field sector number written at each physical slot on the track.
// Real disks (and every .nib image) carry physical sectors 0..15 in order
// around the track; DOS 3.3 / ProDOS apply their own software skew via the
// image order tables above. The original Main_MiSTer dsk2nib_lib.cpp added a
// second interleave here ({0,7,E,6,...}), which makes DOS miss nearly every
// next sector and wait a revolution -- several times slower booting and
// broken timing for fast loaders.
static const uint8_t slot_sector[16] = { 0x0,0x1,0x2,0x3,0x4,0x5,0x6,0x7,0x8,0x9,0xA,0xB,0xC,0xD,0xE,0xF };

static const uint8_t gcr62[64] = {
	0x96,0x97,0x9a,0x9b,0x9d,0x9e,0x9f,0xa6, 0xa7,0xab,0xac,0xad,0xae,0xaf,0xb2,0xb3,
	0xb4,0xb5,0xb6,0xb7,0xb9,0xba,0xbb,0xbc, 0xbd,0xbe,0xbf,0xcb,0xcd,0xce,0xcf,0xd3,
	0xd6,0xd7,0xd9,0xda,0xdb,0xdc,0xdd,0xde, 0xdf,0xe5,0xe6,0xe7,0xe9,0xea,0xeb,0xec,
	0xed,0xee,0xef,0xf2,0xf3,0xf4,0xf5,0xf6, 0xf7,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff
};

int a2_file_sector(int phys, int order)
{
	phys &= 15;
	return order == A2_ORDER_PRODOS ? prodos_order[phys] : dos_order[phys];
}

static void put44(uint8_t *p, int v)
{
	p[0] = ((v >> 1) & 0x55) | 0xaa;
	p[1] = (v & 0x55) | 0xaa;
}

static int get44(uint8_t a, uint8_t b)
{
	return ((a << 1) | 1) & b;
}

static void encode62(const uint8_t *src, uint8_t *dst /* 343 bytes */)
{
	uint8_t prim[256], sec[86];
	memset(sec, 0, sizeof(sec));
	for (int i = 0; i < 256; i++) {
		prim[i] = src[i] >> 2;
		uint8_t pair = ((src[i] & 2) >> 1) | ((src[i] & 1) << 1);
		sec[i % 86] |= pair << ((i / 86) * 2);
	}
	int n = 0;
	dst[n++] = gcr62[sec[0]];
	for (int i = 1; i < 86; i++) dst[n++] = gcr62[(sec[i] ^ sec[i - 1]) & 0x3f];
	dst[n++] = gcr62[(prim[0] ^ sec[85]) & 0x3f];
	for (int i = 1; i < 256; i++) dst[n++] = gcr62[(prim[i] ^ prim[i - 1]) & 0x3f];
	dst[n++] = gcr62[prim[255] & 0x3f];
}

void a2_nibblize_track(const uint8_t *dsk_track, int track, int volume, int order, uint8_t *nib)
{
	for (int slot = 0; slot < 16; slot++) {
		uint8_t *p = nib + slot * NIB_SECTOR_LEN;
		int s = slot_sector[slot];
		memset(p, 0xff, GAP1_LEN); p += GAP1_LEN;
		*p++ = 0xd5; *p++ = 0xaa; *p++ = 0x96;
		put44(p, volume); p += 2;
		put44(p, track);  p += 2;
		put44(p, s);      p += 2;
		put44(p, volume ^ track ^ s); p += 2;
		*p++ = 0xde; *p++ = 0xaa; *p++ = 0xeb;
		memset(p, 0xff, GAP2_LEN); p += GAP2_LEN;
		*p++ = 0xd5; *p++ = 0xaa; *p++ = 0xad;
		encode62(dsk_track + a2_file_sector(s, order) * A2_SECTOR_SIZE, p); p += 343;
		*p++ = 0xde; *p++ = 0xaa; *p++ = 0xeb;
	}
}

static int8_t ungcr[256];
static int ungcr_ready = 0;

static void init_ungcr(void)
{
	if (ungcr_ready) return;
	memset(ungcr, -1, sizeof(ungcr));
	for (int i = 0; i < 64; i++) ungcr[gcr62[i]] = (int8_t)i;
	ungcr_ready = 1;
}

#define NB(k) nib[((unsigned)(k)) % A2_NIB_TRACK_SIZE]

uint32_t a2_denibblize_track(const uint8_t *nib, int track, int order, uint8_t *dsk_track)
{
	init_ungcr();
	uint32_t done = 0;

	for (int p = 0; p < A2_NIB_TRACK_SIZE; p++) {
		if (NB(p) != 0xd5 || NB(p + 1) != 0xaa || NB(p + 2) != 0x96) continue;
		int vol = get44(NB(p + 3), NB(p + 4));
		int trk = get44(NB(p + 5), NB(p + 6));
		int sec = get44(NB(p + 7), NB(p + 8));
		int chk = get44(NB(p + 9), NB(p + 10));
		if ((vol ^ trk ^ sec) != chk || trk != track || sec > 15) continue;

		// Data prologue must follow within a short gap (and before the next
		// address field), otherwise the data belongs to another sector.
		int d = -1;
		for (int q = p + 11; q < p + 11 + 64; q++) {
			if (NB(q) == 0xd5 && NB(q + 1) == 0xaa) {
				if (NB(q + 2) == 0xad) d = q + 3;
				break;
			}
		}
		if (d < 0) continue;

		uint8_t v[343];
		int bad = 0;
		for (int i = 0; i < 343; i++) {
			int x = ungcr[NB(d + i)];
			if (x < 0) { bad = 1; break; }
			v[i] = (uint8_t)x;
		}
		if (bad) continue;

		uint8_t dec[342], x = 0;
		for (int i = 0; i < 342; i++) { x ^= v[i]; dec[i] = x; }
		if (x != v[342]) continue;   // data checksum

		uint8_t *out = dsk_track + a2_file_sector(sec, order) * A2_SECTOR_SIZE;
		for (int i = 0; i < 256; i++) {
			int pr = (dec[i % 86] >> ((i / 86) * 2)) & 3;
			out[i] = (uint8_t)((dec[86 + i] << 2) | ((pr & 1) << 1) | ((pr & 2) >> 1));
		}
		done |= 1u << a2_file_sector(sec, order);
		p = d + 343 - 1;
	}
	return done;
}
