#ifndef PCECD_H
#define PCECD_H


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
#define PCECD_COMM_TESTUNIT			0x00
#define PCECD_COMM_REQUESTSENSE		0x03
#define PCECD_COMM_READ6			0x08
#define PCECD_COMM_SAPSP			0xD8
#define PCECD_COMM_SAPEP			0xD9
#define PCECD_COMM_PAUSE			0xDA
#define PCECD_COMM_READSUBQ			0xDD
#define PCECD_COMM_GETDIRINFO		0xDE

#define PCECD_STATE_IDLE		0
#define PCECD_STATE_READ		1
#define PCECD_STATE_PLAY		2
#define PCECD_STATE_PAUSE		3


#include "../../cd.h"


class pcecdd_t
{
public:
	uint32_t latency;
	uint8_t status;
	uint8_t isData;
	int loaded;
	SendDataFunc SendData;
	int has_status;
	bool can_read_next;

	pcecdd_t();
	int Load(const char *filename);
	void Unload();
	void Reset();
	void Update();
	void CommandExec();
	int GetStatus(uint8_t* buf);
	int SetCommand(uint8_t* buf);

private:
	toc_t toc;
	int index;
	int lba;
	int cnt;
	uint16_t sectorSize;
	int scanOffset;
	int audioLength;
	int audioOffset;
	uint8_t state;
	int CDDAStart;
	int CDDAEnd;

	uint8_t stat[2];
	uint8_t comm[12];

	uint8_t sec_buf[2352 + 2];

	int LoadCUE(const char* filename);
	int SectorSend(uint8_t* header);
	void ReadData(uint8_t *buf);
	int ReadCDDA(uint8_t *buf);
	void ReadSubcode(uint16_t* buf);
	void LBAToMSF(int lba, msf_t* msf);
	void MSFToLBA(int* lba, msf_t* msf);
	void MSFToLBA(int* lba, uint8_t m, uint8_t s, uint8_t f);
	int GetTrackByLBA(int lba, toc_t* toc);
};

#define BCD(v)				 ((uint8_t)((((v)/10) << 4) | ((v)%10)))
#define U8(v)				 ((uint8_t)(((((v)&0xF0) >> 4) * 10) + ((v)&0x0F)))

#define CD_SCAN_SPEED 30

//pcecdd.cpp
extern pcecdd_t pcecdd;


void pcecd_poll();
void pcecd_set_image(int num, const char *filename);
int pcecd_send_data(uint8_t* buf, int len, uint8_t index);
void pcecd_reset();

#endif
