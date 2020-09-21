#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <ios>
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <sys/stat.h>

#include "../../spi.h"
#include "../../user_io.h"
#include "../../file_io.h"
#include "../../hardware.h"
#include "x86.h"

#define ATA_STATUS_BSY  0x80  // busy
#define ATA_STATUS_RDY  0x40  // ready
#define ATA_STATUS_DF   0x20  // device fault
#define ATA_STATUS_WFT  0x20  // write fault (old name)
#define ATA_STATUS_SKC  0x10  // seek complete
#define ATA_STATUS_SERV 0x10  // service
#define ATA_STATUS_DRQ  0x08  // data request
#define ATA_STATUS_IRQ  0x04  // rise IRQ
#define ATA_STATUS_IDX  0x02  // index
#define ATA_STATUS_ERR  0x01  // error (ATA)
#define ATA_STATUS_CHK  0x01  // check (ATAPI)

#define ATA_ERR_ICRC    0x80    // ATA Ultra DMA bad CRC
#define ATA_ERR_BBK     0x80    // ATA bad block
#define ATA_ERR_UNC     0x40    // ATA uncorrected error
#define ATA_ERR_MC      0x20    // ATA media change
#define ATA_ERR_IDNF    0x10    // ATA id not found
#define ATA_ERR_MCR     0x08    // ATA media change request
#define ATA_ERR_ABRT    0x04    // ATA command aborted
#define ATA_ERR_NTK0    0x02    // ATA track 0 not found
#define ATA_ERR_NDAM    0x01    // ATA address mark not found

#define IDE_STATE_IDLE          0
#define IDE_STATE_RESET         1
#define IDE_STATE_INIT_RW       2
#define IDE_STATE_WAIT_RD       3
#define IDE_STATE_WAIT_WR       4
#define IDE_STATE_WAIT_END      5
#define IDE_STATE_WAIT_PKT_CMD  6
#define IDE_STATE_WAIT_PKT_RD   7
#define IDE_STATE_WAIT_PKT_END  8
#define IDE_STATE_WAIT_PKT_MODE 9

#if 0
	#define dbg_printf     printf
	#define dbg_print_regs print_regs
	#define dbg_hexdump    hexdump
#else
	#define dbg_printf(...)   void()
	#define dbg_print_regs    void
	#define dbg_hexdump(...)  void()
#endif

#if 0
	#define cddbg_printf     printf
	#define cddbg_print_regs print_regs
	#define cddbg_hexdump    hexdump
#else
	#define cddbg_printf(...)   void()
	#define cddbg_print_regs    void
	#define cddbg_hexdump(...)  void()
#endif

#define IOWR(base, reg, value, ver) x86_dma_set((base) + ((ver) ? (reg) : ((reg)<<2)), value)

#define ide_send_data(databuf, size) x86_dma_sendbuf(ide->base + 255, (size), (uint32_t*)(databuf))
#define ide_recv_data(databuf, size) x86_dma_recvbuf(ide->base + 255, (size), (uint32_t*)(databuf))
#define ide_reset_buf() x86_dma_set(ide->base + 3, 0)

static const uint32_t io_max_size = 32;
static uint8_t buf[io_max_size * 512];

typedef struct
{
	uint8_t io_done;
	uint8_t features;
	uint8_t sector_count;
	uint8_t sector;
	uint16_t cylinder;
	uint8_t head;
	uint8_t drv;
	uint8_t lba;
	uint8_t cmd;

	uint16_t pkt_size_limit;
	uint16_t pkt_io_size;
	uint32_t pkt_lba;
	uint32_t pkt_cnt;

	uint8_t io_size;
	uint8_t error;
	uint8_t status;
} regs_t;

typedef struct
{
	char     filename[1024];
	uint32_t start;
	uint32_t length;
	uint32_t skip;
	uint16_t sectorSize;
	uint8_t  attr;
	uint8_t  mode2;
	uint8_t  number;
} track_t;

typedef struct
{
	fileTYPE *f;
	uint32_t hd_cylinders;
	uint32_t hd_heads;
	uint32_t hd_spt;
	uint32_t hd_total_sectors;
	uint8_t present;

	uint8_t placeholder;
	uint8_t allow_placeholder;
	uint8_t cd;
	uint8_t load_state;
	uint8_t playing;
	uint8_t paused;
	uint8_t track_cnt;
	uint8_t data_num;
	track_t track[50];

	uint16_t id[256];
} drive_t;

typedef struct
{
	uint32_t base;
	uint32_t state;
	uint32_t null;
	uint32_t prepcnt;
	regs_t   regs;

	drive_t drive[2];
} ide_config;

static ide_config ide_inst[2] = {};

static void print_regs(regs_t *regs)
{
	printf("\nIDE regs:\n");
	printf("   io_done:  %02X\n", regs->io_done);
	printf("   features: %02X\n", regs->features);
	printf("   sec_cnt:  %02X\n", regs->sector_count);
	printf("   sector:   %02X\n", regs->sector);
	printf("   cylinder: %04X\n", regs->cylinder);
	printf("   head:     %02X\n", regs->head);
	printf("   drv:      %02X\n", regs->drv);
	printf("   lba:      %02X\n", regs->lba);
	printf("   command:  %02X\n", regs->cmd);
}

static void get_regs(ide_config *ide)
{
	uint32_t data[3];
	x86_dma_recvbuf(ide->base, 3, data);

	ide->regs.io_done = (uint8_t)(data[0] & 1);
	ide->regs.features = (uint8_t)(data[0] >> 8);
	ide->regs.sector_count = (uint8_t)(data[0] >> 16);
	ide->regs.sector = (uint8_t)(data[0] >> 24);

	ide->regs.cylinder = data[1] & 0xFFFF;
	ide->regs.head = (data[2] >> 16) & 0xF;
	ide->regs.drv = (data[2] >> 20) & 1;
	ide->regs.lba = (data[2] >> 22) & 1;
	ide->regs.cmd = data[2] >> 24;

	ide->regs.error = 0;
	ide->regs.status = 0;

	dbg_print_regs(&ide->regs);
}

static void set_regs(ide_config *ide)
{
	uint32_t data[3];

	if(!(ide->regs.status & (ATA_STATUS_BSY | ATA_STATUS_ERR))) ide->regs.status |= ATA_STATUS_SKC;

	data[0] = (ide->drive[ide->regs.drv].cd) ? 0x80 : ide->regs.io_size;
	data[0] |= ide->regs.error << 8;
	data[0] |= ide->regs.sector_count << 16;
	data[0] |= ide->regs.sector << 24;

	data[1] = ide->regs.cylinder;

	data[2] = (ide->drive[ide->regs.drv].cd) ? ide->regs.pkt_io_size : 0;
	data[2] |= ide->regs.head << 16;
	data[2] |= ide->regs.drv << 20;
	data[2] |= (ide->regs.lba ? 7 : 5) << 21;
	data[2] |= ide->regs.status << 24;

	x86_dma_sendbuf(ide->base, 3, data);

	if (ide->drive[ide->regs.drv].cd)
	{
		if (ide->regs.status & 1)
		{
			cddbg_printf("  status: %02X, error %02X\n", ide->regs.status, ide->regs.error);
		}
		else
		{
			cddbg_printf("  status: %02X\n", ide->regs.status);
		}
	}
}

#define BYTES_PER_RAW_REDBOOK_FRAME    2352
#define BYTES_PER_COOKED_REDBOOK_FRAME 2048
#define REDBOOK_FRAMES_PER_SECOND      75
#define REDBOOK_FRAME_PADDING          150

static int CanReadPVD(fileTYPE *file, int sectorSize, int mode2)
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

static int x86_check_iso_file(fileTYPE *f, uint8_t *mode2, uint16_t *sectorSize)
{
	if (CanReadPVD(f, BYTES_PER_COOKED_REDBOOK_FRAME, false))
	{
		if (sectorSize) *sectorSize = BYTES_PER_COOKED_REDBOOK_FRAME;
		if (mode2) *mode2 = 0;
		return 1;
	}
	else if (CanReadPVD(f, BYTES_PER_RAW_REDBOOK_FRAME, false))
	{
		if (sectorSize) *sectorSize = BYTES_PER_RAW_REDBOOK_FRAME;
		if (mode2) *mode2 = 0;
		return 1;
	}
	else if (CanReadPVD(f, 2336, true))
	{
		if (sectorSize) *sectorSize = 2336;
		if (mode2) *mode2 = 1;
		return 1;
	}
	else if (CanReadPVD(f, BYTES_PER_RAW_REDBOOK_FRAME, true))
	{
		if (sectorSize) *sectorSize = BYTES_PER_RAW_REDBOOK_FRAME;
		if (mode2) *mode2 = 1;
		return 1;
	}

	if (sectorSize) *sectorSize = 0;
	if (mode2) *mode2 = 0;
	return 0;
}

static const char * LoadIsoFile(drive_t *drv, const char* filename)
{
	fileTYPE f;
	memset(drv->track, 0, sizeof(drv->track));
	drv->track_cnt = 0;

	strcpy(drv->track[0].filename, filename);
	if (!FileOpen(&f, filename))
	{
		printf("No CD!\n");
		return 0;
	}

	if(!x86_check_iso_file(&f, &drv->track[0].mode2, &drv->track[0].sectorSize))
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
	return drv->track[0].filename;
}

#define CD_FPS 75
#define MSF_TO_FRAMES(M, S, F) ((M)*60*CD_FPS+(S)*CD_FPS+(F))

typedef struct
{
	unsigned char   min;
	unsigned char   sec;
	unsigned char   fr;
} TMSF;

static int GetCueKeyword(std::string &keyword, std::istream &in)
{
	in >> keyword;
	for (uint32_t i = 0; i < keyword.size(); i++)
	{
		keyword[i] = static_cast<char>(toupper(keyword[i]));
	}
	return 1;
}

static int GetCueFrame(uint32_t &frames, std::istream &in)
{
	std::string msf;
	in >> msf;
	TMSF tmp = { 0, 0, 0 };
	int success = sscanf(msf.c_str(), "%hhu:%hhu:%hhu", &tmp.min, &tmp.sec, &tmp.fr) == 3;
	frames = (int)MSF_TO_FRAMES(tmp.min, tmp.sec, tmp.fr);
	return success;
}

static int GetCueString(std::string &str, std::istream &in)
{
	int pos = (int)in.tellg();
	in >> str;
	if (str[0] == '\"') {
		if (str[str.size() - 1] == '\"')
		{
			str.assign(str, 1, str.size() - 2);
		}
		else
		{
			in.seekg(pos, std::ios::beg);
			static char buffer[1024];
			in.getline(buffer, 1024, '\"');	// skip
			in.getline(buffer, 1024, '\"');
			str = buffer;
		}
	}
	return 1;
}

static int AddTrack(drive_t *drv, track_t *curr, uint32_t &shift, const int32_t prestart, uint32_t &totalPregap, uint32_t currPregap)
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
			printf("CDROM: AddTrack => prestart %d cannot be > curr.start %u\n", prestart, curr->start);
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
		printf("AddTrack: failed consistency checks\n"
			"\tcurr.number (%d) <= 1\n"
			"\tprev.number (%d) + 1 != curr.number (%d)\n"
			"\tcurr.start (%d) < prev.start (%d) + prev.length (%d)\n",
			curr->number, prev->number, curr->number,
			curr->start, prev->start, prev->length);
		return 0;
	}

	memcpy(&drv->track[drv->track_cnt], curr, sizeof(track_t));
	drv->track_cnt++;
	return 1;
}

static std::string dirname(char * file)
{
	char * sep = strrchr(file, '/');
	if (!sep) return "";

	int len = (int)(sep - file);
	static char tmp[1024];
	strncpy(tmp, file, len + 1);
	tmp[len + 1] = 0;
	return tmp;
}

static int GetRealFileName(std::string &filename, std::string &pathname)
{
	// check if file exists
	struct stat test;
	if (stat(filename.c_str(), &test) == 0)
	{
		return 1;
	}

	// check if file with path relative to cue file exists
	std::string tmpstr(pathname + "/" + filename);
	if (stat(tmpstr.c_str(), &test) == 0)
	{
		filename = tmpstr;
		return 1;
	}

	return 0;
}

static const char * LoadCueSheet(drive_t *drv, const char *cuefile)
{
	printf("LoadCueSheet(%s)\n", cuefile);

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
	static char tmp[1024];

	strcpy(tmp, cuefile);
	std::string pathname(dirname(tmp));

	std::ifstream in;
	in.open(cuefile, std::ios::in);
	if (in.fail()) return 0;

	while (!in.eof())
	{
		// get next line
		char buf[256];
		in.getline(buf, sizeof(buf));
		if (in.fail() && !in.eof())
		{
			return 0;  // probably a binary file
		}

		std::istringstream line(buf);

		std::string command;
		GetCueKeyword(command, line);

		//printf("command: %s\n", command.c_str());

		if (command == "TRACK")
		{
			if (canAddTrack) success = AddTrack(drv, &track, shift, prestart, totalPregap, currPregap);
			else success = 1;

			track.start = 0;
			track.skip = 0;
			currPregap = 0;
			prestart = -1;

			line >> track_number; // (cin) read into a true int first

			track.number = static_cast<uint8_t>(track_number);

			std::string type;
			GetCueKeyword(type, line);

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

			canAddTrack = true;
		}
		else if (command == "INDEX")
		{
			int index;
			line >> index;
			uint32_t frame;
			success = GetCueFrame(frame, line);

			if (index == 1) track.start = frame;
			else if (index == 0) prestart = static_cast<int32_t>(frame);
			// ignore other indices
		}
		else if (command == "FILE")
		{
			if (canAddTrack) success = AddTrack(drv, &track, shift, prestart, totalPregap, currPregap);
			else success = 1;
			canAddTrack = 0;

			std::string filename;
			GetCueString(filename, line);
			GetRealFileName(filename, pathname);
			std::string type;
			GetCueKeyword(type, line);
			strcpy(track.filename, filename.c_str());
		}
		else if (command == "PREGAP") success = GetCueFrame(currPregap, line);
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
	if (!AddTrack(drv, &track, shift, prestart, totalPregap, currPregap))
	{
		return 0;
	}

	// add lead-out track
	track.number++;
	track.attr = 0;//sync with load iso
	track.start = 0;
	track.length = 0;

	if (!AddTrack(drv, &track, shift, -1, totalPregap, 0))
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

static int GetCDTracks(drive_t *drv, int& start_track_num, int& end_track_num, TMSF& lead_out_msf)
{
	if (drv->track_cnt < 2 || !drv->track[0].length) return 0;

	start_track_num = drv->track[0].number;
	end_track_num = drv->track[drv->track_cnt - 2].number;
	lead_out_msf = frames_to_msf(drv->track[drv->track_cnt - 1].start + REDBOOK_FRAME_PADDING);
	return 1;
}

static int GetCDTrackInfo(drive_t *drv, int requested_track_num, TMSF& start_msf, unsigned char& attr)
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

	memset(buf, 0, 8);

	if (!GetCDTracks(drv, first, last, leadOut))
	{
		printf("WARNING: ATAPI READ TOC failed to get track info\n");
		return 8;
	}

	/* start 2 bytes out. we'll fill in the data length later */
	write = buf + 2;

	if (Format == 1) /* Read multisession info */
	{
		unsigned char attr;
		TMSF start;

		*write++ = (unsigned char)1;        /* @+2 first complete session */
		*write++ = (unsigned char)1;        /* @+3 last complete session */

		if (!GetCDTrackInfo(drv, first, start, attr))
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

			if (!GetCDTrackInfo(drv, track, start, attr))
			{
				printf("WARNING: ATAPI READ TOC unable to read track %u information\n", track);
				attr = 0x41; /* ADR=1 CONTROL=4 */
				start.min = 0;
				start.sec = 0;
				start.fr = 0;
			}

			if (track < Track) continue;
			if ((write + 8) > (buf + AllocationLength)) break;

			printf("Track %u attr=0x%02x %02u:%02u:%02u\n", first, attr, start.min, start.sec, start.fr);

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

		if ((write + 8) <= (buf + AllocationLength))
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
	unsigned int x = (unsigned int)(write - buf) - 2;
	buf[0] = x >> 8;
	buf[1] = x & 0xFF;

	printf("read_toc result:\n");
	hexdump(buf, write - buf);

	return write - buf;
}

static uint16_t mode_sense(int page)
{
	uint8_t *write = buf;
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
		*write++ = 0xFF;    /* +9 output port 0 volume (0xFF = 0dB atten.) */
		*write++ = 0x02;    /* +10 output port 1 selection (0010b = channel 1) */
		*write++ = 0xFF;    /* +11 output port 1 volume (0xFF = 0dB atten.) */
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
	x = (uint32_t)(write - buf) - 2;
	buf[0] = (unsigned char)(x >> 8u);
	buf[1] = (unsigned char)x;

	hexdump(buf, x + 2);
	return x + 2;
}

static int GetCDSub(drive_t *drv, unsigned char& attr, unsigned char& track_num, unsigned char& index, TMSF& relative_msf, TMSF& absolute_msf)
{
	attr = drv->track[0].attr;
	track_num = 1;
	index = 1;
	relative_msf.min = relative_msf.fr = 0; relative_msf.sec = 2;
	absolute_msf.min = absolute_msf.fr = 0; absolute_msf.sec = 2;
	return 1;
}

static int read_subchannel(drive_t *drv, uint8_t* cmdbuf)
{
	unsigned char paramList = cmdbuf[3];
	unsigned char attr, track, index;
	bool SUBQ = !!(cmdbuf[2] & 0x40);
	bool TIME = !!(cmdbuf[1] & 2);
	unsigned char *write;
	unsigned char astat;
	bool playing = drv->playing, pause = drv->paused;
	TMSF rel, abs;

	if (paramList == 0 || paramList > 3)
	{
		printf("ATAPI READ SUBCHANNEL unknown param list\n");
		memset(buf, 0, 8);
		return 8;
	}
	else if (paramList == 2)
	{
		printf("ATAPI READ SUBCHANNEL Media Catalog Number not supported\n");
		memset(buf, 0, 8);
		return 8;
	}
	else if (paramList == 3) {
		printf("ATAPI READ SUBCHANNEL ISRC not supported\n");
		memset(buf, 0, 8);
		return 8;
	}

	/* get current subchannel position */
	if (!GetCDSub(drv, attr, track, index, rel, abs))
	{
		printf("ATAPI READ SUBCHANNEL unable to read current pos\n");
		memset(buf, 0, 8);
		return 8;
	}

	if (playing)
		astat = pause ? 0x12 : 0x11;
	else
		astat = 0x13;

	memset(buf, 0, 8);
	write = buf;
	*write++ = 0x00;
	*write++ = astat;/* AUDIO STATUS */
	*write++ = 0x00;/* SUBCHANNEL DATA LENGTH */
	*write++ = 0x00;

	if (SUBQ) {
		*write++ = 0x01;    /* subchannel data format code */
		*write++ = (attr >> 4) | 0x10;  /* ADR/CONTROL */
		*write++ = track;
		*write++ = index;
		if (TIME) {
			*write++ = 0x00;
			*write++ = abs.min;
			*write++ = abs.sec;
			*write++ = abs.fr;
			*write++ = 0x00;
			*write++ = rel.min;
			*write++ = rel.sec;
			*write++ = rel.fr;
		}
		else {
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

	{
		unsigned int x = (unsigned int)(write - buf) - 4;
		buf[2] = x >> 8;
		buf[3] = x;
	}

	cddbg_hexdump(buf, write - buf);
	return write - buf;
}

const char * x86_ide_parse_cd(uint32_t num, const char *filename)
{
	int drv = num & 1;
	num >>= 1;

	const char *path = getFullPath(filename);

	const char *res = LoadCueSheet(&ide_inst[num].drive[drv], path);
	if(!res) res = LoadIsoFile(&ide_inst[num].drive[drv], path);

	return res;
}

void x86_ide_set(uint32_t num, uint32_t baseaddr, fileTYPE *f, int ver, int cd)
{
	int drv = (ver == 3) ? (num & 1) : 0;
	if (ver == 3) num >>= 1;

	ide_inst[num].base = baseaddr;
	ide_inst[num].drive[drv].f = f;

	ide_inst[num].drive[drv].hd_cylinders = 0;
	ide_inst[num].drive[drv].hd_heads = 0;
	ide_inst[num].drive[drv].hd_spt = 0;
	ide_inst[num].drive[drv].hd_total_sectors = 0;

	ide_inst[num].drive[drv].present = f ? 1 : 0;
	ide_inst[num].state = IDE_STATE_RESET;

	ide_inst[num].drive[drv].cd = ide_inst[num].drive[drv].present && (ver == 3) && num && cd;

	if (ver == 3)
	{
		if (f && ide_inst[num].drive[drv].placeholder && !ide_inst[num].drive[drv].cd)
		{
			printf("Cannot hot-mount HDD image to CD!\n");
			FileClose(ide_inst[num].drive[drv].f);
			ide_inst[num].drive[drv].f = 0;
			f = 0;
			ide_inst[num].drive[drv].present = 0;
		}

		ide_inst[num].drive[drv].placeholder = ide_inst[num].drive[drv].allow_placeholder;
		if (ide_inst[num].drive[drv].placeholder && ide_inst[num].drive[drv].present && !ide_inst[num].drive[drv].cd) ide_inst[num].drive[drv].placeholder = 0;
		if (ide_inst[num].drive[drv].placeholder) ide_inst[num].drive[drv].cd = 1;

		IOWR(ide_inst[num].base, 6, ((ide_inst[num].drive[drv].present || ide_inst[num].drive[drv].placeholder) ? 9 : 8) << (drv * 4), 1);
		IOWR(ide_inst[num].base, 6, 0x200, 1);
	}
	else if (!ide_inst[num].drive[drv].present)
	{
		return;
	}

	if (ide_inst[num].drive[drv].present && !ide_inst[num].drive[drv].cd)
	{
		ide_inst[num].drive[drv].hd_heads = 16;
		ide_inst[num].drive[drv].hd_spt = (ver == 3) ? 256 : 63;
		ide_inst[num].drive[drv].hd_cylinders = ide_inst[num].drive[drv].f->size / (ide_inst[num].drive[drv].hd_heads * ide_inst[num].drive[drv].hd_spt * 512);

		//Maximum 137GB images are supported.
		if (ide_inst[num].drive[drv].hd_cylinders > 65535) ide_inst[num].drive[drv].hd_cylinders = 65535;
		ide_inst[num].drive[drv].hd_total_sectors = (ide_inst[num].drive[drv].f->size / 512); // ide_inst[num].drive[drv].hd_spt * ide_inst[num].drive[drv].hd_heads * ide_inst[num].drive[drv].hd_cylinders;
	}

	if (!ide_inst[num].drive[drv].cd)
	{
		uint32_t identify[256] =
		{
			0x0040, 											//word 0
			ide_inst[num].drive[drv].hd_cylinders, 				//word 1
			0x0000,												//word 2 reserved
			ide_inst[num].drive[drv].hd_heads,					//word 3
			(uint16_t)(512 * ide_inst[num].drive[drv].hd_spt),	//word 4
			512,												//word 5
			ide_inst[num].drive[drv].hd_spt,					//word 6
			0x0000,												//word 7 vendor specific
			0x0000,												//word 8 vendor specific
			0x0000,												//word 9 vendor specific
			('A' << 8) | 'O',									//word 10
			('H' << 8) | 'D',									//word 11
			('0' << 8) | '0',									//word 12
			('0' << 8) | '0',									//word 13
			('0' << 8) | ' ',									//word 14
			(' ' << 8) | ' ',									//word 15
			(' ' << 8) | ' ',									//word 16
			(' ' << 8) | ' ',									//word 17
			(' ' << 8) | ' ',									//word 18
			(' ' << 8) | ' ',									//word 19
			3,   												//word 20 buffer type
			512,												//word 21 cache size
			4,													//word 22 number of ecc bytes
			0,0,0,0,											//words 23..26 firmware revision
			(' ' << 8) | ' ',									//words 27..46 model number
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			0x8010,												//word 47 max multiple sectors
			1,													//word 48 dword io
			1 << 9,												//word 49 lba supported
			0x4001,												//word 50 reserved
			0x0200,												//word 51 pio timing
			0x0200,												//word 52 pio timing
			0x0007,												//word 53 valid fields
			ide_inst[num].drive[drv].hd_cylinders, 				//word 54
			ide_inst[num].drive[drv].hd_heads,					//word 55
			ide_inst[num].drive[drv].hd_spt,					//word 56
			ide_inst[num].drive[drv].hd_total_sectors & 0xFFFF,	//word 57
			ide_inst[num].drive[drv].hd_total_sectors >> 16,	//word 58
			0x110,												//word 59 multiple sectors
			ide_inst[num].drive[drv].hd_total_sectors & 0xFFFF,	//word 60
			ide_inst[num].drive[drv].hd_total_sectors >> 16,	//word 61
			0x0000,												//word 62 single word dma modes
			0x0000,												//word 63 multiple word dma modes
			0x0000,												//word 64 pio modes
			120,120,120,120,									//word 65..68
			0,0,0,0,0,0,0,0,0,0,0,								//word 69..79
			0x007E,												//word 80 ata modes
			0x0000,												//word 81 minor version number
			(1 << 14) | (1 << 9), 								//word 82 supported commands
			(1 << 14) | (1 << 13) | (1 << 12) | (1 << 10),		//word 83
			1 << 14,	    									//word 84
			(1 << 14) | (1 << 9),  								//word 85
			(1 << 14) | (1 << 13) | (1 << 12) | (1 << 10),		//word 86
			1 << 14,	    									//word 87
			0x0000,												//word 88
			0,0,0,0,											//word 89..92
			1 | (1 << 14) | (1 << 13) | (1 << 9) | (1 << 8) | (1 << 3) | (1 << 1) | (1 << 0), //word 93
			0,0,0,0,0,0,										//word 94..99
			ide_inst[num].drive[drv].hd_total_sectors & 0xFFFF,	//word 100
			ide_inst[num].drive[drv].hd_total_sectors >> 16,	//word 101
			0,													//word 102
			0,													//word 103

			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	//word 104..127

			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,					//word 128..255
			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
		};
		for (int i = 0; i < 256; i++) ide_inst[num].drive[drv].id[i] = (uint16_t)identify[i];
	}
	else
	{
		uint32_t identify[256] =
		{
			0x8580, 											//word 0
			0x0000, 											//word 1
			0x0000,												//word 2 reserved
			0x0000,												//word 3
			0x0000,												//word 4
			0x0000,												//word 5
			0x0000,												//word 6
			0x0000,												//word 7 vendor specific
			0x0000,												//word 8 vendor specific
			0x0000,												//word 9 vendor specific
			('A' << 8) | 'O',									//word 10
			('C' << 8) | 'D',									//word 11
			('0' << 8) | '0',									//word 12
			('0' << 8) | '0',									//word 13
			('0' << 8) | ' ',									//word 14
			(' ' << 8) | ' ',									//word 15
			(' ' << 8) | ' ',									//word 16
			(' ' << 8) | ' ',									//word 17
			(' ' << 8) | ' ',									//word 18
			(' ' << 8) | ' ',									//word 19
			0x0000,												//word 20 buffer type
			0x0000,												//word 21 cache size
			0x0000,												//word 22 number of ecc bytes
			0,0,0,0,											//words 23..26 firmware revision
			(' ' << 8) | ' ',									//words 27..46 model number
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			(' ' << 8) | ' ',
			0x0000,												//word 47
			0x0000,												//word 48
			1 << 9,												//word 49 lba supported
			0x0000,												//word 50
			0x0000,												//word 51
			0x0000,												//word 52
			0x0007,												//word 53 valid fields
			0x0000,												//word 54
			0x0000,												//word 55
			0x0000,												//word 56
			0x0000,												//word 57
			0x0000,												//word 58
			0x0000,												//word 59
			0x0000,												//word 60
			0x0000,												//word 61
			0x0000,												//word 62
			0x0000,												//word 63 multiple word dma modes
			0x0000,												//word 64 pio modes
			120,120,120,120,									//word 65..68
			0,0,0,0,0,0,0,0,0,0,0,								//word 69..79
			0x007E,												//word 80 ata modes
			0x0000,												//word 81 minor version number
			(1 << 9) | (1 << 4), 								//word 82 supported commands
			(1 << 14),											//word 83
			1 << 14,	    									//word 84
			(1 << 14) | (1 << 9) | (1 << 4), 					//word 85
			0,													//word 86
			1 << 14,	    									//word 87
			0x0000,												//word 88
			0,0,0,0,											//word 89..92
			1 | (1 << 14) | (1 << 13) | (1 << 9) | (1 << 8) | (1 << 3) | (1 << 1) | (1 << 0), //word 93
			0,0,0,0,0,0,										//word 94..99
			0x0000,												//word 100
			0x0000,												//word 101
			0x0000,												//word 102
			0x0000,												//word 103

			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	//word 104..127

			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,					//word 128..255
			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
		};

		for (int i = 0; i < 256; i++) ide_inst[num].drive[drv].id[i] = (uint16_t)identify[i];

		ide_inst[num].drive[drv].load_state = ide_inst[num].drive[drv].f ? 1 : 3;
	}

	if (ide_inst[num].drive[drv].present)
	{
		char *name = ide_inst[num].drive[drv].f->name;
		for (int i = 0; i < 20; i++)
		{
			if (*name) ide_inst[num].drive[drv].id[27 + i] = ((*name++) << 8) | 0x20;
			if (*name) ide_inst[num].drive[drv].id[27 + i] = (ide_inst[num].drive[drv].id[27 + i] & 0xFF00) | (*name++);
		}
	}

	if (ver < 3)
	{
		for (int i = 0; i < 128; i++) IOWR(ide_inst[num].base, 0, ide_inst[num].drive[drv].present ? (ide_inst[num].drive[drv].id[2 * i + 1] << 16) | ide_inst[num].drive[drv].id[2 * i + 0] : 0, ver);

		IOWR(ide_inst[num].base, 1, ide_inst[num].drive[drv].hd_cylinders, ver);
		IOWR(ide_inst[num].base, 2, ide_inst[num].drive[drv].hd_heads, ver);
		IOWR(ide_inst[num].base, 3, ide_inst[num].drive[drv].hd_spt, ver);
		IOWR(ide_inst[num].base, 4, ide_inst[num].drive[drv].hd_spt * ide_inst[num].drive[drv].hd_heads, ver);
		IOWR(ide_inst[num].base, 5, ide_inst[num].drive[drv].hd_spt * ide_inst[num].drive[drv].hd_heads * ide_inst[num].drive[drv].hd_cylinders, ver);
		IOWR(ide_inst[num].base, 6, 0, ver); // base LBA
	}

	printf("HDD%d:\n  present %d\n  hd_cylinders %d\n  hd_heads %d\n  hd_spt %d\n  hd_total_sectors %d\n\n", (ver < 3) ? num : (num * 2 + drv), ide_inst[num].drive[drv].present, ide_inst[num].drive[drv].hd_cylinders, ide_inst[num].drive[drv].hd_heads, ide_inst[num].drive[drv].hd_spt, ide_inst[num].drive[drv].hd_total_sectors);
}

static void process_read(ide_config *ide)
{
	uint32_t lba = ide->regs.sector | (ide->regs.cylinder << 8) | (ide->regs.head << 24);

	uint16_t cnt = ide->regs.sector_count;
	if (!cnt || cnt > io_max_size) cnt = io_max_size;

	if (ide->state == IDE_STATE_INIT_RW)
	{
		//printf("Read from LBA: %d\n", lba);
		ide->null = !FileSeekLBA(ide->drive[ide->regs.drv].f, lba);
	}

	if (!ide->null) ide->null = (FileReadAdv(ide->drive[ide->regs.drv].f, buf, cnt * 512, -1) <= 0);
	if (ide->null) memset(buf, 0, cnt * 512);

	ide_send_data(buf, cnt * 128);

	lba += cnt;
	ide->regs.sector_count -= cnt;

	ide->regs.sector = lba;
	lba >>= 8;
	ide->regs.cylinder = lba;
	lba >>= 16;
	ide->regs.head = lba & 0xF;

	ide->state = ide->regs.sector_count ? IDE_STATE_WAIT_RD : IDE_STATE_WAIT_END;

	ide->regs.io_size = cnt;
	ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_DRQ | ATA_STATUS_IRQ;
	set_regs(ide);
}

static void prep_write(ide_config *ide)
{
	ide->prepcnt = ide->regs.sector_count;
	if (!ide->prepcnt || ide->prepcnt > io_max_size) ide->prepcnt = io_max_size;

	ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_DRQ | ATA_STATUS_IRQ;

	if (ide->state == IDE_STATE_INIT_RW)
	{
		uint32_t lba = ide->regs.sector | (ide->regs.cylinder << 8) | (ide->regs.head << 24);
		//printf("Write to LBA: %d\n", lba);
		ide->null = !FileSeekLBA(ide->drive[ide->regs.drv].f, lba);
		ide->regs.status &= ~ATA_STATUS_IRQ;
	}

	ide->state = IDE_STATE_WAIT_WR;
	ide->regs.io_size = ide->prepcnt;
	set_regs(ide);
}

static void process_write(ide_config *ide)
{
	ide_recv_data(buf, ide->prepcnt * 128);
	if (!ide->null) ide->null = (FileWriteAdv(ide->drive[ide->regs.drv].f, buf, ide->prepcnt * 512, -1) <= 0);

	uint32_t lba = ide->regs.sector | (ide->regs.cylinder << 8) | (ide->regs.head << 24);
	lba += ide->prepcnt;
	ide->regs.sector_count -= ide->prepcnt;

	ide->regs.sector = lba;
	lba >>= 8;
	ide->regs.cylinder = lba;
	lba >>= 16;
	ide->regs.head = lba & 0xF;
}

static int handle_hdd(ide_config *ide)
{
	switch (ide->regs.cmd)
	{
	case 0xEC: // identify
	{
		//print_regs(&ide->regs);
		ide_send_data(ide->drive[ide->regs.drv].id, 128);

		uint8_t drv = ide->regs.drv;
		memset(&ide->regs, 0, sizeof(ide->regs));
		ide->regs.drv = drv;
		ide->regs.io_size = 1;
		ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_DRQ | ATA_STATUS_IRQ;
		set_regs(ide);
		ide->state = IDE_STATE_WAIT_END;
	}
	break;

	case 0x20: // read with retry
	case 0x21: // read
	case 0xC4: // read multiple
	{
		if (!ide->regs.lba)
		{
			printf("(!) Unsupported Non-LBA read!\n");
			return 1;
		}

		ide->state = IDE_STATE_INIT_RW;
		process_read(ide);
	}
	break;

	case 0x30: // write with retry
	case 0x31: // write
	case 0xC5: // write multiple
	{
		if (!ide->regs.lba)
		{
			printf("(!) Unsupported Non-LBA write!\n");
			return 1;
		}

		ide->state = IDE_STATE_INIT_RW;
		prep_write(ide);
	}
	break;

	case 0xC6: // set multople
	{
		if (!ide->regs.sector_count || ide->regs.sector_count > io_max_size)
		{
			return 1;
		}

		ide->regs.status = ATA_STATUS_RDY;
		set_regs(ide);
	}
	break;

	case 0x08: // reset (fail)
		printf("reset for ide not supported\n");
		return 1;

	default:
		printf("(!) Unsupported command\n");
		print_regs(&ide->regs);
		return 1;
	}

	return 0;
}

static void pkt_send(ide_config *ide, void *data, uint16_t size)
{
	ide_send_data(data, (size + 3) / 4);

	ide->regs.pkt_io_size = (size+1)/2;
	ide->regs.cylinder = size;
	ide->regs.sector_count = 2;

	ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_DRQ | ATA_STATUS_IRQ;
	set_regs(ide);
	ide->state = IDE_STATE_WAIT_PKT_RD;
}

static void read_cd_sectors(ide_config *ide, int cnt)
{
	uint32_t sz = ide->drive[ide->regs.drv].track[0].sectorSize;
	fileTYPE *f = ide->drive[ide->regs.drv].f;

	if (sz == 2048)
	{
		if (!ide->null) ide->null = (FileReadAdv(f, buf, cnt * sz, -1) <= 0);
		if (ide->null) memset(buf, 0, cnt * sz);
		return;
	}

	uint32_t pre = ide->drive[ide->regs.drv].track[0].mode2 ? 24 : 16;
	uint32_t post = sz - pre - 2048;
	uint32_t off = 0;

	while (cnt--)
	{
		if (!ide->null) ide->null = !FileSeek(f, pre, SEEK_CUR);
		if (!ide->null) ide->null = (FileReadAdv(f, buf + off, 2048, -1) <= 0);
		if (ide->null) memset(buf + off, 0, 2048);
		if (!ide->null) ide->null = !FileSeek(f, post, SEEK_CUR);
		off += 2048;
	}
}

static void process_cd_read(ide_config *ide)
{
	uint32_t cnt = ide->regs.pkt_cnt;
	if ((cnt * 4) > io_max_size) cnt = io_max_size / 4;

	while ((cnt * 2048) > ide->regs.pkt_size_limit)
	{
		if (cnt <= 1) break;
		cnt--;
	}

	if (cnt != ide->regs.pkt_cnt)
	{
		cddbg_printf("** partial CD read\n");
	}

	if (ide->state == IDE_STATE_INIT_RW)
	{
		uint32_t pos = ide->regs.pkt_lba * ide->drive[ide->regs.drv].track[0].sectorSize;

		//printf("Read from pos: %d\n", pos);
		ide->null = (FileSeek(ide->drive[ide->regs.drv].f, pos, SEEK_SET) < 0);
	}

	read_cd_sectors(ide, cnt);

	//printf("\nsector:\n");
	//hexdump(buf, 512, 0);

	ide->regs.pkt_cnt -= cnt;
	pkt_send(ide, buf, cnt * 2048);
}

static int cd_inquiry(uint8_t maxlen)
{
	static const char vendor[] = "MiSTer  ";
	static const char product[] = "CDROM           ";

	memset(buf, 0, 47);
	buf[0] = (0 << 5) | 5;  /* Peripheral qualifier=0   device type=5 (CDROM) */
	buf[1] = 0x80;			/* RMB=1 removable media */
	buf[2] = 0x00;			/* ANSI version */
	buf[3] = 0x21;
	buf[4] = maxlen - 5;    /* additional length */

	for (int i = 0; i < 8; i++) buf[i + 8] = (unsigned char)vendor[i];
	for (int i = 0; i < 16; i++) buf[i + 16] = (unsigned char)product[i];
	for (int i = 0; i < 4; i++) buf[i + 32] = ' ';
	for (int i = 0; i < 11; i++) buf[i + 36] = ' ';

	hexdump(buf, maxlen);
	return maxlen;
}

#define CD_ERR_NO_MEDIUM ((2 << 4) | ATA_ERR_ABRT)

static void cd_reply(ide_config *ide, uint8_t error)
{
	ide->state = IDE_STATE_IDLE;
	ide->regs.sector_count = 3;
	ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_IRQ | (error ? ATA_STATUS_ERR : 0);
	ide->regs.error = error;
	set_regs(ide);
}

static void set_sense(uint8_t SK, uint8_t ASC = 0, uint8_t ASCQ = 0)
{
	int len = 18;
	memset(buf, 0, len);

	buf[0] = 0x70;    /* RESPONSE CODE */
	buf[2] = SK & 0xF;  /* SENSE KEY */
	buf[7] = len - 18;    /* additional sense length */
	buf[12] = ASC;
	buf[13] = ASCQ;
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
		set_sense(0);
		break;
	}

	cddbg_hexdump(buf, 18);
	return 18;
}


static void pause_resume(drive_t *drv, uint8_t *cmdbuf)
{
	bool resume = !!(cmdbuf[8] & 1);
	if(drv->playing) drv->paused = !resume;
}

static void play_audio_msf(drive_t *drv, uint8_t *cmdbuf)
{
	uint32_t start_lba, end_lba;

	if (cmdbuf[3] == 0xFF && cmdbuf[4] == 0xFF && cmdbuf[5] == 0xFF)
	{
		start_lba = 0xFFFFFFFF;
	}
	else
	{
		start_lba = (cmdbuf[3] * 60u * 75u) + (cmdbuf[4] * 75u) + cmdbuf[5];

		if (start_lba >= 150u) start_lba -= 150u; /* LBA sector 0 == M:S:F sector 0:2:0 */
		else end_lba = 0;
	}

	if (cmdbuf[6] == 0xFF && cmdbuf[7] == 0xFF && cmdbuf[8] == 0xFF)
	{
		end_lba = 0xFFFFFFFF;
	}
	else
	{
		end_lba = (cmdbuf[6] * 60u * 75u) + (cmdbuf[7] * 75u) + cmdbuf[8];
		if (end_lba >= 150u) end_lba -= 150u; /* LBA sector 0 == M:S:F sector 0:2:0 */
		else end_lba = 0;
	}

	if (start_lba == end_lba)
	{
		drv->playing = 0;
		drv->paused = 0;
		return;
	}

	/* LBA 0xFFFFFFFF means start playing wherever the optics of the CD sit */
	if (start_lba != 0xFFFFFFFF)
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
	uint32_t start_lba;

	start_lba = ((uint32_t)cmdbuf[2] << 24) + ((uint32_t)cmdbuf[3] << 16) + ((uint32_t)cmdbuf[4] << 8) + ((uint32_t)cmdbuf[5] << 0);
	play_length = ((uint16_t)cmdbuf[7] << 8) + ((uint16_t)cmdbuf[8] << 0);

	if (play_length == 0)
	{
		drv->playing = 0;
		drv->paused = 0;
		return;
	}

	/* LBA 0xFFFFFFFF means start playing wherever the optics of the CD sit */
	if (start_lba != 0xFFFFFFFF)
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

static void process_pkt_cmd(ide_config *ide)
{
	uint8_t cmdbuf[16];
	ide_recv_data(cmdbuf, 3);
	ide_reset_buf();

	cddbg_hexdump(cmdbuf, 12, 0);

	ide->regs.pkt_cnt = 0;
	int err = 0;

	switch (cmdbuf[0])
	{
	case 0xA8: // read(12) sectors
	case 0x28: // read(10) sectors
		//printf("** Read Sectors\n");
		//hexdump(cmdbuf, 12, 0);
		ide->regs.pkt_cnt = (cmdbuf[0] == 0x28) ? ((cmdbuf[7] << 8) | cmdbuf[8]) : ((cmdbuf[6] << 24) | (cmdbuf[7] << 16) | (cmdbuf[8] << 8) | cmdbuf[9]);
		ide->regs.pkt_lba = ((cmdbuf[2] << 24) | (cmdbuf[3] << 16) | (cmdbuf[4] << 8) | cmdbuf[5]);
		if (!ide->regs.pkt_cnt)
		{
			// length 0 is not a error.
			cd_reply(ide, 0);
			break;
		}
		ide->state = IDE_STATE_INIT_RW;
		if(!ide->drive[ide->regs.drv].load_state) process_cd_read(ide);
		else cd_reply(ide, CD_ERR_NO_MEDIUM);
		break;

	case 0x25: // read capacity
		//printf("** Read Capacity\n");
		if (!ide->drive[ide->regs.drv].load_state)
		{
			uint32_t tmp = ide->drive[ide->regs.drv].f->size / 2048;;
			buf[0] = tmp >> 24;
			buf[1] = tmp >> 16;
			buf[2] = tmp >> 8;
			buf[3] = tmp;

			tmp = 2048;
			buf[4] = tmp >> 24;
			buf[5] = tmp >> 16;
			buf[6] = tmp >> 8;
			buf[7] = tmp;
			//hexdump(buf, 8, 0);
			pkt_send(ide, buf, 8);
		}
		else cd_reply(ide, CD_ERR_NO_MEDIUM);
		break;

	case 0x2B: // seek
		ide->drive[ide->regs.drv].playing = 0;
		ide->drive[ide->regs.drv].paused = 0;
		cd_reply(ide, 0);
		break;

	case 0x1E: // lock the cd door - doing nothing.
		cd_reply(ide, 0);
		break;

	case 0x5A: // mode sense
		pkt_send(ide, buf, mode_sense(cmdbuf[2]));
		break;

	case 0x42: // read sub
		cddbg_printf("read sub:\n");
		pkt_send(ide, buf, read_subchannel(&ide->drive[ide->regs.drv], cmdbuf));
		break;

	case 0x43: // read TOC
		pkt_send(ide, buf, read_toc(&ide->drive[ide->regs.drv], cmdbuf));
		break;

	case 0x12: // inquiry
		pkt_send(ide, buf, cd_inquiry(cmdbuf[4]));
		break;

	case 0x03: // mode sense
		cddbg_printf("get sense:\n");
		pkt_send(ide, buf, get_sense(&ide->drive[ide->regs.drv]));
		break;

	case 0x55: // mode select
		printf("mode select\n");
		ide->regs.cylinder = (cmdbuf[7] << 8) | cmdbuf[8];
		if (ide->regs.cylinder > 512) ide->regs.cylinder = 512;
		ide->regs.pkt_io_size = (ide->regs.cylinder + 1) / 2;
		ide->regs.sector_count = 0;
		ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_DRQ | ATA_STATUS_IRQ;
		set_regs(ide);
		ide->state = IDE_STATE_WAIT_PKT_MODE;
		break;

	case 0x00: // test unit ready
		if (!ide->drive[ide->regs.drv].load_state) cd_reply(ide, 0);
		else cd_reply(ide, CD_ERR_NO_MEDIUM);
		break;

	case 0x45: // play lba
		printf("CD PLAY AUDIO(10)\n");
		play_audio10(&ide->drive[ide->regs.drv], cmdbuf);
		cd_reply(ide, 0);
		break;

	case 0x47: // play msf
		printf("CD PLAY AUDIO MSF\n");
		play_audio_msf(&ide->drive[ide->regs.drv], cmdbuf);
		cd_reply(ide, 0);
		break;

	case 0x4B: // pause/resume
		printf("CD PAUSE/RESUME\n");
		pause_resume(&ide->drive[ide->regs.drv], cmdbuf);
		cd_reply(ide, 0);
		break;

	default:
		err = 1;
		break;
	}

	if (err)
	{
		printf("(!) Error in packet command %02X\n", cmdbuf[0]);
		hexdump(cmdbuf, 12, 0);
		cd_reply(ide, ATA_ERR_ABRT);
	}
}

static int handle_cd(ide_config *ide)
{
	uint8_t drv;

	/*
	static uint32_t delay = 0;
	if (delay && !CheckTimer(delay)) return 0;
	uint32_t afterdelay = delay;
	delay = 0;
	*/

	switch (ide->regs.cmd)
	{
	case 0xA1: // identify packet
		//print_regs(&ide->regs);
		cddbg_printf("identify packet\n");
		/*
		if (!afterdelay)
		{
			delay = GetTimer(3);
			cddbg_printf("wait..\n");
			return 0;
		}
		*/
		ide_send_data(ide->drive[ide->regs.drv].id, 128);
		drv = ide->regs.drv;
		memset(&ide->regs, 0, sizeof(ide->regs));
		ide->regs.drv = drv;
		ide->regs.pkt_io_size = 256;
		ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_DRQ | ATA_STATUS_IRQ;
		set_regs(ide);
		ide->state = IDE_STATE_WAIT_END;
		break;

	case 0xEC: // identify (fail)
		cddbg_printf("identify (CD)\n");
		/*
		if (!afterdelay)
		{
			delay = GetTimer(3);
			cddbg_printf("wait..\n");
			return 0;
		}
		*/
		ide->regs.sector = 1;
		ide->regs.sector_count = 1;
		ide->regs.cylinder = 0xEB14;
		ide->regs.head = 0;
		ide->regs.io_size = 0;
		return 1;

	case 0xA0: // packet
		cddbg_printf("cmd A0: %02X\n", ide->regs.features);
		if (ide->regs.features & 1)
		{
			cddbg_printf("Cancel A0 DMA transfer\n");
			return 1;
		}
		/*
		if (!afterdelay)
		{
			delay = GetTimer(3);
			cddbg_printf("wait..\n");
			return 0;
		}
		*/
		ide->regs.pkt_size_limit = ide->regs.cylinder;
		if (!ide->regs.pkt_size_limit) ide->regs.pkt_size_limit = io_max_size * 512;
		ide->regs.pkt_io_size = 6;
		ide->regs.sector_count = 1;
		ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_DRQ; // | ATA_STATUS_IRQ;
		set_regs(ide);
		ide->state = IDE_STATE_WAIT_PKT_CMD;
		break;

	case 0x08: // reset
		cddbg_printf("cmd 08\n");
		/*
		if (!afterdelay)
		{
			delay = GetTimer(3);
			cddbg_printf("wait..\n");
			return 0;
		}
		*/
		ide->drive[ide->regs.drv].playing = 0;
		ide->drive[ide->regs.drv].paused = 0;
		ide->regs.sector = 1;
		ide->regs.sector_count = 1;
		ide->regs.cylinder = 0xEB14;
		ide->regs.head = 0;
		ide->regs.io_size = 0;
		ide->regs.status = ATA_STATUS_RDY;
		set_regs(ide);
		break;

	case 0x00: // nop
		cddbg_printf("cmd 00\n");
		return 1; // must always fail

	default:
		printf("(!) Unsupported command\n");
		print_regs(&ide->regs);
		return 1;
	}

	return 0;
}

void x86_ide_io(int num, int req)
{
	ide_config *ide = &ide_inst[num];

	if (req == 0) // no request
	{
		if (ide->state == IDE_STATE_RESET)
		{
			ide->state = IDE_STATE_IDLE;

			ide->regs.status = ATA_STATUS_RDY;
			set_regs(ide);

			printf("IDE %04X reset finish\n", ide->base);
		}
	}
	else if (req == 4) // command
	{
		ide->state = IDE_STATE_IDLE;
		get_regs(ide);

		int err = 0;

		if (ide->drive[ide->regs.drv].cd) err = handle_cd(ide);
		else if (!ide->drive[ide->regs.drv].present) err = 1;
		else err = handle_hdd(ide);

		if (err)
		{
			ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_ERR | ATA_STATUS_IRQ;
			ide->regs.error = ATA_ERR_ABRT;
			set_regs(ide);
		}
	}
	else if (req == 5) // data request
	{
		dbg_printf("IDE data request\n");
		if (ide->state == IDE_STATE_WAIT_END)
		{
			ide->state = IDE_STATE_IDLE;
			ide->regs.status = ATA_STATUS_RDY;
			set_regs(ide);
		}
		else if (ide->state == IDE_STATE_WAIT_RD)
		{
			process_read(ide);
		}
		else if (ide->state == IDE_STATE_WAIT_WR)
		{
			process_write(ide);
			if (ide->regs.sector_count)
			{
				prep_write(ide);
			}
			else
			{
				ide->state = IDE_STATE_IDLE;
				ide->regs.status = ATA_STATUS_RDY;
				set_regs(ide);
			}
		}
		else if (ide->state == IDE_STATE_WAIT_PKT_CMD)
		{
			cddbg_printf("packet cmd received\n");
			process_pkt_cmd(ide);
		}
		else if (ide->state == IDE_STATE_WAIT_PKT_RD)
		{
			cddbg_printf("packet was read\n");
			if (ide->regs.pkt_cnt) process_cd_read(ide);
			else cd_reply(ide, 0);
		}
		else if (ide->state == IDE_STATE_WAIT_PKT_MODE)
		{
			ide_recv_data(buf, 128);
			printf("mode select data:\n");
			hexdump(buf, ide->regs.cylinder);
			cd_reply(ide, 0);
		}
		else
		{
			printf("(!) IDE unknown state!\n");
			ide->state = IDE_STATE_IDLE;
			ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_ERR | ATA_STATUS_IRQ;
			ide->regs.error = ATA_ERR_ABRT;
			set_regs(ide);
		}
	}
	else if (req == 6) // reset
	{
		if (ide->state != IDE_STATE_RESET)
		{
			printf("IDE %04X reset start\n", ide->base);
		}

		ide->drive[0].playing = 0;
		ide->drive[0].paused = 0;
		ide->drive[1].playing = 0;
		ide->drive[1].paused = 0;

		get_regs(ide);
		ide->regs.head = 0;
		ide->regs.error = 0;
		ide->regs.sector = 1;
		ide->regs.sector_count = 1;
		ide->regs.cylinder = (!ide->drive[ide->regs.drv].present) ? 0xFFFF : ide->drive[ide->regs.drv].cd ? 0xEB14 : 0x0000;
		if (ide->drive[ide->regs.drv].placeholder) ide->regs.cylinder = 0xEB14;
		ide->regs.status = ATA_STATUS_BSY;
		set_regs(ide);

		ide->state = IDE_STATE_RESET;
	}
}

int x86_ide_is_placeholder(int num)
{
	return ide_inst[num / 2].drive[num & 1].placeholder;
}

void x86_ide_reset(uint8_t hotswap)
{
	ide_inst[0].drive[0].placeholder = 0;
	ide_inst[0].drive[1].placeholder = 0;
	ide_inst[1].drive[0].placeholder = 0;
	ide_inst[1].drive[1].placeholder = 0;

	ide_inst[0].drive[0].allow_placeholder = 0;
	ide_inst[0].drive[1].allow_placeholder = 0;
	ide_inst[1].drive[0].allow_placeholder = hotswap & 1;
	ide_inst[1].drive[1].allow_placeholder = (hotswap >> 1) & 1;
}
