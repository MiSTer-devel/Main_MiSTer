#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <algorithm>
#include <vector>

#include "../../file_io.h"
#include "../../hardware.h"

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
