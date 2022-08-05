
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "../../file_io.h"
#include "../../user_io.h"

#include "../chd/mister_chd.h"
#include "pcecd.h"

#define PCECD_DATA_IO_INDEX 2

float get_cd_seek_ms(int start_sector, int target_sector);

pcecdd_t pcecdd;

pcecdd_t::pcecdd_t() {
	latency = 0;
	audiodelay = 0;
	loaded = 0;
	index = 0;
	lba = 0;
	scanOffset = 0;
	isData = 1;
	state = PCECD_STATE_NODISC;
	audioLength = 0;
	audioOffset = 0;
	SendData = NULL;
	has_status = 0;
	data_req = false;
	can_read_next = false;
	CDDAStart = 0;
	CDDAEnd = 0;
	CDDAMode = PCECD_CDDAMODE_SILENT;
	region = 0;

	stat = 0x0000;

}

static int sgets(char *out, int sz, char **in)
{
	*out = 0;
	do
	{
		char *instr = *in;
		int cnt = 0;

		while (*instr && *instr != 10)
		{
			if (*instr == 13)
			{
				instr++;
				continue;
			}

			if (cnt < sz - 1)
			{
				out[cnt++] = *instr;
				out[cnt] = 0;
			}

			instr++;
		}

		if(*instr == 10) instr++;
		*in = instr;
	}
	while (!*out && **in);

	return *out;
}

int pcecdd_t::LoadCUE(const char* filename) {
	static char fname[1024 + 10];
	static char line[128];
	char *ptr, *lptr;
	static char toc[100 * 1024];
	int hdr = 0;

	strcpy(fname, filename);

	memset(toc, 0, sizeof(toc));
	if (!FileLoad(fname, toc, sizeof(toc) - 1)) return 1;

	printf("\x1b[32mPCECD: Open CUE: %s\n\x1b[0m", fname);

	int mm, ss, bb, pregap = 0;

	char *buf = toc;
	while (sgets(line, sizeof(line), &buf))
	{
		lptr = line;
		while (*lptr == 0x20) lptr++;

		/* decode FILE commands */
		if (!(memcmp(lptr, "FILE", 4)))
		{
			ptr = fname + strlen(fname) - 1;
			while ((ptr - fname) && (*ptr != '/') && (*ptr != '\\')) ptr--;
			if (ptr - fname) ptr++;

			lptr += 4;
			while (*lptr == 0x20) lptr++;

			if (*lptr == '\"')
			{
				lptr++;
				while ((*lptr != '\"') && (lptr <= (line + 128)) && (ptr < (fname + 1023)))
					*ptr++ = *lptr++;
			}
			else
			{
				while ((*lptr != 0x20) && (lptr <= (line + 128)) && (ptr < (fname + 1023)))
					*ptr++ = *lptr++;
			}
			*ptr = 0;

			if(!FileOpen(&this->toc.tracks[this->toc.last].f, fname)) return -1;

			printf("\x1b[32mPCECD: Open track file: %s\n\x1b[0m", fname);

			int len = strlen(fname);
			hdr = (len > 4 && !strcasecmp(fname + len - 4, ".wav")) ? 44 : 0;

			pregap = 0;

			this->toc.tracks[this->toc.last].offset = 0;

			if (!strstr(lptr, "BINARY") && !strstr(lptr, "MOTOROLA") && !strstr(lptr, "WAVE"))
			{
				FileClose(&this->toc.tracks[this->toc.last].f);
				printf("\x1b[32mPCECD: unsupported file: %s\n\x1b[0m", fname);

				return -1;
			}
		}

		/* decode TRACK commands */
		else if ((sscanf(lptr, "TRACK %02d %*s", &bb)) || (sscanf(lptr, "TRACK %d %*s", &bb)))
		{
			if (bb != (this->toc.last + 1))
			{
				FileClose(&this->toc.tracks[this->toc.last].f);
				printf("\x1b[32mPCECD: missing tracks: %s\n\x1b[0m", fname);
				break;
			}

			//if (!this->toc.last)
			{
				if (strstr(lptr, "MODE1/2048"))
				{
					this->toc.tracks[this->toc.last].sector_size = 2048;
					this->toc.tracks[this->toc.last].type = 1;
				}
				else if (strstr(lptr, "MODE1/2352"))
				{
					this->toc.tracks[this->toc.last].sector_size = 2352;
					this->toc.tracks[this->toc.last].type = 1;

					FileSeek(&this->toc.tracks[this->toc.last].f, 0x10, SEEK_SET);
				}
				else if (strstr(lptr, "AUDIO"))
				{
					this->toc.tracks[this->toc.last].sector_size = 2352;
					this->toc.tracks[this->toc.last].type = 0;

					FileSeek(&this->toc.tracks[this->toc.last].f, 0, SEEK_SET);
				}

				/*if (this->sectorSize)
				{
					this->toc.tracks[0].type = 1;

					FileReadAdv(&this->toc.tracks[0].f, header, 0x210);
					FileSeek(&this->toc.tracks[0].f, 0, SEEK_SET);
				}*/
			}

			if (this->toc.last)
			{
				if (!this->toc.tracks[this->toc.last].f.opened())
				{
					this->toc.tracks[this->toc.last - 1].end = 0;
				}
			}
		}

		/* decode PREGAP commands */
		else if (sscanf(lptr, "PREGAP %02d:%02d:%02d", &mm, &ss, &bb) == 3)
		{
			pregap += bb + ss * 75 + mm * 60 * 75;
		}

		/* decode INDEX commands */
		else if ((sscanf(lptr, "INDEX 00 %02d:%02d:%02d", &mm, &ss, &bb) == 3) ||
			(sscanf(lptr, "INDEX 0 %02d:%02d:%02d", &mm, &ss, &bb) == 3))
		{
			if (this->toc.last && !this->toc.tracks[this->toc.last - 1].end)
			{
				this->toc.tracks[this->toc.last - 1].end = bb + ss * 75 + mm * 60 * 75 + pregap;
			}
		}
		else if ((sscanf(lptr, "INDEX 01 %02d:%02d:%02d", &mm, &ss, &bb) == 3) ||
			(sscanf(lptr, "INDEX 1 %02d:%02d:%02d", &mm, &ss, &bb) == 3))
		{
			if (!this->toc.tracks[this->toc.last].f.opened())
			{
				FileOpen(&this->toc.tracks[this->toc.last].f, fname);
				this->toc.tracks[this->toc.last].start = bb + ss * 75 + mm * 60 * 75 + pregap;
				this->toc.tracks[this->toc.last].offset = (pregap * this->toc.tracks[this->toc.last].sector_size) - hdr;
				if (this->toc.last && !this->toc.tracks[this->toc.last - 1].end)
				{
					this->toc.tracks[this->toc.last - 1].end = this->toc.tracks[this->toc.last].start;
				}
			}
			else
			{
				FileSeek(&this->toc.tracks[this->toc.last].f, 0, SEEK_SET);

				this->toc.tracks[this->toc.last].start = this->toc.end + pregap;
				this->toc.tracks[this->toc.last].offset = (this->toc.tracks[this->toc.last].start * this->toc.tracks[this->toc.last].sector_size) - hdr;
				this->toc.tracks[this->toc.last].end = this->toc.tracks[this->toc.last].start + ((this->toc.tracks[this->toc.last].f.size - hdr + this->toc.tracks[this->toc.last].sector_size - 1) / this->toc.tracks[this->toc.last].sector_size);

				this->toc.tracks[this->toc.last].start += (bb + ss * 75 + mm * 60 * 75);
				this->toc.end = this->toc.tracks[this->toc.last].end;
			}

			this->toc.last++;
			if (this->toc.last == 99) break;
		}
	}

	if (this->toc.last && !this->toc.tracks[this->toc.last - 1].end)
	{
		this->toc.end += pregap;
		this->toc.tracks[this->toc.last - 1].end = this->toc.end;
	}

	for (int i = 0; i < this->toc.last; i++)
	{
		printf("\x1b[32mPCECD: Track = %u, start = %u, end = %u, offset = %d, sector_size=%d, type = %u\n\x1b[0m", i, this->toc.tracks[i].start, this->toc.tracks[i].end, this->toc.tracks[i].offset, this->toc.tracks[i].sector_size, this->toc.tracks[i].type);
	}

	FileClose(&this->toc.tracks[this->toc.last].f);
	return 0;
}

int pcecdd_t::Load(const char *filename)
{
	Unload();

	const char *ext = filename+strlen(filename)-4;
	if (!strncasecmp(".cue", ext, 4))
	{
		if (LoadCUE(filename)) return -1;
	} else if (!strncasecmp(".chd", ext, 4)) {
		mister_load_chd(filename, &this->toc);
		if (this->chd_hunkbuf)
		{
			free(this->chd_hunkbuf);
			this->chd_hunkbuf = NULL;
		}

		this->chd_hunkbuf = (uint8_t *)malloc(CD_FRAME_SIZE * CD_FRAMES_PER_HUNK);	
		this->chd_hunknum = -1;
	} else {
		return -1;
	}

	if (this->toc.last)
	{
		this->toc.tracks[this->toc.last].start = this->toc.end;
		this->loaded = 1;

		//memcpy(&fname[strlen(fname) - 4], ".sub", 4);
		//this->toc.sub = fopen(getFullPath(fname), "r");

		printf("\x1b[32mPCECD: CD mounted , last track = %u\n\x1b[0m", this->toc.last);
		return 1;
	}

	return 0;
}

void pcecdd_t::Unload()
{
	if (this->loaded)
	{
		if (this->toc.chd_f)
		{
			chd_close(this->toc.chd_f);
			this->toc.chd_f = NULL;
			if (this->chd_hunkbuf)
			{
				free(this->chd_hunkbuf);
				this->chd_hunkbuf = NULL;
				this->chd_hunknum = -1;
			}
		} else {
			for (int i = 0; i < this->toc.last; i++)
			{
				FileClose(&this->toc.tracks[i].f);
			}
		}

		//if (this->toc.sub) fclose(this->toc.sub);

		this->loaded = 0;
	}

	memset(&this->toc, 0x00, sizeof(this->toc));
}

void pcecdd_t::Reset() {
	latency = 0;
	audiodelay = 0;
	index = 0;
	lba = 0;
	scanOffset = 0;
	isData = 1;
	state = loaded ? PCECD_STATE_IDLE : PCECD_STATE_NODISC;
	audioLength = 0;
	audioOffset = 0;
	has_status = 0;
	data_req = false;
	can_read_next = false;
	CDDAStart = 0;
	CDDAEnd = 0;
	CDDAMode = PCECD_CDDAMODE_SILENT;

	stat = 0x0000;

}

void pcecdd_t::Update() {
	if (this->state == PCECD_STATE_READ)
	{
		if (this->latency > 0)
		{
			this->latency--;
			return;
		}

		if (this->index >= this->toc.last)
		{
			this->state = PCECD_STATE_IDLE;
			return;
		}

		if (!this->can_read_next)
			return;

		this->can_read_next = false;

		DISKLED_ON;
		if (this->toc.tracks[this->index].type)
		{
			// CD-ROM (Mode 1)
			sec_buf[0] = 0x00;
			sec_buf[1] = 0x08 | 0x80;
			ReadData(sec_buf + 2);

			if (SendData)
				SendData(sec_buf, 2048 + 2, PCECD_DATA_IO_INDEX);

			//printf("\x1b[32mPCECD: Data sector send = %i\n\x1b[0m", this->lba);
		}
		else
		{
			if (this->lba >= this->toc.tracks[this->index].start)
			{
				this->isData = 0x00;
			}

			//SectorSend(0);
		}

		this->cnt--;

		if (!this->cnt) {
			PendStatus(MAKE_STATUS(PCECD_STATUS_GOOD, 0));

			this->state = PCECD_STATE_IDLE;
		}
		else {

		}

		this->lba++;
		if (this->lba >= this->toc.tracks[this->index].end)
		{
			this->index++;

			this->isData = 0x01;

			if (this->toc.tracks[this->index].f.opened())
			{
				FileSeek(&this->toc.tracks[this->index].f, (this->toc.tracks[this->index].start * 2352) - this->toc.tracks[this->index].offset, SEEK_SET);
			}
		}
	}
	else if (this->state == PCECD_STATE_PLAY)
	{
		if (this->latency > 0)
		{
			this->latency--;
			return;
		}

		if (this->audiodelay > 0)
		{
			this->audiodelay--;
			return;
		}

		this->index = GetTrackByLBA(this->lba, &this->toc);

		DISKLED_ON;

		for (int i = 0; i <= this->CDDAFirst; i++)
		{
			if (!this->toc.tracks[this->index].type)
			{
				if (this->toc.tracks[this->index].f.opened()) 
				{
					FileSeek(&this->toc.tracks[index].f, (this->lba * 2352) - this->toc.tracks[index].offset, SEEK_SET);
				}
				sec_buf[0] = 0x30;
				sec_buf[1] = 0x09;
				ReadCDDA(sec_buf + 2);

				if (SendData)
					SendData(sec_buf, 2352 + 2, PCECD_DATA_IO_INDEX);

				//printf("\x1b[32mPCECD: Audio sector send = %i, track = %i, offset = %i\n\x1b[0m", this->lba, this->index, (this->lba * 2352) - this->toc.tracks[index].offset);
			}
			this->lba++;
		}

		this->CDDAFirst = 0;

		if ((this->lba > this->CDDAEnd) || this->toc.tracks[this->index].type || this->index > this->toc.last)
		{
			if (this->CDDAMode == PCECD_CDDAMODE_LOOP) {
				this->lba = this->CDDAStart;
			}
			else {
				this->state = PCECD_STATE_IDLE;
			}

			if (this->CDDAMode == PCECD_CDDAMODE_INTERRUPT) {
				SendStatus(MAKE_STATUS(PCECD_STATUS_GOOD, 0));
			}

			printf("\x1b[32mPCECD: playback reached the end %d\n\x1b[0m", this->lba);
		}
	}
	else if (this->state == PCECD_STATE_PAUSE)
	{
		if (this->latency > 0)
		{
			this->latency--;
			return;
		}
	}
}

void pcecdd_t::CommandExec() {
	msf_t msf;
	int new_lba = 0;
	static uint8_t buf[32];
	uint32_t temp_latency;

	memset(buf, 0, 32);

	switch (comm[0]) {
	case PCECD_COMM_TESTUNIT:
		if (state == PCECD_STATE_NODISC) {
			CommandError(SENSEKEY_NOT_READY, NSE_NO_DISC, 0, 0);
			SendStatus(MAKE_STATUS(PCECD_STATUS_CHECK_COND, 0));
		}
		else {
			SendStatus(MAKE_STATUS(PCECD_STATUS_GOOD, 0));
		}

		// printf("\x1b[32mPCECD: Command TESTUNIT, state = %u\n\x1b[0m", state);
		break;

	case PCECD_COMM_REQUESTSENSE:
		buf[0] = 18;
		buf[1] = 0 | 0x80;

		buf[2] = 0x70;
		buf[4] = sense.key;
		buf[9] = 0x0A;
		buf[14] = sense.asc;
		buf[15] = sense.ascq;
		buf[16] = sense.fru;

		sense.key = sense.asc = sense.ascq = sense.fru = 0;

		if (SendData)
			SendData(buf, 18 + 2, PCECD_DATA_IO_INDEX);

		printf("\x1b[32mPCECD: Command REQUESTSENSE, key = %02X, asc = %02X, ascq = %02X, fru = %02X\n\x1b[0m", sense.key, sense.asc, sense.ascq, sense.fru);

		SendStatus(MAKE_STATUS(PCECD_STATUS_GOOD, 0));

		break;

	case PCECD_COMM_GETDIRINFO: {
		int len = 0;
		switch (comm[1]) {
		case 0:
		default:
			buf[0] = 4;
			buf[1] = 0 | 0x80;
			buf[2] = 1;
			buf[3] = BCD(this->toc.last);
			buf[4] = 0;
			buf[5] = 0;
			len = 4 + 2;
			break;

		case 1:
			new_lba = this->toc.end + 150;
			LBAToMSF(new_lba, &msf);

			buf[0] = 4;
			buf[1] = 0 | 0x80;
			buf[2] = BCD(msf.m);
			buf[3] = BCD(msf.s);
			buf[4] = BCD(msf.f);
			buf[5] = 0;
			len = 4 + 2;
			break;

		case 2:
			int track = U8(comm[2]);
			new_lba = this->toc.tracks[track - 1].start + 150;
			LBAToMSF(new_lba, &msf);

			buf[0] = 4;
			buf[1] = 0 | 0x80;
			buf[2] = BCD(msf.m);
			buf[3] = BCD(msf.s);
			buf[4] = BCD(msf.f);
			buf[5] = this->toc.tracks[track - 1].type << 2;
			len = 4 + 2;
			break;
		}

		if (SendData && len)
			SendData(buf, len, PCECD_DATA_IO_INDEX);

		printf("\x1b[32mPCECD: Command GETDIRINFO, [1] = %02X, [2] = %02X(%d)\n\x1b[0m", comm[1], comm[2], comm[2]);

		printf("\x1b[32mPCECD: Send data, len = %u, [2] = %02X, [3] = %02X, [4] = %02X, [5] = %02X\n\x1b[0m", len, buf[2], buf[3], buf[4], buf[5]);

		SendStatus(MAKE_STATUS(PCECD_STATUS_GOOD, 0));
	}
		break;

	case PCECD_COMM_READ6: {
		new_lba = ((comm[1] << 16) | (comm[2] << 8) | comm[3]) & 0x1FFFFF;
		int cnt_ = comm[4] ? comm[4] : 256;

		int index = GetTrackByLBA(new_lba, &this->toc);

		this->index = index;

		/* HuVideo streams by fetching 120 sectors at a time, taking advantage of the geometry
		 * of the disc to reduce/eliminate seek time */
		if ((this->lba == new_lba) && (cnt_ == 120))
		{
			this->latency = 0;
		}
		/* Sherlock Holmes streams by fetching 252 sectors at a time, and suffers
		 * from slight pauses at each seek */
		else if ((this->lba == new_lba) && (cnt_ == 252))
		{
			this->latency = 5;
		}
		else if (comm[13] & 0x80) // fast seek (OSD setting)
		{
			this->latency = 0;
		}
		else
		{
			this->latency = (int)(get_cd_seek_ms(this->lba, new_lba)/13.33);
			this->audiodelay = 0;
		}
		printf("seek time ticks: %d\n", this->latency);

		this->lba = new_lba;
		this->cnt = cnt_;

		if (this->toc.tracks[index].f.opened())
		{
			int offset = (new_lba * this->toc.tracks[index].sector_size) - this->toc.tracks[index].offset;
			FileSeek(&this->toc.tracks[index].f, offset, SEEK_SET);
		}

		this->audioOffset = 0;

		this->can_read_next = true;
		this->state = PCECD_STATE_READ;

		printf("\x1b[32mPCECD: Command READ6, lba = %u, cnt = %u\n\x1b[0m", this->lba, this->cnt);
	}
		break;

	case PCECD_COMM_MODESELECT6:
		printf("\x1b[32mPCECD: Command MODESELECT6, cnt = %u\n\x1b[0m", comm[4]);

		if (comm[4]) {
			data_req = true;
		}
		else {
			SendStatus(MAKE_STATUS(PCECD_STATUS_GOOD, 0));
		}

		break;

	case PCECD_COMM_SAPSP: {
		switch (comm[9] & 0xc0)
		{
		default:
		case 0x00:
			new_lba = (comm[3] << 16) | (comm[4] << 8) | comm[5];
			break;

		case 0x40:
			MSFToLBA(&new_lba, U8(comm[2]), U8(comm[3]), U8(comm[4]));
			break;

		case 0x80:
		{
			int track = U8(comm[2]);

			if (!track)
				track = 1;
			else if (track > toc.last)
				track = toc.last;
			new_lba = this->toc.tracks[track - 1].start;
		}
		break;
		}

		if (comm[13] & 0x80) // fast seek (OSD setting)
		{
			this->latency = 0;
			this->audiodelay = 0;
		}
		else
		{
			temp_latency = (int)(get_cd_seek_ms(this->lba, new_lba) / 13.33);
			this->audiodelay = (int)(220 / 13.33);

			if (temp_latency > this->audiodelay)
				this->latency = temp_latency - this->audiodelay;
			else {
				this->latency = temp_latency;
				this->audiodelay = 0;
			}
		}

		printf("seek time ticks: %d\n", this->latency);

		this->lba = new_lba;
		int index = GetTrackByLBA(new_lba, &this->toc);

		this->index = index;

		this->CDDAStart = new_lba;
		this->CDDAEnd = this->toc.end;
		this->CDDAMode = comm[1];
		this->CDDAFirst = 1;

		if (this->CDDAMode == PCECD_CDDAMODE_SILENT) {
			this->state = PCECD_STATE_PAUSE;
		}
		else {
			this->state = PCECD_STATE_PLAY;
		}

		PendStatus(MAKE_STATUS(PCECD_STATUS_GOOD, 0));
	}
		printf("\x1b[32mPCECD: Command SAPSP, start = %d, end = %d, [1] = %02X, [2] = %02X, [9] = %02X\n\x1b[0m", this->CDDAStart, this->CDDAEnd, comm[1], comm[2], comm[9]);
		break;

	case PCECD_COMM_SAPEP: {
		switch (comm[9] & 0xc0)
		{
		default:
		case 0x00:
			new_lba = (comm[3] << 16) | (comm[4] << 8) | comm[5];
			break;

		case 0x40:
			MSFToLBA(&new_lba, U8(comm[2]), U8(comm[3]), U8(comm[4]));
			break;

		case 0x80:
		{
			int track = U8(comm[2]);

			// Note that track (imput from PCE) starts numbering at 1
			// but toc.tracks starts numbering at 0
			//
			if (!track)	track = 1;
			new_lba = ((track-1) >= toc.last) ? this->toc.end : (this->toc.tracks[track - 1].start);
		}
		break;
		}

		this->CDDAMode = comm[1];
		this->CDDAEnd = new_lba;

		if (this->CDDAMode == PCECD_CDDAMODE_SILENT) {
			this->state = PCECD_STATE_IDLE;
		}
		else {
			this->state = PCECD_STATE_PLAY;
		}

		if (this->CDDAMode != PCECD_CDDAMODE_INTERRUPT) {
			SendStatus(MAKE_STATUS(PCECD_STATUS_GOOD, 0));
		}

		printf("\x1b[32mPCECD: Command SAPEP, end = %i, [1] = %02X, [2] = %02X, [9] = %02X\n\x1b[0m", this->CDDAEnd, comm[1], comm[2], comm[9]);
	}
		break;

	case PCECD_COMM_PAUSE: {
		this->state = PCECD_STATE_PAUSE;

		SendStatus(MAKE_STATUS(PCECD_STATUS_GOOD, 0));
	}
		printf("\x1b[32mPCECD: Command PAUSE, current lba = %i\n\x1b[0m", this->lba);
		break;

	case PCECD_COMM_READSUBQ: {
		int lba_rel = this->lba - this->toc.tracks[this->index].start;

		buf[0] = 0x0A;
		buf[1] = 0 | 0x80;
		buf[2] = this->state == PCECD_STATE_PAUSE ? 2 : (this->state == PCECD_STATE_PLAY ? 0 : 3);
		buf[3] = 0;
		buf[4] = BCD(this->index + 1);
		buf[5] = BCD(this->index);

		LBAToMSF(lba_rel, &msf);
		buf[6] = BCD(msf.m);
		buf[7] = BCD(msf.s);
		buf[8] = BCD(msf.f);

		LBAToMSF(this->lba+150, &msf);
		buf[9] = BCD(msf.m);
		buf[10] = BCD(msf.s);
		buf[11] = BCD(msf.f);

		if (SendData)
			SendData(buf, 10 + 2, PCECD_DATA_IO_INDEX);

		//printf("\x1b[32mPCECD: Command READSUBQ, [1] = %02X, track = %i, index = %i, lba_rel = %i, lba_abs = %i\n\x1b[0m", comm[1], this->index + 1, this->index, lba_rel, this->lba);

		SendStatus(MAKE_STATUS(PCECD_STATUS_GOOD, 0));
	}
		break;

	default:
		CommandError(SENSEKEY_ILLEGAL_REQUEST, NSE_INVALID_COMMAND, 0, 0);

		printf("\x1b[32mPCECD: Command undefined, [0] = %02X, [1] = %02X, [2] = %02X, [3] = %02X, [4] = %02X, [5] = %02X\n\x1b[0m", comm[0], comm[1], comm[2], comm[3], comm[4], comm[5]);
		
		has_status = 0;
		SendStatus(MAKE_STATUS(PCECD_STATUS_CHECK_COND, 0));

		break;
	}
}

uint16_t pcecdd_t::GetStatus() {
	return stat;
}

int pcecdd_t::SetCommand(uint8_t* buf) {
	memcpy(comm, buf, 14);
	return 0;
}

void pcecdd_t::PendStatus(uint16_t status) {
	stat = status;
	has_status = 1;
}

void pcecdd_t::SendStatus(uint16_t status) {

	spi_uio_cmd_cont(UIO_CD_SET);
	spi_w(status);
	spi_w(region ? 2 : 0);
	DisableIO();

	//printf("\x1b[32mPCECD: Send status = %02X, message = %02X\n\x1b[0m", status & 0xFF, status >> 8);
}

void pcecdd_t::SendDataRequest() {

	spi_uio_cmd_cont(UIO_CD_SET);
	spi_w(0);
	spi_w((region ? 2 : 0) | 1);
	DisableIO();

	printf("\x1b[32mPCECD: Data request for MODESELECT6\n\x1b[0m");
}

void pcecdd_t::SetRegion(uint8_t rgn) {
	region = rgn;
}

void pcecdd_t::LBAToMSF(int lba, msf_t* msf) {
	msf->m = (lba / 75) / 60;
	msf->s = (lba / 75) % 60;
	msf->f = (lba % 75);
}

void pcecdd_t::MSFToLBA(int* lba, uint8_t m, uint8_t s, uint8_t f) {
	*lba = f + s * 75 + m * 60 * 75 - 150;
}

void pcecdd_t::MSFToLBA(int* lba, msf_t* msf) {
	*lba = msf->f + msf->s * 75 + msf->m * 60 * 75 - 150;
}

int pcecdd_t::GetTrackByLBA(int lba, toc_t* toc) {
	int index = 0;
	while ((toc->tracks[index].end <= lba) && (index < toc->last)) index++;
	return index;
}

void pcecdd_t::ReadData(uint8_t *buf)
{
	if (this->toc.tracks[this->index].type && (this->lba >= 0))
	{
		if (this->toc.chd_f)
		{
			int s_offset = 0;
			if (this->toc.tracks[this->index].sector_size != 2048)
			{
				s_offset += 16;
			}

			mister_chd_read_sector(this->toc.chd_f, this->lba + this->toc.tracks[this->index].offset, 0, s_offset, 2048, buf, this->chd_hunkbuf, &this->chd_hunknum);
		} else {
			if (this->toc.tracks[this->index].sector_size == 2048)
			{
				FileSeek(&this->toc.tracks[this->index].f, this->lba * 2048 - this->toc.tracks[this->index].offset, SEEK_SET);
			} else {
				FileSeek(&this->toc.tracks[this->index].f, this->lba * 2352 + 16 - this->toc.tracks[this->index].offset, SEEK_SET);
			}
			FileReadAdv(&this->toc.tracks[this->index].f, buf, 2048);
		}
	}
}

int pcecdd_t::ReadCDDA(uint8_t *buf)
{
	this->audioLength = 2352;// 2352 + 2352 - this->audioOffset;
	this->audioOffset = 0;// 2352;


	if (this->toc.chd_f)
	{
		mister_chd_read_sector(this->toc.chd_f, this->lba + this->toc.tracks[this->index].offset, 0, 0, this->audioLength, buf, this->chd_hunkbuf, &this->chd_hunknum);
		for (int swapidx = 0; swapidx < this->audioLength; swapidx += 2)
		{
			uint8_t temp = buf[swapidx];
			buf[swapidx] = buf[swapidx+1];
			buf[swapidx+1] = temp;
		}
	} else if (this->toc.tracks[this->index].f.opened()) {
		FileReadAdv(&this->toc.tracks[this->index].f, buf, this->audioLength);
	}

	return this->audioLength;
}

void pcecdd_t::ReadSubcode(uint16_t* buf)
{
	(void)buf;
	/*
	uint8_t subc[96];
	int i, j, n;

	fread(subc, 96, 1, this->toc.sub);

	for (i = 0, n = 0; i < 96; i += 2, n++)
	{
		int code = 0;
		for (j = 0; j < 8; j++)
		{
			int bits = (subc[(j * 12) + (i / 8)] >> (6 - (i & 6))) & 3;
			code |= ((bits & 1) << (7 - j));
			code |= ((bits >> 1) << (15 - j));
		}

		buf[n] = code;
	}
	*/
}

int pcecdd_t::SectorSend(uint8_t* header)
{
	uint8_t buf[2352 + 2352];
	int len = 2352;

	if (header) {
		memcpy(buf + 12, header, 4);
		ReadData(buf + 16);
	}
	else {
		len = ReadCDDA(buf);
	}

	if (SendData)
		return SendData(buf, len, PCECD_DATA_IO_INDEX);

	return 0;
}

void pcecdd_t::CommandError(uint8_t key, uint8_t asc, uint8_t ascq, uint8_t fru) {
	sense.key = key;
	sense.asc = asc;
	sense.ascq = ascq;
	sense.fru = fru;
}




