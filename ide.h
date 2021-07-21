#ifndef IDE_H
#define IDE_H

#include "support/chd/mister_chd.h"

#define ATA_STATUS_BSY  0x80  // busy
#define ATA_STATUS_RDY  0x40  // ready
#define ATA_STATUS_RDP  0x20  // performance read
#define ATA_STATUS_DSC  0x10  // seek complete
#define ATA_STATUS_SERV 0x10  // service
#define ATA_STATUS_DRQ  0x08  // data request
#define ATA_STATUS_IRQ  0x04  // rise IRQ
#define ATA_STATUS_END  0x02  // last read
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
#define IDE_STATE_WAIT_PKT_CMD  3
#define IDE_STATE_WAIT_PKT_RD   4
#define IDE_STATE_WAIT_PKT_END  5
#define IDE_STATE_WAIT_PKT_MODE 6

struct regs_t
{
	uint8_t io_done;
	uint8_t io_fast;
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
};

struct track_t
{
	char     filename[1024];
	uint32_t start;
	uint32_t length;
	uint32_t skip;
	uint16_t sectorSize;
	uint8_t  attr;
	uint8_t  mode2;
	uint8_t  number;
	int      chd_offset;
};

struct drive_t
{
	fileTYPE *f;

	uint8_t  present;
	uint8_t  drvnum;

	uint16_t cylinders;
	uint16_t heads;
	uint16_t spt;
	uint32_t total_sectors;
	uint32_t spb;

	uint32_t offset;
	uint32_t type;

	uint8_t  placeholder;
	uint8_t  allow_placeholder;
	uint8_t  cd;
	uint8_t  load_state;
	uint8_t  last_load_state;
	uint8_t  track_cnt;
	uint8_t  data_num;
	track_t  track[50];

	uint8_t  playing;
	uint8_t  paused;
	uint32_t play_start_lba;
	uint32_t play_end_lba;

	chd_file *chd_f;
	int      chd_hunknum;
	uint8_t	 *chd_hunkbuf;
	uint32_t  chd_total_size;
	uint32_t  chd_last_partial_lba;

	uint16_t id[256];
};

struct ide_config
{
	uint32_t base;
	uint32_t bitoff;
	uint32_t state;
	uint32_t null;
	uint32_t prepcnt;
	regs_t   regs;

	drive_t drive[2];
};

struct chs_t
{
	uint32_t sectors;
	uint32_t heads;
	uint32_t cylinders;
	uint32_t offset;
};

#include "ide_cdrom.h"

extern ide_config ide_inst[2];
extern const uint32_t ide_io_max_size;
extern uint8_t ide_buf[];

void ide_print_regs(regs_t *regs);
void ide_get_regs(ide_config *ide);
void ide_set_regs(ide_config *ide);

void ide_sendbuf(ide_config *ide, uint16_t reg, uint32_t length, uint16_t *data);
void ide_recvbuf(ide_config *ide, uint16_t reg, uint32_t length, uint16_t *data);
void ide_reg_set(ide_config *ide, uint16_t reg, uint16_t value);

uint16_t ide_check(int status = 0);
int ide_img_mount(fileTYPE *f, const char *name, int rw);
void ide_img_set(uint32_t drvnum, fileTYPE *f, int cd, int sectors = 0, int heads = 0, int offset = 0, int type = 0);
int ide_is_placeholder(int num);
void ide_reset(uint8_t hotswap[4]);
int ide_open(uint8_t unit, const char* filename);

void ide_io(int num, int req);

#endif
