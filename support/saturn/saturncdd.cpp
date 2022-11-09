#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>

#include "saturn.h"
#include "../chd/mister_chd.h"

#define CD_DATA_IO_INDEX 4

satcdd_t satcdd;

satcdd_t::satcdd_t() {
	loaded = 0;
	state = Open;
	lid_open = true;
	stop_pend = false;
	seek_pend = false;
	read_pend = false;
	seek_ring = false;
	pause_pend = false;
	read_toc = false;
	track = 0;
	lba = 0;
	seek_lba = 0;
	speed = 0;
	audioLength = 0;
	audioFirst = 0;
	//chd_hunkbuf = NULL;
	//chd_hunknum = -1;
	SendData = NULL;

	stat[0] = SATURN_STAT_OPEN;
	stat[1] = 0x00;
	stat[2] = 0x00;
	stat[3] = 0x00;
	stat[4] = 0x00;
	stat[5] = 0x00;
	stat[6] = 0x00;
	stat[7] = 0x00;
	stat[8] = 0x00;
	stat[9] = 0x00;
	stat[11] = 0x00;
	SetChecksum(stat);
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

		if (*instr == 10) instr++;
		*in = instr;
	} while (!*out && **in);

	return *out;
}

int satcdd_t::LoadCUE(const char* filename) {
	static char fname[1024 + 10];
	static char line[128];
	char *ptr, *lptr;
	static char toc[100 * 1024];

	strcpy(fname, filename);

	memset(toc, 0, sizeof(toc));
	if (!FileLoad(fname, toc, sizeof(toc) - 1)) return 1;

#ifdef SATURN_DEBUG
	printf("\x1b[32mSaturn: Open CUE: %s\n\x1b[0m", fname);
#endif // SATURN_DEBUG

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

			if (!FileOpen(&this->toc.tracks[this->toc.last].f, fname)) return -1;

#ifdef SATURN_DEBUG
			printf("\x1b[32mSaturn: Open track file: %s\n\x1b[0m", fname);
#endif // SATURN_DEBUG

			pregap = 0;

			this->toc.tracks[this->toc.last].offset = 0;

			if (!strstr(lptr, "BINARY") && !strstr(lptr, "MOTOROLA") && !strstr(lptr, "WAVE"))
			{
				FileClose(&this->toc.tracks[this->toc.last].f); 
#ifdef SATURN_DEBUG
				printf("\x1b[32mSaturn: unsupported file: %s\n\x1b[0m", fname);
#endif // SATURN_DEBUG

				return -1;
			}
		}

		/* decode TRACK commands */
		else if ((sscanf(lptr, "TRACK %02d %*s", &bb)) || (sscanf(lptr, "TRACK %d %*s", &bb)))
		{
			if (bb != (this->toc.last + 1))
			{
				FileClose(&this->toc.tracks[this->toc.last].f);
#ifdef SATURN_DEBUG
				printf("\x1b[32mSaturn: missing tracks: %s\n\x1b[0m", fname);
#endif // SATURN_DEBUG
				break;
			}

			if (strstr(lptr, "MODE1/2048"))
			{
				this->sectorSize = 2048;
				this->toc.tracks[this->toc.last].type = 1;
			}
			else if (strstr(lptr, "MODE1/2352"))
			{
				this->sectorSize = 2352;
				this->toc.tracks[this->toc.last].type = 1;

				//FileSeek(&this->toc.tracks[0].f, 0x10, SEEK_SET);
			}
			else if (strstr(lptr, "MODE2/2352"))
			{
				this->sectorSize = 2352;
				this->toc.tracks[this->toc.last].type = 2;

				//FileSeek(&this->toc.tracks[0].f, 0x10, SEEK_SET);
			}

			if (!this->toc.last)
			{
				/*if (strstr(lptr, "MODE1/2048"))
				{
					this->sectorSize = 2048;
					this->toc.tracks[0].type = 1;
				}
				else if (strstr(lptr, "MODE1/2352") || strstr(lptr, "MODE2/2352"))
				{
					this->sectorSize = 2352;
					this->toc.tracks[0].type = 1;

					FileSeek(&this->toc.tracks[0].f, 0x10, SEEK_SET);
				}

				if (this->sectorSize)
				{
					this->toc.tracks[0].type = 1;

					FileReadAdv(&this->toc.tracks[0].f, header, 0x210);
					FileSeek(&this->toc.tracks[0].f, 0, SEEK_SET);
				}*/
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
			this->toc.tracks[this->toc.last].pregap = bb + ss * 75 + mm * 60 * 75;
			pregap += this->toc.tracks[this->toc.last].pregap;
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
					this->toc.tracks[this->toc.last - 1].end = this->toc.tracks[this->toc.last].start - this->toc.tracks[this->toc.last].pregap;
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

#ifdef SATURN_DEBUG
			printf("\x1b[32mSaturn: Track = %u, start = %u, end = %u, offset = %u, type = %u\n\x1b[0m", this->toc.last + 1, this->toc.tracks[this->toc.last].start, this->toc.tracks[this->toc.last].end, this->toc.tracks[this->toc.last].offset, this->toc.tracks[this->toc.last].type);
#endif // SATURN_DEBUG

			this->toc.last++;
			if (this->toc.last == 99) break;
		}
	}

	if (this->toc.last && !this->toc.tracks[this->toc.last - 1].end)
	{
		this->toc.end += pregap;
		this->toc.tracks[this->toc.last - 1].end = this->toc.end;
	}

	FileClose(&this->toc.tracks[this->toc.last].f);
	return 0;
}

int satcdd_t::Load(const char *filename)
{
	//static char header[32];
	//fileTYPE *fd_img;

	Unload();

	const char *ext = filename + strlen(filename) - 4;
	if (!strncasecmp(".cue", ext, 4))
	{
		if (LoadCUE(filename)) {
			return (-1);
		}
	}
	/*else if (!strncasecmp(".chd", ext, 4)) {
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
	}*/
	else {
		return (-1);

	}

	/*if (this->toc.chd_f)
	{
		mister_chd_read_sector(this->toc.chd_f, 0, 0, 0, 0x10, (uint8_t *)header, this->chd_hunkbuf, &this->chd_hunknum);
	}
	else {
		fd_img = &this->toc.tracks[0].f;

		FileSeek(fd_img, 0, SEEK_SET);
		FileReadAdv(fd_img, header, 0x10);
	}*/

	/*if (!memcmp("SEGADISCSYSTEM", header, 14))
	{
		this->sectorSize = 2048;
	}
	else
	{
		this->sectorSize = 2352;
	}*/

#ifdef SATURN_DEBUG
	printf("\x1b[32mSaturn: Sector size = %u, Track 1 end = %u\n\x1b[0m", this->sectorSize, this->toc.tracks[0].end);
#endif // SATURN_DEBUG

	if (this->toc.last)
	{
		this->toc.tracks[this->toc.last].start = this->toc.end;
		this->loaded = 1;
		this->lid_open = false;
		this->stop_pend = true;

#ifdef SATURN_DEBUG
		printf("\x1b[32mSaturn: CD mounted, last track = %u\n\x1b[0m", this->toc.last);
#endif // SATURN_DEBUG

		return 1;
	}

	return 0;
}

void satcdd_t::Unload()
{
	if (this->loaded)
	{
		/*if (this->toc.chd_f)
		{
			chd_close(this->toc.chd_f);
		}

		if (this->chd_hunkbuf)
		{
			free(this->chd_hunkbuf);
			this->chd_hunkbuf = NULL;
		}*/

		for (int i = 0; i < this->toc.last; i++)
		{
			if (this->toc.tracks[i].f.opened())
			{
				FileClose(&this->toc.tracks[i].f);
			}
		}

		this->loaded = 0;
	}

	memset(&this->toc, 0x00, sizeof(this->toc));
	this->sectorSize = 0;

#ifdef SATURN_DEBUG
	printf("\x1b[32mSaturn: ");
	printf("Unload");
	printf("\n\x1b[0m");
#endif // SATURN_DEBUG
}


void satcdd_t::Reset() {
	state = Open;
	lid_open = true;
	stop_pend = false;
	seek_pend = false;
	read_pend = false;
	seek_ring = false;
	seek_ring2 = false;
	pause_pend = false;
	read_toc = false;
	track = 0;
	lba = 0;
	seek_lba = 0;
	speed = 0;
	audioLength = 0;
	audioFirst = 0;
	//chd_audio_read_lba = 0;
	satcdd.SendData = 0;

	stat[0] = SATURN_STAT_OPEN;
	stat[1] = 0x00;
	stat[2] = 0x00;
	stat[3] = 0x00;
	stat[4] = 0x00;
	stat[5] = 0x00;
	stat[6] = 0x00;
	stat[7] = 0x00;
	stat[8] = 0x00;
	stat[9] = 0x00;
	stat[11] = 0x00;
	SetChecksum(stat);

#ifdef SATURN_DEBUG
	printf("\x1b[32mSaturn: ");
	printf("Reset");
	printf("\n\x1b[0m");
#endif // SATURN_DEBUG
}

void satcdd_t::CommandExec() {
	int fad = GetFAD(comm);

	switch (comm[0]) {
	case SATURN_COMM_NOP:
#ifdef SATURN_DEBUG
		//printf("\x1b[32mSaturn: ");
		//printf("Command Nop, status = %02X", status);
		//printf(" (%u)\n\x1b[0m", frame_cnt);
#endif // SATURN_DEBUG
		break;

	case SATURN_COMM_SEEK_RING:
		this->lba = fad - 150;
		this->seek_lba = fad - 150;

		this->seek_ring = true;
		this->read_pend = false;
		this->pause_pend = false;
		this->speed = comm[10] == 1 ? 1 : 2;

#ifdef SATURN_DEBUG
		printf("\x1b[32mSaturn: ");
		printf("Command Seek Security Ring: FAD = %u, track = %u", fad, this->toc.GetTrackByLBA(this->seek_lba) + 1);
		printf(" (%u)\n\x1b[0m", frame_cnt);
#endif // SATURN_DEBUG
		break;

	case SATURN_COMM_TOC:
		this->toc_pos = 0;
		this->read_toc = true;
		this->speed = comm[10] == 1 ? 1 : 2;

#ifdef SATURN_DEBUG
		printf("\x1b[32mSaturn: ");
		printf("Command TOC Read");
		printf(" (%u)\n\x1b[0m", frame_cnt);
#endif // SATURN_DEBUG
		break;

	case SATURN_COMM_STOP:
		this->stop_pend = true;
		this->read_pend = false;

#ifdef SATURN_DEBUG
		printf("\x1b[32mSaturn: ");
		printf("Command Stop");
		//printf(", last FAD = %u", last_lba + 150);
		printf(" (%u)\n\x1b[0m", frame_cnt);
#endif // SATURN_DEBUG
		break;

	case SATURN_COMM_READ:
		this->seek_lba = fad - 150 - 4;
		//if (this->toc.tracks[this->track].type) {
		//	this->lba -= 4;
		//	this->seek_lba -= 4;
		//}
		this->read_pend = true;
		this->seek_pend = true;
		this->pause_pend = false;
		this->speed = comm[10] == 1 ? 1 : 2;

		this->audioFirst = 1;

#ifdef SATURN_DEBUG
		printf("\x1b[32mSaturn: ");
		printf("Command Read Data: FAD = %u, track = %u, speed = %u", fad, this->toc.GetTrackByLBA(this->seek_lba) + 1, this->speed);
		printf(" (%u)\n\x1b[0m", frame_cnt);
#endif // SATURN_DEBUG
		break;

	case SATURN_COMM_PAUSE:
		this->pause_pend = true;
		this->read_pend = false;

#ifdef SATURN_DEBUG
		printf("\x1b[32mSaturn: ");
		printf("Command Pause");
		//printf(", last FAD = %u", last_lba + 150);
		printf(" (%u)\n\x1b[0m", frame_cnt);
#endif // SATURN_DEBUG
		break;

	case SATURN_COMM_SEEK:
		this->lba = fad - 150;
		this->seek_lba = fad - 150;
		//if (this->toc.tracks[this->track].type) {
		//	this->lba -= 4;
		//	this->seek_lba -= 4;
		//}
		this->seek_pend = true;
		this->read_pend = false;
		this->pause_pend = false;
		this->speed = comm[10] == 1 ? 1 : 2;

#ifdef SATURN_DEBUG
		printf("\x1b[32mSaturn: ");
		printf("Command Seek: FAD = %u, track = %u, speed = %u, num = %u", fad, this->toc.GetTrackByLBA(this->seek_lba) + 1, this->speed, comm[8]);
		//printf(", command = %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", comm[0], comm[1], comm[2], comm[3], comm[4], comm[5], comm[6], comm[7], comm[8], comm[9], comm[10], comm[11]);
		printf(" (%u)\n\x1b[0m", frame_cnt);
#endif // SATURN_DEBUG
		break;

	default:
		this->seek_pend = false;
		this->read_pend = false;
		this->pause_pend = false;

#ifdef SATURN_DEBUG
		printf("\x1b[32mSaturn: ");
		printf("Command undefined, command = %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", comm[0], comm[1], comm[2], comm[3], comm[4], comm[5], comm[6], comm[7], comm[8], comm[9], comm[10], comm[11]);
		printf(" (%u)\n\x1b[0m", frame_cnt);
#endif // SATURN_DEBUG
		break;
	}
}

void satcdd_t::Process(uint8_t* time_mode) {
	msf_t amsf = { 0,2,0 };
	msf_t msf = { 0,2,0 };
	uint8_t idx = 0;
	uint8_t q = this->lba < this->toc.end && this->toc.tracks[this->track].type ? 0x40 : 0x00;
	static int seek_time = 8;
	static int idle_cnt = 0;
		
	if (this->lid_open) {
		this->state = Open;

		stat[0] = SATURN_STAT_OPEN;
		stat[1] = 0x41;
		stat[2] = 0x01;
		stat[3] = 0x01;
		stat[4] = 0x00;
		stat[5] = 0x00;
		stat[6] = 0x00;
		stat[7] = 0x06;
		stat[8] = 0x00;
		stat[9] = 0x00;
		stat[10] = 0x00;

		*time_mode = 1;
	}
	else if (this->read_toc) {
		this->state = ReadTOC;

		if (toc_pos < 0x100)
		{
			int lba_ = this->toc.tracks[toc_pos & 0xFF].start + 150;
			LBAToMSF(lba_, &msf);
			idx = BCD(toc_pos + 1);
			q = this->toc.tracks[toc_pos & 0xFF].type ? 0x40 : 0x00;

			toc_pos++;
			if (toc_pos >= this->toc.last) toc_pos = 0x100;
		}
		else {
			if (toc_pos == 0x100) {//track A0
				msf.m = 1;
				msf.s = 0;
				msf.f = 0;
				idx = 0xA0;
				q = this->toc.tracks[0].type ? 0x40 : 0x00;
			}
			else if (toc_pos == 0x101) {//track A1
				msf.m = this->toc.last;
				msf.s = 0;
				msf.f = 0;
				idx = 0xA1;
				q = this->toc.tracks[this->toc.last - 1].type ? 0x40 : 0x00;
			}
			else if (toc_pos == 0x102) {//track A2
				int lba_ = this->toc.end + 150;
				LBAToMSF(lba_, &msf);
				idx = 0xA2;
				q = 0x00;

				this->read_toc = false;
			}
			toc_pos++;
		}

		stat[0] = SATURN_STAT_TOC;
		stat[1] = q | 0x01;
		stat[2] = 0x00;
		stat[3] = idx;
		stat[4] = 0x00;
		stat[5] = 0x00;
		stat[6] = 0x00;
		stat[7] = 0x06;
		stat[8] = BCD(msf.m);
		stat[9] = BCD(msf.s);
		stat[10] = BCD(msf.f);

		*time_mode = 1;

#ifdef SATURN_DEBUG
		printf("\x1b[32mSaturn: ");
		printf("Process read TOC: index = %02X, msf = %02X:%02X:%02X, q = %02X", idx, BCD(msf.m), BCD(msf.s), BCD(msf.f), q);
		printf(" (%u)\n\x1b[0m", frame_cnt);
#endif // SATURN_DEBUG
	}
	else if (this->seek_pend) {
		this->state = Seek;

		LBAToMSF(this->lba + 150, &amsf);
		if (this->lba < 0)
			LBAToMSF(-this->lba, &msf);
		else
			LBAToMSF(this->lba - this->toc.tracks[this->track].start, &msf);

		stat[0] = SATURN_STAT_SEEK;
		/*stat[1] = q | 0x01;
		stat[2] = this->lba < this->toc.end ? BCD(this->track + 1) : 0xAA;
		stat[3] = this->lba < 0 ? 0x00 : 0x01;
		stat[4] = BCD(msf.m);
		stat[5] = BCD(msf.s);
		stat[6] = BCD(msf.f);
		stat[7] = 0x06;
		stat[8] = BCD(amsf.m);
		stat[9] = BCD(amsf.s);
		stat[10] = BCD(amsf.f);*/

		if (seek_time) seek_time--;
		else {
			this->seek_pend = false;
			seek_time = 8;
		}

		*time_mode = this->speed;

#ifdef SATURN_DEBUG
		//printf("\x1b[32mSaturn: ");
		//printf("Process seek, seek fad = %i, amsf = %02X:%02X:%02X, msf = %02X:%02X:%02X, q = %02X", this->seek_lba + 150, BCD(amsf.m), BCD(amsf.s), BCD(amsf.f), BCD(msf.m), BCD(msf.s), BCD(msf.f), q | 0x01);
		//printf(" (%u)\n\x1b[0m", frame_cnt);
#endif // SATURN_DEBUG
	}
	else if (this->seek_ring) {
		this->state = SeekRing;

		int fad = this->lba + 150;

		stat[0] = SATURN_STAT_SEEK_RING;
		stat[1] = 0x00;
		stat[2] = 0x00;
		stat[3] = fad >> 16;
		stat[4] = fad >> 8;
		stat[5] = fad >> 0;
		stat[6] = 0x00;
		stat[7] = 0x00;
		stat[8] = 0x00;
		stat[9] = 0x00;
		stat[10] = 0x00;

		if (seek_time) seek_time--;
		else {
			this->seek_ring = false;
			this->seek_ring2 = true;
			seek_time = 1;
		}

		*time_mode = 1;

#ifdef SATURN_DEBUG
		//printf("\x1b[32mSaturn: ");
		//printf("Process seek ring, fad = %i", fad);
		//printf(" (%u)\n\x1b[0m", frame_cnt);
#endif // SATURN_DEBUG
	}
	else if (this->seek_ring2) {
		this->state = SeekRing;

		int fad = this->lba + 150;

		stat[0] = SATURN_STAT_SEEK_RING | 0x04;
		stat[1] = 0x48;
		stat[2] = 0x5A;
		stat[3] = fad >> 16;
		stat[4] = fad >> 8;
		stat[5] = fad >> 0;
		stat[6] = 0x0A;
		stat[7] = 0x0A;
		stat[8] = 0x09;
		stat[9] = 0x09;
		stat[10] = 0x00;

		if (seek_time) seek_time--;
		else {
			this->seek_ring2 = false;
			seek_time = 8;
		}

		*time_mode = 1;

#ifdef SATURN_DEBUG
		//printf("\x1b[32mSaturn: ");
		//printf("Process seek ring, fad = %i", fad);
		//printf(" (%u)\n\x1b[0m", frame_cnt);
#endif // SATURN_DEBUG
	}
	else if (this->read_pend) {
		this->state = Read;

		LBAToMSF(this->lba + 150, &amsf);
		if (this->lba < 0)
			LBAToMSF(-this->lba, &msf);
		else
			LBAToMSF(this->lba - this->toc.tracks[this->track].start, &msf);

		stat[0] = SATURN_STAT_DATA | (idle_cnt == 0 ? 0x00 : 0x04);
		stat[1] = q | 0x01;
		stat[2] = this->lba < this->toc.end ? BCD(this->track + 1) : 0xAA;
		stat[3] = this->lba < 0 ? 0x00 : 0x01;
		stat[4] = BCD(msf.m);
		stat[5] = BCD(msf.s);
		stat[6] = BCD(msf.f);
		stat[7] = 0x06;
		stat[8] = BCD(amsf.m);
		stat[9] = BCD(amsf.s);
		stat[10] = BCD(amsf.f);

		if (++idle_cnt == 10) idle_cnt = 0;

		*time_mode = this->speed;

#ifdef SATURN_DEBUG
		//printf("\x1b[32mSaturn: ");
		//printf("Process read data, fad = %i, msf = %02X:%02X:%02X, q = %02X", this->lba + 150, BCD(msf.m), BCD(msf.s), BCD(msf.f), q);
		//printf(" (%u)\n\x1b[0m", frame_cnt);
#endif // SATURN_DEBUG
	}
	else if (this->pause_pend) {
		this->state = Idle;

		stat[0] = SATURN_STAT_IDLE | 0x04;
		stat[1] = 0x41;
		stat[2] = 0x01;
		stat[3] = 0x01;
		stat[4] = 0x00;
		stat[5] = 0x00;
		stat[6] = 0x00;
		stat[7] = 0x06;
		stat[8] = 0x00;
		stat[9] = 0x00;
		stat[10] = 0x00;

		this->pause_pend = false;

		*time_mode = 1;

#ifdef SATURN_DEBUG
		LBAToMSF(this->lba + 150, &amsf);
		printf("\x1b[32mSaturn: ");
		printf("Pause, fad = %i, msf = %02X:%02X:%02X, q = %02X", this->lba + 150, BCD(amsf.m), BCD(amsf.s), BCD(amsf.f), q);
		printf(" (%u)\n\x1b[0m", frame_cnt);
#endif // SATURN_DEBUG
	}
	else if (this->stop_pend) {
		this->state = Stop;

		stat[0] = SATURN_STAT_STOP;
		stat[1] = 0x41;
		stat[2] = 0x01;
		stat[3] = 0x01;
		stat[4] = 0x00;
		stat[5] = 0x00;
		stat[6] = 0x00;
		stat[7] = 0x06;
		stat[8] = 0x00;
		stat[9] = 0x00;
		stat[10] = 0x00;

		this->stop_pend = false;

		*time_mode = 1;
	}
	else {
		this->state = Idle;

		LBAToMSF(this->lba + 150, &amsf);
		if (this->lba < 0)
			LBAToMSF(-this->lba, &msf);
		else
			LBAToMSF(this->lba - this->toc.tracks[this->track].start, &msf);

		stat[0] = SATURN_STAT_IDLE | (idle_cnt == 0 ? 0x00 : 0x04);
		stat[1] = q | 0x01;
		stat[2] = this->lba < this->toc.end ? BCD(this->track + 1) : 0xAA;
		stat[3] = this->lba < 0 ? 0x00 : 0x01;
		stat[4] = BCD(msf.m);
		stat[5] = BCD(msf.s);
		stat[6] = BCD(msf.f);
		stat[7] = 0x06;
		stat[8] = BCD(amsf.m);
		stat[9] = BCD(amsf.s);
		stat[10] = BCD(amsf.f);

		if (++idle_cnt == 10) idle_cnt = 0;

		*time_mode = 1;
	}

	SetChecksum(stat);
}

void satcdd_t::Update() {
	msf_t msf = { 0,2,0 };

	switch (this->state)
	{
	case Idle:
		//this->lba++;
		//if (this->lba > this->seek_lba) this->lba -= 4;
		break;

	case Open:
	case ReadTOC:
		break;

	case Read:
		LBAToMSF(this->lba + 150, &msf);

		if (lba >= this->toc.end)
		{
			// CD-ROM Security Ring Data (Mode 2)
			uint8_t header[12];
			header[0] = BCD(msf.m);
			header[1] = BCD(msf.s);
			header[2] = BCD(msf.f);
			header[3] = 0x02;
			header[4] = 0x00;
			header[5] = 0x00;
			header[6] = 0x28;
			header[7] = 0x00;
			header[8] = 0x00;
			header[9] = 0x00;
			header[10] = 0x28;
			header[11] = 0x00;

			RingDataSend(header, this->speed);

#ifdef SATURN_DEBUG
			//printf("\x1b[32mSaturn: ");
			//printf("Read ring data, fad = %i, msf = %02X:%02X:%02X", this->lba + 150, BCD(msf.m), BCD(msf.s), BCD(msf.f));
			//printf(" (%u)\n\x1b[0m", frame_cnt);
#endif // SATURN_DEBUG
		}
		else if (this->toc.tracks[this->track].type)
		{
			// CD-ROM Data (Mode 1/2)
			uint8_t header[4];

			if (this->sectorSize == 2048 || (this->lba - this->toc.tracks[this->track].start) < 0) {
				header[0] = BCD(msf.m);
				header[1] = BCD(msf.s);
				header[2] = BCD(msf.f);
				header[3] = (uint8_t)this->toc.tracks[this->track].type;
				DataSectorSend(header, this->speed);
			}
			else {
				DataSectorSend(0, this->speed);
			}

#ifdef SATURN_DEBUG
			/*if (this->toc.tracks[this->track].type == 2) {
				printf("\x1b[32mSaturn: ");
				printf("Update read data, track = %i, lba = %i, msf = %02X:%02X:%02X, mode = %u", this->track + 1, this->lba + 150, BCD(msf.m), BCD(msf.s), BCD(msf.f), this->toc.tracks[this->track].type);
				printf(" (%u)\n\x1b[0m", frame_cnt);
			}*/
#endif // SATURN_DEBUG
		}
		else
		{
			if (this->lba >= this->toc.tracks[this->track].start)
			{
				//this->isData = 0x00;
			}
			AudioSectorSend();
		}

#ifdef SATURN_DEBUG
		//printf("\x1b[32mSaturn: ");
		//printf("Update read data, lba = %i, msf = %u:%u:%u", this->lba, msf.m, msf.s, msf.f);
		//printf(" (%u)\n\x1b[0m", frame_cnt);
#endif // SATURN_DEBUG

		this->lba++;
		this->track = this->toc.GetTrackByLBA(this->lba);
		//this->chd_audio_read_lba++;

		break;

	case Pause:
	case Stop:
		break;

	case Seek:
	case SeekRead:
		if (!this->seek_pend) {
			this->lba = this->seek_lba;
			this->track = this->toc.GetTrackByLBA(this->lba);
		}

		LBAToMSF(this->lba + 150, &msf);


#ifdef SATURN_DEBUG
		//printf("\x1b[32mSaturn: ");
		//printf("Update seek, lba = %i, seek_lba = %i, msf = %u:%u:%u", this->lba, this->seek_lba, msf.m, msf.s, msf.f);
		//printf(" (%u)\n\x1b[0m", frame_cnt);
#endif // SATURN_DEBUG
		break;

	case SeekRing:
		this->lba = this->seek_lba;
		break;
	}
}


void satcdd_t::SetChecksum(uint8_t* stat) {
	uint8_t sum = 0;
	for (int i = 0; i < 11; i++) sum += stat[i];
	stat[11] = ~sum;
}

void satcdd_t::LBAToMSF(int lba, msf_t* msf) {
	msf->m = (lba / 75) / 60;
	msf->s = (lba / 75) % 60;
	msf->f = (lba % 75);
}

int satcdd_t::GetFAD(uint8_t* cmd) {
	int fad = 0;
	fad |= cmd[3] << 0;
	fad |= cmd[2] << 8;
	fad |= cmd[1] << 16;
	return fad;
}

uint8_t* satcdd_t::GetStatus() {
	return this->stat;
}

int satcdd_t::SetCommand(uint8_t* data) {
	memcpy(this->comm, data, 12);
	return 0;
}

void satcdd_t::MakeSecureRingData(uint8_t *buf) {
	int i, j;
	uint16_t lfsr = 1;
	uint8_t a;
	for (i = 12; i < 2348; i++)
	{
		a = (i & 1) ? 0x59 : 0xa8;
		for (j = 0; j < 8; j++)
		{
			a ^= (lfsr & 1);
			a = (a >> 1) | (a << (7));

			uint16_t x = (lfsr >> 1) ^ lfsr;
			lfsr |= x << 15;
			lfsr >>= 1;
		}
		buf[i] = a;
	}
}

void satcdd_t::ReadData(uint8_t *buf)
{
	int offs = 0; 
	
	if (this->toc.tracks[this->track].type)
	{
		int lba_ = this->lba >= 0 ? this->lba : 0;
		/*if (this->toc.chd_f)
		{
			int read_offset = 0;
			if (this->sectorSize != 2048)
			{
				read_offset += 16;
			}

			mister_chd_read_sector(this->toc.chd_f, this->lba + this->toc.tracks[this->track].offset, 0, read_offset, 2048, buf, this->chd_hunkbuf, &this->chd_hunknum);
		}
		else*/ {
			if (this->sectorSize == 2048)
			{
				offs = (lba_ * 2048) - this->toc.tracks[this->track].offset;
				FileSeek(&this->toc.tracks[this->track].f, offs, SEEK_SET);
				FileReadAdv(&this->toc.tracks[this->track].f, buf + 16, 2048);
			}
			else {
				offs = (lba_ * 2352) - this->toc.tracks[this->track].offset;
				FileSeek(&this->toc.tracks[this->track].f, offs, SEEK_SET);
				FileReadAdv(&this->toc.tracks[this->track].f, buf, 2352);
			}
		}
	}
}

int satcdd_t::ReadCDDA(uint8_t *buf)
{
	this->audioLength = 2352;
	if (this->audioFirst) this->audioLength += 4096 - 2352;

#ifdef SATURN_DEBUG
	//printf("\x1b[32mMCD: AUDIO LENGTH %d LBA: %d INDEX: %d START: %d END %d\n\x1b[0m", this->audioLength, this->lba, this->track, this->toc.tracks[this->track].start, this->toc.tracks[this->track].end);
#endif // SATURN_DEBUG

	/*if (this->isData)
	{
		return this->audioLength;
	}*/

	int offs = 0;
	/*if (this->toc.chd_f)
	{
		for (int i = 0; i < this->audioLength / 2352; i++)
		{
			mister_chd_read_sector(this->toc.chd_f, this->chd_audio_read_lba + this->toc.tracks[this->track].offset, 2352 * i, 0, 2352, buf, this->chd_hunkbuf, &this->chd_hunknum);
		}

		//CHD audio requires byteswap. There's probably a better way to do this...

		for (int swapidx = 0; swapidx < this->audioLength; swapidx += 2)
		{
			uint8_t temp = buf[swapidx];
			buf[swapidx] = buf[swapidx + 1];
			buf[swapidx + 1] = temp;
		}

		if ((this->audioLength / 2352) > 1)
		{
			this->chd_audio_read_lba++;
		}

	}
	else*/ if (this->toc.tracks[this->track].f.opened()) {
		offs = (this->lba * 2352) - this->toc.tracks[this->track].offset;
		if (!this->audioFirst) offs += 4096 - 2352;
		FileSeek(&this->toc.tracks[this->track].f, offs, SEEK_SET);
		FileReadAdv(&this->toc.tracks[this->track].f, buf, this->audioLength);
	}

#ifdef SATURN_DEBUG
	//printf("\x1b[32mSaturn: ");
	//printf("Read CD DA sector: Length = %u, First = %u, offset = %u", this->audioLength, this->audioFirst, offs);
	//printf(" (%u)\n\x1b[0m", frame_cnt);
#endif // SATURN_DEBUG

	this->audioFirst = 0;

	return this->audioLength;
}

int satcdd_t::DataSectorSend(uint8_t* header, int speed)
{
	int len = 2352;
	uint8_t* data_ptr = cd_buf + 2;

	if (header) {
		ReadData(data_ptr);
		memcpy(data_ptr + 12 , header, 4);
	}
	else {
		ReadData(data_ptr);
	}
	cd_buf[0] = cd_buf[1] = (speed == 2 ? 0x01 : 0x00);

	if (SendData)
		return SendData(cd_buf, len + 2, CD_DATA_IO_INDEX);

	return 0;
}

int satcdd_t::AudioSectorSend()
{
	int len = 2352;
	uint8_t* data_ptr = cd_buf + 2;

	len = ReadCDDA(data_ptr);
	cd_buf[0] = cd_buf[1] = 0x02;

	if (SendData)
		return SendData(cd_buf, len + 2, CD_DATA_IO_INDEX);

	return 0;
}

int satcdd_t::RingDataSend(uint8_t* header, int speed)
{
	uint8_t* data_ptr = cd_buf + 2;

	if (header) {
		MakeSecureRingData(data_ptr);
		memcpy(data_ptr + 12, header, 12);
		memset(data_ptr + 2348, 0, 4);
	}
	cd_buf[0] = cd_buf[1] = (speed == 2 ? 1 : 0);

	if (SendData)
		return SendData(cd_buf, 2352 + 2, CD_DATA_IO_INDEX);

	return 0;
}


