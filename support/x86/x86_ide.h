#ifndef X86_IDE_H
#define X86_IDE_H

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
	uint8_t  present;

	uint16_t cylinders;
	uint16_t heads;
	uint16_t spt;
	uint32_t total_sectors;

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

extern ide_config ide_inst[2];
extern const uint32_t ide_io_max_size;
extern uint8_t ide_buf[];

void ide_print_regs(regs_t *regs);
void ide_get_regs(ide_config *ide);
void ide_set_regs(ide_config *ide);

void x86_ide_set(uint32_t num, uint32_t baseaddr, fileTYPE *f, int ver, int cd);
void x86_ide_io(int num, int req);
int x86_ide_is_placeholder(int num);
void x86_ide_reset(uint8_t hotswap);

#endif
