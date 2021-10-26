
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>

#include "megacd.h"
#include "../chd/mister_chd.h"

#define CD_DATA_IO_INDEX 2
#define CD_SUB_IO_INDEX 3

cdd_t cdd;

cdd_t::cdd_t() {
	latency = 10;
	loaded = 0;
	index = 0;
	lba = 0;
	scanOffset = 0;
	isData = 1;
	status = CD_STAT_NO_DISC;
	audioLength = 0;
	audioOffset = 0;
	chd_hunkbuf = NULL;
	chd_hunknum = -1;
	SendData = NULL;

	stat[0] = 0xB;
	stat[1] = 0x0;
	stat[2] = 0x0;
	stat[3] = 0x0;
	stat[4] = 0x0;
	stat[5] = 0x0;
	stat[6] = 0x0;
	stat[7] = 0x0;
	stat[8] = 0x0;
	stat[9] = 0x4;
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


int cdd_t::LoadCUE(const char* filename) {
	static char fname[1024 + 10];
	static char line[128];
	char *ptr, *lptr;
	static char header[1024];
	static char toc[100 * 1024];

	strcpy(fname, filename);

	memset(toc, 0, sizeof(toc));
	if (!FileLoad(fname, toc, sizeof(toc) - 1)) return 1;

	printf("\x1b[32mMCD: Open CUE: %s\n\x1b[0m", fname);

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

			printf("\x1b[32mMCD: Open track file: %s\n\x1b[0m", fname);

			pregap = 0;

			this->toc.tracks[this->toc.last].offset = 0;

			if (!strstr(lptr, "BINARY") && !strstr(lptr, "MOTOROLA") && !strstr(lptr, "WAVE"))
			{
				FileClose(&this->toc.tracks[this->toc.last].f);
				printf("\x1b[32mMCD: unsupported file: %s\n\x1b[0m", fname);

				return -1;
			}
		}

		/* decode TRACK commands */
		else if ((sscanf(lptr, "TRACK %02d %*s", &bb)) || (sscanf(lptr, "TRACK %d %*s", &bb)))
		{
			if (bb != (this->toc.last + 1))
			{
				FileClose(&this->toc.tracks[this->toc.last].f);
				printf("\x1b[32mMCD: missing tracks: %s\n\x1b[0m", fname);
				break;
			}

			if (!this->toc.last)
			{
				if (strstr(lptr, "MODE1/2048"))
				{
					this->sectorSize = 2048;
				}
				else if (strstr(lptr, "MODE1/2352"))
				{
					this->sectorSize = 2352;

					FileSeek(&this->toc.tracks[0].f, 0x10, SEEK_SET);
				}

				if (this->sectorSize)
				{
					this->toc.tracks[0].type = 1;

					FileReadAdv(&this->toc.tracks[0].f, header, 0x210);
					FileSeek(&this->toc.tracks[0].f, 0, SEEK_SET);
				}
			}
			else
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
			this->toc.tracks[this->toc.last].offset += pregap * 2352;

			if (!this->toc.tracks[this->toc.last].f.opened())
			{
				FileOpen(&this->toc.tracks[this->toc.last].f, fname);
				this->toc.tracks[this->toc.last].start = bb + ss * 75 + mm * 60 * 75 + pregap;
				if (this->toc.last && !this->toc.tracks[this->toc.last - 1].end)
				{
					this->toc.tracks[this->toc.last - 1].end = this->toc.tracks[this->toc.last].start;
				}
			}
			else
			{
				FileSeek(&this->toc.tracks[this->toc.last].f, 0, SEEK_SET);

				this->toc.tracks[this->toc.last].start = this->toc.end + pregap;
				this->toc.tracks[this->toc.last].offset += this->toc.end * 2352;

				int sectorSize = 2352;
				if (this->toc.tracks[this->toc.last].type) sectorSize = this->sectorSize;
				this->toc.tracks[this->toc.last].end = this->toc.tracks[this->toc.last].start + ((this->toc.tracks[this->toc.last].f.size + sectorSize - 1) / sectorSize);

				this->toc.tracks[this->toc.last].start += (bb + ss * 75 + mm * 60 * 75);
				this->toc.end = this->toc.tracks[this->toc.last].end;
			}

			printf("\x1b[32mMCD: Track = %u, start = %u, end = %u, offset = %u, type = %u\n\x1b[0m", cdd.toc.last, cdd.toc.tracks[cdd.toc.last].start, cdd.toc.tracks[cdd.toc.last].end, cdd.toc.tracks[cdd.toc.last].offset, cdd.toc.tracks[cdd.toc.last].type);

			this->toc.last++;
			if (this->toc.last == 99) break;
		}
	}

	if (this->toc.last && !this->toc.tracks[this->toc.last - 1].end)
	{
		this->toc.end += pregap;
		this->toc.tracks[this->toc.last - 1].end = this->toc.end;
	}

        memcpy(&fname[strlen(fname) - 4], ".sub", 4);
        FileOpen(&this->toc.sub, getFullPath(fname));

	FileClose(&this->toc.tracks[this->toc.last].f);
	return 0;
}

int cdd_t::Load(const char *filename)
{
	//char fname[1024 + 10];
	static char header[32];
	fileTYPE *fd_img;

	Unload();

	const char *ext = filename+strlen(filename)-4;
	if (!strncasecmp(".cue", ext, 4))
	{
		if (LoadCUE(filename)) {
			return (-1);
		}
	} else if (!strncasecmp(".chd", ext, 4))  {
		chd_error err = mister_load_chd(filename, &this->toc);
		if (err != CHDERR_NONE)
		{
			printf("ERROR %s\n", chd_error_string(err));
			return -1;
		}

		if (this->chd_hunkbuf)
		{
			free(this->chd_hunkbuf);
		}

		this->chd_hunkbuf = (uint8_t *)malloc(CD_FRAME_SIZE * CD_FRAMES_PER_HUNK);
		this->chd_hunknum = -1;
 	} else {
		return (-1);

	}

	if (this->toc.chd_f)
	{
		mister_chd_read_sector(this->toc.chd_f, 0, 0, 0, 0x10, (uint8_t *)header, this->chd_hunkbuf, &this->chd_hunknum);
	} else {
		fd_img = &this->toc.tracks[0].f;

		FileSeek(fd_img, 0, SEEK_SET);
		FileReadAdv(fd_img, header, 0x10);
	}

	if (!memcmp("SEGADISCSYSTEM", header, 14))
	{
		this->sectorSize = 2048;
	}
	else
	{
		this->sectorSize = 2352;
	}

	printf("\x1b[32mMCD: Sector size = %u, Track 0 end = %u\n\x1b[0m", this->sectorSize, this->toc.tracks[0].end);

	if (this->toc.last)
	{
		this->toc.tracks[this->toc.last].start = this->toc.end;
		this->loaded = 1;

		printf("\x1b[32mMCD: CD mounted , last track = %u\n\x1b[0m", this->toc.last);

		return 1;
	}

	return 0;
}

void cdd_t::Unload()
{
	if (this->loaded)
	{
		if (this->toc.chd_f)
		{
			chd_close(this->toc.chd_f);
		}

		if (this->chd_hunkbuf)
		{
			free(this->chd_hunkbuf);
			this->chd_hunkbuf = NULL;
		}

		for (int i = 0; i < this->toc.last; i++)
		{
			if (this->toc.tracks[i].f.opened())
			{
				FileClose(&this->toc.tracks[i].f);
			}
		}

		if (this->toc.sub.opened()) FileClose(&this->toc.sub);

		this->loaded = 0;
	}

	memset(&this->toc, 0x00, sizeof(this->toc));
	this->sectorSize = 0;
}

void cdd_t::Reset() {
	latency = 10;
	index = 0;
	lba = 0;
	scanOffset = 0;
	isData = 1;
	status = CD_STAT_STOP;
	audioLength = 0;
	audioOffset = 0;
	chd_audio_read_lba = 0;

	stat[0] = 0x0;
	stat[1] = 0x0;
	stat[2] = 0x0;
	stat[3] = 0x0;
	stat[4] = 0x0;
	stat[5] = 0x0;
	stat[6] = 0x0;
	stat[7] = 0x0;
	stat[8] = 0x0;
	stat[9] = 0xF;
}

void cdd_t::Update() {
	if (this->status == CD_STAT_STOP || this->status == CD_STAT_TRAY || this->status == CD_STAT_OPEN)
	{
		if (this->latency > 0)
		{
			this->latency--;
			return;
		}
		this->status = this->loaded ? CD_STAT_TOC : CD_STAT_NO_DISC;
	}
	else if (this->status == CD_STAT_SEEK)
	{
		if (this->latency > 0)
		{
			this->latency--;
			return;
		}
		this->status = CD_STAT_PAUSE;
	}
	else if (this->status == CD_STAT_PLAY)
	{
		if (this->latency > 0)
		{
			this->latency--;
			return;
		}

		if (this->index >= this->toc.last)
		{
			this->status = CD_STAT_END;
			return;
		}


		if (this->toc.tracks[this->index].type)
		{
			// CD-ROM (Mode 1)
			uint8_t header[4];
			msf_t msf;
			LBAToMSF(this->lba + 150, &msf);
			header[0] = BCD(msf.m);
			header[1] = BCD(msf.s);
			header[2] = BCD(msf.f);
			header[3] = 0x01;

			SectorSend(header);

		}
		else
		{
			if (this->lba >= this->toc.tracks[this->index].start)
			{
				this->isData = 0x00;
			}
			SectorSend(0);
		}

		this->lba++;
		this->chd_audio_read_lba++;

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
	else if (cdd.status == CD_STAT_SCAN)
	{
		this->lba += this->scanOffset;

		if (this->lba >= this->toc.tracks[this->index].end)
		{
			this->index++;
			if (this->index < this->toc.last)
			{
				this->lba = this->toc.tracks[this->index].start;
			}
			else
			{
				this->lba = this->toc.end;
				this->status = CD_STAT_END;
				this->isData = 0x01;
				return;
			}
		}
		else if (this->lba < this->toc.tracks[this->index].start)
		{
			if (this->index > 0)
			{
				this->index--;
				this->lba = this->toc.tracks[this->index].end;
			}
			else
			{
				this->lba = 0;
			}
		}

		this->isData = this->toc.tracks[this->index].type;

		if (this->toc.sub.opened()) FileSeek(&this->toc.sub, this->lba * 96, SEEK_SET);

		if (this->toc.tracks[this->index].type)
		{
			// DATA track
			FileSeek(&this->toc.tracks[0].f, this->lba * this->sectorSize, SEEK_SET);
		}
		else if (this->toc.tracks[this->index].f.opened())
		{
			// AUDIO track
			FileSeek(&this->toc.tracks[this->index].f, (this->lba * 2352) - this->toc.tracks[this->index].offset, SEEK_SET);
		}
	}
}

void cdd_t::CommandExec() {
	msf_t msf;

	switch (comm[0]) {
	case CD_COMM_IDLE:
		if (this->latency <= 3)
		{
			stat[0] = this->status;
			if (stat[1] == 0x0f)
			{
				int lba = this->lba + 150;
				LBAToMSF(lba, &msf);
				stat[1] = 0x0;
	                        stat[2] = BCD(msf.m) >> 4;
				stat[3] = BCD(msf.m) & 0xF;
				stat[4] = BCD(msf.s) >> 4;
				stat[5] = BCD(msf.s) & 0xF;
				stat[6] = BCD(msf.f) >> 4;
				stat[7] = BCD(msf.f) & 0xF;
				stat[8] = this->toc.tracks[this->index].type ? 0x04 : 0x00;
			} else if (stat[1] == 0x00) {
				int lba = this->lba + 150;
				LBAToMSF(lba, &msf);
                                stat[2] = BCD(msf.m) >> 4;
                                stat[3] = BCD(msf.m) & 0xF;
                                stat[4] = BCD(msf.s) >> 4;
                                stat[5] = BCD(msf.s) & 0xF;
                                stat[6] = BCD(msf.f) >> 4;
                                stat[7] = BCD(msf.f) & 0xF;
                                stat[8] = this->toc.tracks[this->index].type ? 0x04 : 0x00;
			} else if (stat[1] == 0x01) {
				int lba = abs(this->lba - this->toc.tracks[this->index].start);
				LBAToMSF(lba,&msf);
                                stat[2] = BCD(msf.m) >> 4;
                                stat[3] = BCD(msf.m) & 0xF;
                                stat[4] = BCD(msf.s) >> 4;
                                stat[5] = BCD(msf.s) & 0xF;
                                stat[6] = BCD(msf.f) >> 4;
                                stat[7] = BCD(msf.f) & 0xF;
                                stat[8] = this->toc.tracks[this->index].type ? 0x04 : 0x00;
			} else if (stat[1] == 0x02) {
                               stat[2] = (cdd.index < this->toc.last) ? BCD(this->index + 1) >> 4 : 0xA;
                               stat[3] = (cdd.index < this->toc.last) ? BCD(this->index + 1) & 0xF : 0xA;
			}
		}

		//printf("MCD: Command IDLE, status = %u\n\x1b[0m", this->status);
		break;

	case CD_COMM_STOP:
		this->status = CD_STAT_STOP;
		this->isData = 1;

		stat[0] = this->status;
		stat[1] = 0;
		stat[2] = 0;
		stat[3] = 0;
		stat[4] = 0;
		stat[5] = 0;
		stat[6] = 0;
		stat[7] = 0;
		stat[8] = 0;

		//printf("\x1b[32mMCD: Command STOP, status = %u, frame = %u\n\x1b[0m", this->status, frame);
		break;

	case CD_COMM_TOC:
		switch (comm[3]) {
		case 0: {
			int lba_ = this->lba + 150;
			LBAToMSF(lba_, &msf);

			stat[0] = this->status;
			stat[1] = 0x0;
			stat[2] = BCD(msf.m) >> 4;
			stat[3] = BCD(msf.m) & 0xF;
			stat[4] = BCD(msf.s) >> 4;
			stat[5] = BCD(msf.s) & 0xF;
			stat[6] = BCD(msf.f) >> 4;
			stat[7] = BCD(msf.f) & 0xF;
			stat[8] = this->toc.tracks[this->index].type << 2;

			//printf("\x1b[32mMCD: Command TOC 0, lba = %i, command = %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X, status = %02X%08X, frame = %u\n\x1b[0m", lba, comm[9], comm[8], comm[7], comm[6], comm[5], comm[4], comm[3], comm[2], comm[1], comm[0], (uint32_t)(GetStatus() >> 32), (uint32_t)GetStatus(), frame);
		}
			break;

		case 1: {
			int lba_ = abs(this->lba - this->toc.tracks[this->index].start);
			LBAToMSF(lba_, &msf);

			stat[0] = this->status;
			stat[1] = 0x1;
			stat[2] = BCD(msf.m) >> 4;
			stat[3] = BCD(msf.m) & 0xF;
			stat[4] = BCD(msf.s) >> 4;
			stat[5] = BCD(msf.s) & 0xF;
			stat[6] = BCD(msf.f) >> 4;
			stat[7] = BCD(msf.f) & 0xF;
			stat[8] = this->toc.tracks[this->index].type << 2;

			//printf("\x1b[32mMCD: Command TOC 1, lba = %i, command = %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X, status = %02X%08X, frame = %u\n\x1b[0m", lba, comm[9], comm[8], comm[7], comm[6], comm[5], comm[4], comm[3], comm[2], comm[1], comm[0], (uint32_t)(GetStatus() >> 32), (uint32_t)GetStatus(), frame);
		}
			break;

		case 2: {
			stat[0] = this->status;
			stat[1] = 0x2;
			stat[2] = ((this->index < this->toc.last) ?  BCD(this->index + 1) >> 4 : 0xA);
			stat[3] = ((this->index < this->toc.last) ? BCD(this->index + 1) & 0xF : 0xA);
			stat[4] = 0;
			stat[5] = 0;
			stat[6] = 0;
			stat[7] = 0;
			stat[8] = 0;

			//printf("\x1b[32mMCD: Command TOC 2, index = %i, command = %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X, status = %02X%08X, frame = %u\n\x1b[0m", this->index, comm[9], comm[8], comm[7], comm[6], comm[5], comm[4], comm[3], comm[2], comm[1], comm[0], (uint32_t)(GetStatus() >> 32), (uint32_t)GetStatus(), frame);
		}
			break;

		case 3: {
			int lba_ = this->toc.end + 150;
			LBAToMSF(lba_, &msf);

			stat[0] = this->status;
			stat[1] = 0x3;
			stat[2] = BCD(msf.m) >> 4;
			stat[3] = BCD(msf.m) & 0xF;
			stat[4] = BCD(msf.s) >> 4;
			stat[5] = BCD(msf.s) & 0xF;
			stat[6] = BCD(msf.f) >> 4;
			stat[7] = BCD(msf.f) & 0xF;
			stat[8] = 0;

			//printf("\x1b[32mMCD: Command TOC 3, lba = %i, command = %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X, frame = %u\n\x1b[0m", lba, comm[9], comm[8], comm[7], comm[6], comm[5], comm[4], comm[3], comm[2], comm[1], comm[0], frame);
		}
			break;

		case 4: {
			stat[0] = this->status;
			stat[1] = 0x4;
			stat[2] = 0;
			stat[3] = 1;
			stat[4] = BCD(this->toc.last) >> 4;
			stat[5] = BCD(this->toc.last) & 0xF;
			stat[6] = 0;
			stat[7] = 0;
			stat[8] = 0;

			//printf("\x1b[32mMCD: Command TOC 4, last = %i, command = %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X, frame = %u\n\x1b[0m", this->toc.last, comm[9], comm[8], comm[7], comm[6], comm[5], comm[4], comm[3], comm[2], comm[1], comm[0], frame);
		}
			break;

		case 5: {
			int track = comm[4] * 10 + comm[5];
			int lba_ = this->toc.tracks[track - 1].start + 150;
			LBAToMSF(lba_, &msf);

			stat[0] = this->status;
			stat[1] = 0x5;
			stat[2] = BCD(msf.m) >> 4;
			stat[3] = BCD(msf.m) & 0xF;
			stat[4] = BCD(msf.s) >> 4;
			stat[5] = BCD(msf.s) & 0xF;
			stat[6] = (BCD(msf.f) >> 4) | (this->toc.tracks[track - 1].type << 3);
			stat[7] = BCD(msf.f) & 0xF;
			stat[8] = BCD(track) & 0xF;

			//printf("\x1b[32mMCD: Command TOC 5, lba = %i, track = %i, command = %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X, frame = %u\n\x1b[0m", lba_, track, comm[9], comm[8], comm[7], comm[6], comm[5], comm[4], comm[3], comm[2], comm[1], comm[0], frame);
		}
			break;

		case 6:
			stat[0] = this->status;
			stat[1] = 0x6;
			stat[2] = 0;
			stat[3] = 0;
			stat[4] = 0;
			stat[5] = 0;
			stat[6] = 0;
			stat[7] = 0;
			stat[8] = 0;
			break;

		default:

			break;
		}
		break;

	case CD_COMM_PLAY: {
		int index = 0;
		int lba_;
		MSFToLBA(&lba_, comm[2] * 10 + comm[3], comm[4] * 10 + comm[5],	comm[6] * 10 + comm[7]);
		lba_ -= 150;

		//if (!this->latency)
		{
			this->latency = 11;
		}

		this->latency += (abs(lba_ - this->lba) * 120) / 270000;

		this->lba = lba_;

		while ((this->toc.tracks[index].end <= lba_) && (index < this->toc.last)) index++;
		this->index = index;
		if (lba_ < this->toc.tracks[index].start)
		{
			lba_ = this->toc.tracks[index].start;
		}

		if (this->toc.tracks[index].type)
		{
			/* DATA track */
			FileSeek(&this->toc.tracks[0].f, lba_ * this->sectorSize, SEEK_SET);
		}
		else if (this->toc.tracks[index].f.opened())
		{
			/* PCM AUDIO track */
			FileSeek(&this->toc.tracks[index].f, (lba_ * 2352) - this->toc.tracks[index].offset, SEEK_SET);
		}

		this->chd_audio_read_lba = this->lba;
		this->audioOffset = 0;

		if (this->toc.sub.opened()) FileSeek(&this->toc.sub, lba_ * 96, SEEK_SET);

		this->isData = 1;

		this->status = CD_STAT_PLAY;

		stat[0] = CD_STAT_SEEK;
		stat[1] = 0xf;
		stat[2] = 0;
		stat[3] = 0;
		stat[4] = 0;
		stat[5] = 0;
		stat[6] = 0;
		stat[7] = 0;
		stat[8] = 0;

		//printf("\x1b[32mMCD: Command PLAY, lba = %i, index = %i, command = %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X, frame = %u\n\x1b[0m", lba_, this->index, comm[9], comm[8], comm[7], comm[6], comm[5], comm[4], comm[3], comm[2], comm[1], comm[0], frame);
	}
		break;

	case CD_COMM_SEEK: {
		int index = 0;
		int lba_;
		MSFToLBA(&lba_, comm[2] * 10 + comm[3], comm[4] * 10 + comm[5], comm[6] * 10 + comm[7]);
		lba_ -= 150;

		this->latency = (abs(lba_ - this->lba) * 120) / 270000;

		this->lba = lba_;

		while ((this->toc.tracks[index].end <= lba_) && (index < this->toc.last)) index++;
		this->index = index;

		if (lba_ < this->toc.tracks[index].start)
		{
			lba_ = this->toc.tracks[index].start;
		}

		if (this->toc.tracks[index].type)
		{
			// DATA track
			FileSeek(&this->toc.tracks[0].f, lba_ * this->sectorSize, SEEK_SET);
		}
		else if (this->toc.tracks[index].f.opened())
		{
			// AUDIO track
			FileSeek(&this->toc.tracks[index].f, (lba_ * 2352) - this->toc.tracks[index].offset, SEEK_SET);
		}

		if (this->toc.sub.opened()) FileSeek(&this->toc.sub, lba_ * 96, SEEK_SET);

		this->isData = 1;

		this->status = CD_STAT_SEEK;

		stat[0] = this->status;
		stat[1] = 0xf;
		stat[2] = 0;
		stat[3] = 0;
		stat[4] = 0;
		stat[5] = 0;
		stat[6] = 0;
		stat[7] = 0;
		stat[8] = 0;

		//printf("\x1b[32mMCD: Command PLAY, lba = %i, index = %i, command = %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X, frame = %u\n\x1b[0m", lba_, this->index, comm[9], comm[8], comm[7], comm[6], comm[5], comm[4], comm[3], comm[2], comm[1], comm[0], frame);
	}
		break;

	case CD_COMM_PAUSE:
		this->isData = 0x01;

		this->status = CD_STAT_PAUSE;

		stat[0] = this->status;

		//printf("\x1b[32mMCD: Command PAUSE, status = %X, frame = %u\n\x1b[0m", this->status, frame);
		break;

	case CD_COMM_RESUME:
		this->status = CD_STAT_PLAY;
		stat[0] = this->status;
		this->audioOffset = 0;
		//printf("\x1b[32mMCD: Command RESUME, status = %X\n\x1b[0m", this->status);
		break;

	case CD_COMM_FW_SCAN:
		this->scanOffset = CD_SCAN_SPEED;
		this->status = CD_STAT_SCAN;
		stat[0] = this->status;
		break;

	case CD_COMM_RW_SCAN:
		this->scanOffset = -CD_SCAN_SPEED;
		this->status = CD_STAT_SCAN;
		stat[0] = this->status;
		break;

	case CD_COMM_TRACK_MOVE:
		this->isData = 1;
		this->status = CD_STAT_PAUSE;
		stat[0] = this->status;
		break;

	case CD_COMM_TRAY_CLOSE:
		this->isData = 1;
		this->status = this->loaded ? CD_STAT_TOC : CD_STAT_NO_DISC;
		stat[0] = CD_STAT_STOP;

		//printf("\x1b[32mMCD: Command TRAY_CLOSE, status = %u, frame = %u\n\x1b[0m", this->status, frame);
		break;

	case CD_COMM_TRAY_OPEN:
		this->isData = 1;
		this->status = CD_STAT_OPEN;
		stat[0] = CD_STAT_OPEN;

		//printf("\x1b[32mMCD: Command TRAY_OPEN, status = %u, frame = %u\n\x1b[0m", this->status, frame);
		break;

	default:
		stat[0] = this->status;

		//printf("\x1b[32mMCD: Command undefined, status = %u, frame = %u\n\x1b[0m", this->status, frame);
		break;
	}
}

uint64_t cdd_t::GetStatus() {
	uint8_t n9 = ~(stat[0] + stat[1] + stat[2] + stat[3] + stat[4] + stat[5] + stat[6] + stat[7] + stat[8]);
	return ((uint64_t)(n9 & 0xF) << 36) |
		((uint64_t)(stat[8] & 0xF) << 32) |
		((uint64_t)(stat[7] & 0xF) << 28) |
		((uint64_t)(stat[6] & 0xF) << 24) |
		((uint64_t)(stat[5] & 0xF) << 20) |
		((uint64_t)(stat[4] & 0xF) << 16) |
		((uint64_t)(stat[3] & 0xF) << 12) |
		((uint64_t)(stat[2] & 0xF) << 8) |
		((uint64_t)(stat[1] & 0xF) << 4) |
		((uint64_t)(stat[0] & 0xF) << 0);
}

int cdd_t::SetCommand(uint64_t c) {
	comm[0] = (c >> 0) & 0xF;
	comm[1] = (c >> 4) & 0xF;
	comm[2] = (c >> 8) & 0xF;
	comm[3] = (c >> 12) & 0xF;
	comm[4] = (c >> 16) & 0xF;
	comm[5] = (c >> 20) & 0xF;
	comm[6] = (c >> 24) & 0xF;
	comm[7] = (c >> 28) & 0xF;
	comm[8] = (c >> 32) & 0xF;
	comm[9] = (c >> 36) & 0xF;

	uint8_t crc = (~(comm[0] + comm[1] + comm[2] + comm[3] + comm[4] + comm[5] + comm[6] + comm[7] + comm[8])) & 0xF;
	if (comm[9] != crc)
		return -1;

	return 0;
}

void cdd_t::LBAToMSF(int lba, msf_t* msf) {
	msf->m = (lba / 75) / 60;
	msf->s = (lba / 75) % 60;
	msf->f = (lba % 75);
}

void cdd_t::MSFToLBA(int* lba, uint8_t m, uint8_t s, uint8_t f) {
	*lba = f + s * 75 + m * 60 * 75;
}

void cdd_t::MSFToLBA(int* lba, msf_t* msf) {
	*lba = msf->f + msf->s * 75 + msf->m * 60 * 75;
}

void cdd_t::ReadData(uint8_t *buf)
{
	if (this->toc.tracks[this->index].type && (this->lba >= 0))
	{

		if (this->toc.chd_f)
		{
			int read_offset = 0;
			if (this->sectorSize != 2048)
			{
				read_offset += 16;
			}

			mister_chd_read_sector(this->toc.chd_f, this->lba + this->toc.tracks[0].offset, 0, read_offset, 2048, buf, this->chd_hunkbuf, &this->chd_hunknum);
		} else {
			if (this->sectorSize == 2048)
			{
				FileSeek(&this->toc.tracks[0].f, this->lba * 2048, SEEK_SET);
			} else {
			FileSeek(&this->toc.tracks[0].f, this->lba * 2352 + 16, SEEK_SET);
			}
			FileReadAdv(&this->toc.tracks[0].f, buf, 2048);
		}
	}
}

int cdd_t::ReadCDDA(uint8_t *buf)
{
	this->audioLength = 2352 + 2352 - this->audioOffset;
	this->audioOffset = 2352;

	//printf("\x1b[32mMCD: AUDIO LENGTH %d LBA: %d INDEX: %d START: %d END %d\n\x1b[0m", this->audioLength, this->lba, this->index, this->toc.tracks[this->index].start, this->toc.tracks[this->index].end);
	//

	if (this->isData)
	{
		return this->audioLength;
	}

	if (this->toc.chd_f)
	{
		for(int i = 0; i < this->audioLength / 2352; i++)
		{
			mister_chd_read_sector(this->toc.chd_f, this->chd_audio_read_lba + this->toc.tracks[this->index].offset, 2352*i, 0, 2352, buf, this->chd_hunkbuf, &this->chd_hunknum);
		}

		//CHD audio requires byteswap. There's probably a better way to do this...

		for (int swapidx = 0; swapidx < this->audioLength; swapidx += 2)
		{
			uint8_t temp = buf[swapidx];
			buf[swapidx] = buf[swapidx+1];
			buf[swapidx+1] = temp;
		}

		if ((this->audioLength / 2352) > 1)
		{
			this->chd_audio_read_lba++;
		}

	} else if (this->toc.tracks[this->index].f.opened()) {
		FileReadAdv(&this->toc.tracks[this->index].f, buf, this->audioLength);
	}

	return this->audioLength;
}

void InterleaveSubcode(uint8_t *subc_data, uint16_t *buf)
{
	for(int i = 0, n=0; i < 96; i+=2,n++)
	{
		int code = 0;
		for (int j = 0; j < 8; j++)
		{
			int bits = (subc_data[(j * 12) + (i / 8)] >> (6 - (i&6))) & 3;
			code |= ((bits & 1) << (15 - j));
			code |= ((bits >> 1) << (7 - j));
		}
		buf[n] = code;
	}
}

void cdd_t::ReadSubcode(uint16_t* buf)
{
	uint8_t subc[96];
	if (this->toc.chd_f)
	{
		//Just use the read sector call with an offset, since we previously read that sector, it is already in the hunk cache
		if (this->toc.tracks[this->index].sbc_type == SUBCODE_RW_RAW) {
			mister_chd_read_sector(this->toc.chd_f, this->chd_audio_read_lba + this->toc.tracks[this->index].offset, 0, CD_MAX_SECTOR_DATA, 96, (uint8_t *)buf, this->chd_hunkbuf, &this->chd_hunknum);
		} else if (this->toc.tracks[this->index].sbc_type == SUBCODE_RW) {
			mister_chd_read_sector(this->toc.chd_f, this->chd_audio_read_lba + this->toc.tracks[this->index].offset, 0, CD_MAX_SECTOR_DATA, 96, subc, this->chd_hunkbuf, &this->chd_hunknum);
			InterleaveSubcode(subc, buf);
		}
	} else if (this->toc.sub.opened()) {
		FileReadAdv(&this->toc.sub, subc, 96);
		InterleaveSubcode(subc, buf);
	}
}


int cdd_t::SectorSend(uint8_t* header)
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

	SubcodeSend();
	if (SendData)
		return SendData(buf, len, CD_DATA_IO_INDEX);

	return 0;
}


int cdd_t::SubcodeSend()
{
	uint16_t buf[98 / 2];

	ReadSubcode(buf);

	if (SendData)
		return SendData((uint8_t*)buf, 98, CD_SUB_IO_INDEX);

	return 0;
}





