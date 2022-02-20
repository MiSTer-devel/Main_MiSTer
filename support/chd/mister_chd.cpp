#include <stdarg.h>
#include <libchdr/chd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include "../../file_io.h"
#include "../../cd.h"
#include "mister_chd.h"

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

			cd_toc->tracks[cd_toc->last].index1 = pregap;
		}
		else {
			if (pregap_valid)
			{
				cd_toc->tracks[cd_toc->last].start = pregap;
			}
			else {
				cd_toc->tracks[cd_toc->last].start = 0;
			}
			cd_toc->tracks[cd_toc->last].index1 = pregap;
		}

		if (!pregap_valid)
		{
			//Pregap sectors are NOT included in the CHD for this track
			pregap = 0;
		}

		if (!strcmp(track_type, "MODE1_RAW"))
		{
			cd_toc->tracks[cd_toc->last].sector_size = 2352;
			cd_toc->tracks[cd_toc->last].type = 1;
		}
		else if (!strcmp(track_type, "MODE2_RAW")) {
			cd_toc->tracks[cd_toc->last].sector_size = 2352;
			cd_toc->tracks[cd_toc->last].type = 2;
		}
		else if (!strcmp(track_type, "MODE1")) {
			cd_toc->tracks[cd_toc->last].sector_size = 2048;
			cd_toc->tracks[cd_toc->last].type = 1;
		}
		else if (!strcmp(track_type, "MODE2")) {
			cd_toc->tracks[cd_toc->last].sector_size = 2336;
			cd_toc->tracks[cd_toc->last].type = 2;
		}
		else if (!strcmp(track_type, "AUDIO")) {
			cd_toc->tracks[cd_toc->last].sector_size = 2352;
			cd_toc->tracks[cd_toc->last].type = 0;
		}
		else {
			cd_toc->tracks[cd_toc->last].sector_size = 0;
			cd_toc->tracks[cd_toc->last].type = 0;
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
