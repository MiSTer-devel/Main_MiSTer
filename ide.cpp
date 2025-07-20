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

#include "support/x86/x86.h"
#include "support/vhd/vhdcfg.h"
#include "support/minimig/minimig_hdd.h"
#include "support/minimig/minimig_config.h"
#include "spi.h"
#include "user_io.h"
#include "file_io.h"
#include "hardware.h"
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

#if 0
	#define dbg2_printf     printf
#else
	#define dbg2_printf(...)   void()
#endif

#define IDE0_BASE 0xF000
#define IDE1_BASE 0xF100

#define ide_send_data(databuf, size) ide_sendbuf(ide, 255, (size), (uint16_t*)(databuf))
#define ide_recv_data(databuf, size) ide_recvbuf(ide, 255, (size), (uint16_t*)(databuf))

#define SWAP(a)  ((((a)&0x000000ff)<<24)|(((a)&0x0000ff00)<<8)|(((a)&0x00ff0000)>>8)|(((a)&0xff000000)>>24))

void ide_reg_set(ide_config *ide, uint16_t reg, uint16_t value)
{
	EnableIO();
	spi8(UIO_DMA_WRITE);
	spi32_w(ide->base + reg);
	spi_w(value);
	DisableIO();
}

void ide_sendbuf(ide_config *ide, uint16_t reg, uint32_t length, uint16_t *data)
{
	EnableIO();
	fpga_spi_fast(UIO_DMA_WRITE);
	fpga_spi_fast(ide->base + reg);
	fpga_spi_fast(0);
	fpga_spi_fast_block_write(data, length);
	DisableIO();
}

void ide_recvbuf(ide_config *ide, uint16_t reg, uint32_t length, uint16_t *data)
{
	EnableIO();
	fpga_spi_fast(UIO_DMA_READ);
	fpga_spi_fast(ide->base + reg);
	fpga_spi_fast(0);
	fpga_spi_fast_block_read(data, length);
	DisableIO();
}

const uint32_t ide_io_max_size = 32;
uint8_t ide_buf[ide_io_max_size * 512];

ide_config ide_inst[2] = {};

uint16_t ide_check()
{
	uint16_t res;
	EnableIO();
	res = spi_w(UIO_DMA_SDIO);
	if (!res) res = (uint8_t)spi_w(0);
	DisableIO();
	return res;
}

int ide_img_mount(fileTYPE *f, const char *name, int rw)
{
	FileClose(f);
	int writable = 0, ret = 0;

	int len = strlen(name);
	if (len)
	{
		const char *ext = name + len - 4;
		if (!strncasecmp(".chd", ext, 4))
		{
			ret = 1;
		}
		else {
			writable = rw && FileCanWrite(name);
			ret = FileOpenEx(f, name, writable ? (O_RDWR | O_SYNC) : O_RDONLY);
			if (!ret) printf("Failed to open file %s\n", name);
			else strcpy(f->path, name);
		}
	}

	if (!ret)
	{
		f->size = 0;
		return 0;
	}

	printf("Mount %s as %s\n", name, writable ? "read-write" : "read-only");
	return 1;
}

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
	ide_recvbuf(ide, 0, 6, (uint16_t*)data);

	ide->regs.io_done = (uint8_t)(data[0] & 1);
	ide->regs.io_fast = (uint8_t)(data[0] & 2);
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
	if (!(ide->regs.status & (ATA_STATUS_BSY | ATA_STATUS_ERR))) ide->regs.status |= ATA_STATUS_DSC;

	uint8_t data[12] =
	{
		(uint8_t)((ide->drive[ide->regs.drv].cd) ? 0x80 : ide->regs.io_size),
		(uint8_t)(ide->regs.error),
		(uint8_t)(ide->regs.sector_count),
		(uint8_t)(ide->regs.sector),

		(uint8_t)(ide->regs.cylinder),
		(uint8_t)(ide->regs.cylinder >> 8),
		(uint8_t)(ide->regs.cylinder >> 16),
		(uint8_t)(ide->regs.cylinder >> 24),

		(uint8_t)(ide->drive[ide->regs.drv].cd ? ide->regs.pkt_io_size : 0),
		(uint8_t)(ide->drive[ide->regs.drv].cd ? ide->regs.pkt_io_size >> 8 : 0),
		(uint8_t)((ide->regs.lba ? 0xE0 : 0xA0) | (ide->regs.drv ? 0x10 : 0x00) | ide->regs.head),
		(uint8_t)(ide->regs.status)
	};

	//hexdump(data, 12, ide->base);
	ide_sendbuf(ide, 0, 6, (uint16_t*)data);
}

static void calc_geometry(chs_t *chs, uint64_t size)
{
	uint32_t head = 0, cyl = 0, spt = 0;
	uint32_t sptt[] = { 63, 127, 255, 0 };
	uint32_t total = size / 512;
	for (int i = 0; sptt[i] != 0; i++)
	{
		spt = sptt[i];
		for (head = 4; head <= 16; head++)
		{
			cyl = total / (head * spt);
			if (total <= 1024 * 1024)
			{
				if (cyl <= 1023) break;
			}
			else
			{
				if (cyl < 16383) break;
				if (cyl < 32767 && head >= 5) break;
				if (cyl <= 65536) break;
			}
		}
		if (head <= 16) break;
	}

	chs->cylinders = cyl;
	chs->heads = (uint16_t)head;
	chs->sectors = (uint16_t)spt;
}

static void get_rdb_geometry(RigidDiskBlock *rdb, chs_t *chs, uint64_t size)
{
	chs->heads = SWAP(rdb->rdb_Heads);
	chs->sectors = SWAP(rdb->rdb_Sectors);
	chs->cylinders = SWAP(rdb->rdb_Cylinders);
	if (chs->sectors > 255 || chs->heads > 16)
	{
		printf("ATTN: Illegal CHS value(s).");
		if (!(chs->sectors & 1) && (chs->sectors < 512) && (chs->heads <= 8))
		{
			printf(" Translate: sectors %d->%d, heads %d->%d.\n", chs->sectors, chs->sectors / 2, chs->heads, chs->heads * 2);
			chs->sectors /= 2;
			chs->heads *= 2;
			return;
		}

		printf(" DANGEROUS: Cannot translate to legal CHS values. Re-calculate the CHS.\n");
		calc_geometry(chs, size);
	}
}

static void guess_geometry(fileTYPE *f, chs_t *chs, int allow_vrdb)
{
	uint8_t flg = 0;
	chs->offset = 0;

	for (int i = 0; i < 16; ++i)
	{
		struct RigidDiskBlock *rdb = (struct RigidDiskBlock *)ide_buf;
		if (!FileReadSec(f, ide_buf)) break;
		for (int i = 0; i < 512; i++) flg |= ide_buf[i];

		if (rdb->rdb_ID == RDB_MAGIC)
		{
			printf("Found RDB header -> native Amiga image.\n");
			get_rdb_geometry(rdb, chs, f->size);
			return;
		}
	}

	if (allow_vrdb && flg)
	{
		chs->heads = 16;
		chs->sectors = 128;

		for (int i = 32; i <= 2048; i <<= 1)
		{
			int cylinders = f->size / (512 * i) + 1;
			if (cylinders < 65536)
			{
				chs->sectors = (i < 128) ? i : 128;
				chs->heads = i / chs->sectors;
				break;
			}
		}

		int spc = chs->heads * chs->sectors;
		chs->cylinders = f->size / (512 * spc) + 1;
		if (chs->cylinders > 65535) chs->cylinders = 65535;
		chs->offset = -spc;
		printf("No RDB header found in HDF image. Assume it's image of single partition. Use Virtual RDB header.\n");
	}
	else
	{
		calc_geometry(chs, f->size);
		if(allow_vrdb) printf("No RDB header found. Possible non-Amiga or empty image.\n");
	}
}

static void ide_set_geometry(drive_t *drive, uint16_t sectors, uint16_t heads)
{
	int info = 0;
	if (drive->heads != heads || drive->spt != sectors)
	{
		info = 1;
		printf("SPT=%d, Heads=%d\n", sectors, heads);
	}

	drive->heads = heads ? heads : 16;
	drive->spt = sectors ? sectors : 256;

	uint32_t cylinders = drive->f->size / (drive->heads * drive->spt * 512);
	if (drive->offset)
	{
		cylinders++;
		drive->offset = drive->heads * drive->spt;
	}
	if (cylinders > 65535) cylinders = 65535;

	//Maximum 137GB images are supported.
	drive->cylinders = cylinders;
	if(info) printf("New SPT=%d, Heads=%d, Cylinders=%d\n", drive->spt, drive->heads, drive->cylinders);
}

static uint32_t checksum_rdb(uint32_t *p, int set)
{
	uint32_t count = SWAP(p[1]);
	uint32_t result = 0;
	if (set) p[2] = 0;

	for (uint32_t i = 0; i < count; ++i) result += SWAP(p[i]);
	if (!set) return result;

	result = 0 - result;
	p[2] = SWAP(result);
	return 0;
}

static void fill_fake_rdb(drive_t *drive, uint32_t sector, int cnt)
{
	printf("fill_fake_rdb(%u,%d)\n", sector, cnt);

	uint8_t *buff = ide_buf;
	memset(ide_buf, 0, sizeof(ide_buf));

	while (cnt)
	{
		// if we're asked for LBA 0 we create an RDSK block, and if LBA 1, a PART block
		if (sector == 0)
		{
			// RDB
			struct RigidDiskBlock *rdb = (struct RigidDiskBlock *)buff;
			rdb->rdb_ID = 'R' << 24 | 'D' << 16 | 'S' << 8 | 'K';
			rdb->rdb_Summedlongs = 0x40;
			rdb->rdb_HostID = 0x07;
			rdb->rdb_BlockBytes = 0x200;
			rdb->rdb_Flags = 0x12;                 // (Disk ID valid, no LUNs after this one)
			rdb->rdb_BadBlockList = 0xffffffff;    // We don't provide a bad block list
			rdb->rdb_PartitionList = 1;
			rdb->rdb_FileSysHeaderList = 0xffffffff;
			rdb->rdb_DriveInit = 0xffffffff;
			rdb->rdb_Reserved1[0] = 0xffffffff;
			rdb->rdb_Reserved1[1] = 0xffffffff;
			rdb->rdb_Reserved1[2] = 0xffffffff;
			rdb->rdb_Reserved1[3] = 0xffffffff;
			rdb->rdb_Reserved1[4] = 0xffffffff;
			rdb->rdb_Reserved1[5] = 0xffffffff;
			rdb->rdb_Cylinders = drive->cylinders;
			rdb->rdb_Sectors = drive->spt;
			rdb->rdb_Heads = drive->heads;
			rdb->rdb_Interleave = 1;
			rdb->rdb_Park = rdb->rdb_Cylinders;
			rdb->rdb_WritePreComp = rdb->rdb_Cylinders;
			rdb->rdb_ReducedWrite = rdb->rdb_Cylinders;
			rdb->rdb_StepRate = 3;
			rdb->rdb_RDBBlocksLo = 0;
			rdb->rdb_RDBBlocksHi = 1;
			rdb->rdb_LoCylinder = 1;
			rdb->rdb_HiCylinder = rdb->rdb_Cylinders - 1;
			rdb->rdb_CylBlocks = rdb->rdb_Heads * rdb->rdb_Sectors;
			rdb->rdb_AutoParkSeconds = 0;
			rdb->rdb_HighRDSKBlock = 1;
			strcpy(rdb->rdb_DiskVendor, "DON'T   REPARTITION!    0.00");
			uint32_t *p = (uint32_t*)buff;
			for (int i = 0; i < 40; i++) p[i] = SWAP(p[i]);
		}
		else if(sector == 1)
		{
			// Partition
			struct PartitionBlock *pb = (struct PartitionBlock *)buff;
			pb->pb_ID = 'P' << 24 | 'A' << 16 | 'R' << 8 | 'T';
			pb->pb_Summedlongs = 0x40;
			pb->pb_HostID = 0x07;
			pb->pb_Next = 0xffffffff;
			pb->pb_Flags = 0x1; // bootable
			pb->pb_DevFlags = 0;
			strcpy(pb->pb_DriveName, "0HD\003");  // "DHx" BCPL string
			pb->pb_DriveName[0] = drive->drvnum + '0';
			pb->pb_Environment.de_TableSize = 0x10;
			pb->pb_Environment.de_SizeBlock = 0x80;
			pb->pb_Environment.de_Surfaces = drive->heads;
			pb->pb_Environment.de_SectorPerBlock = 1;
			pb->pb_Environment.de_BlocksPerTrack = drive->spt;
			pb->pb_Environment.de_Reserved = 2;
			pb->pb_Environment.de_LowCyl = 1;
			pb->pb_Environment.de_HighCyl = drive->cylinders - 1;
			pb->pb_Environment.de_NumBuffers = 30;
			pb->pb_Environment.de_MaxTransfer = 0xffffff;
			pb->pb_Environment.de_Mask = 0x7ffffffe;
			pb->pb_Environment.de_DosType = 0x444f5301;
			uint32_t *p = (uint32_t*)buff;
			for (int i = 0; i < 64; i++) p[i] = SWAP(p[i]);
		}
		else
		{
			break;
		}

		checksum_rdb((uint32_t*)buff, 1);
		//hexdump(buff, 256);
		cnt--;
		sector++;
		buff += 512;
	}
}

void ide_img_set(uint32_t drvnum, fileTYPE *f, int cd, int sectors, int heads, int offset, int type)
{
	int drv = (drvnum & 1);
	int port = (drvnum >> 1);

	drive_t *drive = &ide_inst[port].drive[drv];

	ide_inst[port].base = port ? IDE1_BASE : IDE0_BASE;
	ide_inst[port].drive[drv].drvnum = drvnum;

	if (drive->f && (f != drive->f) && drive->f->opened())
	{
		FileClose(drive->f);
	}

	drive->f = f;

	drive->cylinders = 0;
	drive->heads = 0;
	drive->spt = 0;
	drive->spb = 16;
	drive->offset = 0;
	drive->type = 0;

	drive->present = f ? 1 : 0;
	ide_inst[port].state = IDE_STATE_RESET;
	ide_inst[port].bitoff = port * 3;

	drive->cd = drive->present && cd;

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

	ide_reg_set(&ide_inst[port], 6, ((drive->present || drive->placeholder) ? 9 : 8) << (drv * 4));
	ide_reg_set(&ide_inst[port], 6, 0x200);

	if(drive->f)
	{
		if (!drive->chd_f) drive->total_sectors = (drive->f->size / 512);
	}
	else
	{
		drive->total_sectors = 0;
	}

	if (!drive->cd)
	{
		if (drive->present)
		{
			if (!drive->chd_f) 
			{
				if (parse_vhd_config(drive)) ide_set_geometry(drive, sectors, heads);
				else ide_set_geometry(drive, drive->spt, drive->heads);
			}
			else ide_set_geometry(drive, sectors, heads);
			if (offset && drive->cylinders < 65535) drive->cylinders++;
			drive->offset = offset;
			drive->type = type;
		}

		uint16_t identify[256] =
		{
			0x0040, 											//word 0
			drive->cylinders,									//word 1 cylinders         (used by e.g. ao486)
			0x0000,												//word 2 reserved
			drive->heads,										//word 3 heads             (used by e.g. ao486)
			(uint16_t)(512 * drive->spt),						//word 4 bytes per track
			512,												//word 5 bytes per sector  (used by e.g. ao486)
			drive->spt,											//word 6 sectors per track (used by e.g. ao486)
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
			0x8020,												//word 47 max multiple sectors
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
			(1 << 14) | (1 << 13) | (1 << 12),					//word 83
			1 << 14,	    									//word 84
			(1 << 14) | (1 << 9),  								//word 85
			(1 << 14) | (1 << 13) | (1 << 12),					//word 86
			1 << 14,	    									//word 87
			0x0000,												//word 88
			0,0,0,0,											//word 89..92
			(1 << 14) | (1 << 13) | (1 << 9) | (1 << 8) | (1 << 3) | (1 << 1) | (1 << 0), //word 93
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

	if (ide_inst[port].drive[drv].present)
	{
		char *name = ide_inst[port].drive[drv].f->name;
		for (int i = 0; i < 20; i++)
		{
			if (*name) drive->id[27 + i] = ((*name++) << 8) | 0x20;
			if (*name) drive->id[27 + i] = (drive->id[27 + i] & 0xFF00) | (*name++);
		}
		//hexdump(drive->id, 256);
	}

	printf("HDD%d:\n  present %d\n  hd_cylinders %d\n  hd_heads %d\n  hd_spt %d\n  hd_total_sectors %d\n\n",
		drvnum, drive->present, drive->cylinders, drive->heads, drive->spt, drive->total_sectors);
}

static uint32_t get_lba(ide_config *ide)
{
	uint32_t lba;
	if (ide->regs.lba)
	{
		lba = ide->regs.sector | (ide->regs.cylinder << 8) | (ide->regs.head << 24);
	}
	else
	{
		drive_t *drive = &ide->drive[ide->regs.drv];
		dbg2_printf("  CHS: %d/%d/%d (%d/%d)\n", ide->regs.cylinder, ide->regs.head, ide->regs.sector, drive->heads, drive->spt);
		lba = ide->regs.cylinder;
		lba *= drive->heads;
		lba += ide->regs.head;
		lba *= drive->spt;
		lba += ide->regs.sector - 1;
	}

	dbg2_printf("  LBA: %u\n", lba);
	return lba;
}

static void put_lba(ide_config *ide, uint32_t lba)
{
	lba--;
	dbg2_printf("  putLBA: %u\n", lba);
	if (ide->regs.lba)
	{
		ide->regs.sector = lba;
		lba >>= 8;
		ide->regs.cylinder = lba;
		lba >>= 16;
		ide->regs.head = lba & 0xF;
	}
	else
	{
		drive_t *drive = &ide->drive[ide->regs.drv];
		uint32_t hspt = drive->heads * drive->spt;
		ide->regs.cylinder = lba / hspt;
		lba = lba % hspt;
		ide->regs.head = lba / drive->spt;
		lba = lba % drive->spt;
		ide->regs.sector = lba + 1;
	}
}

inline uint16_t get_cnt(ide_config *ide)
{
	drive_t *drive = &ide->drive[ide->regs.drv];
	dbg2_printf("  Cnt: %d (max = %d)\n", ide->regs.sector_count, drive->spb);
	uint16_t cnt = ide->regs.sector_count;
	if (!cnt || cnt > drive->spb)
	{
		cnt = drive->spb;
		dbg2_printf("  New cnt: %d\n", cnt);
	}
	return cnt;
}

inline int readhdd(drive_t *drive, uint32_t lba, int cnt)
{
	if (lba < drive->offset)
	{
		if (!drive->type) fill_fake_rdb(drive, lba, cnt);
		else memset(ide_buf, 0, sizeof(ide_buf));
		return 1;
	}
	else
	{
		return FileReadAdv(drive->f, ide_buf, cnt * 512, -1);
	}
}

static void process_read(ide_config *ide, int multi)
{
	uint32_t lba = get_lba(ide);
	uint16_t ide_req = 0;

	dbg2_printf("  sector_count: %d\n", ide->regs.sector_count);

	uint32_t cnt = multi ? get_cnt(ide) : 1;
	ide->null = !FileSeekLBA(ide->drive[ide->regs.drv].f, (lba <= ide->drive[ide->regs.drv].offset) ? 0 : (lba - ide->drive[ide->regs.drv].offset));
	if (!ide->null) ide->null = (readhdd(&ide->drive[ide->regs.drv], lba, cnt) <= 0);
	if (ide->null) memset(ide_buf, 0, cnt * 512);

	while (1)
	{
		lba += cnt;
		ide->regs.sector_count -= cnt;
		put_lba(ide, lba);

		ide->regs.io_size = cnt;
		ide->regs.status = ATA_STATUS_RDP | ATA_STATUS_RDY | ATA_STATUS_DRQ | ATA_STATUS_IRQ;
		if (!ide->regs.sector_count) ide->regs.status |= ATA_STATUS_END;

		if (ide->regs.io_fast)
		{
			ide_set_regs(ide);
			ide_send_data(ide_buf, cnt * 256);
		}
		else
		{
			ide_send_data(ide_buf, cnt * 256);
			ide->regs.status &= ~ATA_STATUS_RDP;
			ide_set_regs(ide);
		}

		if (!ide->regs.sector_count)
		{
			//ATA_STATUS_END will set ATA_STATUS_RDY at the end
			ide->state = IDE_STATE_IDLE;
			break;
		}

		cnt = multi ? get_cnt(ide) : 1;
		if (!ide->null) ide->null = (readhdd(&ide->drive[ide->regs.drv], lba, cnt) <= 0);
		if (ide->null) memset(ide_buf, 0, cnt * 512);

		ide_req = 0;
		while (!ide_req) ide_req = (ide_check() >> ide->bitoff) & 7;

		if (ide_req != 5)
		{
			ide->state = IDE_STATE_IDLE;
			break;
		}
	}

	dbg2_printf("  finish\n");
}

static void process_write(ide_config *ide, int multi)
{
	uint32_t lba = get_lba(ide);
	uint32_t cnt = 1;
	uint16_t ide_req;

	ide->null = (ide->regs.cmd != 0xFA) ? !FileSeekLBA(ide->drive[ide->regs.drv].f, (lba <= ide->drive[ide->regs.drv].offset) ? 0 : (lba - ide->drive[ide->regs.drv].offset)) : 1;
	uint8_t irq = 0;

	while (1)
	{
		cnt = multi ? get_cnt(ide) : 1;
		ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_DRQ | irq;
		irq = ATA_STATUS_IRQ;

		ide->regs.io_size = cnt;
		ide_set_regs(ide);

		ide_req = 0;
		while (!ide_req) ide_req = (ide_check() >> ide->bitoff) & 7;

		if (ide_req != 5)
		{
			ide->state = IDE_STATE_IDLE;
			break;
		}

		ide_recv_data(ide_buf, cnt * 256);

		if (ide->regs.cmd == 0xFA)
		{
			ide->regs.sector_count = 0;
			char* filename = user_io_make_filepath(HomeDir(), (char*)ide_buf);
			int drvnum = (ide->regs.head == 1) ? 0 : (ide->regs.head == 2) ? 1 : (ide->drive[ide->regs.drv].drvnum + 2);

			static const char* names[6] = { "fdd0", "fdd1", "ide00", "ide01", "ide10", "ide11" };
			printf("Request for new image for drive %s: %s\n", names[drvnum], filename);
			if(is_x86()) x86_set_image(drvnum, filename);
		}
		else
		{
			if (!ide->null) ide->null = (lba < ide->drive[ide->regs.drv].offset) ? 0 : (FileWriteAdv(ide->drive[ide->regs.drv].f, ide_buf, cnt * 512, -1) <= 0);
			lba += cnt;
			ide->regs.sector_count -= cnt;
			put_lba(ide, lba);
		}

		if (!ide->regs.sector_count)
		{
			ide->state = IDE_STATE_IDLE;
			ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_IRQ;
			ide_set_regs(ide);
			break;
		}
	}
}

static int handle_hdd(ide_config *ide)
{
	switch (ide->regs.cmd)
	{
	case 0xEC: // identify
		{
			uint8_t drv = ide->regs.drv;
			memset(&ide->regs, 0, sizeof(ide->regs));
			ide->regs.drv = drv;
		}
		ide->regs.io_size = 1;
		ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_DRQ | ATA_STATUS_IRQ | ATA_STATUS_END;
		ide_send_data(ide->drive[ide->regs.drv].id, 256);
		ide_set_regs(ide);
		break;

	case 0xC4: // read multiple
		process_read(ide, 1);
		break;

	case 0x20: // read with retry
	case 0x21: // read
		process_read(ide, 0);
		break;

	case 0xC5: // write multiple
		process_write(ide, 1);
		break;

	case 0x30: // write with retry
	case 0x31: // write
		process_write(ide, 0);
		break;

	case 0xFA: // mount image
		ide->regs.pkt_io_size = 256;
		process_write(ide, 0);
		break;

	case 0xC6: // set multople
		if (ide->regs.sector_count > ide_io_max_size)
		{
			return 1;
		}
		ide->drive[ide->regs.drv].spb = ide->regs.sector_count;
		dbg_printf("New block size: %d\n", ide->drive[ide->regs.drv].spb);
		ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_IRQ;
		ide_set_regs(ide);
		break;

	case 0x08: // reset (fail)
		dbg_printf("Reset command (08h) for HDD not supported\n");
		return 1;

	case 0x10 ... 0x1F: // recalibrate
		ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_IRQ;
		ide->regs.cylinder = 0;
		ide_set_regs(ide);
		break;

	case 0x40: // READ VERIFY
		dbg_printf("Received read verify command. Not implemented but returning OK.\n");
		ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_IRQ;
		ide_set_regs(ide);
		break;

	case 0x91: // initialize device parameters
		ide_set_geometry(&ide->drive[ide->regs.drv], ide->regs.sector_count, ide->regs.head + 1);
		ide->regs.status = ATA_STATUS_RDY | ATA_STATUS_IRQ;
		ide_set_regs(ide);
		break;

	default:
		dbg_printf("(!) Unsupported command (%04X)\n", ide->base);
		dbg_print_regs(&ide->regs);
		return 1;
	}

	return 0;
}

void ide_io(int num, int req)
{
	ide_config *ide = &ide_inst[num];

	//printf("req: %d, disk: %d\n", req, num);

	if (req == 0) // no request
	{
		if (ide->state == IDE_STATE_RESET)
		{
			ide->state = IDE_STATE_IDLE;

			ide->regs.status = ATA_STATUS_RDY;
			ide_set_regs(ide);

			dbg_printf("IDE %04X reset finish\n", ide->base);
		}
	}
	else if (req == 4) // command
	{
		ide->state = IDE_STATE_IDLE;
		ide_get_regs(ide);

		dbg2_printf("IDE command: %02X (on %d)\n", ide->regs.cmd, ide->regs.drv);
		int err = 0;

		if(ide->regs.cmd == 0xFA) err = handle_hdd(ide);
		else if (ide->drive[ide->regs.drv].cd) err = cdrom_handle_cmd(ide);
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
		dbg2_printf("IDE data request (on %d)\n", ide->regs.drv);
		if (ide->state == IDE_STATE_WAIT_PKT_CMD)
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
			ide_recv_data(ide_buf, 256);
			printf("mode select data:\n");
			hexdump(ide_buf, ide->regs.cylinder);
			cdrom_mode_select(ide);
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

int ide_is_placeholder(int num)
{
	return ide_inst[num / 2].drive[num & 1].placeholder;
}

void ide_reset(uint8_t hotswap[4])
{
	ide_inst[0].drive[0].placeholder = 0;
	ide_inst[0].drive[1].placeholder = 0;
	ide_inst[1].drive[0].placeholder = 0;
	ide_inst[1].drive[1].placeholder = 0;

	ide_inst[0].drive[0].allow_placeholder = hotswap[0];
	ide_inst[0].drive[1].allow_placeholder = hotswap[1];
	ide_inst[1].drive[0].allow_placeholder = hotswap[2];
	ide_inst[1].drive[1].allow_placeholder = hotswap[3];


	ide_inst[0].drive[0].volume_r = 1.0f;
	ide_inst[0].drive[1].volume_r = 1.0f;
	ide_inst[1].drive[0].volume_r = 1.0f;
	ide_inst[1].drive[1].volume_r = 1.0f;

	ide_inst[0].drive[0].volume_l = 1.0f;
	ide_inst[0].drive[1].volume_l = 1.0f;
	ide_inst[1].drive[0].volume_l = 1.0f;
	ide_inst[1].drive[1].volume_l = 1.0f;

	ide_inst[0].drive[0].mcr_flag = false;
	ide_inst[0].drive[1].mcr_flag = false;
	ide_inst[1].drive[0].mcr_flag = false;
	ide_inst[1].drive[1].mcr_flag = false;
}

int ide_open(uint8_t unit, const char* filename)
{
	static fileTYPE hdd_file[4] = {};
	chs_t chs = {};

	if (!is_minimig() || ((minimig_config.ide_cfg & 1) && minimig_config.hardfile[unit].cfg))
	{
		printf("\nChecking HDD %d\n", unit);
		if (filename[0] && FileOpenEx(&hdd_file[unit], filename, FileCanWrite(filename) ? O_RDWR : O_RDONLY))
		{
			printf("file: \"%s\": ", hdd_file[unit].name);
			guess_geometry(&hdd_file[unit], &chs, is_minimig() && !strcasecmp(".hdf", filename + strlen(filename) - 4));
			printf("size: %llu (%llu MB)\n", hdd_file[unit].size, hdd_file[unit].size >> 20);
			printf("CHS: %u/%u/%u", chs.cylinders, chs.heads, chs.sectors);
			printf(" (%llu MB), ", ((((uint64_t)chs.cylinders) * chs.heads * chs.sectors) >> 11));
			printf("Offset: %d\n", chs.offset);

			int present = 0;
			int cd = 0;

			int len = strlen(filename);
			const char *ext = filename + len - 4;
			int vhd = (len > 4 && (!strcasecmp(ext, ".hdf") || (!strcasecmp(ext, ".vhd"))));

			if (!vhd)
			{
				const char *img_name = cdrom_parse(unit, filename);
				if (img_name) present = ide_img_mount(&hdd_file[unit], img_name, 0);
				if (present) cd = 1;
				else vhd = 1;
			}

			if (!present && vhd) present = ide_img_mount(&hdd_file[unit], filename, 1);
			ide_img_set(unit, present ? &hdd_file[unit] : 0, cd, chs.sectors, chs.heads, cd ? 0 : -chs.offset);
			if (present) return 1;
		}

		printf("HDD %d: not present\n", unit);
	}

	// close if opened earlier.
	ide_img_set(unit, 0, 0);
	FileClose(&hdd_file[unit]);
	return 0;
}
