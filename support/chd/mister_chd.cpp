#include <stdarg.h>
#include <libchdr/chd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <limits.h>
#include <errno.h>
#include <new>
#include <string>
#include "../../lib/md5/md5.h"
#include "../../file_io.h"
#include "../../cd.h"
#include "mister_chd.h"
#include "chd_hdd_overlay.h"

void lba_to_hunkinfo(chd_file *chd_f, int lba, int *hunknumber, int *hunkoffset)
{
	const chd_header *chd_header = chd_get_header(chd_f);
	int sectors_per_hunk = chd_header->hunkbytes / chd_header->unitbytes;
	*hunknumber = lba / sectors_per_hunk;
	*hunkoffset = lba % sectors_per_hunk;
	return;
}

int mister_chd_log(const char *format, ...)
{
	char logline[1024];
	va_list args;
	va_start(args, format);
	vsprintf(logline, format, args);
	va_end(args);
	return printf("\x1b[32m%s\x1b[0m", logline);
}

chd_error mister_load_chd(const char *filename, toc_t *cd_toc)
{
	cd_toc->last = -1;
	
	chd_error err = chd_open(getFullPath(filename), CHD_OPEN_READ, NULL, &cd_toc->chd_f);
	if (err != CHDERR_NONE)
	{
		cd_toc->chd_f = NULL;
		return err;
	}

	//TODO: deal with non v5 chd versions
	const chd_header *chd_header = chd_get_header(cd_toc->chd_f);
	if (!chd_header)
	{
		chd_close(cd_toc->chd_f);
		return CHDERR_NO_INTERFACE; //I'm not sure this error condition is possible, so just use whatever
	}

	mister_chd_log("hunkbytes %d unitbytes %d logical length %llu\n", chd_header->hunkbytes, chd_header->unitbytes, chd_header->logicalbytes);
	cd_toc->chd_hunksize = chd_header->hunkbytes;

	//Set CLOEXEC on underlying FD
	int chd_fd = fileno((FILE *)chd_core_file(cd_toc->chd_f)->argp);
	if (chd_fd) fcntl(chd_fd, F_SETFD, FD_CLOEXEC);

	//Load track info
	int sector_cnt = 0;
	for (cd_toc->last = 0; cd_toc->last < 99; cd_toc->last++)
	{
		char tmp[512];
		int track_id = 0, frames = 0, pregap = 0, postgap = 0;
		char track_type[64], subtype[32], pgtype[32], pgsub[32];

		if (chd_get_metadata(cd_toc->chd_f, CDROM_TRACK_METADATA2_TAG, cd_toc->last, tmp, sizeof(tmp), NULL, NULL, NULL) == CHDERR_NONE)
		{
			if (sscanf(tmp, CDROM_TRACK_METADATA2_FORMAT, &track_id, track_type, subtype, &frames, &pregap, pgtype, pgsub, &postgap) != 8) break;
		}
		else if (chd_get_metadata(cd_toc->chd_f, CDROM_TRACK_METADATA_TAG, cd_toc->last, tmp, sizeof(tmp), NULL, NULL, NULL) == CHDERR_NONE) {
			if (sscanf(tmp, CDROM_TRACK_METADATA_FORMAT, &track_id, track_type, subtype, &frames) != 4) break;
		}
		else {
			//No more tracks
			break;
		}

		bool pregap_valid = true;

		if (pgtype[0] != 'V')
		{
			pregap_valid = false;

		}

		if (cd_toc->last)
		{
			if (!pregap_valid)
			{
				cd_toc->tracks[cd_toc->last - 1].end += pregap;
			}
			cd_toc->end = cd_toc->tracks[cd_toc->last - 1].end;
			cd_toc->tracks[cd_toc->last].start = cd_toc->end;
			if (pregap_valid)
			{
				cd_toc->tracks[cd_toc->last].start += pregap;
			}

			cd_toc->tracks[cd_toc->last].indexes[1] = pregap;
		}
		else {
			if (pregap_valid)
			{
				cd_toc->tracks[cd_toc->last].start = pregap;
			}
			else {
				cd_toc->tracks[cd_toc->last].start = 0;
			}
			cd_toc->tracks[cd_toc->last].indexes[1] = pregap;
		}
		cd_toc->tracks[cd_toc->last].index_num = 2;

		if (!pregap_valid)
		{
			//Pregap sectors are NOT included in the CHD for this track
			pregap = 0;
		}

		if (!strcmp(track_type, "MODE1_RAW"))
		{
			cd_toc->tracks[cd_toc->last].sector_size = 2352;
			cd_toc->tracks[cd_toc->last].type = TT_MODE1;
		}
		else if (!strcmp(track_type, "MODE2_RAW")) {
			cd_toc->tracks[cd_toc->last].sector_size = 2352;
			cd_toc->tracks[cd_toc->last].type = TT_MODE2;
		}
		else if (!strcmp(track_type, "MODE1")) {
			cd_toc->tracks[cd_toc->last].sector_size = 2048;
			cd_toc->tracks[cd_toc->last].type = TT_MODE1;
		}
		else if (!strcmp(track_type, "MODE2")) {
			cd_toc->tracks[cd_toc->last].sector_size = 2336;
			cd_toc->tracks[cd_toc->last].type = TT_MODE2;
		}
		else if (!strcmp(track_type, "AUDIO")) {
			cd_toc->tracks[cd_toc->last].sector_size = 2352;
			cd_toc->tracks[cd_toc->last].type = TT_CDDA;
		}
		else {
			cd_toc->tracks[cd_toc->last].sector_size = 0;
			cd_toc->tracks[cd_toc->last].type = TT_CDDA;
		}

		cd_toc->tracks[cd_toc->last].sbc_type = SUBCODE_NONE;
		if (!strcmp(subtype, "RW")) {
			cd_toc->tracks[cd_toc->last].sbc_type = SUBCODE_RW;
		}
		else if (!strcmp(subtype, "RW_RAW")) {
			cd_toc->tracks[cd_toc->last].sbc_type = SUBCODE_RW_RAW;
		}

		//CHD pads tracks to a multiple of 4 sectors, keep track of the overall sector count and calculate the difference between the cdrom lba and the effective chd lba
		cd_toc->tracks[cd_toc->last].offset = (sector_cnt + pregap - cd_toc->tracks[cd_toc->last].start);
		cd_toc->tracks[cd_toc->last].end = cd_toc->tracks[cd_toc->last].start + frames - pregap;
		cd_toc->end = cd_toc->tracks[cd_toc->last].end + postgap;
		sector_cnt += ((frames + CD_TRACK_PADDING - 1) / CD_TRACK_PADDING) * CD_TRACK_PADDING;
		mister_chd_log("Track %d: Type: %s PreGap: %d PreGapType: %s Frames: %d start: %d end %d\n", cd_toc->last, track_type, pregap, pgtype, frames, cd_toc->tracks[cd_toc->last].start, cd_toc->tracks[cd_toc->last].end);

	}
	return CHDERR_NONE;
}

chd_error mister_chd_read_sector(chd_file *chd_f, int lba, uint32_t d_offset, uint32_t s_offset, int length, uint8_t *destbuf, uint8_t *hunkbuf, int *hunknum)
{

	int tmphnum = 0;
	int hunkofs = 0;

	lba_to_hunkinfo(chd_f, lba, &tmphnum, &hunkofs);


	//mister_chd_log("READ LBA: %d, dest_offset: %d sector offset: %d length %d chd_f %p\n", lba, d_offset, s_offset, length, chd_f);
	if (tmphnum != *hunknum)
	{
		chd_error err = chd_read(chd_f, tmphnum, hunkbuf);
		if (err != CHDERR_NONE)
		{
			mister_chd_log("ERROR %s\n", chd_error_string(err));
			return err;
		}
		*hunknum = tmphnum;
	}
	int sector_offset = hunkofs * CD_FRAME_SIZE;
	memcpy(destbuf + d_offset, hunkbuf + sector_offset + s_offset, length);
	return CHDERR_NONE;
}

struct chd_hdd_file
{
	chd_file *file;
	uint8_t *hunkbuf;
	uint32_t hunknum;
	chd_hdd_overlay *overlay;
	bool writable;
};

void mister_chd_hdd_close(chd_hdd_file *disk)
{
	if (!disk) return;
	chd_hdd_overlay_close(disk->overlay);
	if (disk->file) chd_close(disk->file);
	free(disk->hunkbuf);
	free(disk);
}

static chd_error hdd_identity(FILE *file, const chd_header *h, uint8_t identity[20], uint8_t *type)
{
	memcpy(identity, h->sha1, 20);
	uint8_t zero[20] = {};
	*type = 1;
	if (memcmp(identity, zero, 20)) return CHDERR_NONE;
	MD5Context md5;
	MD5Init(&md5);
	uint8_t buffer[32768];
	__off64_t offset = 0;
	for (;;)
	{
		ssize_t bytes = pread64(fileno(file), buffer, sizeof(buffer), offset);
		if (bytes < 0) { if (errno == EINTR) continue; return CHDERR_READ_ERROR; }
		if (!bytes) break;
		MD5Update(&md5, buffer, bytes);
		offset += bytes;
	}
	MD5Final(identity, &md5);
	*type = 2;
	return CHDERR_NONE;
}

chd_error mister_chd_hdd_open(FILE *file, const char *path, bool writable, chd_hdd_file **disk, uint64_t *size)
{
	*disk = nullptr;
	*size = 0;
	chd_hdd_file *d = (chd_hdd_file *)calloc(1, sizeof(chd_hdd_file));
	if (!d) return CHDERR_OUT_OF_MEMORY;

	chd_error err = chd_open_file(file, CHD_OPEN_READ, nullptr, &d->file);
	if (err == CHDERR_NONE)
	{
		char metadata[128] = {};
		err = chd_get_metadata(d->file, HARD_DISK_METADATA_TAG, 0, metadata,
			sizeof(metadata) - 1, nullptr, nullptr, nullptr);
		if (err == CHDERR_METADATA_NOT_FOUND && chd_get_header(d->file)->unitbytes != CD_FRAME_SIZE)
			err = CHDERR_UNSUPPORTED_FORMAT;
		if (err == CHDERR_NONE)
		{
			int cylinders, heads, sectors, bps;
			const chd_header *h = chd_get_header(d->file);
			if (sscanf(metadata, HARD_DISK_METADATA_FORMAT, &cylinders, &heads, &sectors, &bps) != 4 ||
				cylinders <= 0 || heads <= 0 || sectors <= 0 || bps != 512 || h->unitbytes != 512 ||
				!h->hunkbytes || h->hunkbytes % 512 || !h->logicalbytes ||
				h->logicalbytes % 512 || h->logicalbytes > INT64_MAX ||
				(h->logicalbytes - 1) / h->hunkbytes >= h->totalhunks)
			{
				err = CHDERR_UNSUPPORTED_FORMAT;
			}
			else
			{
				d->hunkbuf = (uint8_t *)malloc(h->hunkbytes);
				if (!d->hunkbuf) err = CHDERR_OUT_OF_MEMORY;
				else
				{
					uint8_t identity[20], type;
					err = hdd_identity(file, h, identity, &type);
					if (err == CHDERR_NONE)
					{
						try
						{
							const std::string source_path(path);
							char save_path[2100];
							FileGenerateSavePath(source_path.c_str(), save_path);
							std::string overlay_path(getFullPath(save_path));
							const std::string root(getFullPath(""));
							if (chd_hdd_overlay_open(overlay_path.c_str(), root.c_str(), h->logicalbytes,
								identity, type, writable, &d->overlay))
							{
								printf("HDD CHD overlay error: %s (%s).\n", overlay_path.c_str(), strerror(errno));
								err = writable ? CHDERR_WRITE_ERROR : CHDERR_READ_ERROR;
							}
						}
						catch (const std::bad_alloc &) { err = CHDERR_OUT_OF_MEMORY; }
					}
					if (err == CHDERR_NONE)
					{
						d->writable = writable;
						d->hunknum = UINT32_MAX;
						*size = h->logicalbytes;
						*disk = d;
						return CHDERR_NONE;
					}
				}
			}
		}
	}

	mister_chd_hdd_close(d);
	return err;
}

bool mister_chd_hdd_writable(chd_hdd_file *disk)
{
	return disk->writable;
}

chd_error mister_chd_hdd_read(chd_hdd_file *disk, uint64_t offset, void *buffer, uint32_t length)
{
	const chd_header *h = chd_get_header(disk->file);
	if (offset > h->logicalbytes || length > h->logicalbytes - offset)
		return CHDERR_HUNK_OUT_OF_RANGE;

	uint8_t *dest = (uint8_t *)buffer;
	while (length)
	{
		uint8_t sector[512];
		uint32_t in_sector = offset % 512;
		uint32_t bytes = 512 - in_sector;
		if (bytes > length) bytes = length;
		int overlaid = chd_hdd_overlay_read(disk->overlay, offset / 512, sector);
		if (overlaid < 0) return CHDERR_READ_ERROR;
		if (overlaid)
		{
			memcpy(dest, sector + in_sector, bytes);
			dest += bytes;
			offset += bytes;
			length -= bytes;
			continue;
		}
		uint32_t hunk = offset / h->hunkbytes;
		uint32_t within = offset % h->hunkbytes;
		if (hunk != disk->hunknum)
		{
			disk->hunknum = UINT32_MAX;
			chd_error err = chd_read(disk->file, hunk, disk->hunkbuf);
			if (err != CHDERR_NONE) return err;
			disk->hunknum = hunk;
		}
		memcpy(dest, disk->hunkbuf + within, bytes);
		dest += bytes;
		offset += bytes;
		length -= bytes;
	}
	return CHDERR_NONE;
}

chd_error mister_chd_hdd_write(chd_hdd_file *disk, uint64_t offset, const void *buffer, uint32_t length)
{
	return chd_hdd_overlay_write(disk->overlay, offset, buffer, length) ? CHDERR_WRITE_ERROR : CHDERR_NONE;
}
