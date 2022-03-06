#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <algorithm>
#include <vector>

#include "../../file_io.h"
#include "../../user_io.h"
#include "../../hardware.h"

//#define dbgprintf printf
#define dbgprintf(...)

#define CBM_PRG                  0x82

#define T64_BYTE_PER_HEADER      32U
#define T64_FILL_VALUE           0x20

#define D64_FILE_STRIDE          10U
#define D64_DIR_STRIDE           3U

#define D64_TRACK_PER_DISK       35U
#define D64_SECTOR_PER_DISK      683U
#define D64_FILE_SECTOR_PER_DISK (D64_SECTOR_PER_DISK - 19U)
#define D64_BYTE_PER_DIR         32U
#define D64_BYTE_PER_SECTOR      256U
#define D64_BYTE_PER_FILE_SECTOR 254U
#define D64_DIR_PER_SECTOR       8U
#define D64_FILE_PER_DISK        144U
#define D64_BYTE_PER_BAM_ENTRY   4U
#define D64_BYTE_PER_STRING      16U

#define D64_BAM_TRACK            18U
#define D64_BAM_SECTOR           0U
#define D64_DIR_TRACK            18U
#define D64_DIR_SECTOR           1U
#define D64_FILE_TRACK           1U // 1-35
#define D64_FILE_SECTOR          0U

#define D64_FILL_VALUE           0xA0
#define D64_INIT_VALUE           0x00 // DIR relies on 0x00 initial value

struct FileRecord {
	char           name[D64_BYTE_PER_STRING];
	unsigned char  cbm;
	unsigned short start;
	unsigned short size;
	unsigned       offset;
	unsigned       index;
};

static bool cmp_offset(const FileRecord& a, const FileRecord& b) { return a.offset < b.offset; }
static bool cmp_index(const FileRecord& a, const FileRecord& b) { return a.index < b.index; }

static unsigned char d64_sector_per_track(unsigned char trackNum) {
	return	  (trackNum <= 17) ? 21U
			: (trackNum <= 24) ? 19U
			: (trackNum <= 30) ? 18U
			:                    17U;
}

static unsigned d64_file_sector(unsigned size) {
	return (size + D64_BYTE_PER_FILE_SECTOR - 1) / D64_BYTE_PER_FILE_SECTOR;
}

static unsigned d64_offset(unsigned char trackNum, unsigned char sectorNum)
{
	static unsigned TrackFileOffset[D64_TRACK_PER_DISK] = {
		0x00000, /*  1 */  0x01500, /*  2 */  0x02A00, /*  3 */  0x03F00, /*  4 */  0x05400, /*  5 */
		0x06900, /*  6 */  0x07E00, /*  7 */  0x09300, /*  8 */  0x0A800, /*  9 */  0x0BD00, /* 10 */
		0x0D200, /* 11 */  0x0E700, /* 12 */  0x0FC00, /* 13 */  0x11100, /* 14 */  0x12600, /* 15 */
		0x13B00, /* 16 */  0x15000, /* 17 */  0x16500, /* 18 */  0x17800, /* 19 */  0x18B00, /* 20 */
		0x19E00, /* 21 */  0x1B100, /* 22 */  0x1C400, /* 23 */  0x1D700, /* 24 */  0x1EA00, /* 25 */
		0x1FC00, /* 26 */  0x20E00, /* 27 */  0x22000, /* 28 */  0x23200, /* 29 */  0x24400, /* 30 */
		0x25600, /* 31 */  0x26700, /* 32 */  0x27800, /* 33 */  0x28900, /* 34 */  0x29A00, /* 35 */
	};

	return TrackFileOffset[trackNum - 1] + sectorNum * D64_BYTE_PER_SECTOR;
}

static void d64_advance_pointer(unsigned char& trackNum, unsigned char& sectorNum) {
	unsigned stride = trackNum == D64_DIR_TRACK ? D64_DIR_STRIDE : D64_FILE_STRIDE;
	unsigned sectorPerTrack = d64_sector_per_track(trackNum);

	sectorNum = (sectorNum + stride) % sectorPerTrack;
	// 18 sectors with a stride of 10 has a common divisor of 2.  NOTE: this doesn't handle all combinations of sectorsPerTrack and stride
	if (!(sectorPerTrack & 1) && !(stride & 1) && !(sectorNum >> 1)) sectorNum ^= 1;
	// caller needs to handle DIR track
	if (!sectorNum) trackNum = trackNum % D64_TRACK_PER_DISK + 1;
}

static void d64_advance_dir_pointer(unsigned char& trackNum, unsigned char& sectorNum) {
	unsigned stride = trackNum == D64_DIR_TRACK ? D64_DIR_STRIDE : D64_FILE_STRIDE;
	unsigned sectorPerTrack = d64_sector_per_track(trackNum);

	// don't leave current track
	sectorNum = (sectorNum + stride) % sectorPerTrack;
	// caller needs to handle BAM sector
}

static int c64_convert_t64_to_d64(fileTYPE* f_in, fileTYPE* f_out)
{
	std::vector<FileRecord> files;
	char header[T64_BYTE_PER_HEADER];
	char name[D64_BYTE_PER_STRING];

	if (!FileSeek(f_in, 0, SEEK_SET)) return 0;
	// ignore signature
	if(!FileReadAdv(f_in, header, sizeof(header))) return 0;
	if (memcmp(header, "C64", strlen("C64"))) return 0;
	// header
	if (!FileReadAdv(f_in, header, sizeof(header))) return 0;

	unsigned short numRecords = (header[0x03] << 8) | header[0x02];
	memcpy(name, header + 0x08, sizeof(name));

	for (unsigned i = 0; i < numRecords; i++)
	{
		// record
		if (!FileReadAdv(f_in, header, sizeof(header))) return 0;
		if (!header[0x00] || !header[0x01]) continue;

		FileRecord r;
		r.cbm = CBM_PRG; // header[0x01];
		r.start = (header[0x03] << 8) | header[0x02];
		r.size = ((header[0x05] << 8) | header[0x04]) - r.start;
		r.offset = (header[0x0B] << 24) | (header[0x0A] << 16) | (header[0x09] << 8) | header[0x08];
		memcpy(r.name, header + 0x10, sizeof(r.name));
		r.index = i;

		files.push_back(r);
	}

	// workaround incorrect end/limit address
	std::sort(files.begin(), files.end(), cmp_offset);
	for (unsigned i = 0; i < files.size(); i++)
	{
		unsigned short size = (i < files.size() - 1 ? files[i + 1].offset : f_in->size) - files[i].offset;
		if (size < files[i].size) files[i].size = size;
	}
	std::sort(files.begin(), files.end(), cmp_index);

	// account for start address in PRG format
	for (unsigned i = 0; i < files.size(); i++) files[i].size += 2;

	// drop files until they fit
	if (files.size() > D64_FILE_PER_DISK) files.resize(D64_FILE_PER_DISK);
	for (unsigned i = 0, fileSectors = 0; i < files.size(); i++)
	{
		fileSectors += d64_file_sector(files[i].size);
		if (fileSectors > D64_FILE_SECTOR_PER_DISK)
		{
			files.resize(i);
			break;
		}
	}

	//printf("T64: %d records\n", files.size()); for (auto r : files) printf("start: %x, size: %x, offset %x, index: %d\n", r.start, r.size, r.offset, r.index);

	if (!files.size()) return 0;

	unsigned char sector[D64_BYTE_PER_SECTOR];
	memset(sector, D64_INIT_VALUE, sizeof(sector));
	for (unsigned i = 0; i < D64_SECTOR_PER_DISK; i++) if (!FileWriteAdv(f_out, sector, sizeof(sector))) return 0;
	if (!FileSeek(f_out, 0, SEEK_SET)) return 0;

	unsigned char bam[D64_BYTE_PER_SECTOR];
	memset(bam, D64_INIT_VALUE, sizeof(bam));

	unsigned char dir[D64_BYTE_PER_DIR];

	unsigned char fileTrackNum = D64_FILE_TRACK, fileSectorNum = D64_FILE_SECTOR, dirTrackNum = D64_DIR_TRACK, dirSectorNum = D64_DIR_SECTOR, dirEntry = 0;
	for (unsigned i = 0; i < files.size(); i++) {
		FileRecord& r = files[i];

		// DIR sector
		if (dirEntry == 0 && i != 0)
		{
			// set next track/sector pointer of prev node
			if (!FileSeek(f_out, d64_offset(dirTrackNum, dirSectorNum), SEEK_SET)) return 0;
			do d64_advance_dir_pointer(dirTrackNum, dirSectorNum); while (dirTrackNum == D64_BAM_TRACK && dirSectorNum == D64_BAM_SECTOR);
			dir[0x00] = dirTrackNum;
			dir[0x01] = dirSectorNum;
			if (!FileWriteAdv(f_out, dir, 2)) return 0;

			// check for overflow
			bool success = dirSectorNum != D64_DIR_SECTOR;
			if (!success) printf("T64: dir overflow on file: %d\n", i);
			if (!success) return 0;
		}

		if (!FileSeek(f_out, d64_offset(dirTrackNum, dirSectorNum) + dirEntry * D64_BYTE_PER_DIR, SEEK_SET)) return 0;
		dir[0x00] = 0x00;
		dir[0x01] = dirEntry == 0 ? 0xFF : 0x00;
		dir[0x02] = r.cbm;
		dir[0x03] = fileTrackNum;
		dir[0x04] = fileSectorNum;
		memcpy(dir + 0x05, r.name, sizeof(r.name));
		for (int o = sizeof(r.name) - 1; o >= 0; o--) if (dir[0x05 + o] != T64_FILL_VALUE) break; else dir[0x05 + o] = D64_FILL_VALUE;
		memset(dir + 0x15, 0x00, 0x17 - 0x15 + 1); // REL
		memset(dir + 0x18, 0x00, 0x1D - 0x18 + 1);
		unsigned sectorSize= d64_file_sector(files[i].size);
		dir[0x1E] = (sectorSize >> 0) & 0xFF;
		dir[0x1F] = (sectorSize >> 8) & 0xFF;
		if (!FileWriteAdv(f_out, dir, sizeof(dir))) return 0;

		dirEntry = (dirEntry + 1) % D64_DIR_PER_SECTOR;

		// file sectors
		if (!FileSeek(f_in, r.offset, SEEK_SET)) return 0;
		bool writeAddress = true;
		for (unsigned s = 0; s < r.size; s+= D64_BYTE_PER_FILE_SECTOR)
		{
			if (!FileSeek(f_out, d64_offset(fileTrackNum, fileSectorNum), SEEK_SET)) return 0;
			d64_advance_pointer(fileTrackNum, fileSectorNum);
			if (fileTrackNum == D64_DIR_TRACK) fileTrackNum += 1;
			memset(sector, D64_FILL_VALUE, sizeof(sector));

			unsigned cnt = std::min(D64_BYTE_PER_FILE_SECTOR, r.size - s);
			sector[0x00] = (s + D64_BYTE_PER_FILE_SECTOR < r.size) ? fileTrackNum : 0x00;
			sector[0x01] = (s + D64_BYTE_PER_FILE_SECTOR < r.size) ? fileSectorNum : (cnt + 2 - 1);
			if (writeAddress)
			{
				sector[0x02] = (r.start >> 0) & 0xFF;
				sector[0x03] = (r.start >> 8) & 0xFF;
			}

			if (!FileReadAdv(f_in, sector + (writeAddress ? 4 : 2), cnt - (writeAddress ? 2 : 0))) return 0;
			if (!FileWriteAdv(f_out, sector, sizeof(sector))) return 0;
			writeAddress = false;
		}
	}

	// BAM
	bam[0x00] = D64_DIR_TRACK;
	bam[0x01] = D64_DIR_SECTOR;
	bam[0x02] = 0x41;
	bam[0x03] = 0x00;
	// set available
	while (true) {
		d64_advance_pointer(fileTrackNum, fileSectorNum);
		if (fileTrackNum == D64_FILE_TRACK && fileSectorNum == D64_FILE_SECTOR) break;
		if (fileTrackNum == D64_DIR_TRACK) continue;

		bam[0x04 + (fileTrackNum - 1) * D64_BYTE_PER_BAM_ENTRY] += 1;
		bam[0x04 + (fileTrackNum - 1) * D64_BYTE_PER_BAM_ENTRY + 1 + fileSectorNum / 8] |= 1 << (fileSectorNum % 8);
	};
	while (true) {
		d64_advance_dir_pointer(dirTrackNum, dirSectorNum);
		if (dirTrackNum == D64_DIR_TRACK && dirSectorNum == D64_DIR_SECTOR) break;
		if (dirTrackNum == D64_BAM_TRACK && dirSectorNum == D64_BAM_SECTOR) continue;

		bam[0x04 + (dirTrackNum - 1) * D64_BYTE_PER_BAM_ENTRY] += 1;
		bam[0x04 + (dirTrackNum - 1) * D64_BYTE_PER_BAM_ENTRY + 1 + dirSectorNum / 8] |= 1 << (dirSectorNum % 8);
	}
	memcpy(bam + 0x90, name, sizeof(name));
	for (int o = sizeof(name) - 1; o >= 0; o--) if (bam[0x90 + o] != T64_FILL_VALUE) break; else bam[0x90 + o] = D64_FILL_VALUE;
	bam[0xA0] = D64_FILL_VALUE;
	bam[0xA1] = D64_FILL_VALUE;
	bam[0xA2] = 0x30;
	bam[0xA3] = 0x30;
	bam[0xA4] = D64_FILL_VALUE;
	bam[0xA5] = 0x32;
	bam[0xA6] = 0x41;
	memset(bam + 0xA7, D64_FILL_VALUE, 0xAA - 0xA7 + 1);
	memset(bam + 0xAB, 0x00, 0xFF - 0xAB + 1);

	if (!FileSeek(f_out, d64_offset(D64_BAM_TRACK, D64_BAM_SECTOR), SEEK_SET)) return 0;
	if (!FileWriteAdv(f_out, bam, sizeof(bam))) return 0;

	f_out->size = FileGetSize(f_out);
	printf("Virtual D64 size = %llu\n", f_out->size);
	return 1;
}

int c64_openT64(const char *path, fileTYPE* f)
{
	if (!FileOpenEx(f, "vdsk", -1))
	{
		printf("ERROR: fail to create vdsk\n");
		return 0;
	}

	fileTYPE f_in;
	if (!FileOpen(&f_in, path))
	{
		FileClose(f);
		return 0;
	}

	int ret = c64_convert_t64_to_d64(&f_in, f);
	FileClose(&f_in);

	if (!ret)
	{
		printf("Failed to convert T64 (%s).\n", path);
		FileClose(f);
	}

	return ret;
}

// -----------------------------------------------------------------------------------------

struct img_info
{
	fileTYPE *f;
	int type;
	uint8_t id[2];
	int32_t trk_sz;
	uint32_t trk_map[84];
};

static img_info gcr_info[16] = {};

int c64_openGCR(const char *path, fileTYPE *f, int idx)
{
	gcr_info[idx].f = f;
	if (!strcasecmp(path + strlen(path) - 4, ".g64"))
	{
		char str[16];
		FileReadAdv(f, str, 12);
		if (memcmp(str, "GCR-1541", 8))
		{
			printf("Not a G64 format!\n");
			return 0;
		}
		else
		{
			gcr_info[idx].type = 2;
			memset(gcr_info[idx].trk_map, 0, sizeof(gcr_info[idx].trk_map));
			FileReadAdv(f, gcr_info[idx].trk_map, 84 * 4);
		}
	}
	else
	{
		gcr_info[idx].type = 1;
		FileSeek(f, 0x165a2, SEEK_SET);
		gcr_info[idx].id[0] = 0;
		gcr_info[idx].id[1] = 0;
		FileReadAdv(f, gcr_info[idx].id, 2);
		printf("D64 disk id1=%02X, id2=%02X\n", gcr_info[idx].id[0], gcr_info[idx].id[1]);
	}

	return 1;
}

void c64_closeGCR(int idx)
{
	gcr_info[idx].type = 0;
}

static uint8_t trk_buf[8192];
static uint8_t gcr_buf[8192*2];

static int start_sectors[41] = {
	  0,  21,  42,  63,  84, 105, 126, 147, 168, 189, 210, 231, 252, 273, 294, 315, 336, 357, 376, 395,
	414, 433, 452, 471, 490, 508, 526, 544, 562, 580, 598, 615, 632, 649, 666, 683, 700, 717, 734, 751,
	768
};

static const uint8_t gcr_lut[16] = {
	0x0a, 0x0b, 0x12, 0x13,
	0x0e, 0x0f, 0x16, 0x17,
	0x09, 0x19, 0x1a, 0x1b,
	0x0d, 0x1d, 0x1e, 0x15
};

static const uint8_t bin_lut[32] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 8, 0, 1, 0, 12, 4, 5,
	0, 0, 2, 3, 0, 15, 6, 7,
	0, 9, 10, 11, 0, 13, 14, 0
};

static uint8_t *gcrptr;
static int gcrcnt = 0;

void bin2gcr(uint8_t bin)
{
	static uint64_t gcr = 0;

	gcr <<= 5;
	gcr |= gcr_lut[(bin >> 4) & 0xF];
	gcr <<= 5;
	gcr |= gcr_lut[bin & 0xF];

	gcrcnt++;
	if (gcrcnt == 4)
	{
		gcrcnt = 0;
		*gcrptr++ = (uint8_t)(gcr >> 32);
		*gcrptr++ = (uint8_t)(gcr >> 24);
		*gcrptr++ = (uint8_t)(gcr >> 16);
		*gcrptr++ = (uint8_t)(gcr >> 8);
		*gcrptr++ = (uint8_t)(gcr);
	}
}

void gcr2bin(uint8_t *gcr, uint8_t *bin)
{
	// from VICE
	register uint32_t tmp = *gcr;
	tmp <<= 13;

	for (int i = 5; i < 13; i += 2, bin++)
	{
		gcr++;
		tmp |= ((uint32_t)(*gcr)) << i;
		*bin = bin_lut[(tmp >> 16) & 0x1f] << 4;
		tmp <<= 5;
		*bin |= bin_lut[(tmp >> 16) & 0x1f];
		tmp <<= 5;
	}
}

void c64_readGCR(int idx, uint8_t track)
{
	if (!gcr_info[idx].type) return;

	if (gcr_info[idx].type == 2)
	{
		if (!gcr_info[idx].trk_map[track])
		{
			gcr_info[idx].trk_sz = 4096;
			memset(gcr_buf, 0, gcr_info[idx].trk_sz);
			dbgprintf("Track %d%s: no data\n", track >> 1, (track & 1) ? ".5" : "");
		}
		else
		{
			FileSeek(gcr_info[idx].f, gcr_info[idx].trk_map[track], SEEK_SET);
			FileReadAdv(gcr_info[idx].f, gcr_buf, 8192);
			gcr_info[idx].trk_sz = (gcr_buf[1] << 8) | gcr_buf[0];
			dbgprintf("Track %d%s: size %d\n", (track >> 1) + 1, (track & 1) ? ".5" : "", gcr_info[idx].trk_sz);
			gcr_info[idx].trk_sz += 2;
		}
	}
	else if (track & 1)
	{
		track >>= 1;
		gcr_info[idx].trk_sz = (start_sectors[track + 1] - start_sectors[track]) * 256;
		memset(gcr_buf, 0, gcr_info[idx].trk_sz);

		track++;
		dbgprintf("\nBetween tracks %d <|> %d.\n", track, track+1);
	}
	else
	{
		track >>= 1;

		int size = (start_sectors[track + 1] - start_sectors[track]) * 256;
		FileSeek(gcr_info[idx].f, start_sectors[track] * 256, SEEK_SET);
		FileReadAdv(gcr_info[idx].f, trk_buf, size);

		track++;
		uint8_t sec = 0;
		gcrptr = gcr_buf + 2;
		for (int ptr = 0; ptr < size; ptr += 256)
		{
			gcrcnt = 0;
			*gcrptr++ = 0xFF; *gcrptr++ = 0xFF; *gcrptr++ = 0xFF; *gcrptr++ = 0xFF; *gcrptr++ = 0xFF;
			bin2gcr(0x08);
			bin2gcr(sec ^ track ^ gcr_info[idx].id[0] ^ gcr_info[idx].id[1]);
			bin2gcr(sec);
			bin2gcr(track);
			bin2gcr(gcr_info[idx].id[1]);
			bin2gcr(gcr_info[idx].id[0]);
			bin2gcr(0x0F);
			bin2gcr(0x0F);
			*gcrptr++ = 0x55; *gcrptr++ = 0x55; *gcrptr++ = 0x55; *gcrptr++ = 0x55; *gcrptr++ = 0x55;
			*gcrptr++ = 0x55; *gcrptr++ = 0x55; *gcrptr++ = 0x55; *gcrptr++ = 0x55;

			uint8_t cs = 0;
			uint8_t bt;

			*gcrptr++ = 0xFF; *gcrptr++ = 0xFF; *gcrptr++ = 0xFF; *gcrptr++ = 0xFF; *gcrptr++ = 0xFF;
			bin2gcr(0x07);
			for (int i = 0; i < 256; i++)
			{
				bt = trk_buf[ptr + i];
				cs ^= bt;
				bin2gcr(bt);
			}
			bin2gcr(cs);
			bin2gcr(0);
			bin2gcr(0);

			int gap = (track < 18) ? 8 : (track < 25) ? 17 : (track < 31) ? 12 : 9;
			while (gap--) *gcrptr++ = 0x55;
			sec++;
		}

		gcr_info[idx].trk_sz = gcrptr - gcr_buf;
		dbgprintf("Read GCR track %d: bin_size = %d, gcr_size  = %d\n", track, size, gcr_info[idx].trk_sz);
	}

	uint16_t sz = gcr_info[idx].trk_sz - 1;
	gcr_buf[0] = (uint8_t)sz;
	gcr_buf[1] = (uint8_t)(sz >> 8);

	EnableIO();
	spi_w(UIO_SECTOR_RD | (idx << 8));
	spi_block_write(gcr_buf, user_io_get_width(), gcr_info[idx].trk_sz);
	DisableIO();
}

static uint8_t* align(uint8_t* src, int size)
{
	static uint8_t buf[512];
	memcpy(buf, src, size);

	int rol = 0;
	while (buf[0] & 0x80)
	{
		rol++;
		uint8_t c = 0, t;
		for (int i = size - 1; i >= 0; i--)
		{
			t = buf[i] & 0x80;
			buf[i] = (buf[i] << 1) | c;
			c = t ? 1 : 0;
		}
	}

	if (rol)
	{
		dbgprintf("** ROL = %d ** ", rol);
	}
	return buf;
}

void c64_writeGCR(int idx, uint8_t track)
{
	if (!gcr_info[idx].type) return;

	static uint8_t sec_buf[260];

	EnableIO();
	spi_w(UIO_SECTOR_WR | (idx << 8));
	spi_block_read(gcr_buf, user_io_get_width(), 8192);
	DisableIO();

	if (gcr_info[idx].type == 2)
	{
		if (gcr_info[idx].trk_map[track])
		{
			FileSeek(gcr_info[idx].f, gcr_info[idx].trk_map[track]+2, SEEK_SET);
			FileWriteAdv(gcr_info[idx].f, gcr_buf + 2, gcr_info[idx].trk_sz - 2);
			dbgprintf("Write Track %d%s: size %d\n", (track >> 1) + 1, (track & 1) ? ".5" : "", gcr_info[idx].trk_sz - 2);
		}
		return;
	}

	if (track & 1)
	{
		dbgprintf("Discard data between tracks!\n");
		return;
	}

	track >>= 1;

	dbgprintf("\n\nTrack = %d\n", track + 1);
	//hexdump(gcr_buf, 8192);

	int sec_cnt = start_sectors[track + 1] - start_sectors[track];

	int sync = 0;
	uint8_t prev = 0, started = 0;
	int off = 0, ptr = 2;
	uint8_t sec = 0xFF;

	memcpy(gcr_buf + gcr_info[idx].trk_sz, gcr_buf + 2, gcr_info[idx].trk_sz - 2);
	memset(trk_buf, 0, sizeof(trk_buf));

	while(ptr < gcr_info[idx].trk_sz)
	{
		if (prev == 0xFF && gcr_buf[ptr + off] == 0xFF)
		{
			sync = 1;
		}

		if (gcr_buf[ptr + off] != 0xFF && sync)
		{
			uint8_t *hdr = align(gcr_buf + ptr + off, 11);

			uint32_t bin;
			gcr2bin(hdr, (uint8_t*)&bin);
			if (!started && (bin & 0xFF) == 8)
			{
				off = ptr - 2;
				ptr = 2;
				started = 1;
				dbgprintf("Start at %d\n\n", off);
			}

			dbgprintf("Sync = %08X: ", bin);

			if ((bin & 0xFF) == 8)
			{
				sec = (uint8_t)(bin >> 16);
				gcr2bin(hdr + 5, (uint8_t*)&bin);
				gcr_info[idx].id[1] = (uint8_t)(bin);
				gcr_info[idx].id[0] = (uint8_t)(bin >> 8);

				dbgprintf("sec = %d, id1 = %02X, id2 = %02X\n", sec, gcr_info[idx].id[0], gcr_info[idx].id[1]);
			}
			else if ((bin & 0xFF) == 7)
			{
				if (sec < sec_cnt)
				{
					dbgprintf("data...\n\n");
					uint8_t *data = align(gcr_buf + ptr + off, 330);

					int dst = 0;
					int src = 0;
					for (; dst < 260; src += 5, dst += 4)
					{
						gcr2bin(data + src, sec_buf + dst);
					}

					memcpy(trk_buf + (sec * 256), sec_buf + 1, 256);
					/*
					printf("Sec data:\n");
					hexdump(trk_buf + (sec * 256), 256);
					printf("\n");
					*/
				}
				else
				{
					dbgprintf("nothing here.\n\n");
					sec = 0xFF;
				}

				ptr += 256; // a little before the end
			}
			else
			{
				/*
				printf("\noff %X\n", ptr + off - 20);
				hexdump(gcr_buf + ptr + off - 20, 256);
				*/
			}
			sync = 0;
		}
		else
		{
			prev = gcr_buf[ptr + off];
			ptr++;
		}
	}

	FileSeek(gcr_info[idx].f, start_sectors[track] * 256, SEEK_SET);
	FileWriteAdv(gcr_info[idx].f, trk_buf, sec_cnt * 256);
}
