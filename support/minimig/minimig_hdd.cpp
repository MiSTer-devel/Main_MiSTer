/*
Copyright 2008, 2009 Jakub Bednarski
Copyright 2017, 2018 Sorgelig

This file is part of Minimig

Minimig is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

Minimig is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// 2009-11-22 - read/write multiple implemented
// 2018-05-13 - 4xIDE implemented
// 2018-05-xx - Use RDB CHS values if valid
// 2018-05-29 - LBA mode implemented

#include <stdio.h>
#include <string.h>
#include "../../hardware.h"
#include "../../file_io.h"
#include "minimig_hdd.h"
#include "../../menu.h"
#include "minimig_config.h"
#include "../../debug.h"
#include "../../user_io.h"

#define CMD_IDECMD                 0x04
#define CMD_IDEDAT                 0x08

#define CMD_IDE_REGS_RD            0x80
#define CMD_IDE_REGS_WR            0x90
#define CMD_IDE_DATA_WR            0xA0
#define CMD_IDE_DATA_RD            0xB0
#define CMD_IDE_STATUS_WR          0xF0

#define IDE_STATUS_END             0x80
#define IDE_STATUS_IRQ             0x10
#define IDE_STATUS_RDY             0x08
#define IDE_STATUS_REQ             0x04
#define IDE_STATUS_ERR             0x01

#define ACMD_RECALIBRATE           0x10
#define ACMD_DIAGNOSTIC            0x90
#define ACMD_IDENTIFY_DEVICE       0xEC
#define ACMD_INITIALIZE_PARAMETERS 0x91
#define ACMD_READ_SECTORS          0x20
#define ACMD_WRITE_SECTORS         0x30
#define ACMD_READ_MULTIPLE         0xC4
#define ACMD_WRITE_MULTIPLE        0xC5
#define ACMD_SET_MULTIPLE_MODE     0xC6

#define SWAP(a)  ((((a)&0x000000ff)<<24)|(((a)&0x0000ff00)<<8)|(((a)&0x00ff0000)>>8)|(((a)&0xff000000)>>24))
#define SWAPW(a) ((((a)<<8)&0xff00)|(((a)>>8)&0x00ff))

// hardfile structure
typedef struct
{
	int      unit;
	int      enabled;
	fileTYPE file;
	uint32_t cylinders;
	uint16_t heads;
	uint16_t sectors;
	uint16_t sectors_per_block;
	int32_t  offset; // if a partition, the lba offset of the partition.  Can be negative if we've synthesized an RDB.

	uint8_t  lu;
	int32_t  lba, nextlba;
	uint16_t sector;
	uint16_t cylinder;
	uint16_t head;
	uint16_t sector_count;
} hdfTYPE;

static hdfTYPE HDF[4] = {};
static uint8_t sector_buffer[512];

static void CalcGeometry(hdfTYPE *hdf)
{
	uint32_t head = 0, cyl = 0, spt = 0;
	uint32_t sptt[] = { 63, 127, 255, 0 };
	uint32_t total = hdf->file.size / 512;
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
				if (cyl  < 16383) 				break;
				if (cyl  < 32767 && head >= 5) 	break;
				if (cyl <= 65536)				break;
			}
		}
		if (head <= 16) break;
	}

	hdf->cylinders = cyl;
	hdf->heads = (uint16_t)head;
	hdf->sectors = (uint16_t)spt;
}

static void GetRDBGeometry(hdfTYPE *hdf)
{
	struct RigidDiskBlock *rdb = (struct RigidDiskBlock *)sector_buffer;
	hdf->heads = SWAP(rdb->rdb_Heads);
	hdf->sectors = SWAP(rdb->rdb_Sectors);
	hdf->cylinders = SWAP(rdb->rdb_Cylinders);
	if (hdf->sectors > 255 || hdf->heads > 16)
	{
		printf("ATTN: Illegal CHS value(s).");
		if (!(hdf->sectors & 1) && (hdf->sectors < 512) && (hdf->heads <= 8))
		{
			printf(" Translate: sectors %d->%d, heads %d->%d.\n", hdf->sectors, hdf->sectors / 2, hdf->heads, hdf->heads * 2);
			hdf->sectors /= 2;
			hdf->heads *= 2;
			return;
		}

		printf(" DANGEROUS: Cannot translate to legal CHS values. Re-calculate the CHS.\n");
		CalcGeometry(hdf);
	}
}

static void SetHardfileGeometry(hdfTYPE *hdf, int isHDF)
{
	struct RigidDiskBlock *rdb = (struct RigidDiskBlock *)sector_buffer;
	uint8_t flg = 0;

	hdf->offset = 0;

	for (int i = 0; i<16; ++i)
	{
		if (!FileReadSec(&hdf->file, sector_buffer)) break;
		for (int i = 0; i < 512; i++) flg |= sector_buffer[i];

		if (rdb->rdb_ID == RDB_MAGIC)
		{
			printf("Found RDB header -> native Amiga image.\n");
			GetRDBGeometry(hdf);
			return;
		}
	}

	if (isHDF && flg)
	{
		//use UAE settings.
		hdf->heads = 1;
		hdf->sectors = 32;

		int spc = hdf->heads * hdf->sectors;
		hdf->cylinders = hdf->file.size / (512 * spc) + 1;
		hdf->offset = -spc;

		printf("No RDB header found in HDF image. Assume it's image of single partition. Use Virtual RDB header.\n");
	}
	else
	{
		CalcGeometry(hdf);
		printf("No RDB header found. Possible non-Amiga or empty image.\n");
	}
}

static uint8_t GetDiskStatus(void)
{
	uint8_t status;

	EnableFpga();
	status = (uint8_t)(spi_w(0) >> 8);
	spi_w(0);
	spi_w(0);
	DisableFpga();

	return status;
}

static uint32_t RDBChecksum(uint32_t *p, int set)
{
	uint32_t count = SWAP(p[1]);
	uint32_t result = 0;
	if(set) p[2] = 0;

	for (uint32_t i = 0; i<count; ++i) result += SWAP(p[i]);
	if (!set) return result;

	result = 0 - result;
	p[2] = SWAP(result);
	return 0;
}

// if the HDF file doesn't have a RigidDiskBlock, we synthesize one
static void FakeRDB(hdfTYPE *hdf)
{
	// start by clearing the sector buffer
	memset(sector_buffer, 0, 512);

	// if we're asked for LBA 0 we create an RDSK block, and if LBA 1, a PART block
	switch (hdf->lba)
	{
		case 0: {
			// RDB
			struct RigidDiskBlock *rdb = (struct RigidDiskBlock *)sector_buffer;
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
			rdb->rdb_Cylinders = hdf->cylinders;
			rdb->rdb_Sectors = hdf->sectors;
			rdb->rdb_Heads = hdf->heads;
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
			uint32_t *p = (uint32_t*)(sector_buffer);
			for (int i = 0; i < 40; i++) p[i] = SWAP(p[i]);
			RDBChecksum(p, 1);
			break;
		}
		case 1: {
			// Partition
			struct PartitionBlock *pb = (struct PartitionBlock *)sector_buffer;
			pb->pb_ID = 'P' << 24 | 'A' << 16 | 'R' << 8 | 'T';
			pb->pb_Summedlongs = 0x40;
			pb->pb_HostID = 0x07;
			pb->pb_Next = 0xffffffff;
			pb->pb_Flags = 0x1; // bootable
			pb->pb_DevFlags = 0;
			strcpy(pb->pb_DriveName, "0HD\003");  // "DHx" BCPL string
			pb->pb_DriveName[0] = hdf->unit + '0';
			pb->pb_Environment.de_TableSize = 0x10;
			pb->pb_Environment.de_SizeBlock = 0x80;
			pb->pb_Environment.de_Surfaces = hdf->heads;
			pb->pb_Environment.de_SectorPerBlock = 1;
			pb->pb_Environment.de_BlocksPerTrack = hdf->sectors;
			pb->pb_Environment.de_Reserved = 2;
			pb->pb_Environment.de_LowCyl = 1;
			pb->pb_Environment.de_HighCyl = hdf->cylinders - 1;
			pb->pb_Environment.de_NumBuffers = 30;
			pb->pb_Environment.de_MaxTransfer = 0xffffff;
			pb->pb_Environment.de_Mask = 0x7ffffffe;
			pb->pb_Environment.de_DosType = 0x444f5301;
			uint32_t *p = (uint32_t*)(sector_buffer);
			for (int i = 0; i < 64; i++) p[i] = SWAP(p[i]);
			RDBChecksum(p, 1);
			break;
		}
	}
}

// builds Identify Device struct
static void IdentifyDevice(uint16_t *pBuffer, hdfTYPE *hdf)
{
	char *p, x;
	int i;
	uint32_t total_sectors = hdf->cylinders * hdf->heads * hdf->sectors;
	memset(pBuffer, 0, 512);

	if(hdf->enabled)
	{
		pBuffer[0] = 1 << 6; // hard disk
		pBuffer[1] = hdf->cylinders; // cyl count
		pBuffer[3] = hdf->heads; // head count
		pBuffer[6] = hdf->sectors; // sectors per track
										// FIXME - can get serial no from card itself.
		memcpy((char*)&pBuffer[10], "MiniMigHardfile0000 ", 20); // serial number - byte swapped
		p = (char*)&pBuffer[27];

		if (hdf->offset < 0)
		{
			memcpy((char*)&pBuffer[23], ".000    ", 8); // firmware version - byte swapped
			memcpy(p, "DON'T   REPARTITION!                    ", 40);
		}
		else
		{
			memcpy((char*)&pBuffer[23], ".100    ", 8); // firmware version - byte swapped
			memcpy(p, "MiSTer                                  ", 40); // model name - byte swapped
			p += 8;
			char *s = strrchr(minimig_config.hardfile[hdf->unit].filename, '/');
			if (s) s++;
			else s = minimig_config.hardfile[hdf->unit].filename;

			i = strlen(s);
			if (i > 32) s += i - 32;
			for (i = 0; (x = s[i]) && i < 16; i++) p[i] = x; // copy file name as model name
		}

		p = (char*)&pBuffer[27];
		for (i = 0; i < 40; i += 2)
		{
			char c = p[i];
			p[i] = p[i + 1];
			p[i + 1] = c;
		}
	}

	pBuffer[47] = 0x8010; // maximum sectors per block in Read/Write Multiple command
	pBuffer[49] = 1<<9;   // LBA support
	pBuffer[53] = 1;
	pBuffer[54] = hdf->cylinders;
	pBuffer[55] = hdf->heads;
	pBuffer[56] = hdf->sectors;
	pBuffer[57] = (uint16_t)total_sectors;
	pBuffer[58] = (uint16_t)(total_sectors >> 16);
	pBuffer[60] = (uint16_t)total_sectors;
	pBuffer[61] = (uint16_t)(total_sectors >> 16);
}

static void WriteTaskFile(uint8_t error, uint8_t sector_count, uint8_t sector_number, uint8_t cylinder_low, uint8_t cylinder_high, uint8_t drive_head)
{
	EnableFpga();

	spi_w(CMD_IDE_REGS_WR<<8); // write task file registers command
	spi_w(0); // dummy
	spi_w(0); // dummy

	spi_w(0);             // data (dummy)
	spi_w(error);         // error
	spi_w(sector_count);  // sector count
	spi_w(sector_number); // sector number
	spi_w(cylinder_low);  // cylinder low
	spi_w(cylinder_high); // cylinder high
	spi_w(drive_head);    // drive/head

	DisableFpga();
}

static void WriteStatus(uint8_t status)
{
	EnableFpga();
	spi_w((CMD_IDE_STATUS_WR<<8) | status);
	spi_w(0);
	spi_w(0);
	DisableFpga();
}

static void ATA_Recalibrate(uint8_t* tfr, hdfTYPE *hdf)
{
	// Recalibrate 0x10-0x1F (class 3 command: no data)
	(void)hdf;
	hdd_debugf("IDE%d: Recalibrate", hdf->unit);
	WriteTaskFile(0, 0, tfr[6] & 0x40 ? 0 : 1, 0, 0, tfr[6] & 0xF0);
	WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
}

static void ATA_Diagnostic(uint8_t* tfr, hdfTYPE *hdf)
{
	// Execute Drive Diagnostic (0x90)
	(void)hdf;
	(void)tfr;
	hdd_debugf("IDE: Drive Diagnostic");
	WriteTaskFile(1, 0, 0, 0, 0, 0);
	WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
}

static void ATA_IdentifyDevice(uint8_t* tfr, hdfTYPE *hdf)
{
	// Identify Device (0xec)
	hdd_debugf("IDE%d: Identify Device", hdf->unit);
	IdentifyDevice((uint16_t*)sector_buffer, hdf);
	WriteTaskFile(0, tfr[2], tfr[3], tfr[4], tfr[5], tfr[6]);
	WriteStatus(IDE_STATUS_RDY); // pio in (class 1) command type
	EnableFpga();
	spi_w(CMD_IDE_DATA_WR<<8); // write data command
	spi_w(0);
	spi_w(0);
	spi_block_write_16be((uint16_t*)sector_buffer);
	DisableFpga();
	WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
}

static void ATA_Initialize(uint8_t* tfr, hdfTYPE *hdf)
{
	// Initialize Device Parameters (0x91)
	(void)hdf;
	hdd_debugf("Initialize Device Parameters");
	hdd_debugf("IDE%d: %02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X", hdf->unit, tfr[0], tfr[1], tfr[2], tfr[3], tfr[4], tfr[5], tfr[6], tfr[7]);
	WriteTaskFile(0, tfr[2], tfr[3], tfr[4], tfr[5], tfr[6]);
	WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
}

static void ATA_SetMultipleMode(uint8_t* tfr, hdfTYPE *hdf)
{
	// Set Multiple Mode (0xc6)
	hdd_debugf("Set Multiple Mode");
	hdd_debugf("IDE%d: %02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X", hdf->unit, tfr[0], tfr[1], tfr[2], tfr[3], tfr[4], tfr[5], tfr[6], tfr[7]);
	hdf->sectors_per_block = tfr[2];
	WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
}

static int Preface(uint8_t* tfr, hdfTYPE *hdf)
{
	hdf->sector = tfr[3];
	hdf->cylinder = tfr[4] | (tfr[5] << 8);
	hdf->head = tfr[6] & 0x0F;
	hdf->lu = tfr[6] & 0xF0;
	hdf->sector_count = tfr[2];
	if (hdf->sector_count == 0) hdf->sector_count = 256;

	uint8_t uselba = hdf->lu & 0x40;

	if (uselba)
	{
		hdf->lba = (hdf->head << 24) | (hdf->cylinder << 8) | hdf->sector;
	}
	else
	{
		hdf->lba  = hdf->cylinder;
		hdf->lba *= hdf->heads;
		hdf->lba += hdf->head;
		hdf->lba *= hdf->sectors;
		hdf->lba += hdf->sector - 1;
	}

	//printf("setCHS: %s: %d,%d,%d -> %d\n", uselba ? "LBA" : "CHS", hdf->cylinder, hdf->head, hdf->sector, hdf->lba);
	hdf->nextlba = hdf->lba;

	if (hdf->enabled && hdf->lba >= 0 && hdf->file.size)
	{
		FileSeekLBA(&hdf->file, (hdf->lba + hdf->offset) < 0 ? 0 : hdf->lba + hdf->offset);
		return 1;
	}

	return 0;
}

static void nextCHS(hdfTYPE *hdf)
{
	// do not increment after last sector
	if (hdf->sector_count)
	{
		hdf->nextlba++;
		if (hdf->lu & 0x40)
		{
			hdf->sector = (uint8_t)hdf->nextlba;
			hdf->cylinder = (uint16_t)(hdf->nextlba >> 8);
			hdf->head = 0xF & (uint8_t)(hdf->nextlba >> 24);
		}
		else
		{
			if (hdf->sector == hdf->sectors)
			{
				hdf->sector = 1;
				hdf->head++;
				if (hdf->head == hdf->heads)
				{
					hdf->head = 0;
					hdf->cylinder++;
				}
			}
			else
			{
				hdf->sector++;
			}
		}
	}
}

static void updateTaskFile(hdfTYPE *hdf)
{
	WriteTaskFile(0, hdf->sector_count, hdf->sector, (uint8_t)hdf->cylinder, (uint8_t)(hdf->cylinder >> 8), (uint8_t)(hdf->lu | hdf->head));
}

static void ReadSector(hdfTYPE *hdf)
{
	// sector outside limit (fake rdb header)
	if ((hdf->lba + hdf->offset) < 0)
		FakeRDB(hdf);
	else
		FileReadSec(&hdf->file, sector_buffer);
}

static void SendSector()
{
	EnableFpga();
	spi_w(CMD_IDE_DATA_WR << 8); // write data command
	spi_w(0);
	spi_w(0);
	spi_block_write_16be((uint16_t*)sector_buffer);
	DisableFpga();
}

static void RecvSector()
{
	EnableFpga();
	spi_w(CMD_IDE_DATA_RD << 8); // read data command
	spi_w(0);
	spi_w(0);
	spi_block_read_16be((uint16_t*)sector_buffer);
	DisableFpga();
}

static void WriteSector(hdfTYPE *hdf)
{
	//Do not write to fake RDB header
	if ((hdf->lba + hdf->offset) < 0) return;

	//Write RDB header, grab the CHS!
	if (!hdf->offset && hdf->lba < 16 && (*(uint32_t*)sector_buffer) == RDB_MAGIC)
	{
		printf("Writing RDB header, LBA=%d: ", hdf->lba);
		uint32_t sum = RDBChecksum((uint32_t*)sector_buffer, 0);
		if (sum)
		{
			printf("Checksumm is incorrect(0x%08X)! Ignore the RDB parameters.\n", sum);
		}
		else
		{
			GetRDBGeometry(hdf);
			printf("Using new CHS: %u/%u/%u (%llu MB)\n", hdf->cylinders, hdf->heads, hdf->sectors, ((((uint64_t)hdf->cylinders) * hdf->heads * hdf->sectors) >> 11));
		}
	}
	FileWriteSec(&hdf->file, sector_buffer);
}

// Read Sectors (0x20)
static void ATA_ReadSectors(uint8_t* tfr, hdfTYPE *hdf)
{
	WriteStatus(IDE_STATUS_RDY); // pio in (class 1) command type

	if(Preface(tfr, hdf))
	{
		while (hdf->sector_count)
		{
			while (!(GetDiskStatus() & CMD_IDECMD)); // wait for empty sector buffer
			WriteStatus(IDE_STATUS_IRQ);

			ReadSector(hdf);

			// to be modified sector of first partition
			if (!hdf->unit && !hdf->lba)
			{
				struct RigidDiskBlock *rdb = (struct RigidDiskBlock *)sector_buffer;
				if (rdb->rdb_ID == RDB_MAGIC)
				{
					// adjust checksum by the difference between old and new flag value
					rdb->rdb_ChkSum = SWAP(SWAP(rdb->rdb_ChkSum) + SWAP(rdb->rdb_Flags) - 0x12);
					rdb->rdb_Flags = SWAP(0x12);
				}
			}
			SendSector();

			hdf->lba++;
			hdf->sector_count--;
			nextCHS(hdf);
			updateTaskFile(hdf);
		}
	}
	WriteStatus(IDE_STATUS_END);
}

// multiple sector transfer per IRQ
static void ATA_ReadMultiple(uint8_t* tfr, hdfTYPE *hdf)
{
	WriteStatus(IDE_STATUS_RDY); // pio in (class 1) command type

	if (Preface(tfr, hdf))
	{
		while (hdf->sector_count)
		{
			while (!(GetDiskStatus() & CMD_IDECMD)); // wait for empty sector buffer
			uint16_t block_count = hdf->sector_count;
			if (block_count > hdf->sectors_per_block) block_count = hdf->sectors_per_block;
			WriteStatus(IDE_STATUS_IRQ);
			while (block_count--)
			{
				ReadSector(hdf);
				SendSector();

				hdf->lba++;
				hdf->sector_count--;
				nextCHS(hdf);
			}
			updateTaskFile(hdf);
		}
	}
	WriteStatus(IDE_STATUS_END);
}

static void ATA_WriteSectors(uint8_t* tfr, hdfTYPE *hdf)
{
	WriteStatus(IDE_STATUS_REQ); // pio out (class 2) command type

	if (Preface(tfr, hdf))
	{
		hdf->lba += hdf->offset;
		while (hdf->sector_count)
		{
			while (!(GetDiskStatus() & CMD_IDEDAT)); // wait for full write buffer

			RecvSector();
			hdf->sector_count--;

			nextCHS(hdf);
			updateTaskFile(hdf);
			WriteStatus(hdf->sector_count ? IDE_STATUS_IRQ : IDE_STATUS_END | IDE_STATUS_IRQ);

			WriteSector(hdf);
			hdf->lba++;
		}
	}
}

static void ATA_WriteMultiple(uint8_t* tfr, hdfTYPE *hdf)
{
	WriteStatus(IDE_STATUS_REQ); // pio out (class 2) command type

	if (Preface(tfr, hdf))
	{
		hdf->lba += hdf->offset;
		while (hdf->sector_count)
		{
			uint16_t block_count = hdf->sector_count;
			if (block_count > hdf->sectors_per_block) block_count = hdf->sectors_per_block;
			while (block_count)
			{
				while (!(GetDiskStatus() & CMD_IDEDAT)); // wait for full write buffer

				RecvSector();
				WriteSector(hdf);
				hdf->lba++;

				block_count--;
				hdf->sector_count--;
				nextCHS(hdf);
			}
			updateTaskFile(hdf);
			WriteStatus(hdf->sector_count ? IDE_STATUS_IRQ : IDE_STATUS_END | IDE_STATUS_IRQ);
		}
	}
}

void HandleHDD(uint8_t c1, uint8_t c2)
{
	(void)c2;

	if (c1 & CMD_IDECMD)
	{
		uint8_t  unit = 0;
		uint8_t  tfr[8];
		DISKLED_ON;

		EnableFpga();
		spi_w(CMD_IDE_REGS_RD<<8); // read task file registers
		spi_w(0);
		spi_w(0);

		for (int i = 0; i < 8; i++)
		{
			uint16_t tmp = spi_w(0);
			tfr[i] = (uint8_t)tmp;
			if (i == 6) unit = ((tmp >> 7) & 2) | ((tmp >> 4) & 1);
		}
		DisableFpga();

		//printf("IDE%d: %02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X\n", unit, tfr[0], tfr[1], tfr[2], tfr[3], tfr[4], tfr[5], tfr[6], tfr[7]);
		hdfTYPE *hdf = &HDF[unit];

		if (hdf->enabled)
		{
			if ((tfr[7] & 0xF0) == ACMD_RECALIBRATE)       ATA_Recalibrate     (tfr, hdf);
			else if (tfr[7] == ACMD_DIAGNOSTIC)            ATA_Diagnostic      (tfr, hdf);
			else if (tfr[7] == ACMD_IDENTIFY_DEVICE)       ATA_IdentifyDevice  (tfr, hdf);
			else if (tfr[7] == ACMD_INITIALIZE_PARAMETERS) ATA_Initialize      (tfr, hdf);
			else if (tfr[7] == ACMD_SET_MULTIPLE_MODE)     ATA_SetMultipleMode (tfr, hdf);
			else if (tfr[7] == ACMD_READ_SECTORS)          ATA_ReadSectors     (tfr, hdf);
			else if (tfr[7] == ACMD_READ_MULTIPLE)         ATA_ReadMultiple    (tfr, hdf);
			else if (tfr[7] == ACMD_WRITE_SECTORS)         ATA_WriteSectors    (tfr, hdf);
			else if (tfr[7] == ACMD_WRITE_MULTIPLE)        ATA_WriteMultiple   (tfr, hdf);
			else
			{
				printf("Unknown ATA command: IDE%d: %02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X\n", unit, tfr[0], tfr[1], tfr[2], tfr[3], tfr[4], tfr[5], tfr[6], tfr[7]);
				WriteTaskFile(0x04, tfr[2], tfr[3], tfr[4], tfr[5], tfr[6]);
				WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ | IDE_STATUS_ERR);
			}
		}
		else
		{
			printf("IDE%d not enabled: %02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X\n", unit, tfr[0], tfr[1], tfr[2], tfr[3], tfr[4], tfr[5], tfr[6], tfr[7]);
			WriteTaskFile(0x04, tfr[2], tfr[3], tfr[4], tfr[5], tfr[6]);
			WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ | IDE_STATUS_ERR);
		}
		DISKLED_OFF;
	}
}

uint8_t OpenHardfile(uint8_t unit)
{
	hdfTYPE *hdf = &HDF[unit];
	hdf->unit = unit;
	hdf->enabled = 0;
	if (minimig_config.enable_ide && minimig_config.hardfile[unit].enabled)
	{
		printf("\nChecking HDD %d\n", unit);
		if (minimig_config.hardfile[unit].filename[0])
		{
			if (FileOpenEx(&hdf->file, minimig_config.hardfile[unit].filename, FileCanWrite(minimig_config.hardfile[unit].filename) ? O_RDWR : O_RDONLY))
			{
				hdf->enabled = 1;
				printf("file: \"%s\": ", hdf->file.name);
				SetHardfileGeometry(hdf, !strcasecmp(".hdf", minimig_config.hardfile[unit].filename + strlen(minimig_config.hardfile[unit].filename) - 4));
				printf("size: %llu (%llu MB)\n", hdf->file.size, hdf->file.size >> 20);
				printf("CHS: %u/%u/%u", hdf->cylinders, hdf->heads, hdf->sectors);
				printf(" (%llu MB), ", ((((uint64_t)hdf->cylinders) * hdf->heads * hdf->sectors) >> 11));
				printf("Offset: %d\n", hdf->offset);
				return 1;
			}
		}
		printf("HDD %d: not present\n", unit);
	}

	// close if opened earlier.
	FileClose(&hdf->file);
	return 0;
}

int checkHDF(const char* name, struct RigidDiskBlock **rdb)
{
	fileTYPE file = {};

	*rdb = NULL;
	if (FileOpenEx(&file, name, O_RDONLY))
	{
		*rdb = (struct RigidDiskBlock *)sector_buffer;
		for (int i = 0; i<16; ++i)
		{
			if (!FileReadSec(&file, sector_buffer)) break;
			if ((*rdb)->rdb_ID == RDB_MAGIC)
			{
				FileClose(&file);
				(*rdb)->rdb_Heads = SWAP((*rdb)->rdb_Heads);
				(*rdb)->rdb_Sectors = SWAP((*rdb)->rdb_Sectors);
				(*rdb)->rdb_Cylinders = SWAP((*rdb)->rdb_Cylinders);
				return ((*rdb)->rdb_Heads <= 16 && (*rdb)->rdb_Sectors <= 255 && (*rdb)->rdb_Cylinders <= 65536);
			}
		}

		FileClose(&file);
		return 1; // non-HDF file
	}
	return 0;
}