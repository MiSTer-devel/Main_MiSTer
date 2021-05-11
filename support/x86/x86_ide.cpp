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

#include "../../spi.h"
#include "../../user_io.h"
#include "../../file_io.h"
#include "../../hardware.h"
#include "x86.h"
#include "x86_ide.h"
#include "x86_cdrom.h"

#if 0
	#define dbg_printf     printf
	#define dbg_print_regs ide_print_regs
	#define dbg_hexdump    hexdump
#else
	#define dbg_printf(...)   void()
	#define dbg_print_regs    void
	#define dbg_hexdump(...)  void()
#endif

#define IOWR(base, reg, value, ver) x86_dma_set((base) + ((ver) ? (reg) : ((reg)<<2)), value)

#define ide_send_data(databuf, size) x86_dma_sendbuf(ide->base + 255, (size), (uint32_t*)(databuf))
#define ide_recv_data(databuf, size) x86_dma_recvbuf(ide->base + 255, (size), (uint32_t*)(databuf))

const uint32_t ide_io_max_size = 32;
uint8_t ide_buf[ide_io_max_size * 512];

ide_config ide_inst[2] = {};

void ide_print_regs(regs_t *regs)
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

void ide_get_regs(ide_config *ide)
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

void ide_set_regs(ide_config *ide)
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
}

void x86_ide_set(uint32_t num, uint32_t baseaddr, fileTYPE *f, int ver, int cd)
{
	int drvnum = num;
	int drv = (ver == 3) ? (num & 1) : 0;
	if (ver == 3) num >>= 1;

	drive_t *drive = &ide_inst[num].drive[drv];

	ide_inst[num].base = baseaddr;
	ide_inst[num].drive[drv].drvnum = drvnum;

	drive->f = f;

	drive->cylinders = 0;
	drive->heads = 0;
	drive->spt = 0;
	//drive->total_sectors = 0;

	drive->present = f ? 1 : 0;
	ide_inst[num].state = IDE_STATE_RESET;

	drive->cd = drive->present && (ver == 3) && num && cd;

	if (ver == 3)
	{
		if (f && drive->placeholder && !drive->cd)
		{
			printf("Cannot hot-mount HDD image to CD!\n");
			FileClose(drive->f);
			drive->f = 0;
			f = 0;
			drive->present = 0;
		}

		drive->placeholder = drive->allow_placeholder;
		if (drive->placeholder && drive->present && !drive->cd) drive->placeholder = 0;
		if (drive->placeholder) drive->cd = 1;

		IOWR(ide_inst[num].base, 6, ((drive->present || drive->placeholder) ? 9 : 8) << (drv * 4), 1);
		IOWR(ide_inst[num].base, 6, 0x200, 1);
	}
	else if (!drive->present)
	{
		return;
	}

	if(drive->f)
	{
		if (!drive->chd_f) drive->total_sectors = (drive->f->size / 512);
	} else {
		drive->total_sectors = 0;
	}

	if (!drive->cd)
	{
		if (drive->present)
		{
			drive->heads = 16;
			drive->spt = (ver == 3) ? 256 : 63;

			uint32_t cylinders = drive->f->size / (drive->heads * drive->spt * 512);
			if (cylinders > 65535) cylinders = 65535;

			//Maximum 137GB images are supported.
			drive->cylinders = cylinders;
		}

		uint16_t identify[256] =
		{
			0x0040, 											//word 0
			drive->cylinders,									//word 1
			0x0000,												//word 2 reserved
			drive->heads,										//word 3
			0x0000,												//word 4 obsolete
			0x0000,												//word 5 obsolete
			drive->spt,											//word 6
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
			drive->cylinders, 									//word 54
			drive->heads,										//word 55
			drive->spt,											//word 56
			(uint16_t)(drive->total_sectors & 0xFFFF),			//word 57
			(uint16_t)(drive->total_sectors >> 16),				//word 58
			0x110,												//word 59 multiple sectors
			(uint16_t)(drive->total_sectors & 0xFFFF),			//word 60 LBA-28
			(uint16_t)(drive->total_sectors >> 16),				//word 61 LBA-28
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
			(uint16_t)(drive->total_sectors & 0xFFFF),			//word 100 LBA-48
			(uint16_t)(drive->total_sectors >> 16),				//word 101 LBA-48
			0,													//word 102 LBA-48
			0,													//word 103 LBA-48

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
		for (int i = 0; i < 256; i++) drive->id[i] = identify[i];
	}
	else
	{
		uint16_t identify[256] =
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

		for (int i = 0; i < 256; i++) drive->id[i] = identify[i];
		drive->load_state = (drive->f || drive->chd_f) ? 1 : 3;
	}

	if (ide_inst[num].drive[drv].present)
	{
		char *name = ide_inst[num].drive[drv].f->name;
		for (int i = 0; i < 20; i++)
		{
			if (*name) drive->id[27 + i] = ((*name++) << 8) | 0x20;
			if (*name) drive->id[27 + i] = (drive->id[27 + i] & 0xFF00) | (*name++);
		}
	}

	if (ver < 3)
	{
		for (int i = 0; i < 128; i++) IOWR(ide_inst[num].base, 0, drive->present ? (drive->id[2 * i + 1] << 16) | drive->id[2 * i + 0] : 0, ver);

		IOWR(ide_inst[num].base, 1, drive->cylinders, ver);
		IOWR(ide_inst[num].base, 2, drive->heads, ver);
		IOWR(ide_inst[num].base, 3, drive->spt, ver);
		IOWR(ide_inst[num].base, 4, drive->spt * drive->heads, ver);
		IOWR(ide_inst[num].base, 5, drive->spt * drive->heads * drive->cylinders, ver);
		IOWR(ide_inst[num].base, 6, 0, ver); // base LBA
	}

	printf("HDD%d:\n  present %d\n  hd_cylinders %d\n  hd_heads %d\n  hd_spt %d\n  hd_total_sectors %d\n\n",
		(ver < 3) ? num : (num * 2 + drv), drive->present, drive->cylinders, drive->heads, drive->spt, drive->total_sectors);
}

static void process_read(ide_config *ide)
{
	uint32_t lba = ide->regs.sector | (ide->regs.cylinder << 8) | (ide->regs.head << 24);

	uint16_t cnt = ide->regs.sector_count;
	if (!cnt || cnt > ide_io_max_size) cnt = ide_io_max_size;

	if (ide->state == IDE_STATE_INIT_RW)
	{
		//printf("Read from LBA: %d\n", lba);
		ide->null = !FileSeekLBA(ide->drive[ide->regs.drv].f, lba);
	}

	if (!ide->null) ide->null = (FileReadAdv(ide->drive[ide->regs.drv].f, ide_buf, cnt * 512, -1) <= 0);
	if (ide->null) memset(ide_buf, 0, cnt * 512);

	ide_send_data(ide_buf, cnt * 128);

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
	ide_set_regs(ide);
}

static void prep_write(ide_config *ide)
{
	ide->prepcnt = ide->regs.sector_count;
	if (!ide->prepcnt || ide->prepcnt > ide_io_max_size) ide->prepcnt = ide_io_max_size;

	ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_DRQ | ATA_STATUS_IRQ;

	if (ide->state == IDE_STATE_INIT_RW)
	{
		ide->regs.status &= ~ATA_STATUS_IRQ;
		ide->null = 1;

		if (ide->regs.cmd != 0xFA)
		{
			uint32_t lba = ide->regs.sector | (ide->regs.cylinder << 8) | (ide->regs.head << 24);
			//printf("Write to LBA: %d\n", lba);
			ide->null = !FileSeekLBA(ide->drive[ide->regs.drv].f, lba);
		}
	}

	ide->state = IDE_STATE_WAIT_WR;
	ide->regs.io_size = ide->prepcnt;
	ide_set_regs(ide);
}

static void process_write(ide_config *ide)
{
	ide_recv_data(ide_buf, ide->prepcnt * 128);
	if (ide->regs.cmd == 0xFA)
	{
		ide->regs.sector_count = 0;
		char* filename = user_io_make_filepath(HomeDir(), (char*)ide_buf);
		int drvnum = (ide->regs.head == 1) ? 0 : (ide->regs.head == 2) ? 1 : (ide->drive[ide->regs.drv].drvnum + 2);

		static const char* names[6] = { "fdd0", "fdd1", "ide00", "ide01", "ide10", "ide11" };
		printf("Request for new image for drive %s: %s\n", names[drvnum], filename);
		x86_set_image(drvnum, filename);
	}
	else
	{
		if (!ide->null) ide->null = (FileWriteAdv(ide->drive[ide->regs.drv].f, ide_buf, ide->prepcnt * 512, -1) <= 0);

		uint32_t lba = ide->regs.sector | (ide->regs.cylinder << 8) | (ide->regs.head << 24);
		lba += ide->prepcnt;
		ide->regs.sector_count -= ide->prepcnt;

		ide->regs.sector = lba;
		lba >>= 8;
		ide->regs.cylinder = lba;
		lba >>= 16;
		ide->regs.head = lba & 0xF;
	}
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
		ide_set_regs(ide);
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

	case 0xFA: // mount image
	{
		ide->state = IDE_STATE_INIT_RW;
		prep_write(ide);
	}
	break;

	case 0xC6: // set multople
	{
		if (!ide->regs.sector_count || ide->regs.sector_count > ide_io_max_size)
		{
			return 1;
		}

		ide->regs.status = ATA_STATUS_RDY;
		ide_set_regs(ide);
	}
	break;

	case 0x08: // reset (fail)
		printf("Reset command (08h) for HDD not supported\n");
		return 1;

	default:
		printf("(!) Unsupported command\n");
		ide_print_regs(&ide->regs);
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
			ide_set_regs(ide);

			printf("IDE %04X reset finish\n", ide->base);
		}
	}
	else if (req == 4) // command
	{
		ide->state = IDE_STATE_IDLE;
		ide_get_regs(ide);

		int err = 0;

		if (ide->drive[ide->regs.drv].cd) err = cdrom_handle_cmd(ide);
		else if (!ide->drive[ide->regs.drv].present) err = 1;
		else err = handle_hdd(ide);

		if (err)
		{
			ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_ERR | ATA_STATUS_IRQ;
			ide->regs.error = ATA_ERR_ABRT;
			ide_set_regs(ide);
		}
	}
	else if (req == 5) // data request
	{
		dbg_printf("IDE data request\n");
		if (ide->state == IDE_STATE_WAIT_END)
		{
			ide->state = IDE_STATE_IDLE;
			ide->regs.status = ATA_STATUS_RDY;
			ide_set_regs(ide);
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
				ide_set_regs(ide);
			}
		}
		else if (ide->state == IDE_STATE_WAIT_PKT_CMD)
		{
			cdrom_handle_pkt(ide);
		}
		else if (ide->state == IDE_STATE_WAIT_PKT_RD)
		{
			if (ide->regs.pkt_cnt) cdrom_read(ide);
			else cdrom_reply(ide, 0);
		}
		else if (ide->state == IDE_STATE_WAIT_PKT_MODE)
		{
			ide_recv_data(ide_buf, 128);
			printf("mode select data:\n");
			hexdump(ide_buf, ide->regs.cylinder);
			cdrom_reply(ide, 0);
		}
		else
		{
			printf("(!) IDE unknown state!\n");
			ide->state = IDE_STATE_IDLE;
			ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_ERR | ATA_STATUS_IRQ;
			ide->regs.error = ATA_ERR_ABRT;
			ide_set_regs(ide);
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

		ide_get_regs(ide);
		ide->regs.head = 0;
		ide->regs.error = 0;
		ide->regs.sector = 1;
		ide->regs.sector_count = 1;
		ide->regs.cylinder = (!ide->drive[ide->regs.drv].present) ? 0xFFFF : ide->drive[ide->regs.drv].cd ? 0xEB14 : 0x0000;
		if (ide->drive[ide->regs.drv].placeholder) ide->regs.cylinder = 0xEB14;
		ide->regs.status = ATA_STATUS_BSY;
		ide_set_regs(ide);

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
