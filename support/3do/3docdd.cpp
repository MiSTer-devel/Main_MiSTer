#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>

#include "3do.h"
#include "../chd/mister_chd.h"
#include "../../hardware.h"

p3docdd_t p3docdd;

p3docdd_t::p3docdd_t() {
	loaded = 0;
	state = P3DO_Open;
	lid_open = false;
	stop_pend = false;
	read_pend = false;
	da_clock = false;
	read_toc = false;
	track = 0;
	lba = 0;
	speed = 0;
	read_fmt = P3DO_FMT_DATA;
	read_sub = false;
	memset(audio_le, 0, sizeof(audio_le));
	memset(track_flags, 0, sizeof(track_flags));
	chd_hunkbuf = NULL;
	chd_hunknum = -1;
	SendData = NULL;
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

int p3docdd_t::LoadCUE(const char* filename) {
	static char fname[1024 + 10];
	static char line[256];
	char *ptr, *lptr;
	static char cue[100 * 1024];
	int new_file = 0;
	int file_size = 0;

	strcpy(fname, filename);

	memset(cue, 0, sizeof(cue));
	if (!FileLoad(fname, cue, sizeof(cue) - 1)) return 1;

#ifdef P3DO_DEBUG
	printf("\x1b[32m3DO: Open CUE: %s\n\x1b[0m", fname);
#endif // P3DO_DEBUG

	this->toc.last = -1;
	int idx, mm, ss, bb, pregap = 0;
	bool file_le = true;

	char *buf = cue;
	while (sgets(line, sizeof(line), &buf))
	{
		lptr = line;
		while (*lptr == 0x20) lptr++;

		/* decode FILE commands */
		if (!(memcmp(lptr, "FILE", 4)))
		{
			if (this->toc.last == 99) break;

			ptr = fname + strlen(fname) - 1;
			while ((ptr - fname) && (*ptr != '/') && (*ptr != '\\')) ptr--;
			if (ptr - fname) ptr++;

			lptr += 4;
			while (*lptr == 0x20) lptr++;

			if (*lptr == '\"')
			{
				lptr++;
				while ((*lptr != '\"') && (lptr <= (line + 256)) && (ptr < (fname + 1023)))
					*ptr++ = *lptr++;
			}
			else
			{
				while ((*lptr != 0x20) && (lptr <= (line + 256)) && (ptr < (fname + 1023)))
					*ptr++ = *lptr++;
			}
			*ptr = 0;

			if (!FileOpen(&this->toc.tracks[this->toc.last + 1].f, fname)) return -1;
			FileSeek(&this->toc.tracks[this->toc.last + 1].f, 0, SEEK_SET);
			file_size = this->toc.tracks[this->toc.last + 1].f.size;

#ifdef P3DO_DEBUG
			printf("\x1b[32m3DO: Open track file: %s\n\x1b[0m", fname);
#endif // P3DO_DEBUG

			pregap = 0;

			this->toc.tracks[this->toc.last + 1].offset = 0;

			if (!strstr(lptr, "BINARY") && !strstr(lptr, "MOTOROLA") && !strstr(lptr, "WAVE"))
			{
				FileClose(&this->toc.tracks[this->toc.last + 1].f);
#ifdef P3DO_DEBUG
				printf("\x1b[32m3DO: unsupported file: %s\n\x1b[0m", fname);
#endif // P3DO_DEBUG

				return -1;
			}
			file_le = !strstr(lptr, "MOTOROLA");
		}

		/* decode TRACK commands */
		else if ((sscanf(lptr, "TRACK %02d %*s", &bb)) || (sscanf(lptr, "TRACK %d %*s", &bb)))
		{
			if (this->toc.last == 99) break;
			this->toc.last++;
			this->audio_le[this->toc.last] = file_le;

			if (bb != (this->toc.last + 1))
			{
				FileClose(&this->toc.tracks[this->toc.last].f);
#ifdef P3DO_DEBUG
				printf("\x1b[32m3DO: missing tracks: %s\n\x1b[0m", fname);
#endif // P3DO_DEBUG
				break;
			}

			if (strstr(lptr, "MODE1/2048"))
			{
				this->sectorSize = 2048;
				this->toc.tracks[this->toc.last].type = TT_MODE1;
			}
			else if (strstr(lptr, "MODE1/2352"))
			{
				this->sectorSize = 2352;
				this->toc.tracks[this->toc.last].type = TT_MODE1;

				//FileSeek(&this->toc.tracks[0].f, 0x10, SEEK_SET);
			}
			else if (strstr(lptr, "MODE2/2352"))
			{
				this->sectorSize = 2352;
				this->toc.tracks[this->toc.last].type = TT_MODE2;

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

			new_file = 1;
			if (!this->toc.tracks[this->toc.last].f.opened())
			{
				FileOpen(&this->toc.tracks[this->toc.last].f, fname);
				new_file = 0;
			}

#ifdef P3DO_DEBUG
			printf("\x1b[32m3DO: track = %u, type = %u\n\x1b[0m", this->toc.last + 1, (int)this->toc.tracks[this->toc.last].type);
#endif // P3DO_DEBUG
		}

		/* decode FLAGS commands (track control bits) */
		else if (!memcmp(lptr, "FLAGS", 5))
		{
			if (this->toc.last >= 0)
			{
				uint8_t fl = 0;
				if (strstr(lptr, "PRE")) fl |= 0x01;
				if (strstr(lptr, "DCP")) fl |= 0x02;
				if (strstr(lptr, "4CH")) fl |= 0x08;
				this->track_flags[this->toc.last] = fl;
			}
		}

		/* decode PREGAP commands */
		else if (sscanf(lptr, "PREGAP %02d:%02d:%02d", &mm, &ss, &bb) == 3)
		{
			this->toc.tracks[this->toc.last].pregap = bb + ss * 75 + mm * 60 * 75;
			pregap += this->toc.tracks[this->toc.last].pregap;
		}

		/* decode INDEX commands */
		else if ((sscanf(lptr, "INDEX %02d %02d:%02d:%02d", &idx, &mm, &ss, &bb) == 4) ||
			(sscanf(lptr, "INDEX %01d %02d:%02d:%02d", &idx, &mm, &ss, &bb) == 4))
		{
			int idx_pos = bb + ss * 75 + mm * 60 * 75;
			if (idx == 0) {
				if (this->toc.last && !this->toc.tracks[this->toc.last - 1].end)
				{
					this->toc.tracks[this->toc.last - 1].end = pregap;
				}
			}
			else if (idx == 1) {
				this->toc.tracks[this->toc.last].offset += pregap * 2352;

				if (!new_file)
				{
					this->toc.tracks[this->toc.last].start = idx_pos + pregap;
					if (this->toc.last && !this->toc.tracks[this->toc.last - 1].end)
					{
						this->toc.tracks[this->toc.last - 1].end = this->toc.tracks[this->toc.last].start - this->toc.tracks[this->toc.last].pregap;
#ifdef P3DO_DEBUG
						printf("\x1b[32m3DO: track = %u, start = %u, end = %u\n\x1b[0m", this->toc.last - 1 + 1, this->toc.tracks[this->toc.last - 1].start, this->toc.tracks[this->toc.last - 1].end);
#endif // P3DO_DEBUG
					}
				}
				else
				{
					this->toc.tracks[this->toc.last].start = this->toc.end + pregap;
					this->toc.tracks[this->toc.last].offset += this->toc.end * 2352;

					int sectorSize = 2352;
					if (this->toc.tracks[this->toc.last].type != TT_CDDA) sectorSize = this->sectorSize;
					this->toc.tracks[this->toc.last].end = this->toc.tracks[this->toc.last].start + ((file_size + sectorSize - 1) / sectorSize);

					this->toc.end = this->toc.tracks[this->toc.last].end;
#ifdef P3DO_DEBUG
					printf("\x1b[32m3DO: track = %u, start = %u, end = %u\n\x1b[0m", this->toc.last + 1, this->toc.tracks[this->toc.last].start, this->toc.tracks[this->toc.last].end);
#endif // P3DO_DEBUG
				}

#ifdef P3DO_DEBUG
				printf("\x1b[32m3DO: track = %u, offset = %u\n\x1b[0m", this->toc.last + 1, this->toc.tracks[this->toc.last].offset);
#endif // P3DO_DEBUG
			}

			if (idx == 0) {
				this->toc.tracks[this->toc.last].indexes[idx] = 0;
			}
			else {
				if (!new_file)
				{
					this->toc.tracks[this->toc.last].indexes[idx] = idx_pos - this->toc.tracks[this->toc.last].start;
				}
				else
				{
					this->toc.tracks[this->toc.last].indexes[idx] = idx_pos;
				}
			}
			this->toc.tracks[this->toc.last].index_num = idx + 1;
#ifdef P3DO_DEBUG
			//printf("\x1b[32mSaturn: index = %u, pos = %u\n\x1b[0m", idx, this->toc.tracks[this->toc.last].indexes[idx]);
#endif // P3DO_DEBUG
		}
	}
	this->toc.last++;

	if (this->toc.last && !this->toc.tracks[this->toc.last - 1].end)
	{
		this->toc.end += pregap;
		this->toc.tracks[this->toc.last - 1].end = this->toc.end;
	}

	return 0;
}

int p3docdd_t::LoadISO(const char* filename) {
	static char fname[1024 + 10];
	strcpy(fname, filename);

	if (!FileOpen(&this->toc.tracks[0].f, filename)) return -1;
	FileSeek(&this->toc.tracks[0].f, 0, SEEK_SET);
	int file_size = this->toc.tracks[0].f.size;

	this->toc.tracks[0].start = 0;
	this->toc.tracks[0].offset = 0;
	this->toc.end = this->toc.tracks[0].end = file_size / 2048;
	this->toc.sectorSize = this->toc.tracks[0].sector_size = 2048;
	this->toc.tracks[0].type = TT_MODE1;
	this->toc.last = 1;

#ifdef P3DO_DEBUG
	printf("\x1b[32m3DO: track = %u, start = %u, end = %u\n\x1b[0m", this->toc.last, this->toc.tracks[this->toc.last - 1].start, this->toc.tracks[this->toc.last - 1].end);
#endif // P3DO_DEBUG

	return 0;
}

int p3docdd_t::Load(const char *filename)
{
	Unload();

	const char *ext = filename + strlen(filename) - 4;
	if (!strncasecmp(".cue", ext, 4))
	{
		if (LoadCUE(filename)) {
			return (-1);
		}
	}
	else if (!strncasecmp(".iso", ext, 4)) {
		if (LoadISO(filename)) {
			return (-1);
		}

		if (this->toc.tracks[0].sector_size)
		{
			this->sectorSize = this->toc.tracks[0].sector_size;
		}
	}
	else if (!strncasecmp(".chd", ext, 4)) {
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

		this->chd_hunkbuf = (uint8_t *)malloc(this->toc.chd_hunksize);
		this->chd_hunknum = -1;
		if (this->toc.tracks[0].sector_size)
		{
			this->sectorSize = this->toc.tracks[0].sector_size;
		}
	}
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

#ifdef P3DO_DEBUG
	printf("\x1b[32m3DO: Sector size = %u, Track 1 end = %u\n\x1b[0m", this->sectorSize, this->toc.tracks[0].end);
#endif // P3DO_DEBUG

	if (this->toc.last)
	{
		this->toc.tracks[this->toc.last].start = this->toc.end;
		this->loaded = 1;
		this->lid_open = false;
		this->stop_pend = true;

#ifdef P3DO_DEBUG
		printf("\x1b[32m3DO: CD mounted, last track = %u\n\x1b[0m", this->toc.last);
#endif // P3DO_DEBUG

		return 1;
	}

	return 0;
}

void p3docdd_t::Unload()
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

		this->loaded = 0;
	}

	memset(&this->toc, 0x00, sizeof(this->toc));
	memset(this->audio_le, 0, sizeof(this->audio_le));
	memset(this->track_flags, 0, sizeof(this->track_flags));
	this->sectorSize = 0;

#ifdef P3DO_DEBUG
	printf("\x1b[32m3DO: ");
	printf("Unload");
	printf("\n\x1b[0m");
#endif // P3DO_DEBUG
}

int p3docdd_t::GetDiscInfo(uint8_t *buf) {
	if (this->toc.last < 0) return -1;
	
	memset(buf, 0x00, 2048);

	msf_t msf = { 0,2,0 };
	LBAToMSF(this->toc.end + 150, &msf);

	buf[1] = P3DO_DISC_DA_OR_CDROM;	//disc id
	buf[2] = 0x01;	//first track
	buf[3] = this->toc.last;	//last track
	buf[4] = msf.m;	//mm
	buf[5] = msf.s;	//ss
	buf[6] = msf.f;	//ff
	
	for (int i = 0, offs = 0x10; i <= this->toc.last; i++, offs+=0x10)
	{
		LBAToMSF(this->toc.tracks[i].start + 150, &msf);

		buf[offs + 1] = 0;
		buf[offs + 2] = 0x10 | TrackControl(i);	// ADR 1 (position) in the high nibble, control bits in the low one
		buf[offs + 3] = i + 1;
		buf[offs + 4] = 0;
		buf[offs + 5] = msf.m;    //min
		buf[offs + 6] = msf.s;    //sec
		buf[offs + 7] = msf.f;    //frames
		buf[offs + 8] = 0;
	}

	this->lba = 0;
	ReadData(cd_buf);
	memcpy(buf+1024, cd_buf, 128);

	return 1;
}

void p3docdd_t::Reset() {
	state = P3DO_Open;
	lid_open = false;
	stop_pend = false;
	read_pend = false;
	da_clock = false;
	read_toc = false;
	track = 0;
	lba = 0;
	speed = 0;
	p3docdd.SendData = 0;

#ifdef P3DO_DEBUG
	printf("\x1b[32m3DO: ");
	printf("Reset");
	printf("\n\x1b[0m");
#endif // P3DO_DEBUG
}

void p3docdd_t::CommandExec() {
	int cmd_lba = GetLBAFromCommand(comm);
	int cmd_blocks = GetBlocksFromCommand(comm);

	switch (comm[0]) {
	case P3DO_COMM_NOP:
#ifdef P3DO_DEBUG
		//printf("\x1b[32m3DO: ");
		//printf("Command Nop");
		//printf(" (%u)\n\x1b[0m", p3do_frame_cnt);
#endif // P3DO_DEBUG
		break;

	case P3DO_COMM_READ:
		this->lba = cmd_lba - 150;

		this->track = this->toc.GetTrackByLBA(this->lba);

		this->read_pend = true;
		this->block_reads = cmd_blocks;
		this->block_count = 0;
		this->speed = 2;
		this->read_fmt = comm[7] & 3;
		this->read_sub = (comm[7] & 4) != 0;

		// A drive reads CD-DA at the speed of the disc (75 sectors per second at single speed), a
		// little ahead of it at most: the 3DO CD player depends on it. A READ that starts where the
		// previous one ended continues the same clock.
		if (this->toc.tracks[this->track].type == TT_CDDA && this->read_fmt != P3DO_FMT_DATA) {
			if (!this->da_clock || this->lba != this->da_next_lba) {
				this->da_base_lba = this->lba;
				this->da_base_ms = GetTimer(0);
				this->da_clock = true;
			}
			this->da_next_lba = this->lba + cmd_blocks;
		}
		else {
			this->da_clock = false;
		}

#ifdef P3DO_DEBUG
		//printf("\x1b[32m3DO: ");
		//printf("Command = %02X %02X %02X %02X %02X %02X %02X %02X", comm[0], comm[1], comm[2], comm[3], comm[4], comm[5], comm[6], comm[7]);
		//printf("\n\x1b[0m");
		printf("\x1b[32m3DO: ");
		printf("Command Read Data: cmd_lba = %u, cmd_blocks = %u, track = %u, track_start = %u, speed = %u", cmd_lba, cmd_blocks, this->track + 1, this->toc.tracks[this->track].start, this->speed);
		printf(" (%u)\n\x1b[0m", p3do_frame_cnt);
#endif // P3DO_DEBUG 
		break;

	case P3DO_COMM_NEXT:
		if (this->block_reads > 0) this->read_pend = true;

#ifdef P3DO_DEBUG
		//printf("\x1b[32m3DO: ");
		//printf("Command Next Data");
		//printf(" (%u)\n\x1b[0m", p3do_frame_cnt);
#endif // P3DO_DEBUG 
		break;

	default:
		this->read_pend = false;
		this->da_clock = false;

#ifdef P3DO_DEBUG
		printf("\x1b[32m3DO: ");
		printf("Command undefined, command = %02X %02X %02X %02X %02X %02X %02X %02X", comm[0], comm[1], comm[2], comm[3], comm[4], comm[5], comm[6], comm[7]);
		printf(" (%u)\n\x1b[0m", p3do_frame_cnt);
#endif // P3DO_DEBUG
		break;
	}
}

void p3docdd_t::Process(uint8_t* time_mode) {
	if (this->lid_open) {
		this->state = P3DO_Open;

		*time_mode = 1;

#ifdef P3DO_DEBUG
		printf("\x1b[32m3DO: ");
		printf("Lid open");
		printf(" (%u)\n\x1b[0m", p3do_frame_cnt);
#endif // P3DO_DEBUG
	}
	else if (this->read_pend && this->da_clock &&
	         this->lba >= this->da_base_lba + P3DO_DA_READ_AHEAD +
	                      (int)((GetTimer(0) - this->da_base_ms) * P3DO_DA_SECTORS_PER_S / 1000)) {
		// next audio sector not due yet: it is sent by a later poll (read_pend kept)
		this->state = P3DO_Idle;

		*time_mode = 1;
	}
	else if (this->read_pend) {
		this->state = P3DO_Read;

		*time_mode = this->speed;

		/*if (this->block_count >= 16)*/ this->read_pend = false;

#ifdef P3DO_DEBUG
		//printf("\x1b[32m3DO: ");
		//printf("Process read data, track = %i, lba = %i, amsf = %02X:%02X:%02X, msf = %02X:%02X:%02X", this->track + 1, this->lba, BCD(amsf.m), BCD(amsf.s), BCD(amsf.f), BCD(msf.m), BCD(msf.s), BCD(msf.f));
		//printf(" (%u)\n\x1b[0m", p3do_frame_cnt);
#endif // P3DO_DEBUG
	}
	else {
		this->state = P3DO_Idle;

		*time_mode = 1;
	}
}

void p3docdd_t::Update() {
	msf_t msf = { 0,2,0 };

	switch (this->state)
	{
	case P3DO_Idle:
		this->track = this->toc.GetTrackByLBA(this->lba);
		break;

	case P3DO_Open:
		break;

	case P3DO_Read:
		LBAToMSF(this->lba + 150, &msf);

		if (this->read_fmt != P3DO_FMT_DATA || this->read_sub || this->toc.tracks[this->track].type != TT_MODE1)
		{
			// Raw blocks (CD-DA, 2352/2336/2340 bytes), subcode, or user data of a Mode 2 track
			RawSectorSend();
		}
		else if (this->toc.tracks[this->track].type == TT_MODE1)
		{
			// CD-ROM Data (Mode 1/2)
			uint8_t header[16];

#ifdef P3DO_DEBUG
			//printf("\x1b[32m3DO: ");
			//printf("Update read data, track = %i, lba = %i, msf = %02X:%02X:%02X, mode = %u", this->track + 1, this->lba + 150, BCD(msf.m), BCD(msf.s), BCD(msf.f), this->toc.tracks[this->track].type);
			//printf("\n\x1b[0m");
#endif // P3DO_DEBUG

			if (this->sectorSize == 2048 || (this->lba - this->toc.tracks[this->track].start) < 0) {
				header[0] = 0x00;
				header[1] = header[2] = header[3] = header[4] = header[5] = header[6] = header[7] = header[8] = header[9] = header[10] = 0xFF;
				header[11] = 0x00;
				header[12] = BCD(msf.m);
				header[13] = BCD(msf.s);
				header[14] = BCD(msf.f);
				header[15] = (uint8_t)(this->toc.tracks[this->track].type != TT_CDDA);
				DataSectorSend(header);
			}
			else {
				DataSectorSend(0);
			}
		}

#ifdef P3DO_DEBUG
		//printf("\x1b[32m3DO: ");
		//printf("Update read data, lba = %i, blocks = %i msf = %u:%u:%u", this->lba, this->block_reads, msf.m, msf.s, msf.f);
		//printf(" (%u)\n\x1b[0m", p3do_frame_cnt);
#endif // P3DO_DEBUG

		this->lba++;
		this->track = this->toc.GetTrackByLBA(this->lba);
		this->block_reads--;
		this->block_count++;
		//if (this->block_count == 16 || this->block_reads == 0) this->read_pend = false;
		break;

	case P3DO_Pause:
	case P3DO_Stop:
		this->track = this->toc.GetTrackByLBA(this->lba);
		break;
	}
}

void p3docdd_t::LBAToMSF(int lba, msf_t* msf) {
	msf->m = (lba / 75) / 60;
	msf->s = (lba / 75) % 60;
	msf->f = (lba % 75);
}

int p3docdd_t::MSFToLBA(msf_t* msf) {
	return (int)(msf->m) * 60 * 75 + (int)(msf->s) * 75 + (int)(msf->f);
}

// READ DATA (MKE drive, as sent by the Portfolio CD-ROM driver): bytes 1-3 = start MSF,
// bytes 4-6 = block count. Byte 4 is the top byte of the count, not an address mode flag.
int p3docdd_t::GetLBAFromCommand(uint8_t* cmd) {
	msf_t msf = { cmd[1],cmd[2],cmd[3] };
	return MSFToLBA(&msf);
}

int p3docdd_t::GetBlocksFromCommand(uint8_t* cmd) {
	int num = 0;
	num |= cmd[6] << 0;
	num |= cmd[5] << 8;
	num |= cmd[4] << 16;

	return num;
}

uint8_t* p3docdd_t::GetStatus() {
	return this->stat;
}

int p3docdd_t::SetCommand(uint8_t* data) {
	memcpy(this->comm, data, 8);
	return 0;
}

void p3docdd_t::ReadData(uint8_t *buf)
{
	int offs = 0; 
	
	if (this->toc.tracks[this->track].type == TT_MODE1)
	{
		int lba_ = this->lba >= 0 ? this->lba : 0;
		if (this->toc.chd_f)
		{
			int read_offset = 0;
			if (this->sectorSize == 2048)
			{
				read_offset += 16;
			}

			mister_chd_read_sector(this->toc.chd_f, lba_ + this->toc.tracks[this->track].offset, read_offset, 0, this->sectorSize, buf, this->chd_hunkbuf, &this->chd_hunknum);
		}
		else {
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
#ifdef P3DO_DEBUG
			//printf("\x1b[32m3DO: ");
			//printf("Read data, lba = %i, track = %i, offset = %i", lba_, this->track, offs);
			//printf(" (%u)\n\x1b[0m", p3do_frame_cnt);
#endif // P3DO_DEBUG
		}
	}
}

int p3docdd_t::DataSectorSend(uint8_t* header)
{
	if (header) {
		ReadData(cd_buf);
		memcpy(cd_buf, header, 16);
	}
	else {
		ReadData(cd_buf);
	}

	uint16_t crc = 0;
	for (int i = 0; i < 2048; i++)
	{
		crc += cd_buf[i + 16];
	}
	cd_buf[16 + 2048 + 0] = crc & 0xFF;
	cd_buf[16 + 2048 + 1] = crc >> 8;

	if (SendData)
		return SendData(cd_buf + 16, 2048 + 2, CD_DATA_IO_INDEX);

	return 0;
}

static void p3do_swap_samples(uint8_t *buf)
{
	for (int i = 0; i < 2352; i += 2)
	{
		uint8_t t = buf[i];
		buf[i] = buf[i + 1];
		buf[i + 1] = t;
	}
}

// The raw 2352-byte sector at this->lba, as the drive returns it in raw or digital audio mode.
// CD-DA samples go to the 3DO in disc order (little-endian), as the drive returns them: the BIOS
// audio CD player swaps the bytes itself (fixedstereo16swap.dsp). BIN/WAVE tracks are sent as they
// are; MOTOROLA tracks and CHD audio (stored big-endian) are byte-swapped. Sectors of 2048/2336-byte
// images get a sync pattern and a header, with zero EDC/ECC. Gaps and the lead-out read as silence.
void p3docdd_t::ReadRawSector(uint8_t *buf)
{
	cd_track_t *trk = &this->toc.tracks[this->track];
	int lba = this->lba;

	memset(buf, 0, 2352);
	if (this->track >= this->toc.last || lba < trk->start || lba >= trk->end) return;

	int size = (trk->type == TT_CDDA) ? 2352 : (trk->sector_size ? trk->sector_size : this->sectorSize);
	int offs = 0;
	if (trk->type != TT_CDDA && size != 2352)
	{
		msf_t msf;
		LBAToMSF(lba + 150, &msf);
		memset(buf + 1, 0xFF, 10);
		buf[12] = BCD(msf.m);
		buf[13] = BCD(msf.s);
		buf[14] = BCD(msf.f);
		buf[15] = (trk->type == TT_MODE2) ? 2 : 1;
		offs = 16;
	}
	if (size <= 0 || size + offs > 2352) size = 2352 - offs;

	if (this->toc.chd_f)
	{
		mister_chd_read_sector(this->toc.chd_f, lba + trk->offset, offs, 0, size, buf, this->chd_hunkbuf, &this->chd_hunknum);
		if (trk->type == TT_CDDA) p3do_swap_samples(buf);
	}
	else
	{
		int pos = (size == 2048) ? (lba * 2048) - trk->offset : (lba * 2352) - trk->offset;
		FileSeek(&trk->f, pos, SEEK_SET);
		FileReadAdv(&trk->f, buf + offs, size);

		if (trk->type == TT_CDDA && !this->audio_le[this->track]) p3do_swap_samples(buf);
	}
}

// Control bits of a track (Q channel CONTROL field): 0x04 data, plus the CUE FLAGS (pre-emphasis,
// copy permitted, 4 channels). The lead-out (track = last) takes those of the last track.
uint8_t p3docdd_t::TrackControl(int track)
{
	if (track >= this->toc.last) track = this->toc.last - 1;
	if (track < 0) return 0;
	return ((this->toc.tracks[track].type == TT_CDDA) ? 0x00 : 0x04) | (this->track_flags[track] & 0x0B);
}

// CRC of the Q subchannel: CCITT polynomial x^16 + x^12 + x^5 + 1, initial value 0, inverted
static uint16_t p3do_subq_crc(const uint8_t *q, int len)
{
	uint16_t crc = 0;
	for (int i = 0; i < len; i++)
	{
		crc ^= (uint16_t)q[i] << 8;
		for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
	}
	return (uint16_t)~crc;
}

// The 96 subcode bytes of this->lba, as the 3DO drive returns them after CD-DA blocks of 2448/2449
// bytes: one byte per symbol, P (bit 7) masked, the Q channel in bit 6, R-W (bits 5-0) not stored.
// Q is mode 1 (position): control/ADR, track and index (BCD), relative and absolute MSF (BCD), CRC.
void p3docdd_t::MakeSubcode(uint8_t *sub)
{
	uint8_t q[12];
	int t = this->track;
	bool lead_out = t >= this->toc.last;
	int rel;
	uint8_t idx = 1;

	if (lead_out)
		rel = this->lba - this->toc.end;
	else if (this->lba < this->toc.tracks[t].start)
	{
		rel = this->toc.tracks[t].start - this->lba;	// pregap: counts down to the track start
		idx = 0;
	}
	else
		rel = this->lba - this->toc.tracks[t].start;
	if (rel < 0) rel = 0;

	msf_t r, a;
	LBAToMSF(rel, &r);
	LBAToMSF(this->lba + 150, &a);

	q[0] = (uint8_t)((TrackControl(t) << 4) | 0x01);
	q[1] = lead_out ? 0xAA : BCD(t + 1);
	q[2] = BCD(idx);
	q[3] = BCD(r.m); q[4] = BCD(r.s); q[5] = BCD(r.f);
	q[6] = 0;
	q[7] = BCD(a.m); q[8] = BCD(a.s); q[9] = BCD(a.f);
	uint16_t crc = p3do_subq_crc(q, 10);
	q[10] = crc >> 8;
	q[11] = crc & 0xFF;

	for (int i = 0; i < 96; i++)
	{
		sub[i] = ((q[i >> 3] >> (7 - (i & 7))) & 1) << 6;
	}
}

// One block in the format the core asked for (P3DO_FMT_*), the subcode if asked, then the 16-bit byte sum
int p3docdd_t::RawSectorSend()
{
	static const int fmt_len[4] = { 2048, 2352, 2336, 2340 };
	static const int fmt_skip[4] = { 16, 0, 16, 12 };

	int fmt = this->read_fmt & 3;
	enum TrackType type = this->toc.tracks[this->track].type;
	int len = fmt_len[fmt];
	int skip = fmt_skip[fmt];
	if (fmt == P3DO_FMT_DATA)
	{
		// user data: Mode 2 Form 1 data follows the 8-byte subheader; CD-DA as it is
		skip = (type == TT_MODE2) ? 24 : (type == TT_CDDA) ? 0 : 16;
	}

	ReadRawSector(cd_buf);

	uint8_t *p = cd_buf + skip;
	if (this->read_sub)
	{
		MakeSubcode(p + len);
		len += 96;
	}
	uint16_t crc = 0;
	for (int i = 0; i < len; i++)
	{
		crc += p[i];
	}
	p[len + 0] = crc & 0xFF;
	p[len + 1] = crc >> 8;

#ifdef P3DO_DEBUG
	printf("\x1b[32m3DO: raw block lba = %d, track = %d, type = %d, format = %d\n\x1b[0m", this->lba, this->track + 1, (int)type, fmt);
#endif // P3DO_DEBUG

	if (SendData)
		return SendData(p, len + 2, CD_DATA_IO_INDEX);

	return 0;
}

