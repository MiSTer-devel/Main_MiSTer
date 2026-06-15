// Apple IIgs disk-format conversions (pure, dependency-free codec).
// See iigs_fmt.h and IIGS_DISK_SUPPORT.md.

#include "iigs_fmt.h"
#include <string.h>

// ---------------------------------------------------------------------------
// little/big-endian helpers
// ---------------------------------------------------------------------------
static inline uint16_t rd_le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t rd_le32(const uint8_t *p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint32_t rd_be32(const uint8_t *p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static inline void wr_le16(uint8_t *p, uint16_t v) { p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; }
static inline void wr_le32(uint8_t *p, uint32_t v) {
	p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff;
}
static inline void wr_be32(uint8_t *p, uint32_t v) {
	p[0] = (v >> 24) & 0xff; p[1] = (v >> 16) & 0xff; p[2] = (v >> 8) & 0xff; p[3] = v & 0xff;
}

// ---------------------------------------------------------------------------
// CRC32 (zlib/WOZ compatible)
// ---------------------------------------------------------------------------
uint32_t woz_crc32(const uint8_t *data, size_t len)
{
	uint32_t crc = 0xFFFFFFFFu;
	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int b = 0; b < 8; b++)
			crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1) + 1));
	}
	return crc ^ 0xFFFFFFFFu;
}

// ===========================================================================
// 2MG / 2IMG
// ===========================================================================
int twomg_parse(const uint8_t *buf, size_t size, TwoMG *out)
{
	if (size < 64 || memcmp(buf, "2IMG", 4) != 0) return 0;

	uint16_t hdr_len   = rd_le16(buf + 8);
	uint32_t format    = rd_le32(buf + 12);
	uint32_t flags     = rd_le32(buf + 16);
	uint32_t blocks    = rd_le32(buf + 20);
	uint32_t data_off  = rd_le32(buf + 24);
	uint32_t data_len  = rd_le32(buf + 28);

	// data_len can be 0 for ProDOS images -- derive from block count.
	if (data_len == 0) data_len = blocks * A2_BLOCK_SIZE;

	// Validate before trusting (matches sim_blkdevice.cpp).
	if (hdr_len != 64 || data_off < 64 ||
	    (uint64_t)data_off + data_len > size)
		return 0;

	if (out) {
		out->format          = format;
		out->flags           = flags;
		out->blocks          = blocks;
		out->data_offset     = data_off;
		out->data_len        = data_len;
		out->write_protected = (flags & 0x80000000u) ? 1 : 0;
	}
	return 1;
}

size_t twomg_build(uint8_t *out, size_t out_cap,
                   const uint8_t *payload, uint32_t payload_len, uint32_t format)
{
	size_t total = 64 + (size_t)payload_len;
	if (out_cap < total) return 0;

	memset(out, 0, 64);
	memcpy(out, "2IMG", 4);
	memcpy(out + 4, "MSTR", 4);                 // creator
	wr_le16(out + 8, 64);                        // header length
	wr_le16(out + 10, 1);                        // version
	wr_le32(out + 12, format);                   // image format
	wr_le32(out + 16, 0x00000100u);              // flags (volume-number-valid bit)
	wr_le32(out + 20, payload_len / A2_BLOCK_SIZE);  // block count
	wr_le32(out + 24, 64);                       // data offset
	wr_le32(out + 28, payload_len);              // data length

	memcpy(out + 64, payload, payload_len);
	return total;
}

// ===========================================================================
// DiskCopy 4.2
// ===========================================================================
uint32_t dc42_checksum(const uint8_t *data, size_t len)
{
	uint32_t chk = 0;
	// Iterate big-endian 16-bit words: add, then rotate right by 1.
	for (size_t i = 0; i + 1 < len; i += 2) {
		uint16_t w = (uint16_t)((data[i] << 8) | data[i + 1]);
		chk += w;
		chk = (chk >> 1) | (chk << 31);
	}
	return chk;
}

int dc42_probe(const uint8_t *buf, size_t size)
{
	if (size < 84) return 0;
	uint8_t  namelen  = buf[0];
	uint32_t dataSize = rd_be32(buf + 0x40);
	uint32_t tagSize  = rd_be32(buf + 0x44);
	if (namelen > 63) return 0;
	if (buf[0x52] != 0x01 || buf[0x53] != 0x00) return 0;   // magic 0x0100
	if (dataSize == 0 || (dataSize & 511)) return 0;         // multiple of 512
	if (tagSize % 12) return 0;                              // tags are 12 B/block
	if ((uint64_t)84 + dataSize + tagSize != size) return 0; // exact size equation
	return 1;
}

int dc42_parse(const uint8_t *buf, size_t size, DC42 *out)
{
	if (!dc42_probe(buf, size)) return 0;
	if (out) {
		out->data_size     = rd_be32(buf + 0x40);
		out->tag_size      = rd_be32(buf + 0x44);
		out->data_checksum = rd_be32(buf + 0x48);
		out->tag_checksum  = rd_be32(buf + 0x4C);
		out->disk_format   = buf[0x50];
		out->format_byte   = buf[0x51];
	}
	return 1;
}

size_t dc42_build(uint8_t *out, size_t out_cap, const uint8_t *payload,
                  uint32_t payload_len, uint8_t format_byte, const char *name)
{
	size_t total = 84 + (size_t)payload_len;
	if (out_cap < total) return 0;

	memset(out, 0, 84);
	size_t nl = name ? strlen(name) : 0;
	if (nl > 63) nl = 63;
	out[0] = (uint8_t)nl;
	if (nl) memcpy(out + 1, name, nl);

	wr_be32(out + 0x40, payload_len);            // dataSize
	wr_be32(out + 0x44, 0);                       // tagSize (no tags)
	wr_be32(out + 0x48, dc42_checksum(payload, payload_len)); // dataChecksum
	wr_be32(out + 0x4C, 0);                       // tagChecksum
	out[0x50] = (payload_len == A2_35_IMAGE_SIZE) ? 1 : 0;   // diskFormat
	out[0x51] = format_byte;                       // formatByte (0x24 = ProDOS)
	out[0x52] = 0x01; out[0x53] = 0x00;            // magic

	memcpy(out + 84, payload, payload_len);
	return total;
}

// ===========================================================================
// WOZ disk type
// ===========================================================================
int woz_disk_type(const uint8_t *buf, size_t size)
{
	if (size < 12) return -1;
	if (memcmp(buf, "WOZ1", 4) != 0 && memcmp(buf, "WOZ2", 4) != 0) return -1;

	// Walk chunks looking for INFO; disk type is INFO data byte +1.
	size_t pos = 12;
	while (pos + 8 <= size) {
		uint32_t csize = rd_le32(buf + pos + 4);
		if (memcmp(buf + pos, "INFO", 4) == 0) {
			if (pos + 8 + 2 <= size) return buf[pos + 8 + 1];
			return -1;
		}
		pos += 8 + csize;
	}
	return -1;
}

// ===========================================================================
// 140K sector order (DOS 3.3 <-> ProDOS)
// ===========================================================================
// DOS logical sector -> ProDOS sector position within a track. Inverse pair.
static const int DOS_TO_PRODOS[16] = { 0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15 };
static const int PRODOS_TO_DOS[16] = { 0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15 };

static void reorder_140k(uint8_t *dst, const uint8_t *src, const int map[16])
{
	for (int t = 0; t < A2_TRACKS_525; t++) {
		const uint8_t *st = src + (size_t)t * A2_TRACK_SIZE;
		uint8_t *dt = dst + (size_t)t * A2_TRACK_SIZE;
		for (int s = 0; s < A2_SECTORS_PER_TRACK; s++)
			memcpy(dt + map[s] * A2_SECTOR_SIZE, st + s * A2_SECTOR_SIZE, A2_SECTOR_SIZE);
	}
}

void a2_dos_to_prodos(uint8_t *dst, const uint8_t *src) { reorder_140k(dst, src, DOS_TO_PRODOS); }
void a2_prodos_to_dos(uint8_t *dst, const uint8_t *src) { reorder_140k(dst, src, PRODOS_TO_DOS); }

// ===========================================================================
// 5.25" 6-and-2 GCR (DSK <-> NIB), ported from dsk2nib_lib.cpp
// ===========================================================================
#define PRIMARY_BUF_LEN   256
#define SECONDARY_BUF_LEN 86
#define DATA_LEN          (PRIMARY_BUF_LEN + SECONDARY_BUF_LEN)
#define GAP1_LEN          48
#define GAP2_LEN          5
#define BYTES_PER_NIB_SECTOR 416
#define DEFAULT_VOLUME    254
#define GAP_BYTE          0xff

static const uint8_t addr_prolog[] = { 0xd5, 0xaa, 0x96 };
static const uint8_t addr_epilog[] = { 0xde, 0xaa, 0xeb };
static const uint8_t data_prolog[] = { 0xd5, 0xaa, 0xad };
static const uint8_t data_epilog[] = { 0xde, 0xaa, 0xeb };

static const int soft_interleave[16] =
    { 0, 7, 0xE, 6, 0xD, 5, 0xC, 4, 0xB, 3, 0xA, 2, 9, 1, 8, 0xF };
static const int phys_interleave[16] =
    { 0, 0xD, 0xB, 9, 7, 5, 3, 1, 0xE, 0xC, 0xA, 8, 6, 4, 2, 0xF };

static const uint8_t gcr6_table[0x40] = {
	0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
	0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
	0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
	0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
	0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
	0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
	0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
	0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};

static inline uint8_t translate(uint8_t b) { return gcr6_table[b & 0x3f]; }

static uint8_t untranslate(uint8_t x)
{
	const uint8_t *p = (const uint8_t *)memchr(gcr6_table, x, 0x40);
	return p ? (uint8_t)(p - gcr6_table) : 0;
}

static void odd_even_encode(uint8_t a[2], int i)
{
	a[0] = ((i >> 1) & 0x55) | 0xaa;
	a[1] = (i & 0x55) | 0xaa;
}
static uint8_t odd_even_decode(uint8_t b1, uint8_t b2)
{
	return (uint8_t)(((b1 << 1) & 0xaa) | (b2 & 0x55));
}

// Encode one 256-byte sector's data field (342 nibbles + checksum) at dest.
static void nibbilize(const uint8_t *src, uint8_t *dest)
{
	uint8_t primary[PRIMARY_BUF_LEN];
	uint8_t secondary[SECONDARY_BUF_LEN];
	memset(secondary, 0, sizeof(secondary));

	for (int i = 0; i < PRIMARY_BUF_LEN; i++) {
		primary[i] = src[i] >> 2;
		int index = i % SECONDARY_BUF_LEN;
		int section = i / SECONDARY_BUF_LEN;
		uint8_t pair = ((src[i] & 2) >> 1) | ((src[i] & 1) << 1);
		secondary[index] |= pair << (section * 2);
	}

	int idx = 0;
	dest[idx++] = translate(secondary[0]);
	for (int i = 1; i < SECONDARY_BUF_LEN; i++)
		dest[idx++] = translate(secondary[i] ^ secondary[i - 1]);
	dest[idx++] = translate(primary[0] ^ secondary[SECONDARY_BUF_LEN - 1]);
	for (int i = 1; i < PRIMARY_BUF_LEN; i++)
		dest[idx++] = translate(primary[i] ^ primary[i - 1]);
	dest[idx++] = translate(primary[PRIMARY_BUF_LEN - 1]);   // data checksum
}

// Build one full 6656-byte NIB track from a 4096-byte DOS-order track.
static void nib_track_from_dsk(uint8_t *nibtrack, const uint8_t *dsktrack, int track)
{
	for (int phys = 0; phys < A2_SECTORS_PER_TRACK; phys++) {
		// physical position -> logical sector
		int logical = 0;
		for (int i = 0; i < A2_SECTORS_PER_TRACK; i++)
			if (phys_interleave[i] == phys) { logical = i; break; }

		int soft = soft_interleave[logical];
		const uint8_t *sec = dsktrack + soft * A2_SECTOR_SIZE;

		uint8_t *o = nibtrack + phys * BYTES_PER_NIB_SECTOR;
		memset(o, GAP_BYTE, GAP1_LEN);
		o += GAP1_LEN;

		// address field
		memcpy(o, addr_prolog, 3); o += 3;
		odd_even_encode(o, DEFAULT_VOLUME); o += 2;
		odd_even_encode(o, track);          o += 2;
		odd_even_encode(o, logical);        o += 2;
		odd_even_encode(o, DEFAULT_VOLUME ^ track ^ logical); o += 2;
		memcpy(o, addr_epilog, 3); o += 3;

		memset(o, GAP_BYTE, GAP2_LEN); o += GAP2_LEN;

		// data field
		memcpy(o, data_prolog, 3); o += 3;
		nibbilize(sec, o); o += DATA_LEN + 1;
		memcpy(o, data_epilog, 3); o += 3;
	}
}

void a2_dsk_to_nib(uint8_t *nib, const uint8_t *dsk)
{
	for (int t = 0; t < A2_TRACKS_525; t++)
		nib_track_from_dsk(nib + (size_t)t * A2_NIB_TRACK_SIZE,
		                   dsk + (size_t)t * A2_TRACK_SIZE, t);
}

// Parse one NIB sector out of a byte stream. Returns 1 and fills sector/track
// on success. State machine ported from dsk2nib_lib.cpp parse_nib_sector.
static int parse_nib_sector(const uint8_t *d, int len, uint8_t *out256, int *track, int *sector)
{
	int pos = 0, state = 0;
	uint8_t primary[PRIMARY_BUF_LEN], secondary[SECONDARY_BUF_LEN], checksum;

	while (pos < len) {
		uint8_t b = d[pos++];
		switch (state) {
		case 0: if (b == 0xd5) state = 1; break;
		case 1: state = (b == 0xaa) ? 2 : 0; break;
		case 2: state = (b == 0x96) ? 3 : 0; break;
		case 3: if (pos >= len) return 0; odd_even_decode(b, d[pos++]); state = 4; break; // volume
		case 4: if (pos >= len) return 0; *track  = odd_even_decode(b, d[pos++]); state = 5; break;
		case 5: if (pos >= len) return 0; *sector = odd_even_decode(b, d[pos++]); state = 6; break;
		case 6: if (pos >= len) return 0; pos++;  state = 7; break;  // checksum (ignored)
		case 7: if (b == 0xde) state = 8; break;
		case 8: state = (b == 0xaa) ? 9 : 7; break;
		case 9: if (b == 0xd5) state = 10; break;
		case 10: state = (b == 0xaa) ? 11 : 9; break;
		case 11: state = (b == 0xad) ? 12 : 9; break;
		case 12: {
			checksum = untranslate(b);
			secondary[0] = checksum;
			for (int i = 1; i < SECONDARY_BUF_LEN; i++) {
				if (pos >= len) return 0;
				checksum ^= untranslate(d[pos++]);
				secondary[i] = checksum;
			}
			for (int i = 0; i < PRIMARY_BUF_LEN; i++) {
				if (pos >= len) return 0;
				checksum ^= untranslate(d[pos++]);
				primary[i] = checksum;
			}
			if (pos >= len) return 0;
			checksum ^= untranslate(d[pos++]);   // data checksum (ignored)

			for (int i = 0; i < PRIMARY_BUF_LEN; i++) {
				int index = i % SECONDARY_BUF_LEN;
				uint8_t bit0, bit1;
				switch (i / SECONDARY_BUF_LEN) {
				case 0: bit0 = (secondary[index] & 2) > 0; bit1 = (secondary[index] & 1) > 0; break;
				case 1: bit0 = (secondary[index] & 8) > 0; bit1 = (secondary[index] & 4) > 0; break;
				default: bit0 = (secondary[index] & 0x20) > 0; bit1 = (secondary[index] & 0x10) > 0; break;
				}
				out256[i] = (primary[i] << 2) | (bit1 << 1) | bit0;
			}
			return 1;
		}
		default: state = 0; break;
		}
	}
	return 0;
}

// Parse one 6656-byte NIB track into a 4096-byte DOS-order track. Returns the
// number of sectors recovered.
static int nib_track_to_dsk_track(const uint8_t *nt, uint8_t *dt)
{
	int got = 0, pos = 0;
	while (pos < A2_NIB_TRACK_SIZE - 400) {
		uint8_t sec[A2_SECTOR_SIZE];
		int tr, s;
		if (parse_nib_sector(nt + pos, A2_NIB_TRACK_SIZE - pos, sec, &tr, &s)) {
			if (s >= 0 && s < A2_SECTORS_PER_TRACK) {
				memcpy(dt + soft_interleave[s] * A2_SECTOR_SIZE, sec, A2_SECTOR_SIZE);
				got++;
			}
			pos += BYTES_PER_NIB_SECTOR;
		} else {
			pos++;
		}
	}
	return got;
}

int a2_nib_to_dsk(uint8_t *dsk, const uint8_t *nib)
{
	int ok = 1;
	for (int t = 0; t < A2_TRACKS_525; t++) {
		int got = nib_track_to_dsk_track(nib + (size_t)t * A2_NIB_TRACK_SIZE,
		                                 dsk + (size_t)t * A2_TRACK_SIZE);
		if (got < A2_SECTORS_PER_TRACK) ok = 0;
	}
	return ok;
}

// ===========================================================================
// 5.25" "easy WOZ" (DOS-order DSK <-> WOZ2)
// ===========================================================================
// Layout (matches make_blank_woz.py): header(12) + INFO chunk(8+60) +
// TMAP chunk(8+160) + TRKS chunk(8 + 1280 dir + BITS). First BITS block = 3.
#define WOZ_INFO_LEN   60
#define WOZ_TMAP_LEN   160
#define WOZ_TRK_DIR    (160 * 8)         // 1280
#define WOZ525_BLOCKS_PER_TRACK 13
#define WOZ525_FIRST_BLOCK 3
#define WOZ525_BITS    (WOZ525_BLOCKS_PER_TRACK * A2_BLOCK_SIZE * 8)  // 53248

#define WOZ525_SIZE    (1536 + A2_TRACKS_525 * WOZ525_BLOCKS_PER_TRACK * A2_BLOCK_SIZE)

static void woz_put_chunk_hdr(uint8_t *p, const char *id, uint32_t size)
{
	memcpy(p, id, 4);
	wr_le32(p + 4, size);
}

size_t a2_dsk_to_woz525(uint8_t *woz, size_t woz_cap, const uint8_t *dsk)
{
	if (woz_cap < WOZ525_SIZE) return 0;
	memset(woz, 0, WOZ525_SIZE);

	// nibblize all tracks first
	static uint8_t nib[A2_NIB_IMAGE_SIZE];
	a2_dsk_to_nib(nib, dsk);

	uint8_t *p = woz;
	// File header (CRC filled in at the end)
	memcpy(p, "WOZ2", 4);
	p[4] = 0xFF; p[5] = 0x0A; p[6] = 0x0D; p[7] = 0x0A;
	// p[8..11] CRC later
	p += 12;

	// INFO chunk
	woz_put_chunk_hdr(p, "INFO", WOZ_INFO_LEN); p += 8;
	uint8_t *info = p;
	info[0] = 2;     // version
	info[1] = 1;     // disk type 5.25"
	info[2] = 0;     // write protected
	info[3] = 0;     // synchronized
	info[4] = 0;     // cleaned
	memset(info + 5, ' ', 32);
	memcpy(info + 5, "MiSTer IIgs easy-WOZ 5.25", 25);
	info[37] = 1;    // sides
	info[38] = 0;    // boot sector format
	info[39] = 32;   // optimal bit timing (4us)
	wr_le16(info + 44, WOZ525_BLOCKS_PER_TRACK);   // largest track
	p += WOZ_INFO_LEN;

	// TMAP chunk (quarter-track map; whole tracks at t*4 +/- 1)
	woz_put_chunk_hdr(p, "TMAP", WOZ_TMAP_LEN); p += 8;
	uint8_t *tmap = p;
	memset(tmap, 0xFF, WOZ_TMAP_LEN);
	for (int t = 0; t < A2_TRACKS_525; t++) {
		int center = t * 4;
		for (int off = -1; off <= 1; off++) {
			int idx = center + off;
			if (idx >= 0 && idx < WOZ_TMAP_LEN) tmap[idx] = (uint8_t)t;
		}
	}
	p += WOZ_TMAP_LEN;

	// TRKS chunk: 1280-byte dir + BITS data
	uint32_t trks_data_len = WOZ_TRK_DIR + A2_TRACKS_525 * WOZ525_BLOCKS_PER_TRACK * A2_BLOCK_SIZE;
	woz_put_chunk_hdr(p, "TRKS", trks_data_len); p += 8;
	uint8_t *trk_dir = p;                    // 160 * 8
	uint8_t *bits = p + WOZ_TRK_DIR;
	int block = WOZ525_FIRST_BLOCK;
	for (int t = 0; t < A2_TRACKS_525; t++) {
		uint8_t *e = trk_dir + t * 8;
		wr_le16(e + 0, (uint16_t)block);                 // starting block
		wr_le16(e + 2, WOZ525_BLOCKS_PER_TRACK);          // block count
		wr_le32(e + 4, WOZ525_BITS);                      // bit count
		// BITS = the 6656-byte NIB track, byte-aligned (each nibble byte = 8 bits)
		memcpy(bits + (size_t)t * WOZ525_BLOCKS_PER_TRACK * A2_BLOCK_SIZE,
		       nib + (size_t)t * A2_NIB_TRACK_SIZE, A2_NIB_TRACK_SIZE);
		block += WOZ525_BLOCKS_PER_TRACK;
	}

	// CRC32 over everything after byte 12
	uint32_t crc = woz_crc32(woz + 12, WOZ525_SIZE - 12);
	wr_le32(woz + 8, crc);
	return WOZ525_SIZE;
}

// Locate a chunk by id; returns pointer to its data and fills *out_size, or NULL.
static const uint8_t *woz_find_chunk(const uint8_t *woz, size_t size,
                                     const char *id, uint32_t *out_size)
{
	size_t pos = 12;
	while (pos + 8 <= size) {
		uint32_t csize = rd_le32(woz + pos + 4);
		if (memcmp(woz + pos, id, 4) == 0) {
			if (out_size) *out_size = csize;
			return woz + pos + 8;
		}
		pos += 8 + csize;
	}
	return NULL;
}

int a2_woz525_to_dsk(uint8_t *dsk, const uint8_t *woz, size_t woz_size)
{
	if (woz_disk_type(woz, woz_size) != 1) return 0;
	const uint8_t *trks = woz_find_chunk(woz, woz_size, "TRKS", NULL);
	if (!trks) return 0;

	static uint8_t nib[A2_NIB_IMAGE_SIZE];
	memset(nib, 0, sizeof(nib));

	for (int t = 0; t < A2_TRACKS_525; t++) {
		const uint8_t *e = trks + t * 8;
		uint16_t start_block = rd_le16(e + 0);
		uint16_t block_cnt   = rd_le16(e + 2);
		if (block_cnt == 0) continue;
		size_t off = (size_t)start_block * A2_BLOCK_SIZE;
		if (off + A2_NIB_TRACK_SIZE > woz_size) return 0;
		memcpy(nib + (size_t)t * A2_NIB_TRACK_SIZE, woz + off, A2_NIB_TRACK_SIZE);
	}
	return a2_nib_to_dsk(dsk, nib);
}

// Decode a single 5.25" track from a WOZ into dsk (143360) at track*4096.
int a2_woz525_decode_track(const uint8_t *woz, size_t woz_size, int track, uint8_t *dsk)
{
	if (woz_disk_type(woz, woz_size) != 1) return 0;
	const uint8_t *trks = woz_find_chunk(woz, woz_size, "TRKS", NULL);
	if (!trks || track < 0 || track >= A2_TRACKS_525) return 0;
	const uint8_t *e = trks + track * 8;
	uint16_t start_block = rd_le16(e + 0);
	uint16_t block_cnt   = rd_le16(e + 2);
	if (block_cnt == 0) return 0;
	size_t off = (size_t)start_block * A2_BLOCK_SIZE;
	if (off + A2_NIB_TRACK_SIZE > woz_size) return 0;
	return nib_track_to_dsk_track(woz + off, dsk + (size_t)track * A2_TRACK_SIZE);
}

// Map a WOZ file LBA (512-block index) to its track index, or -1 (header/unmapped).
int a2_woz_track_for_lba(const uint8_t *woz, size_t woz_size, uint32_t lba)
{
	const uint8_t *trks = woz_find_chunk(woz, woz_size, "TRKS", NULL);
	if (!trks) return -1;
	for (int t = 0; t < 160; t++) {
		const uint8_t *e = trks + t * 8;
		uint16_t sb = rd_le16(e + 0);
		uint16_t bc = rd_le16(e + 2);
		if (bc == 0) continue;
		if (lba >= sb && lba < (uint32_t)sb + bc) return t;
	}
	return -1;
}

// ===========================================================================
// 3.5" 800K Apple zoned GCR (PO <-> WOZ2)
// Algorithm ported from Clemens (clem_disk.c), itself from CiderPress Nibble35.
// ===========================================================================
#define NT35            160              // nib tracks (80 cyl x 2 sides)
#define TAG35           12               // tag bytes prepended to each sector

// physical sector position -> logical (ProDOS) sector, per speed region (2:1)
static const int phys_to_logical_35[5][16] = {
	{ 0, 6, 1, 7, 2, 8, 3, 9, 4, 10, 5, 11, -1, -1, -1, -1 },
	{ 0, 6, 1, 7, 2, 8, 3, 9, 4, 10, 5, -1, -1, -1, -1, -1 },
	{ 0, 5, 1, 6, 2, 7, 3, 8, 4,  9, -1, -1, -1, -1, -1, -1 },
	{ 0, 5, 1, 6, 2, 7, 3, 8, 4, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 4, 1, 5, 2, 6, 3, 7, -1, -1, -1, -1, -1, -1, -1, -1 },
};
static const int sectors_per_region_35[5] = { 12, 11, 10, 9, 8 };
static const int region_track_start_35[6] = { 0, 32, 64, 96, 128, 160 };

static int region_of_track(int nt)
{
	for (int r = 0; r < 5; r++)
		if (nt < region_track_start_35[r + 1]) return r;
	return 4;
}
// base ProDOS block index for nib track nt (cumulative sectors before it)
static int base_block_of_track(int nt)
{
	int base = 0;
	for (int k = 0; k < nt; k++) base += sectors_per_region_35[region_of_track(k)];
	return base;
}

// reverse 6-and-2 table (disk byte 0x80..0xFF -> 6-bit value; 0x80 = invalid)
static uint8_t from_gcr6[128];
static void init_from_gcr6(void)
{
	static int done = 0;
	if (done) return;
	memset(from_gcr6, 0x80, sizeof(from_gcr6));
	for (int i = 0; i < 0x40; i++) from_gcr6[gcr6_table[i] - 0x80] = (uint8_t)i;
	done = 1;
}
static inline uint8_t un62(uint8_t disk) { return (disk & 0x80) ? from_gcr6[disk - 0x80] : 0x80; }

// ---- bit writer (MSB-first), matching Clemens' bit ordering ----
typedef struct { uint8_t *buf; size_t cap; uint32_t bit; } BitW;
static void bw_bits(BitW *w, uint32_t val, int nbits)
{
	for (int i = nbits - 1; i >= 0; i--) {
		size_t byte = w->bit >> 3;
		int off = 7 - (int)(w->bit & 7);
		if (byte < w->cap) {
			if (val & (1u << i)) w->buf[byte] |= (1 << off);
			else                 w->buf[byte] &= ~(1 << off);
		}
		w->bit++;
	}
}
static inline void bw_byte(BitW *w, uint8_t v)   { bw_bits(w, v, 8); }
static inline void bw_sync(BitW *w, int cnt)     { for (int i = 0; i < cnt; i++) bw_bits(w, 0xFF, 10); }
static inline void bw_62(BitW *w, uint8_t v)     { bw_byte(w, gcr6_table[v & 0x3f]); }

// Encode one 512-byte sector's data field (12 tag + 512 data -> GCR + checksum).
static void encode_data_35(BitW *w, const uint8_t *buf)
{
	uint8_t s0[175], s1[175], s2[175];
	uint8_t data[524];
	unsigned chk[3] = { 0, 0, 0 };
	unsigned di = 0, si = 0;
	uint8_t v;

	memset(data, 0, TAG35);
	memcpy(data + TAG35, buf, 512);

	while (di < 524) {
		chk[0] = (chk[0] & 0xff) << 1;
		if (chk[0] & 0x100) chk[0]++;
		v = data[di++];
		chk[2] += v;
		if (chk[0] & 0x100) { chk[2]++; chk[0] &= 0xff; }
		s0[si] = (v ^ chk[0]) & 0xff;

		v = data[di++];
		chk[1] += v;
		if (chk[2] > 0xff) { chk[1]++; chk[2] &= 0xff; }
		s1[si] = (v ^ chk[2]) & 0xff;

		if (di < 524) {
			v = data[di++];
			chk[0] += v;
			if (chk[1] > 0xff) { chk[0]++; chk[1] &= 0xff; }
			s2[si] = (v ^ chk[1]) & 0xff;
			si++;
		}
	}
	s2[si++] = 0;

	for (unsigned i = 0; i < si; i++) {
		uint8_t top = ((s0[i] & 0xc0) >> 2) | ((s1[i] & 0xc0) >> 4) | ((s2[i] & 0xc0) >> 6);
		bw_62(w, top);
		bw_62(w, s0[i]);
		bw_62(w, s1[i]);
		if (i < si - 1) bw_62(w, s2[i]);
	}
	uint8_t top = ((chk[0] & 0xc0) >> 6) | ((chk[1] & 0xc0) >> 4) | ((chk[2] & 0xc0) >> 2);
	bw_62(w, top);
	bw_62(w, chk[2]);
	bw_62(w, chk[1]);
	bw_62(w, chk[0]);
}

// Decode one sector's 699-byte GCR data field back to 512 bytes. 1 ok.
static int decode_data_35(const uint8_t *gcr, uint8_t *out512)
{
	uint8_t s0[175], s1[175], s2[175];
	const uint8_t *p = gcr;
	for (int i = 0; i < 175; i++) {
		uint8_t b = un62(*p++); if (b == 0x80) return 0;
		uint8_t r0 = un62(*p++); if (r0 == 0x80) return 0;
		uint8_t r1 = un62(*p++); if (r1 == 0x80) return 0;
		uint8_t r2 = 0;
		if (i < 174) { r2 = un62(*p++); if (r2 == 0x80) return 0; }
		s0[i] = (uint8_t)(((b << 2) & 0xc0) | r0);
		s1[i] = (uint8_t)(((b << 4) & 0xc0) | r1);
		s2[i] = (uint8_t)(((b << 6) & 0xc0) | r2);
	}
	unsigned chk[3] = { 0, 0, 0 };
	uint8_t data[524];
	int di = 0;
	for (int i = 0; i < 175; i++) {
		chk[0] = (chk[0] & 0xff) << 1;
		if (chk[0] & 0x100) chk[0]++;
		uint8_t v0 = s0[i] ^ chk[0];
		chk[2] += v0;
		if (chk[0] & 0x100) { chk[2]++; chk[0] &= 0xff; }
		data[di++] = v0;

		uint8_t v1 = s1[i] ^ chk[2];
		chk[1] += v1;
		if (chk[2] >= 0x100) { chk[1]++; chk[2] &= 0xff; }
		data[di++] = v1;
		if (di == 524) break;

		uint8_t v2 = s2[i] ^ chk[1];
		chk[0] += v2;
		if (chk[1] >= 0x100) { chk[0]++; chk[1] &= 0xff; }
		data[di++] = v2;
	}
	memcpy(out512, data + TAG35, 512);
	return 1;
}

// Self-sync gap sizes (sync byte counts), derived from Clemens GAP1=500,GAP3=53.
#define GAP1_SYNC_35 ((500 * 8) / 10)
#define GAP3_SYNC_35 ((53  * 8) / 10)

static void encode_track_35(BitW *w, int nt, const uint8_t *po)
{
	int region = region_of_track(nt);
	int count = sectors_per_region_35[region];
	int ltrack = nt / 2;
	int side = nt % 2;
	int base = base_block_of_track(nt);
	uint8_t side_track64 = (uint8_t)((side << 5) | (ltrack >> 6));
	uint8_t fmt = (uint8_t)(0x20 | 0x2);   // double-sided, interleave 2

	bw_byte(w, 0xff);
	bw_sync(w, GAP1_SYNC_35);

	for (int phys = 0; phys < count; phys++) {
		int lsec = phys_to_logical_35[region][phys];
		const uint8_t *src = po + (size_t)(base + lsec) * 512;

		bw_byte(w, 0xff);
		// address field
		bw_byte(w, 0xd5); bw_byte(w, 0xaa); bw_byte(w, 0x96);
		bw_62(w, (uint8_t)ltrack);
		bw_62(w, (uint8_t)lsec);
		bw_62(w, side_track64);
		bw_62(w, fmt);
		bw_62(w, (uint8_t)(ltrack ^ lsec ^ side_track64 ^ fmt));
		bw_byte(w, 0xde); bw_byte(w, 0xaa); bw_byte(w, 0xff);
		bw_sync(w, 4);
		bw_byte(w, 0xff);
		// data field
		bw_byte(w, 0xd5); bw_byte(w, 0xaa); bw_byte(w, 0xad);
		bw_62(w, (uint8_t)lsec);            // spare byte
		encode_data_35(w, src);
		bw_byte(w, 0xde); bw_byte(w, 0xaa);
		if (phys + 1 < count) {
			bw_byte(w, 0xff); bw_byte(w, 0xff); bw_byte(w, 0xff);
			bw_sync(w, GAP3_SYNC_35);
		}
	}
}

size_t a2_po_to_woz35(uint8_t *woz, size_t woz_cap, const uint8_t *po)
{
	const size_t bits_start = 1536;          // block 3
	if (woz_cap < bits_start) return 0;
	memset(woz, 0, bits_start);

	// header
	memcpy(woz, "WOZ2", 4);
	woz[4] = 0xFF; woz[5] = 0x0A; woz[6] = 0x0D; woz[7] = 0x0A;

	// INFO chunk
	uint8_t *p = woz + 12;
	woz_put_chunk_hdr(p, "INFO", WOZ_INFO_LEN); p += 8;
	uint8_t *info = p;
	info[0] = 2; info[1] = 2;               // version, disk type 3.5"
	memset(info + 5, ' ', 32);
	memcpy(info + 5, "MiSTer IIgs easy-WOZ 3.5", 24);
	info[37] = 2;                            // sides
	info[38] = 0;
	info[39] = 16;                           // bit timing (2us)
	p += WOZ_INFO_LEN;

	// TMAP chunk (1:1 for 160 tracks)
	woz_put_chunk_hdr(p, "TMAP", WOZ_TMAP_LEN); p += 8;
	for (int i = 0; i < WOZ_TMAP_LEN; i++) p[i] = (uint8_t)i;
	p += WOZ_TMAP_LEN;

	// TRKS chunk header (size filled after we know total BITS)
	uint8_t *trks_hdr = p;
	uint8_t *trk_dir = p + 8;
	memset(trk_dir, 0, WOZ_TRK_DIR);

	static uint8_t tmp[16 * 1024];
	int block = WOZ525_FIRST_BLOCK;
	int largest = 0;
	for (int nt = 0; nt < NT35; nt++) {
		memset(tmp, 0, sizeof(tmp));
		BitW w = { tmp, sizeof(tmp), 0 };
		encode_track_35(&w, nt, po);
		uint32_t bits = w.bit;
		uint32_t bytes = (bits + 7) / 8;
		uint16_t blocks = (uint16_t)((bytes + A2_BLOCK_SIZE - 1) / A2_BLOCK_SIZE);
		size_t off = (size_t)block * A2_BLOCK_SIZE;
		if (off + (size_t)blocks * A2_BLOCK_SIZE > woz_cap) return 0;
		memset(woz + off, 0, (size_t)blocks * A2_BLOCK_SIZE);
		memcpy(woz + off, tmp, bytes);

		uint8_t *e = trk_dir + nt * 8;
		wr_le16(e + 0, (uint16_t)block);
		wr_le16(e + 2, blocks);
		wr_le32(e + 4, bits);
		block += blocks;
		if (blocks > largest) largest = blocks;
	}
	wr_le16(info + 44, (uint16_t)largest);    // largest track (now known)

	uint32_t trks_data_len = WOZ_TRK_DIR + (uint32_t)(block - WOZ525_FIRST_BLOCK) * A2_BLOCK_SIZE;
	woz_put_chunk_hdr(trks_hdr, "TRKS", trks_data_len);

	size_t total = (size_t)block * A2_BLOCK_SIZE;
	uint32_t crc = woz_crc32(woz + 12, total - 12);
	wr_le32(woz + 8, crc);
	return total;
}

// Frame a track's WOZ BITS into disk bytes (simple self-sync framer).
static int frame_track(const uint8_t *bits, uint32_t bit_count, uint8_t *out, int out_cap)
{
	int n = 0;
	uint8_t reg = 0;
	for (uint32_t i = 0; i < bit_count; i++) {
		int bit = (bits[i >> 3] >> (7 - (i & 7))) & 1;
		reg = (uint8_t)((reg << 1) | bit);
		if (reg & 0x80) {
			if (n < out_cap) out[n++] = reg;
			reg = 0;
		}
	}
	return n;
}

// Decode a single 3.5" track nt into po (819200). Reports the ProDOS block range
// written via *base_block / *block_count. Returns the number of sectors decoded.
int a2_woz35_decode_track(const uint8_t *woz, size_t woz_size, int nt, uint8_t *po,
                          int *base_block, int *block_count)
{
	init_from_gcr6();
	if (nt < 0 || nt >= NT35) return 0;
	const uint8_t *trks = woz_find_chunk(woz, woz_size, "TRKS", NULL);
	if (!trks) return 0;

	const uint8_t *e = trks + nt * 8;
	uint16_t start_block = rd_le16(e + 0);
	uint16_t blk_cnt     = rd_le16(e + 2);
	uint32_t bit_count   = rd_le32(e + 4);
	if (blk_cnt == 0 || bit_count == 0) return 0;
	size_t off = (size_t)start_block * A2_BLOCK_SIZE;
	if (off + (size_t)blk_cnt * A2_BLOCK_SIZE > woz_size) return 0;

	static uint8_t bytes[20 * 1024];
	int n = frame_track(woz + off, bit_count, bytes, sizeof(bytes));
	int base = base_block_of_track(nt);
	int want = sectors_per_region_35[region_of_track(nt)];
	int got = 0;

	int i = 0;
	while (i + 3 < n && got < want) {
		if (bytes[i] == 0xd5 && bytes[i + 1] == 0xaa && bytes[i + 2] == 0xad) {
			int dpos = i + 4;                        // skip mark(3) + spare(1)
			if (dpos + 699 > n) break;
			uint8_t lsec = un62(bytes[i + 3]);        // logical sector from spare byte
			if (lsec < (uint8_t)want) {
				uint8_t sec[512];
				if (decode_data_35(bytes + dpos, sec)) {
					memcpy(po + (size_t)(base + lsec) * 512, sec, 512);
					got++;
				}
			}
			i = dpos + 699;
		} else {
			i++;
		}
	}
	if (base_block)  *base_block = base;
	if (block_count) *block_count = want;
	return got;
}

int a2_woz35_to_po(uint8_t *po, const uint8_t *woz, size_t woz_size)
{
	if (woz_disk_type(woz, woz_size) != 2) return 0;
	int placed = 0;
	for (int nt = 0; nt < NT35; nt++)
		placed += a2_woz35_decode_track(woz, woz_size, nt, po, NULL, NULL);
	return placed == (A2_35_IMAGE_SIZE / 512) ? 1 : 0;
}

// ===========================================================================
// classification
// ===========================================================================
static int ext_is(const char *ext, const char *want)
{
	return ext && strcmp(ext, want) == 0;
}

DiskClass iigs_classify(const uint8_t *buf, size_t size, const char *ext)
{
	// content probes first
	int wt = woz_disk_type(buf, size);
	if (wt == 1) return DC_FLOPPY_525;
	if (wt == 2) return DC_FLOPPY_35;

	TwoMG m;
	if (twomg_parse(buf, size, &m)) {
		if (m.format == 2) return DC_FLOPPY_525;          // NIB payload
		if (m.data_len == A2_525_IMAGE_SIZE) return DC_FLOPPY_525;
		if (m.data_len == A2_35_IMAGE_SIZE)  return DC_FLOPPY_35;
		return DC_HDD;
	}

	if (dc42_probe(buf, size)) {
		uint32_t ds = rd_be32(buf + 0x40);
		if (ds == A2_35_IMAGE_SIZE || ds == 409600) return DC_FLOPPY_35;
		return DC_HDD;
	}

	// bare images by size/extension
	if (ext_is(ext, "nib") || size == A2_NIB_IMAGE_SIZE) return DC_FLOPPY_525;
	if (size == A2_525_IMAGE_SIZE) return DC_FLOPPY_525;
	if (size == A2_35_IMAGE_SIZE)  return DC_FLOPPY_35;   // §4 edge: 800K assumed floppy
	if (ext_is(ext, "hdv") || ext_is(ext, "po")) return DC_HDD;
	if (size > 0 && (size % A2_BLOCK_SIZE) == 0) return DC_HDD;
	return DC_UNKNOWN;
}
