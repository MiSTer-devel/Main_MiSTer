#ifndef P3DO_H
#define P3DO_H

#include "../../cd.h"

//#define P3DO_DEBUG				1

// CDD command
#define P3DO_COMM_NOP				0x00
#define P3DO_COMM_READ			    0x10
#define P3DO_COMM_NEXT			    0x20

#define P3DO_DISC_DA_OR_CDROM    0x00
#define P3DO_DISC_CDI            0x10
#define P3DO_DISC_CDROM_XA       0x20

typedef enum {
	P3DO_Idle,
	P3DO_Open,
	P3DO_Read,
	P3DO_Pause,
	P3DO_Stop
} p3dostate_t;

class p3docdd_t
{
public:
	int loaded;
	p3dostate_t state;
	int speed;
	SendDataFunc SendData;

	p3docdd_t();
	int Load(const char *filename);
	void Unload();
	void Reset();
	void Process(uint8_t* time_mode);
	void Update();
	void CommandExec();
	uint8_t* GetStatus();
	int SetCommand(uint8_t* data);
	int GetDiscInfo(uint8_t *buf);

private:
	toc_t toc;
	int lba;
	int track;
	uint16_t sectorSize;
	int toc_pos;
	p3dostate_t next_state;
	bool lid_open;
	bool stop_pend;
	bool read_pend;
	bool read_toc;
	int block_reads;
	int block_count;
	int read_fmt;            // block format of the current read, from the core (READ command byte 7)
	bool read_sub;           // the core also wants the 96 subcode bytes of each block
	bool audio_le[100];      // CD-DA track stored little-endian (BIN/WAVE): sent as it is (disc order)
	uint8_t track_flags[100];// CUE FLAGS: control bits of the track (0x01 pre-emphasis, 0x02 copy permitted, 0x08 4 channels)
	bool da_clock;           // a CD-DA read is going on: its sectors come at the speed of the disc
	int da_base_lba;         // sector and time (ms) where that audio clock started
	unsigned long da_base_ms;
	int da_next_lba;         // first sector after the current audio read (a READ from there continues it)
	uint8_t stat[8];
	uint8_t comm[8];
	uint8_t cd_buf[4096 + 2];
	int chd_hunknum;
	uint8_t *chd_hunkbuf;

	int LoadCUE(const char* filename);
	int LoadISO(const char* filename);
	void LBAToMSF(int lba, msf_t* msf);
	int MSFToLBA(msf_t* msf);
	int GetLBAFromCommand(uint8_t* cmd);
	int GetBlocksFromCommand(uint8_t* cmd);
	void ReadData(uint8_t *buf);
	int DataSectorSend(uint8_t* header);
	void ReadRawSector(uint8_t *buf);
	int RawSectorSend();
	uint8_t TrackControl(int track);
	void MakeSubcode(uint8_t *sub);
};

// Block formats the core asks for (READ command byte 7 bits 1-0, from the drive's MODE SET block length;
// bit 2 = subcode requested)
#define P3DO_FMT_DATA     0  // 2048 bytes of user data
#define P3DO_FMT_RAW      1  // 2352 bytes: raw sector, or CD-DA (samples in disc order)
#define P3DO_FMT_RAW_2336 2  // 2336 bytes: raw sector without sync and header
#define P3DO_FMT_RAW_2340 3  // 2340 bytes: raw sector without sync

// CD-DA reads: sectors per second of the disc at single speed, and how many sectors the drive may
// deliver ahead of that clock
#define P3DO_DA_SECTORS_PER_S 75
#define P3DO_DA_READ_AHEAD    48

extern p3docdd_t p3docdd;
extern uint32_t p3do_frame_cnt;


#define SAVE_IO_INDEX		0x4 // fake download to trigger save loading
#define CD_DATA_IO_INDEX	0x8
#define CD_INFO_IO_INDEX	0xC

void p3do_poll();
void p3do_set_image(int num, const char *filename);
void p3do_reset();
void p3do_fill_blanksave(uint8_t *buf, uint32_t lba);
int p3do_send_data(uint8_t* buf, int len, uint8_t index);

#endif
