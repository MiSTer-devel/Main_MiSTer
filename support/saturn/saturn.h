#ifndef SATURN_H
#define SATURN_H

#include "../../cd.h"

// CDD command
#define SATURN_COMM_NOP				0x00
#define SATURN_COMM_SEEK_RING		0x02
#define SATURN_COMM_TOC				0x03
#define SATURN_COMM_STOP			0x04
#define SATURN_COMM_READ			0x06
#define SATURN_COMM_PAUSE			0x08
#define SATURN_COMM_SEEK			0x09
#define SATURN_COMM_FW_SCAN			0x0A
#define SATURN_COMM_RW_SCAN			0x0B

// CDD status
#define SATURN_STAT_NOP				0x00
#define SATURN_STAT_TOC				0x06
#define SATURN_STAT_STOP			0x12
#define SATURN_STAT_SEEK			0x22
#define SATURN_STAT_AUDIO			0x34
#define SATURN_STAT_DATA			0x36
#define SATURN_STAT_IDLE			0x46
#define SATURN_STAT_OPEN			0x80
#define SATURN_STAT_NODISK			0x83
#define SATURN_STAT_SEEK_RING		0xB2
#define SATURN_STAT_SEEK_RING2		0xB6

typedef enum {
	Idle,
	Open,
	ReadTOC,
	Read,
	Play,
	Pause,
	Stop,
	Seek,
	SeekRead,
	SeekRing
} satstate_t;

class satcdd_t
{
public:
	int loaded;
	satstate_t state;
	int speed;
	SendDataFunc SendData;

	satcdd_t();
	int Load(const char *filename);
	void Unload();
	void Reset();
	void Process(uint8_t* time_mode);
	void Update();
	void CommandExec();
	uint8_t* GetStatus();
	int SetCommand(uint8_t* data);

private:
	toc_t toc;
	int lba;
	int index;
	int seek_lba;
	uint16_t sectorSize;
	int toc_pos;
	satstate_t next_state;
	uint8_t stat[12];
	uint8_t comm[12];
	uint8_t cd_buf[4096 + 2];
	int audioLength;
	int audioFirst;

	int LoadCUE(const char* filename);
	void LBAToMSF(int lba, msf_t* msf);
	int GetFAD(uint8_t* cmd);
	void SetChecksum(uint8_t* stat);
	void ReadData(uint8_t *buf);
	int ReadCDDA(uint8_t *buf);
	void MakeSecureRingData(uint8_t *buf);
	int DataSectorSend(uint8_t* header, int speed);
	int AudioSectorSend();
	int RingDataSend(uint8_t* header, int speed);
};

extern satcdd_t satcdd;
extern uint32_t frame_cnt;

void saturn_poll();
void saturn_set_image(int num, const char *filename);
void saturn_reset();
void saturn_fill_blanksave(uint8_t *buffer, uint32_t lba);
int saturn_send_data(uint8_t* buf, int len, uint8_t index);

#endif
