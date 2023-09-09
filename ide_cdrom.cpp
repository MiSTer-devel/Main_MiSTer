#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <fcntl.h>
#include <time.h>
#include <ios>
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <sys/stat.h>
#include <cmath>
#include <libchdr/chd.h>
#include <byteswap.h>
#include "spi.h"
#include "user_io.h"
#include "file_io.h"
#include "hardware.h"
#include "cd.h"
#include "ide.h"

#if 0
#define dbg_printf     printf
#define dbg_print_regs ide_print_regs
#define dbg_hexdump    hexdump
#else
#define dbg_printf(...)   void()
#define dbg_print_regs    void
#define dbg_hexdump(...)  void()
#endif

#define ide_send_data(databuf, size) ide_sendbuf(ide, 255, (size), (uint16_t*)(databuf))
#define ide_recv_data(databuf, size) ide_recvbuf(ide, 255, (size), (uint16_t*)(databuf))
#define ide_reset_buf() ide_reg_set(ide, 7, 0)

#define BYTES_PER_RAW_REDBOOK_FRAME    2352
#define BYTES_PER_COOKED_REDBOOK_FRAME 2048
#define REDBOOK_FRAMES_PER_SECOND      75
#define REDBOOK_FRAME_PADDING          150

#define CD_FPS 75
#define MSF_TO_FRAMES(M, S, F) ((M)*60*CD_FPS+(S)*CD_FPS+(F))

#define CD_ERR_NO_DISK 2 
#define CD_ERR_ILLEGAL_REQUEST 5 
#define CD_ERR_UNIT_ATTENTION 6 

#define CD_ASC_CODE_COMMAND_SEQUENCE_ERR 0x2C
#define CD_ASC_CODE_ILLEGAL_OPCODE 0x20


typedef struct
{
	unsigned char   min;
	unsigned char   sec;
	unsigned char   fr;
} TMSF;

static int check_magic(fileTYPE *file, int sectorSize, int mode2)
{
	// Initialize our array in the event file->read() doesn't fully write it
	static uint8_t pvd[BYTES_PER_COOKED_REDBOOK_FRAME];
	memset(pvd, 0, sizeof(pvd));

	uint32_t seek = 16 * sectorSize;  // first vd is located at sector 16
	if (sectorSize == BYTES_PER_RAW_REDBOOK_FRAME && !mode2) seek += 16;
	if (mode2) seek += 24;
	FileSeek(file, seek, SEEK_SET);
	if (!FileReadAdv(file, pvd, BYTES_PER_COOKED_REDBOOK_FRAME)) return 0;

	// pvd[0] = descriptor type, pvd[1..5] = standard identifier,
	// pvd[6] = iso version (+8 for High Sierra)
	return ((pvd[0] == 1 && !strncmp((char*)(&pvd[1]), "CD001", 5) && pvd[6] == 1) ||
		(pvd[8] == 1 && !strncmp((char*)(&pvd[9]), "CDROM", 5) && pvd[14] == 1));
}

static int check_iso_file(fileTYPE *f, uint8_t *mode2, uint16_t *sectorSize)
{
	if (check_magic(f, BYTES_PER_COOKED_REDBOOK_FRAME, false))
	{
		if (sectorSize) *sectorSize = BYTES_PER_COOKED_REDBOOK_FRAME;
		if (mode2) *mode2 = 0;
		return 1;
	}
	else if (check_magic(f, BYTES_PER_RAW_REDBOOK_FRAME, false))
	{
		if (sectorSize) *sectorSize = BYTES_PER_RAW_REDBOOK_FRAME;
		if (mode2) *mode2 = 0;
		return 1;
	}
	else if (check_magic(f, 2336, true))
	{
		if (sectorSize) *sectorSize = 2336;
		if (mode2) *mode2 = 1;
		return 1;
	}
	else if (check_magic(f, BYTES_PER_RAW_REDBOOK_FRAME, true))
	{
		if (sectorSize) *sectorSize = BYTES_PER_RAW_REDBOOK_FRAME;
		if (mode2) *mode2 = 1;
		return 1;
	}

	if (sectorSize) *sectorSize = 0;
	if (mode2) *mode2 = 0;
	return 0;
}

static const char * load_iso_file(drive_t *drv, const char* filename)
{
	fileTYPE f;
	memset(drv->track, 0, sizeof(drv->track));
	drv->track_cnt = 0;

	strcpy(drv->track[0].filename, filename);
	if (!FileOpen(&f, filename))
	{
		printf("Cannot open ISO file!\n");
		return 0;
	}

	if (!check_iso_file(&f, &drv->track[0].mode2, &drv->track[0].sectorSize))
	{
		printf("Fail to parse ISO!\n");
		FileClose(&f);
		return 0;
	}

	drv->track[0].attr = 0x40; //data track
	drv->track[0].length = f.size / drv->track[0].sectorSize;
	drv->track[0].number = 1;

	FileClose(&f);

	// lead-out track (track 2)
	drv->track[1].start = drv->track[0].length;
	drv->track[2].number = 2;

	printf("ISO: mode2 = %d, sectorSize = %d, sectors = %d\n", drv->track[0].mode2, drv->track[0].sectorSize, drv->track[0].length);
	drv->track_cnt = 2;

	drv->data_num = 0;
	if (!FileOpen(&drv->track[0].f, drv->track[0].filename))
	{
		printf("Cannot open ISO file! (First track)\n");
		return 0;
	}
	return drv->track[0].filename;
}

static int get_word(std::string &keyword, std::istream &in)
{
	in >> keyword;
	for (uint32_t i = 0; i < keyword.size(); i++) keyword[i] = (char)toupper(keyword[i]);
	return keyword.size();
}

static int get_timecode(uint32_t &frames, std::istream &in)
{
	std::string msf;
	in >> msf;
	TMSF tmp = { 0, 0, 0 };
	int success = sscanf(msf.c_str(), "%hhu:%hhu:%hhu", &tmp.min, &tmp.sec, &tmp.fr) == 3;
	frames = (int)MSF_TO_FRAMES(tmp.min, tmp.sec, tmp.fr);
	return success;
}

static track_t *get_track_from_lba(drive_t *drive, uint32_t lba, bool &index0)
{
	track_t *ret = NULL;
	index0 = false;
	for (int i = 0; i < drive->track_cnt; i++)
	{
		uint32_t start_lba = i ? drive->track[i-1].start + drive->track[i-1].length : 0;
		uint32_t end_lba = drive->track[i].start + drive->track[i].length;

		if (lba >= start_lba && lba <= end_lba)
		{
			ret = &drive->track[i];
			//In the "pregap" section
			if (lba < drive->track[i].start) index0 = true;
			break;
		}
	}
	return ret;
}

static int add_track(drive_t *drv, track_t *curr, uint32_t &shift, const int32_t prestart, uint32_t &totalPregap, uint32_t currPregap)
{
	uint32_t skip = 0;
	if (drv->track_cnt >= sizeof(drv->track) / sizeof(drv->track[0]))
	{
		printf("CDROM: too many tracks(%d)\n", drv->track_cnt);
		return 0;
	}

	// frames between index 0(prestart) and 1(curr.start) must be skipped
	if (prestart >= 0)
	{
		if (prestart > static_cast<int>(curr->start))
		{
			printf("CDROM: add_track => prestart %d cannot be > curr.start %u\n", prestart, curr->start);
			return 0;
		}
		skip = static_cast<uint32_t>(static_cast<int>(curr->start) - prestart);
	}

	// Add the first track, if our vector is empty
	if (!drv->track_cnt)
	{
		//assertm(curr.number == 1, "The first track must be labelled number 1 [BUG!]");
		curr->skip = skip * curr->sectorSize;
		curr->start += currPregap;
		totalPregap = currPregap;

		memcpy(&drv->track[drv->track_cnt], curr, sizeof(track_t));
		FileOpenEx(&drv->track[drv->track_cnt].f, curr->filename, O_RDONLY);
		drv->track_cnt++;
		return 1;
	}

	// Guard against undefined behavior in subsequent tracks.back() call
	//assert(!tracks.empty());
	track_t *prev = &drv->track[drv->track_cnt - 1];

	// current track consumes data from the same file as the previous
	if (!strcmp(prev->filename, curr->filename))
	{
		curr->start += shift;
		if (!prev->length)
		{
			prev->length = curr->start + totalPregap - prev->start - skip;
		}
		curr->skip += prev->skip + prev->length * prev->sectorSize + skip * curr->sectorSize;
		totalPregap += currPregap;
		curr->start += totalPregap;
		// current track uses a different file as the previous track
	}
	else
	{
		uint32_t size = FileLoad(prev->filename, 0, 0);
		const uint32_t tmp = size - prev->skip;
		prev->length = tmp / prev->sectorSize;

		if (tmp % prev->sectorSize != 0) prev->length++; // padding

		curr->start += prev->start + prev->length + currPregap;
		curr->skip = skip * curr->sectorSize;
		shift += prev->start + prev->length;
		totalPregap = currPregap;
	}

	// error checks
	if (curr->number <= 1
		|| prev->number + 1 != curr->number
		|| curr->start < prev->start + prev->length) {
		printf("add_track: failed consistency checks\n"
			"\tcurr.number (%d) <= 1\n"
			"\tprev.number (%d) + 1 != curr.number (%d)\n"
			"\tcurr.start (%d) < prev.start (%d) + prev.length (%d)\n",
			curr->number, prev->number, curr->number,
			curr->start, prev->start, prev->length);
		return 0;
	}

	memcpy(&drv->track[drv->track_cnt], curr, sizeof(track_t));
	FileOpenEx(&drv->track[drv->track_cnt].f, drv->track[drv->track_cnt].filename, O_RDONLY);
	drv->track_cnt++;
	return 1;
}

static const char* load_chd_file(drive_t *drv, const char *chdfile)
{

	//Borrow the cd.h "toc_t" and mister_chd* parse function. Then translate the toc_t to drive_t+track_t.
	//TODO: abstract all the bin/cue+chd+iso parsing and reading into a shared class
	//

	const char *ext = chdfile + strlen(chdfile) - 4;
	uint32_t total_sector_size = 0;


	if (strncasecmp(".chd", ext, 4))
	{
		//Not a CHD
		return 0;
	}
	toc_t tmpTOC = { };
	memset(drv->track, 0, sizeof(drv->track));
	drv->track_cnt = 0;
	chd_error err = mister_load_chd(chdfile, &tmpTOC);
	if (err != CHDERR_NONE)
	{
		return 0;
	}

	if (drv->chd_hunkbuf)
	{
		free(drv->chd_hunkbuf);
	}

	drv->chd_hunkbuf = (uint8_t *)malloc(CD_FRAME_SIZE * CD_FRAMES_PER_HUNK);
	drv->chd_hunknum = -1;
	drv->chd_f = tmpTOC.chd_f;

	//don't use add_track, just do it ourselves...
	for (int i = 0; i < tmpTOC.last; i++)
	{
		cd_track_t *chd_track = &tmpTOC.tracks[i];
		track_t *trk = &drv->track[i];
		trk->number = i + 1;
		trk->sectorSize = chd_track->sector_size;
		if (chd_track->type)
		{
			trk->attr = 0x40;
			if (chd_track->type == 2)
			{
				trk->mode2 = true;
			}

		}

		trk->chd_offset = chd_track->offset;
		trk->start = chd_track->start;
		trk->length = chd_track->end - chd_track->start;
		drv->track_cnt++;
		total_sector_size += trk->length * trk->sectorSize;
	}

	//Add the lead-out track

	track_t *lead_out = &drv->track[drv->track_cnt];
	lead_out->number = drv->track_cnt + 1;
	lead_out->attr = 0;
	lead_out->start = tmpTOC.tracks[tmpTOC.last - 1].end;
	lead_out->length = 0;
	drv->track_cnt++;

	drv->total_sectors = total_sector_size / 512;
	drv->chd_total_size = total_sector_size;

	for (uint8_t i = 0; i < drv->track_cnt; i++)
	{
		if (drv->track[i].attr == 0x40)
		{
			drv->data_num = i;
		}
	}

	return chdfile;
}


static const char* load_cue_file(drive_t *drv, const char *cuefile)
{
	memset(drv->track, 0, sizeof(drv->track));
	drv->track_cnt = 0;

	track_t track = {};
	uint32_t shift = 0;
	uint32_t currPregap = 0;
	uint32_t totalPregap = 0;
	int32_t prestart = -1;
	int track_number;
	int success;
	int canAddTrack = 0;

	std::string pathname(cuefile);
	std::size_t found = pathname.find_last_of('/');
	if (found == std::string::npos) return 0; // no folder name?
	pathname.resize(found + 1);

	std::ifstream in;
	in.open(cuefile, std::ios::in);
	if (in.fail()) return 0;

	while (!in.eof())
	{
		// get next line
		std::string buf;
		std::getline(in, buf);

		if (in.fail() && !in.eof()) return 0;  // probably a binary file

		std::istringstream line(buf);

		std::string command;
		get_word(command, line);

		//printf("command: %s\n", command.c_str());

		if (command == "TRACK")
		{
			if (canAddTrack) success = add_track(drv, &track, shift, prestart, totalPregap, currPregap);
			else success = 1;

			track.start = 0;
			track.skip = 0;
			currPregap = 0;
			prestart = -1;

			line >> track_number; // (cin) read into a true int first

			track.number = static_cast<uint8_t>(track_number);

			std::string type;
			get_word(type, line);

			//printf("  type: %s\n", type.c_str());

			if (type == "AUDIO")
			{
				track.sectorSize = BYTES_PER_RAW_REDBOOK_FRAME;
				track.attr = 0;
				track.mode2 = false;
			}
			else if (type == "MODE1/2048")
			{
				track.sectorSize = BYTES_PER_COOKED_REDBOOK_FRAME;
				track.attr = 0x40;
				track.mode2 = false;
			}
			else if (type == "MODE1/2352")
			{
				track.sectorSize = BYTES_PER_RAW_REDBOOK_FRAME;
				track.attr = 0x40;
				track.mode2 = false;
			}
			else if (type == "MODE2/2336")
			{
				track.sectorSize = 2336;
				track.attr = 0x40;
				track.mode2 = true;
			}
			else if (type == "MODE2/2352")
			{
				track.sectorSize = BYTES_PER_RAW_REDBOOK_FRAME;
				track.attr = 0x40;
				track.mode2 = true;
			}
			else success = 0;

			canAddTrack = 1;
		}
		else if (command == "INDEX")
		{
			int index;
			line >> index;
			uint32_t frame;
			success = get_timecode(frame, line);

			if (index == 1) track.start = frame;
			else if (index == 0) prestart = static_cast<int32_t>(frame);
			// ignore other indices
		}
		else if (command == "FILE")
		{
			if (canAddTrack) success = add_track(drv, &track, shift, prestart, totalPregap, currPregap);
			else success = 1;
			canAddTrack = 0;

			std::string filename;
			std::getline(std::getline(line, filename, '"'), filename, '"');

			strcpy(track.filename, pathname.c_str());
			strcat(track.filename, filename.c_str());

			printf("cue: got new file name: %s\n", track.filename);
		}
		else if (command == "PREGAP") success = get_timecode(currPregap, line);
		// ignored commands
		else if (command == "CATALOG" || command == "CDTEXTFILE" || command == "FLAGS" || command == "ISRC" ||
			command == "PERFORMER" || command == "POSTGAP" || command == "REM" ||
			command == "SONGWRITER" || command == "TITLE" || command.empty())
		{
			success = 1;
		}
		// failure
		else
		{
			success = 0;
		}

		if (!success)
		{
			return 0;
		}
	}

	// add last track
	if (!add_track(drv, &track, shift, prestart, totalPregap, currPregap))
	{
		return 0;
	}

	// add lead-out track
	track.number++;
	track.filename[0] = 0;
	track.attr = 0;//sync with load iso
	track.start = drv->track[track.number - 1].start + drv->track[track.number - 1].length;
	track.length = 0;

	if (!add_track(drv, &track, shift, -1, totalPregap, 0))
	{
		return 0;
	}

	for (uint8_t i = 0; i < drv->track_cnt; i++)
	{
		if (drv->track[i].attr == 0x40)
		{
			drv->data_num = i;
			return drv->track[i].filename;
		}
	}

	return 0;
}

inline TMSF frames_to_msf(uint32_t frames)
{
	TMSF msf = { 0, 0, 0 };
	msf.fr = frames % REDBOOK_FRAMES_PER_SECOND;
	frames /= REDBOOK_FRAMES_PER_SECOND;
	msf.sec = frames % 60;
	frames /= 60;
	msf.min = static_cast<uint8_t>(frames);
	return msf;
}

static int get_tracks(drive_t *drv, int& start_track_num, int& end_track_num, TMSF& lead_out_msf)
{
	if (drv->track_cnt < 2 || !drv->track[0].length) return 0;

	start_track_num = drv->track[0].number;
	end_track_num = drv->track[drv->track_cnt - 2].number;
	lead_out_msf = frames_to_msf(drv->track[drv->track_cnt - 1].start + REDBOOK_FRAME_PADDING);
	return 1;
}

static int get_track_info(drive_t *drv, int requested_track_num, TMSF& start_msf, unsigned char& attr)
{
	if (drv->track_cnt < 2 || requested_track_num < 1 || requested_track_num >= drv->track_cnt)
	{
		return 0;
	}

	start_msf = frames_to_msf(drv->track[requested_track_num - 1].start + REDBOOK_FRAME_PADDING);
	attr = drv->track[requested_track_num - 1].attr;
	return 1;
}

static int read_toc(drive_t *drv, uint8_t *cmdbuf)
{
	/* NTS: The SCSI MMC standards say we're allowed to indicate the return data
	 *      is longer than it's allocation length. But here's the thing: some MS-DOS
	 *      CD-ROM drivers will ask for the TOC but only provide enough room for one
	 *      entry (OAKCDROM.SYS) and if we signal more data than it's buffer, it will
	 *      reject our response and render the CD-ROM drive inaccessible. So to make
	 *      this emulation work, we have to cut our response short to the driver's
	 *      allocation length */
	unsigned int AllocationLength = ((unsigned int)cmdbuf[7] << 8) + cmdbuf[8];
	unsigned char Format = cmdbuf[2] & 0xF;
	unsigned char Track = cmdbuf[6];
	bool TIME = !!(cmdbuf[1] & 2);
	unsigned char *write;
	int first, last, track;
	TMSF leadOut;

	printf("read_toc in:\n");
	hexdump(cmdbuf, 12);

	memset(ide_buf, 0, 8);

	if (!get_tracks(drv, first, last, leadOut))
	{
		printf("WARNING: ATAPI READ TOC failed to get track info\n");
		return 8;
	}

	/* start 2 bytes out. we'll fill in the data length later */
	write = ide_buf + 2;

	if (Format == 1) /* Read multisession info */
	{
		unsigned char attr;
		TMSF start;

		*write++ = (unsigned char)1;        /* @+2 first complete session */
		*write++ = (unsigned char)1;        /* @+3 last complete session */

		if (!get_track_info(drv, first, start, attr))
		{
			printf("WARNING: ATAPI READ TOC unable to read track %u information\n", first);
			attr = 0x41; /* ADR=1 CONTROL=4 */
			start.min = 0;
			start.sec = 0;
			start.fr = 0;
		}

		printf("Track %u attr=0x%02x %02u:%02u:%02u\n", first, attr, start.min, start.sec, start.fr);

		*write++ = 0x00;        /* entry+0 RESERVED */
		*write++ = (attr >> 4) | 0x10;  /* entry+1 ADR=1 CONTROL=4 (DATA) */
		*write++ = (unsigned char)first;/* entry+2 TRACK */
		*write++ = 0x00;        /* entry+3 RESERVED */

		/* then, start address of first track in session */
		if (TIME)
		{
			*write++ = 0x00;
			*write++ = start.min;
			*write++ = start.sec;
			*write++ = start.fr;
		}
		else
		{
			uint32_t sec = (start.min * 60u * 75u) + (start.sec * 75u) + start.fr - 150u;
			*write++ = (unsigned char)(sec >> 24u);
			*write++ = (unsigned char)(sec >> 16u);
			*write++ = (unsigned char)(sec >> 8u);
			*write++ = (unsigned char)(sec >> 0u);
		}
	}
	else if (Format == 0) /* Read table of contents */
	{
		*write++ = (unsigned char)first;    /* @+2 */
		*write++ = (unsigned char)last;     /* @+3 */

		for (track = first; track <= last; track++)
		{
			unsigned char attr;
			TMSF start;

			if (!get_track_info(drv, track, start, attr))
			{
				printf("WARNING: ATAPI READ TOC unable to read track %u information\n", track);
				attr = 0x41; /* ADR=1 CONTROL=4 */
				start.min = 0;
				start.sec = 0;
				start.fr = 0;
			}

			if (track < Track) continue;
			if ((write + 8) > (ide_buf + AllocationLength)) break;

			printf("Track %u attr=0x%02x %02u:%02u:%02u\n", track, attr, start.min, start.sec, start.fr);

			*write++ = 0x00;        /* entry+0 RESERVED */
			*write++ = (attr >> 4) | 0x10; /* entry+1 ADR=1 CONTROL=4 (DATA) */
			*write++ = (unsigned char)track;/* entry+2 TRACK */
			*write++ = 0x00;        /* entry+3 RESERVED */
			if (TIME)
			{
				*write++ = 0x00;
				*write++ = start.min;
				*write++ = start.sec;
				*write++ = start.fr;
			}
			else
			{
				uint32_t sec = (start.min * 60u * 75u) + (start.sec * 75u) + start.fr - 150u;
				*write++ = (unsigned char)(sec >> 24u);
				*write++ = (unsigned char)(sec >> 16u);
				*write++ = (unsigned char)(sec >> 8u);
				*write++ = (unsigned char)(sec >> 0u);
			}
		}

		if ((write + 8) <= (ide_buf + AllocationLength))
		{
			*write++ = 0x00;
			*write++ = 0x14;
			*write++ = 0xAA;/*TRACK*/
			*write++ = 0x00;
			if (TIME)
			{
				*write++ = 0x00;
				*write++ = leadOut.min;
				*write++ = leadOut.sec;
				*write++ = leadOut.fr;
			}
			else
			{
				uint32_t sec = (leadOut.min * 60u * 75u) + (leadOut.sec * 75u) + leadOut.fr - 150u;
				*write++ = (unsigned char)(sec >> 24u);
				*write++ = (unsigned char)(sec >> 16u);
				*write++ = (unsigned char)(sec >> 8u);
				*write++ = (unsigned char)(sec >> 0u);
			}
		}
	}
	else
	{
		printf("WARNING: ATAPI READ TOC Format=%u not supported\n", Format);
		return 8;
	}

	/* update the TOC data length field */
	unsigned int x = (unsigned int)(write - ide_buf) - 2;
	ide_buf[0] = x >> 8;
	ide_buf[1] = x & 0xFF;

	printf("read_toc result:\n");
	hexdump(ide_buf, write - ide_buf);

	return write - ide_buf;
}

void cdrom_mode_select(ide_config *ide)
{

	uint8_t *mode_page = &ide_buf[8];
	drive_t *drv = &ide->drive[ide->regs.drv];
	uint8_t page_code = mode_page[0] & 0x3F;

	switch (page_code) {
	case 0x0E:
	{
		uint8_t p0vol = mode_page[9];
		uint8_t p1vol = mode_page[11];
		//in gain factor
		drv->volume_l = (p0vol + 1) / 256.0f;
		drv->volume_r = (p1vol + 1) / 256.0f;
	}
	break;
	}
}

static uint16_t mode_sense(drive_t *drv, int page)
{
	uint8_t *write = ide_buf;
	uint8_t *plen;

	uint32_t x;

	int valid = 0;

	printf("mode_sense page: %X\n", page);

	/* Mode Parameter List MMC-3 Table 340 */
	/* - Mode parameter header */
	/* - Page(s) */

	/* Mode Parameter Header (response for 10-byte MODE SENSE) SPC-2 Table 148 */
	*write++ = 0x00;    /* MODE DATA LENGTH                     (MSB) */
	*write++ = 0x00;    /*                                      (LSB) */
	*write++ = 0x00;    /* MEDIUM TYPE */
	*write++ = 0x00;    /* DEVICE-SPECIFIC PARAMETER */
	*write++ = 0x00;    /* Reserved */
	*write++ = 0x00;    /* Reserved */
	*write++ = 0x00;    /* BLOCK DESCRIPTOR LENGTH              (MSB) */
	*write++ = 0x00;    /*                                      (LSB) */
	/* NTS: MMC-3 Table 342 says that BLOCK DESCRIPTOR LENGTH is zero, where it would be 8 for legacy units */

	/* Mode Page Format MMC-3 Table 341 */
	if (page == 0x01 || page == 0x3F)
	{
		valid = 1;
		*write++ = 1;       /* PS|reserved|Page Code */
		plen = write;
		*write++ = 0x00;    /* Page Length (n - 1) ... Length in bytes of the mode parameters that follow */

		*write++ = 0x00;    /* +2 Error recovery Parameter  AWRE|ARRE|TB|RC|Reserved|PER|DTE|DCR */
		*write++ = 3;       /* +3 Read Retry Count */
		*write++ = 0x00;    /* +4 Reserved */
		*write++ = 0x00;    /* +5 Reserved */
		*write++ = 0x00;    /* +6 Reserved */
		*write++ = 0x00;    /* +7 Reserved */

		*plen = write - plen - 1;
	}

	/* CD-ROM audio control MMC-3 Section 6.3.7 table 354 */
	/* also MMC-1 Section 5.2.3.1 table 97 */
	if (page == 0x0E || page == 0x3F)
	{
		valid = 1;
		*write++ = 0x0E;    /* PS|reserved|Page Code */
		plen = write;
		*write++ = 0x00;    /* Page Length (n - 1) ... Length in bytes of the mode parameters that follow */

		*write++ = 0x04;    /* +2 Reserved|IMMED=1|SOTC=0|Reserved */
		*write++ = 0x00;    /* +3 Reserved */
		*write++ = 0x00;    /* +4 Reserved */
		*write++ = 0x00;    /* +5 Reserved */
		*write++ = 0x00;    /* +6 Obsolete (75) */
		*write++ = 75;      /* +7 Obsolete (75) */
		*write++ = 0x01;    /* +8 output port 0 selection (0001b = channel 0) */
		*write++ = (uint8_t)((drv->volume_l * 256) - 1);    /* +9 output port 0 volume (0xFF = 0dB atten.) */
		*write++ = 0x02;    /* +10 output port 1 selection (0010b = channel 1) */
		*write++ = (uint8_t)((drv->volume_l * 256) - 1);    /* +11 output port 1 volume (0xFF = 0dB atten.) */
		*write++ = 0x00;    /* +12 output port 2 selection (none) */
		*write++ = 0x00;    /* +13 output port 2 volume (0x00 = mute) */
		*write++ = 0x00;    /* +14 output port 3 selection (none) */
		*write++ = 0x00;    /* +15 output port 3 volume (0x00 = mute) */

		*plen = write - plen - 1;
	}

	/* CD-ROM mechanical status MMC-3 Section 6.3.11 table 361 */
	if (page == 0x2A || page == 0x3F)
	{
		valid = 1;
		*write++ = 0x2A;    /* PS|reserved|Page Code */
		plen = write;
		*write++ = 0x00;    /* Page Length (n - 1) ... Length in bytes of the mode parameters that follow */

							/*    MSB            |             |             |             |              |               |              |       LSB */
		*write++ = 0x07;    /* +2 Reserved       |Reserved     |DVD-RAM read |DVD-R read   |DVD-ROM read  |   Method 2    | CD-RW read   | CD-R read */
		*write++ = 0x00;    /* +3 Reserved       |Reserved     |DVD-RAM write|DVD-R write  |   Reserved   |  Test Write   | CD-RW write  | CD-R write */
		*write++ = 0x71;    /* +4 Buffer Underrun|Multisession |Mode 2 form 2|Mode 2 form 1|Digital Port 2|Digital Port 1 |  Composite   | Audio play */
		*write++ = 0xFF;    /* +5 Read code bar  |UPC          |ISRC         |C2 Pointers  |R-W deintcorr | R-W supported |CDDA accurate |CDDA support */
		*write++ = 0x2F;    /* +6 Loading mechanism type                     |Reserved     |Eject         |Prevent Jumper |Lock state    |Lock */
							/*      0 (0x00) = Caddy
							 *      1 (0x20) = Tray
							 *      2 (0x40) = Popup
							 *      3 (0x60) = Reserved
							 *      4 (0x80) = Changer with indivually changeable discs
							 *      5 (0xA0) = Changer using a magazine mechanism
							 *      6 (0xC0) = Reserved
							 *      6 (0xE0) = Reserved */
		*write++ = 0x03;    /* +7 Reserved       |Reserved     |R-W in leadin|Side chg cap |S/W slot sel  |Changer disc pr|Sep. ch. mute |Sep. volume levels */

		x = 176 * 8;        /* +8 maximum speed supported in kB: 8X  (obsolete in MMC-3) */
		*write++ = x >> 8;
		*write++ = x;

		x = 256;            /* +10 Number of volume levels supported */
		*write++ = x >> 8;
		*write++ = x;

		x = 6 * 256;        /* +12 buffer size supported by drive in kB */
		*write++ = x >> 8;
		*write++ = x;

		x = 176 * 8;        /* +14 current read speed selected in kB: 8X  (obsolete in MMC-3) */
		*write++ = x >> 8;
		*write++ = x;

		*plen = write - plen - 1;
	}

	if (!valid)
	{
		*write++ = page;    /* PS|reserved|Page Code */
		*write++ = 0x06;    /* Page Length (n - 1) ... Length in bytes of the mode parameters that follow */

		memset(write, 0, 6); write += 6;
		printf("WARNING: MODE SENSE on page 0x%02x not supported\n", page);
	}

	/* mode param header, data length */
	x = (uint32_t)(write - ide_buf) - 2;
	ide_buf[0] = (unsigned char)(x >> 8u);
	ide_buf[1] = (unsigned char)x;

	hexdump(ide_buf, x + 2);
	return x + 2;
}

static int get_subchan(drive_t *drv, unsigned char& attr, unsigned char& track_num, unsigned char& index, TMSF& relative_msf, TMSF& absolute_msf)
{
	attr = 0;
	track_num = 1;
	index = 1;
	relative_msf.min = relative_msf.fr = 0; relative_msf.sec = 2;
	absolute_msf.min = absolute_msf.fr = 0; absolute_msf.sec = 2;

	if (drv->play_start_lba == 0xFFFFFFFF) return 0;

	//TODO: use current play position when audio playback will be implemented
	uint32_t cur_pos = drv->play_start_lba;
	bool is_index0;
	track_t *cur_track = NULL;
	cur_track = get_track_from_lba(drv, cur_pos, is_index0);

	if (cur_track)
	{
		track_num = cur_track->number;
		attr = cur_track->attr;
		absolute_msf = frames_to_msf(cur_pos + REDBOOK_FRAME_PADDING);
		int relative_diff = cur_pos - cur_track->start;

		relative_msf = frames_to_msf(abs(relative_diff));
		index = is_index0 ? 0 : 1;
		return 1;
	}

	return 0;
}

static int read_subchannel(drive_t *drv, uint8_t* cmdbuf)
{
	unsigned char paramList = cmdbuf[3];
	unsigned char attr, track, index;
	bool SUBQ = !!(cmdbuf[2] & 0x40);
	bool TIME = !!(cmdbuf[1] & 2);
	unsigned char *write;
	TMSF rel, abs;

	if (paramList == 0 || paramList > 3)
	{
		printf("ATAPI READ SUBCHANNEL unknown param list\n");
		memset(ide_buf, 0, 8);
		return 8;
	}
	else if (paramList == 2)
	{
		printf("ATAPI READ SUBCHANNEL Media Catalog Number not supported\n");
		memset(ide_buf, 0, 8);
		return 8;
	}
	else if (paramList == 3) {
		printf("ATAPI READ SUBCHANNEL ISRC not supported\n");
		memset(ide_buf, 0, 8);
		return 8;
	}

	/* get current subchannel position */
	if (!get_subchan(drv, attr, track, index, rel, abs))
	{
		printf("ATAPI READ SUBCHANNEL unable to read current pos\n");
		memset(ide_buf, 0, 8);
		return 8;
	}

	memset(ide_buf, 0, 8);
	write = ide_buf;
	*write++ = 0x00;
	*write++ = (!drv->playing) ? 0x13 : drv->paused ? 0x12 : 0x11; /* AUDIO STATUS */
	*write++ = 0x00;  /* SUBCHANNEL DATA LENGTH */
	*write++ = 0x00;

	if (SUBQ)
	{
		*write++ = 0x01;    /* subchannel data format code */
		*write++ = (attr >> 4) | 0x10;  /* ADR/CONTROL */
		*write++ = track;
		*write++ = index;
		if (TIME)
		{
			*write++ = 0x00;
			*write++ = abs.min;
			*write++ = abs.sec;
			*write++ = abs.fr;
			*write++ = 0x00;
			*write++ = rel.min;
			*write++ = rel.sec;
			*write++ = rel.fr;
		}
		else
		{
			uint32_t sec;

			sec = (abs.min * 60u * 75u) + (abs.sec * 75u) + abs.fr - 150u;
			*write++ = (unsigned char)(sec >> 24u);
			*write++ = (unsigned char)(sec >> 16u);
			*write++ = (unsigned char)(sec >> 8u);
			*write++ = (unsigned char)(sec >> 0u);

			sec = (rel.min * 60u * 75u) + (rel.sec * 75u) + rel.fr - 150u;
			*write++ = (unsigned char)(sec >> 24u);
			*write++ = (unsigned char)(sec >> 16u);
			*write++ = (unsigned char)(sec >> 8u);
			*write++ = (unsigned char)(sec >> 0u);
		}
	}

	unsigned int x = (unsigned int)(write - ide_buf) - 4;
	ide_buf[2] = x >> 8;
	ide_buf[3] = x;

	dbg_hexdump(ide_buf, write - ide_buf);
	return write - ide_buf;
}

static void pkt_send(ide_config *ide, void *data, uint16_t size)
{
	ide->regs.pkt_io_size = (size + 1) / 2;
	ide_send_data(data, ide->regs.pkt_io_size);

	ide->regs.cylinder = size;
	ide->regs.sector_count = 2;

	ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_DRQ | ATA_STATUS_IRQ;
	ide_set_regs(ide);
	ide->state = IDE_STATE_WAIT_PKT_RD;
}

static void read_cd_sectors(ide_config *ide, track_t *track, int cnt)
{
	drive_t *drv = &ide->drive[ide->regs.drv];
	uint32_t sz = drv->track[drv->data_num].sectorSize;

	if (sz == 2048)
	{
		if (!ide->null) ide->null = (FileReadAdv(&track->f, ide_buf, cnt * sz, -1) <= 0);
		if (ide->null) memset(ide_buf, 0, cnt * sz);
		return;
	}

	uint32_t pre = drv->track[drv->data_num].mode2 ? 24 : 16;
	uint32_t post = sz - pre - 2048;
	uint32_t off = 0;

	while (cnt--)
	{
		if (!ide->null) ide->null = !FileSeek(&track->f, pre, SEEK_CUR);
		if (!ide->null) ide->null = (FileReadAdv(&track->f, ide_buf + off, 2048, -1) <= 0);
		if (ide->null) memset(ide_buf + off, 0, 2048);
		if (!ide->null) ide->null = !FileSeek(&track->f, post, SEEK_CUR);
		off += 2048;
	}
}

void cdrom_read(ide_config *ide)
{
	bool is_index0 = false;
	uint32_t cnt = ide->regs.pkt_cnt;
	drive_t *drive = &ide->drive[ide->regs.drv];

	if ((cnt * 4) > ide_io_max_size) cnt = ide_io_max_size / 4;

	while ((cnt * 2048) > ide->regs.pkt_size_limit)
	{
		if (cnt <= 1) break;
		cnt--;
	}


	if (cnt != ide->regs.pkt_cnt)
	{
		dbg_printf("** partial CD read\n");
	}


	track_t *track = get_track_from_lba(drive, ide->regs.pkt_lba, is_index0);

	if (ide->state == IDE_STATE_INIT_RW && !drive->chd_f && track)
	{
		uint32_t pos = track->skip + (ide->regs.pkt_lba - track->start) * track->sectorSize;
		ide->null = (FileSeek(&track->f, pos, SEEK_SET) < 0);
	}


	if (drive->chd_f) {

		uint32_t hdr = drive->track[drive->data_num].mode2 ? 24 : 16;
		if (drive->track[drive->data_num].sectorSize == 2048)
		{
			hdr = 0;
		}
		uint32_t d_offset = 0;

		if (ide->state == IDE_STATE_INIT_RW)
		{
			drive->chd_last_partial_lba = ide->regs.pkt_lba;
		}

		for (uint32_t i = 0; i < cnt; i++)
		{

			if (mister_chd_read_sector(drive->chd_f, drive->chd_last_partial_lba + drive->track[drive->data_num].chd_offset, d_offset, hdr, 2048, ide_buf, drive->chd_hunkbuf, &drive->chd_hunknum) != CHDERR_NONE)
			{
				//I don't think anything else uses this, but set it just in case.
				ide->null = 1;
				memset(ide_buf + d_offset, 0, 2048);
			}
			else
			{
				ide->null = 0;
			}
			d_offset += 2048;
			drive->chd_last_partial_lba++;
		}

	}
	else
	{
		read_cd_sectors(ide, track, cnt);
	}

	dbg_printf("\nsector:\n");
	dbg_hexdump(ide_buf, 512, 0);

	ide->regs.pkt_cnt -= cnt;
	pkt_send(ide, ide_buf, cnt * 2048);
}

static int cd_inquiry(uint8_t maxlen)
{
	static const char vendor[] = "MiSTer  ";
	static const char product[] = "CDROM           ";

	memset(ide_buf, 0, 47);
	ide_buf[0] = (0 << 5) | 5;  /* Peripheral qualifier=0   device type=5 (CDROM) */
	ide_buf[1] = 0x80;			/* RMB=1 removable media */
	ide_buf[2] = 0x00;			/* ANSI version */
	ide_buf[3] = 0x21;
	ide_buf[4] = maxlen - 5;    /* additional length */

	for (int i = 0; i < 8; i++) ide_buf[i + 8] = (unsigned char)vendor[i];
	for (int i = 0; i < 16; i++) ide_buf[i + 16] = (unsigned char)product[i];
	for (int i = 0; i < 4; i++) ide_buf[i + 32] = ' ';
	for (int i = 0; i < 11; i++) ide_buf[i + 36] = ' ';

	hexdump(ide_buf, maxlen);
	return maxlen;
}

static void set_sense(uint8_t SK, uint8_t ASC = 0, uint8_t ASCQ = 0)
{
	int len = 18;
	memset(ide_buf, 0, len);

	ide_buf[0] = 0x70;      /* RESPONSE CODE */
	ide_buf[2] = SK & 0xF;  /* SENSE KEY */
	ide_buf[7] = len - 18;  /* additional sense length */
	ide_buf[12] = ASC;
	ide_buf[13] = ASCQ;
}

static int get_sense(drive_t *drv)
{
	switch (drv->load_state)
	{
	case 3:
		set_sense(2, 0x3A);
		break;

	case 2:
		set_sense(2, 4, 1);
		drv->load_state--;
		break;

	case 1:
		set_sense(2, 28, 0);
		drv->load_state--;
		break;

	default:
		set_sense(drv->atapi_sense_key, drv->atapi_asc_code, drv->atapi_ascq_code);
		break;
	}

	dbg_hexdump(ide_buf, 18);
	return 18;
}

static bool pause_resume(drive_t *drv, uint8_t *cmdbuf)
{
	bool resume = !!(cmdbuf[8] & 1);
	if (drv->playing) 
	{
		drv->paused = !resume;
		return true;
	}
	return false;
}

static void play_audio_msf(drive_t *drv, uint8_t *cmdbuf)
{
	if (cmdbuf[3] == 0xFF && cmdbuf[4] == 0xFF && cmdbuf[5] == 0xFF)
	{
		drv->play_start_lba = 0xFFFFFFFF;
	}
	else
	{
		drv->play_start_lba = (cmdbuf[3] * 60u * 75u) + (cmdbuf[4] * 75u) + cmdbuf[5];

		if (drv->play_start_lba >= 150u) drv->play_start_lba -= 150u; /* LBA sector 0 == M:S:F sector 0:2:0 */
		else drv->play_start_lba = 0;
	}

	if (cmdbuf[6] == 0xFF && cmdbuf[7] == 0xFF && cmdbuf[8] == 0xFF)
	{
		drv->play_end_lba = 0xFFFFFFFF;
	}
	else
	{
		drv->play_end_lba = (cmdbuf[6] * 60u * 75u) + (cmdbuf[7] * 75u) + cmdbuf[8];
		if (drv->play_end_lba >= 150u) drv->play_end_lba -= 150u; /* LBA sector 0 == M:S:F sector 0:2:0 */
		else drv->play_end_lba = 0;
	}

	if (drv->play_start_lba == drv->play_end_lba)
	{
		drv->playing = 0;
		drv->paused = 0;
		return;
	}

	/* LBA 0xFFFFFFFF means start playing wherever the optics of the CD sit */
	if (drv->play_start_lba != 0xFFFFFFFF)
	{
		drv->playing = 1;
		drv->paused = 0;
	}
	else
	{
		drv->playing = 0;
		drv->paused = 1;
	}
}

void play_audio10(drive_t *drv, uint8_t *cmdbuf)
{
	uint16_t play_length;

	drv->play_start_lba = ((uint32_t)cmdbuf[2] << 24) + ((uint32_t)cmdbuf[3] << 16) + ((uint32_t)cmdbuf[4] << 8) + ((uint32_t)cmdbuf[5] << 0);
	play_length = ((uint16_t)cmdbuf[7] << 8) + ((uint16_t)cmdbuf[8] << 0);
	drv->play_end_lba = drv->play_start_lba + play_length;

	if (play_length == 0)
	{
		drv->playing = 0;
		drv->paused = 0;
		return;
	}

	/* LBA 0xFFFFFFFF means start playing wherever the optics of the CD sit */
	if (drv->play_start_lba != 0xFFFFFFFF)
	{
		drv->playing = 1;
		drv->paused = 0;
	}
	else
	{
		drv->playing = 0;
		drv->paused = 1;
	}
}

static void cdrom_nodisk(ide_config *ide)
{
	drive_t *drv = &ide->drive[ide->regs.drv];
	drv->last_load_state = drv->load_state;
	if (drv->load_state && drv->load_state < 3) drv->load_state--;

	cdrom_reply(ide, CD_ERR_NO_DISK);
}

void cdrom_handle_pkt(ide_config *ide)
{
	drive_t *drv = &ide->drive[ide->regs.drv];
	uint8_t cmdbuf[16];

	ide_recv_data(cmdbuf, 6);
	ide_reset_buf();
	dbg_hexdump(cmdbuf, 12, 0);

	ide->regs.pkt_cnt = 0;
	int err = 0;

	//See MMC-5 section 4.1.6.1
	//If the no disk/load state isn't "done", most commands need to return CHECK CONDITION+sense data. 
	//The only commands that ignore this are the ones listed below.
	//GET CONFIG ,GET EVENT STATUS NOTIFICATION, INQUIRY, REQUEST SENSE
	//0x46, 0x4A, 0x12, 0x3h
	if (drv->load_state || drv->mcr_flag)
	{
		if ((cmdbuf[0] != 0x46) && (cmdbuf[0] != 0x4A) && (cmdbuf[0] != 0x12) && (cmdbuf[0] != 0x3))
		{
			cdrom_nodisk(ide);
			return;
		}
	}
	switch (cmdbuf[0])
	{
	case 0xA8: // read(12) sectors
	case 0x28: // read(10) sectors
		dbg_printf("** Read Sectors\n");
		//hexdump(cmdbuf, 12, 0);
		ide->regs.pkt_cnt = (cmdbuf[0] == 0x28) ? ((cmdbuf[7] << 8) | cmdbuf[8]) : ((cmdbuf[6] << 24) | (cmdbuf[7] << 16) | (cmdbuf[8] << 8) | cmdbuf[9]);
		ide->regs.pkt_lba = ((cmdbuf[2] << 24) | (cmdbuf[3] << 16) | (cmdbuf[4] << 8) | cmdbuf[5]);
		if (!ide->regs.pkt_cnt)
		{
			dbg_printf("  length 0 is not a error.\n");
			cdrom_reply(ide, 0);
			break;
		}

		dbg_printf("** par: lba = %d, cnt = %d, load_state = %d\n", ide->regs.pkt_lba, ide->regs.pkt_cnt, drv->load_state);
		ide->state = IDE_STATE_INIT_RW;
		if (!drv->load_state) cdrom_read(ide);
		else cdrom_nodisk(ide);
		break;

	case 0x25: // read capacity
		dbg_printf("** Read Capacity\n");
		if (!drv->load_state)
		{
			uint32_t tmp = 0;

			tmp = drv->track[drv->track_cnt-1].start;

			ide_buf[0] = tmp >> 24;
			ide_buf[1] = tmp >> 16;
			ide_buf[2] = tmp >> 8;
			ide_buf[3] = tmp;

			tmp = 2048;
			ide_buf[4] = tmp >> 24;
			ide_buf[5] = tmp >> 16;
			ide_buf[6] = tmp >> 8;
			ide_buf[7] = tmp;
			//hexdump(buf, 8, 0);
			pkt_send(ide, ide_buf, 8);
		}
		else cdrom_nodisk(ide);
		break;

	case 0x2B: // seek

		dbg_printf("** Seek\n");
		drv->playing = 0;
		drv->paused = 0;
		cdrom_reply(ide, 0);
		break;

	case 0x1B: //START STOP UNIT
		dbg_printf("** Start Stop Unit\n");
		cdrom_reply(ide, 0);
		break;

	case 0x1E: // lock the cd door - doing nothing.
		dbg_printf("** Lock Door\n");
		cdrom_reply(ide, 0);
		break;

	case 0x5A: // mode sense
		dbg_printf("** Mode Sense\n");
		pkt_send(ide, ide_buf, mode_sense(drv, cmdbuf[2]));
		break;

	case 0x42: // read sub
		dbg_printf("** read sub:\n");
		if (!drv->load_state)
		{
			pkt_send(ide, ide_buf, read_subchannel(drv, cmdbuf));
		}
		else cdrom_nodisk(ide);
		break;

	case 0x43: // read TOC
		dbg_printf("** Read TOC\n");
		if (!drv->load_state) 
		{
			pkt_send(ide, ide_buf, read_toc(drv, cmdbuf));
		} 
		else cdrom_nodisk(ide);
		break;

	case 0x12: // inquiry
		dbg_printf("** Inquiry\n");
		pkt_send(ide, ide_buf, cd_inquiry(cmdbuf[4]));
		break;

	case 0x03: // mode sense
		dbg_printf("** get sense:\n");
		pkt_send(ide, ide_buf, get_sense(drv));
		break;


	case 0x55: // mode select
		dbg_printf("** mode select\n");
		ide->regs.cylinder = (cmdbuf[7] << 8) | cmdbuf[8];
		if (ide->regs.cylinder > 512) ide->regs.cylinder = 512;
		ide->regs.pkt_io_size = (ide->regs.cylinder + 1) / 2;
		ide->regs.sector_count = 0;
		ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_DRQ | ATA_STATUS_IRQ;
		ide_set_regs(ide);
		ide->state = IDE_STATE_WAIT_PKT_MODE;
		break;

	case 0x00: // test unit ready
		dbg_printf("** Test Unit Ready\n");
		if (!drv->load_state) cdrom_reply(ide, 0, false);
		else cdrom_nodisk(ide);
		break;

	case 0x45: // play lba
		dbg_printf("** CD PLAY AUDIO(10)\n");
		play_audio10(drv, cmdbuf);
		cdrom_reply(ide, 0);
		break;

	case 0x47: // play msf
		dbg_printf("** CD PLAY AUDIO MSF\n");
		play_audio_msf(drv, cmdbuf);
		cdrom_reply(ide, 0);
		break;

	case 0x4B: // pause/resume
		dbg_printf("** CD PAUSE/RESUME\n");
		cdrom_reply(ide, pause_resume(drv, cmdbuf) ? 0 : CD_ERR_ILLEGAL_REQUEST, CD_ASC_CODE_COMMAND_SEQUENCE_ERR);
		break;

	default:
		err = 1;
		break;
	}

	if (err)
	{
		printf("(!) Error in packet command %02X\n", cmdbuf[0]);
		hexdump(cmdbuf, 12, 0);
		cdrom_reply(ide, CD_ERR_ILLEGAL_REQUEST, CD_ASC_CODE_ILLEGAL_OPCODE);
	}
}

int cdrom_handle_cmd(ide_config *ide)
{
	uint8_t drv;

	switch (ide->regs.cmd)
	{
	case 0xA1: // identify packet
		//print_regs(&ide->regs);
		dbg_printf("identify packet\n");
		ide_send_data(ide->drive[ide->regs.drv].id, 256);
		drv = ide->regs.drv;
		memset(&ide->regs, 0, sizeof(ide->regs));
		ide->regs.drv = drv;
		ide->regs.pkt_io_size = 256;
		ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_DRQ | ATA_STATUS_IRQ | ATA_STATUS_END;
		ide_set_regs(ide);
		ide->state = IDE_STATE_IDLE;
		break;

	case 0xEC: // identify (fail)
		dbg_printf("identify (CD)\n");
		ide->regs.sector = 1;
		ide->regs.sector_count = 1;
		ide->regs.cylinder = 0xEB14;
		ide->regs.head = 0;
		ide->regs.io_size = 0;
		return 1;

	case 0xA0: // packet
		dbg_printf("cmd A0: %02X\n", ide->regs.features);
		if (ide->regs.features & 1)
		{
			dbg_printf("Cancel A0 DMA transfer\n");
			return 1;
		}
		ide->regs.pkt_size_limit = ide->regs.cylinder;
		if (!ide->regs.pkt_size_limit) ide->regs.pkt_size_limit = ide_io_max_size * 512;
		ide->regs.pkt_io_size = 6;
		ide->regs.sector_count = 1;
		ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_DRQ; // | ATA_STATUS_IRQ;
		ide_set_regs(ide);
		ide->state = IDE_STATE_WAIT_PKT_CMD;
		break;

	case 0x08: // reset
		dbg_printf("cmd 08\n");
		ide->drive[ide->regs.drv].playing = 0;
		ide->drive[ide->regs.drv].paused = 0;
		ide->regs.sector = 1;
		ide->regs.sector_count = 1;
		ide->regs.cylinder = 0xEB14;
		ide->regs.head = 0;
		ide->regs.io_size = 0;
		ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_DSC;
		ide_set_regs(ide);
		break;

	case 0x00: // nop
		dbg_printf("cmd 00\n");
		return 1; // must always fail

	default:
		printf("(!) Unsupported command\n");
		ide_print_regs(&ide->regs);
		return 1;
	}

	return 0;
}


//error is the atapi sense_key
void cdrom_reply(ide_config *ide, uint8_t error, uint8_t asc_code, uint8_t ascq_code, bool unit_attention)
{
	ide->state = IDE_STATE_IDLE;
	ide->regs.sector_count = 3;
	if (ide->drive[ide->regs.drv].mcr_flag && unit_attention) {
		ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_IRQ | ATA_STATUS_ERR;
		ide->regs.error = (CD_ERR_UNIT_ATTENTION << 4) | ATA_ERR_MC;
		ide->drive[ide->regs.drv].mcr_flag = false;
	}
	else
	{
		ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_IRQ | (error ? ATA_STATUS_ERR : 0);
		ide->regs.error = error << 4;
		ide->drive[ide->regs.drv].atapi_sense_key = error;
		ide->drive[ide->regs.drv].atapi_asc_code = asc_code;
		ide->drive[ide->regs.drv].atapi_ascq_code = ascq_code;
	}

	ide_set_regs(ide);
}


void cdrom_close_chd(drive_t *drv)
{

	if (drv->chd_f)
	{
		chd_close(drv->chd_f);
		drv->chd_f = NULL;
	}

	if (drv->chd_hunkbuf)
	{
		free(drv->chd_hunkbuf);
		drv->chd_hunkbuf = NULL;
	}
	drv->chd_hunknum = -1;
}

const char* cdrom_parse(uint32_t num, const char *filename)
{
	const char *res = 0;

	int drv = num & 1;
	num >>= 1;

	//always close files and reset state. empty filename == unmounted cd from OSD
	cdrom_close_chd(&ide_inst[num].drive[drv]);
	for (uint8_t i = 0; i < sizeof(ide_inst[num].drive[drv].track) / sizeof(track_t); i++)
	{
		if (ide_inst[num].drive[drv].track[i].f.opened())
		{
			FileClose(&ide_inst[num].drive[drv].track[i].f);
		}
	}
	ide_inst[num].drive[drv].mcr_flag = true;
	ide_inst[num].drive[drv].playing = 0;
	ide_inst[num].drive[drv].paused = 0;
	ide_inst[num].drive[drv].play_start_lba = 0;
	ide_inst[num].drive[drv].play_end_lba = 0;
	if (strlen(filename))
	{
		const char *path = getFullPath(filename);
		res = load_chd_file(&ide_inst[num].drive[drv], path);
		if (!res) res = load_cue_file(&ide_inst[num].drive[drv], path);
		if (!res) res = load_iso_file(&ide_inst[num].drive[drv], path);
	}
	return res;
}

void ide_cdda_send_sector()
{
	bool is_index0 = false;
	static uint8_t cdda_buf[BYTES_PER_RAW_REDBOOK_FRAME];
	drive_t *drv = NULL;
	ide_config *ide = NULL;
	int ide_idx = -1;
	for (ide_idx = 0; ide_idx < 2; ide_idx++)
	{
		for (int drv_idx = 0; drv_idx < 2; drv_idx++)
		{
			if (ide_inst[ide_idx].drive[drv_idx].playing == 1 && ide_inst[ide_idx].drive[drv_idx].paused == 0)
			{
				drv = &ide_inst[ide_idx].drive[drv_idx];
				ide = &ide_inst[ide_idx];
				break;
			}
		}
		if (drv) break;
	}

	if (!drv || !ide) return;

	bool needs_swap = false;
	track_t *track = get_track_from_lba(drv, drv->play_start_lba, is_index0);

	if (!track->attr)
	{
		if (drv->chd_f)
		{
			mister_chd_read_sector(drv->chd_f, drv->play_start_lba + drv->track[drv->data_num].chd_offset, 0, 0, BYTES_PER_RAW_REDBOOK_FRAME, cdda_buf, drv->chd_hunkbuf, &drv->chd_hunknum);
			needs_swap = true;
		}
		else
		{
			//If we're in the index0 area "audio pregap", that data is actually in the
			//previous track. Use that file object, seek and return the data from there.
			//If the seek fails just return zero data.
			//It may be a 'PREGAP' which indicates no stored data

			track_t *read_track = track;
			if (is_index0 && track->number > 1)
			{
				//track number is 1-based, track array is zero. 
				read_track = &drv->track[track->number-2];

			}
			uint32_t pos = read_track->skip + (drv->play_start_lba - read_track->start) * read_track->sectorSize;
			if (FileSeek(&read_track->f, pos, SEEK_SET))
			{
				FileReadAdv(&read_track->f, cdda_buf, sizeof(cdda_buf), -1);
			} else {
				memset(cdda_buf, 0, sizeof(cdda_buf));
			}
		}
	}
	else
	{
		memset(cdda_buf, 0, sizeof(cdda_buf));
	}

	int16_t *cdda_buf16 = (int16_t *)cdda_buf;
	const int buf_wsize = sizeof(cdda_buf) / 2;

	for (int sidx = 0; sidx < buf_wsize; sidx++)
	{
		if (needs_swap) cdda_buf16[sidx] = bswap_16(cdda_buf16[sidx]);
		double tmps = (double)cdda_buf16[sidx];
		cdda_buf16[sidx] = (int16_t)(tmps*((sidx & 1) ? drv->volume_l : drv->volume_r));
	}

	ide_sendbuf(ide, 0x200, buf_wsize, (uint16_t *)cdda_buf);

	drv->play_start_lba++;
	if (drv->play_start_lba >= drv->play_end_lba)
	{
		drv->playing = 0;
		drv->paused = 0;
	}
}
