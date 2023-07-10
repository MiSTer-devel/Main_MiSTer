#ifndef MEGACD_H
#define MEGACD_H


// CDD status
#define CD_STAT_STOP			0x00
#define CD_STAT_PLAY			0x01
#define CD_STAT_SEEK			0x02
#define CD_STAT_SCAN			0x03
#define CD_STAT_PAUSE			0x04
#define CD_STAT_OPEN			0x05
#define CD_STAT_NO_VALID_CHK	0x06
#define CD_STAT_NO_VALID_CMD	0x07
#define CD_STAT_ERROR			0x08
#define CD_STAT_TOC				0x09
#define CD_STAT_TRACK_MOVE		0x0A
#define CD_STAT_NO_DISC			0x0B
#define CD_STAT_END				0x0C
#define CD_STAT_TRAY			0x0E
#define CD_STAT_TEST			0x0F

// CDD command
#define CD_COMM_IDLE			0x00
#define CD_COMM_STOP			0x01
#define CD_COMM_TOC				0x02
#define CD_COMM_PLAY			0x03
#define CD_COMM_SEEK			0x04
//#define CD_COMM_OPEN			0x05
#define CD_COMM_PAUSE			0x06
#define CD_COMM_RESUME			0x07
#define CD_COMM_FW_SCAN			0x08
#define CD_COMM_RW_SCAN			0x09
#define CD_COMM_TRACK_MOVE		0x0A
#define CD_COMM_TRACK_PLAY		0x0B
#define CD_COMM_TRAY_CLOSE		0x0C
#define CD_COMM_TRAY_OPEN		0x0D

#define MCD_DATA_IO_INDEX 2
#define MCD_SUB_IO_INDEX 3
#define MCD_CDDA_IO_INDEX 4

#include "../../cd.h"
#include <libchdr/chd.h>

class cdd_t
{
public:
	uint32_t latency;
	uint8_t status;
	uint8_t isData;
	int loaded;
	SendDataFunc SendData;
	int (*CanSendData)(uint8_t type);

	cdd_t();
	int Load(const char *filename);
	void Unload();
	void Reset();
	void Update();
	void CommandExec();
	uint64_t GetStatus(uint8_t crc_start);
	int SetCommand(uint64_t c, uint8_t crc_start);

private:
	toc_t toc;
	int index;
	int lba;
	uint16_t sectorSize;
	int scanOffset;
	int audioLength;
	int audioOffset;
	int chd_hunknum;
	uint8_t *chd_hunkbuf;
	int chd_audio_read_lba;
	uint8_t stat[10];
	uint8_t comm[10];

	int LoadCUE(const char* filename);
	int LoadCHD(const char* filename);
	int SectorSend(uint8_t* header);
	int SubcodeSend();
	void ReadData(uint8_t *buf);
	int ReadCDDA(uint8_t *buf);
	int ReadSubcode(uint16_t* buf);
	void LBAToMSF(int lba, msf_t* msf);
	void MSFToLBA(int* lba, msf_t* msf);
	void MSFToLBA(int* lba, uint8_t m, uint8_t s, uint8_t f);
	void SeekToLBA(int lba, int play);
};

#define BCD(v)				 ((uint8_t)((((v)/10) << 4) | ((v)%10)))

#define CD_SCAN_SPEED 30

//cdd.cpp
extern cdd_t cdd;


void mcd_poll();
void mcd_set_image(int num, const char *filename);
void mcd_reset();
int mcd_send_data(uint8_t* buf, int len, uint8_t index);
int mcd_can_send_data(uint8_t type);
void mcd_fill_blanksave(uint8_t *buffer, uint32_t lba);

#endif
