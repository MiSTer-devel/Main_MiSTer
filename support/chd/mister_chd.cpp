#include <stdarg.h>
#include <libchdr/chd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <pthread.h>
#include "../../file_io.h"
#include "../../offload.h"
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


// Decompress-ahead. mister_chd_read_sector() caches one hunk, so sequential
// playback misses on the first sector of every hunk (every 8th with the usual
// geometry) and pays a full chd_read() on the main thread. This decompresses
// hunk N+1 on the offload worker while the core consumes hunk N.
//
// chd_file is not thread-safe: only the main thread schedules, and it always
// waits for an in-flight prefetch before its own chd_read(), so the two never
// overlap. Any failure falls back to the synchronous path.
struct chd_prefetch
{
	chd_file *chd_f;
	uint8_t *buf;
	uint32_t hunkbytes;
	uint32_t hunkcount;
	int num;
	int inflight;
	pthread_mutex_t lock;
	pthread_cond_t idle;
};

chd_prefetch *mister_chd_prefetch_create(chd_file *chd_f, uint32_t hunkbytes)
{
	if (!chd_f || !hunkbytes) return NULL;

	chd_prefetch *pf = (chd_prefetch *)calloc(1, sizeof(chd_prefetch));
	if (!pf) return NULL;

	pf->buf = (uint8_t *)malloc(hunkbytes);
	if (!pf->buf)
	{
		free(pf);
		return NULL;
	}

	const chd_header *hdr = chd_get_header(chd_f);
	pf->chd_f = chd_f;
	pf->hunkbytes = hunkbytes;
	pf->hunkcount = hdr ? hdr->totalhunks : 0;
	pf->num = -1;
	pf->inflight = -1;
	pthread_mutex_init(&pf->lock, NULL);
	pthread_cond_init(&pf->idle, NULL);
	return pf;
}

// Must be called before chd_close(): the worker reads through chd_f.
void mister_chd_prefetch_destroy(chd_prefetch **pfp)
{
	if (!pfp || !*pfp) return;
	chd_prefetch *pf = *pfp;

	pthread_mutex_lock(&pf->lock);
	while (pf->inflight >= 0) pthread_cond_wait(&pf->idle, &pf->lock);
	pthread_mutex_unlock(&pf->lock);

	pthread_mutex_destroy(&pf->lock);
	pthread_cond_destroy(&pf->idle);
	free(pf->buf);
	free(pf);
	*pfp = NULL;
}

static void chd_prefetch_schedule(chd_prefetch *pf, int num)
{
	if (num < 0 || (pf->hunkcount && (uint32_t)num >= pf->hunkcount)) return;

	pthread_mutex_lock(&pf->lock);
	if (pf->inflight >= 0 || pf->num == num)
	{
		pthread_mutex_unlock(&pf->lock);
		return;
	}
	pf->inflight = num;
	pthread_mutex_unlock(&pf->lock);

	// Non-blocking: offload_add_work() would wait on a full queue and stall the
	// loop this is meant to keep moving.
	if (!offload_try_add_work([pf, num]()
		{
			chd_error err = chd_read(pf->chd_f, num, pf->buf);

			pthread_mutex_lock(&pf->lock);
			pf->num = (err == CHDERR_NONE) ? num : -1;
			pf->inflight = -1;
			pthread_cond_broadcast(&pf->idle);
			pthread_mutex_unlock(&pf->lock);
		}))
	{
		pthread_mutex_lock(&pf->lock);
		pf->inflight = -1;
		pthread_cond_broadcast(&pf->idle);
		pthread_mutex_unlock(&pf->lock);
	}
}

// Leaves the context idle, so the caller may safely enter chd_read() after.
static bool chd_prefetch_take(chd_prefetch *pf, int wanted, uint8_t *hunkbuf)
{
	bool hit = false;

	pthread_mutex_lock(&pf->lock);
	while (pf->inflight >= 0) pthread_cond_wait(&pf->idle, &pf->lock);

	if (pf->num == wanted)
	{
		memcpy(hunkbuf, pf->buf, pf->hunkbytes);
		pf->num = -1;
		hit = true;
	}
	pthread_mutex_unlock(&pf->lock);

	return hit;
}

chd_error mister_chd_read_sector(chd_file *chd_f, int lba, uint32_t d_offset, uint32_t s_offset, int length, uint8_t *destbuf, uint8_t *hunkbuf, int *hunknum, chd_prefetch *pf)
{

	int tmphnum = 0;
	int hunkofs = 0;

	lba_to_hunkinfo(chd_f, lba, &tmphnum, &hunkofs);


	//mister_chd_log("READ LBA: %d, dest_offset: %d sector offset: %d length %d chd_f %p\n", lba, d_offset, s_offset, length, chd_f);
	if (tmphnum != *hunknum)
	{
		if (pf && pf->chd_f != chd_f) pf = NULL;
		if (!pf || !chd_prefetch_take(pf, tmphnum, hunkbuf))
		{
			chd_error err = chd_read(chd_f, tmphnum, hunkbuf);
			if (err != CHDERR_NONE)
			{
				mister_chd_log("ERROR %s\n", chd_error_string(err));
				return err;
			}
		}
		*hunknum = tmphnum;

		if (pf) chd_prefetch_schedule(pf, tmphnum + 1);
	}
	int sector_offset = hunkofs * CD_FRAME_SIZE;
	memcpy(destbuf + d_offset, hunkbuf + sector_offset + s_offset, length);
	return CHDERR_NONE;
}
