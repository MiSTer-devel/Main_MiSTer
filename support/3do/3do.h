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
};

extern p3docdd_t p3docdd;
extern uint32_t p3do_frame_cnt;


#define CD_DATA_IO_INDEX	0x8
#define CD_TOC_IO_INDEX	    0xC
#define SAVE_IO_INDEX		0x4 // fake download to trigger save loading

void p3do_poll();
void p3do_set_image(int num, const char *filename);
void p3do_reset();
//void p3do_fill_blanksave(uint8_t *buffer, uint32_t lba);
int p3do_send_data(uint8_t* buf, int len, uint8_t index);

#endif
