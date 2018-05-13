/*
Copyright 2008, 2009 Jakub Bednarski

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

#include <stdio.h>
#include <string.h>
#include "hardware.h"
#include "file_io.h"
#include "minimig_hdd.h"
#include "minimig_hdd_internal.h"
#include "menu.h"
#include "minimig_config.h"
#include "debug.h"
#include "fpga_io.h"

#define CMD_IDECMD  0x04
#define CMD_IDEDAT  0x08

#define CMD_IDE_REGS_RD   0x80
#define CMD_IDE_REGS_WR   0x90
#define CMD_IDE_DATA_WR   0xA0
#define CMD_IDE_DATA_RD   0xB0
#define CMD_IDE_STATUS_WR 0xF0

#define IDE_STATUS_END  0x80
#define IDE_STATUS_IRQ  0x10
#define IDE_STATUS_RDY  0x08
#define IDE_STATUS_REQ  0x04
#define IDE_STATUS_ERR  0x01

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
	int             enabled;
	fileTYPE        file;
	unsigned short  cylinders;
	unsigned short  heads;
	unsigned short  sectors;
	unsigned short  sectors_per_block;
	unsigned short  partition; // partition no.
	long            offset; // if a partition, the lba offset of the partition.  Can be negative if we've synthesized an RDB.
} hdfTYPE;

static hdfTYPE hdf[4] = { 0 };

static uint8_t sector_buffer[512];

static unsigned char GetDiskStatus(void)
{
	unsigned char status;

	EnableFpga();
	status = (uint8_t)(spi_w(0) >> 8);
	spi_w(0);
	spi_w(0);
	DisableFpga();

	return status;
}

static void RDBChecksum(unsigned long *p)
{
	unsigned long count = p[1];
	unsigned long c2;
	long result = 0;
	p[2] = 0;
	for (c2 = 0; c2<count; ++c2) result += p[c2];
	p[2] = (unsigned long)-result;
}

// if the HDF file doesn't have a RigidDiskBlock, we synthesize one
static void FakeRDB(int unit, int block)
{
	int i;
	// start by clearing the sector buffer
	memset(sector_buffer, 0, 512);

	// if we're asked for LBA 0 we create an RDSK block, and if LBA 1, a PART block
	switch (block) {
	case 0: {
		// RDB
		hdd_debugf("FAKE: RDB");
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
		rdb->rdb_Cylinders = hdf[unit].cylinders;
		rdb->rdb_Sectors = hdf[unit].sectors;
		rdb->rdb_Heads = hdf[unit].heads;
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
		strcpy(rdb->rdb_DiskVendor, "Do not ");
		strcpy(rdb->rdb_DiskProduct, "repartition!");
		// swap byte order of strings to be able to "unswap" them after checksum
		unsigned long *p = (unsigned long*)rdb;
		for (i = 0; i<(8 + 16) / 4; i++) p[40 + i] = SWAP(p[40 + i]);
		RDBChecksum((unsigned long *)rdb);
		// swap byte order of first 0x40 long values
		for (i = 0; i<0x40; i++) p[i] = SWAP(p[i]);
		break;
	}
	case 1: {
		// Partition
		hdd_debugf("FAKE: Partition");
		struct PartitionBlock *pb = (struct PartitionBlock *)sector_buffer;
		pb->pb_ID = 'P' << 24 | 'A' << 16 | 'R' << 8 | 'T';
		pb->pb_Summedlongs = 0x40;
		pb->pb_HostID = 0x07;
		pb->pb_Next = 0xffffffff;
		pb->pb_Flags = 0x1; // bootable
		pb->pb_DevFlags = 0;
		strcpy(pb->pb_DriveName, "0HD\003");  // "DHx" BCPL string
		pb->pb_DriveName[0] = unit + '0';
		pb->pb_Environment.de_TableSize = 0x10;
		pb->pb_Environment.de_SizeBlock = 0x80;
		pb->pb_Environment.de_Surfaces = hdf[unit].heads;
		pb->pb_Environment.de_SectorPerBlock = 1;
		pb->pb_Environment.de_BlocksPerTrack = hdf[unit].sectors;
		pb->pb_Environment.de_Reserved = 2;
		pb->pb_Environment.de_LowCyl = 1;
		pb->pb_Environment.de_HighCyl = hdf[unit].cylinders - 1;
		pb->pb_Environment.de_NumBuffers = 30;
		pb->pb_Environment.de_MaxTransfer = 0xffffff;
		pb->pb_Environment.de_Mask = 0x7ffffffe;
		pb->pb_Environment.de_DosType = 0x444f5301;
		RDBChecksum((unsigned long *)pb);
		// swap byte order of first 0x40 entries
		unsigned long *p = (unsigned long*)pb;
		for (i = 0; i<0x40; i++) p[i] = SWAP(p[i]);
		break;
	}
	default: {
		break;
	}
	}
}

// builds Identify Device struct
static void IdentifyDevice(unsigned short *pBuffer, unsigned char unit)
{
	char *p, i, x;
	unsigned long total_sectors = hdf[unit].cylinders * hdf[unit].heads * hdf[unit].sectors;
	memset(pBuffer, 0, 512);

	if(hdf[unit].enabled)
	{
		pBuffer[0] = 1 << 6; // hard disk
		pBuffer[1] = hdf[unit].cylinders; // cyl count
		pBuffer[3] = hdf[unit].heads; // head count
		pBuffer[6] = hdf[unit].sectors; // sectors per track
										// FIXME - can get serial no from card itself.
		memcpy((char*)&pBuffer[10], "MiniMigHardfile0000 ", 20); // serial number - byte swapped
		memcpy((char*)&pBuffer[23], ".100    ", 8); // firmware version - byte swapped
		p = (char*)&pBuffer[27];

		if (hdf[unit].offset < 0)
		{
			memcpy(p, "DON'T                                   ", 40);
			p += 7;
			memcpy(p, "REPARTITION!    ", 16);
		}
		else
		{
			memcpy(p, "MiSTer                                  ", 40); // model name - byte swapped
			p += 8;
			char *s = strrchr(config.hardfile[unit].filename, '/');
			if (s) s++;
			else s = config.hardfile[unit].filename;

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
	pBuffer[53] = 1;
	pBuffer[54] = hdf[unit].cylinders;
	pBuffer[55] = hdf[unit].heads;
	pBuffer[56] = hdf[unit].sectors;
	pBuffer[57] = (unsigned short)total_sectors;
	pBuffer[58] = (unsigned short)(total_sectors >> 16);
}

static uint32_t chs2lba(unsigned short cylinder, unsigned char head, unsigned short sector, unsigned char unit)
{
	uint32_t lba = cylinder;
	lba *= hdf[unit].heads;
	lba += head;
	lba *= hdf[unit].sectors;
	return lba + sector - 1;
}

static void WriteTaskFile(unsigned char error, unsigned char sector_count, unsigned char sector_number, unsigned char cylinder_low, unsigned char cylinder_high, unsigned char drive_head)
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

static void WriteStatus(unsigned char status)
{
	EnableFpga();
	spi_w((CMD_IDE_STATUS_WR<<8) | status);
	spi_w(0);
	spi_w(0);
	DisableFpga();
}

static void ATA_Recalibrate(unsigned char* tfr, unsigned char unit)
{
	// Recalibrate 0x10-0x1F (class 3 command: no data)
	hdd_debugf("IDE%d: Recalibrate", unit);
	WriteTaskFile(0, 0, 1, 0, 0, tfr[6] & 0xF0);
	WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
}

static void ATA_Diagnostic(unsigned char* tfr)
{
	// Execute Drive Diagnostic (0x90)
	hdd_debugf("IDE: Drive Diagnostic");
	WriteTaskFile(1, 0, 0, 0, 0, 0);
	WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
}

static void ATA_IdentifyDevice(unsigned char* tfr, unsigned char unit)
{
	int i;
	// Identify Device (0xec)
	hdd_debugf("IDE%d: Identify Device", unit);
	IdentifyDevice((uint16_t*)sector_buffer, unit);
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

static void ATA_Initialize(unsigned char* tfr, unsigned char unit)
{
	// Initialize Device Parameters (0x91)
	hdd_debugf("Initialize Device Parameters");
	hdd_debugf("IDE%d: %02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X", unit, tfr[0], tfr[1], tfr[2], tfr[3], tfr[4], tfr[5], tfr[6], tfr[7]);
	WriteTaskFile(0, tfr[2], tfr[3], tfr[4], tfr[5], tfr[6]);
	WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
}

static void ATA_SetMultipleMode(unsigned char* tfr, unsigned char unit)
{
	// Set Multiple Mode (0xc6)
	hdd_debugf("Set Multiple Mode");
	hdd_debugf("IDE%d: %02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X", unit, tfr[0], tfr[1], tfr[2], tfr[3], tfr[4], tfr[5], tfr[6], tfr[7]);
	hdf[unit].sectors_per_block = tfr[2];
	WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
}

static int HardFileSeek(hdfTYPE *pHDF, unsigned long lba)
{
	return FileSeekLBA(&pHDF->file, lba);
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

static void ATA_ReadSectors(unsigned char* tfr, unsigned char unit)
{
	// Read Sectors (0x20)
	long lba;
	unsigned short sector = tfr[3];
	unsigned short cylinder = tfr[4] | (tfr[5] << 8);
	unsigned short head = tfr[6] & 0x0F;
	unsigned short sector_count = tfr[2];
	if (sector_count == 0) sector_count = 0x100;
	hdd_debugf("IDE%d: read %d.%d.%d, %d", unit, cylinder, head, sector, sector_count);

	if(hdf[unit].enabled && ((lba = chs2lba(cylinder, head, sector, unit))>=0))
	{
		if (hdf[unit].file.size) HardFileSeek(&hdf[unit], (lba + hdf[unit].offset) < 0 ? 0 : lba + hdf[unit].offset);
		while (sector_count)
		{
			// decrease sector count
			if (sector_count != 1) {
				if (sector == hdf[unit].sectors) {
					sector = 1;
					head++;
					if (head == hdf[unit].heads) {
						head = 0;
						cylinder++;
					}
				}
				else {
					sector++;
				}
			}

			WriteTaskFile(0, tfr[2], sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);
			WriteStatus(IDE_STATUS_RDY); // pio in (class 1) command type

			// sector outside limit (fake rdb header) or to be modified sector of first partition
			if (((lba + hdf[unit].offset)<0) || (!unit && !lba))
			{
				if ((lba + hdf[unit].offset)<0)
				{
					FakeRDB(unit, lba);
				}
				else
				{
					// read sector into buffer
					FileReadSec(&hdf[unit].file, sector_buffer);
					// adjust checksum by the difference between old and new flag value
					struct RigidDiskBlock *rdb = (struct RigidDiskBlock *)sector_buffer;
					rdb->rdb_ChkSum = SWAP(SWAP(rdb->rdb_ChkSum) + SWAP(rdb->rdb_Flags) - 0x12);

					// adjust flags
					rdb->rdb_Flags = SWAP(0x12);
				}

				EnableFpga();
				SendSector();
				WriteStatus(sector_count == 1 ? IDE_STATUS_IRQ | IDE_STATUS_END : IDE_STATUS_IRQ);
			}
			else
			{
				while (!(GetDiskStatus() & CMD_IDECMD)); // wait for empty sector buffer
				WriteStatus(IDE_STATUS_IRQ);
				if (hdf[unit].file.size)
				{
					FileReadSec(&hdf[unit].file, sector_buffer);
					SendSector();
				}
			}
			lba++;
			sector_count--; // decrease sector count
		}
	}
}

// multiple sector transfer per IRQ
static void ATA_ReadMultiple(unsigned char* tfr, unsigned char unit)
{
	WriteStatus(IDE_STATUS_RDY); // pio in (class 1) command type

	long lba;
	unsigned short sector = tfr[3];
	unsigned short cylinder = tfr[4] | (tfr[5] << 8);
	unsigned short head = tfr[6] & 0x0F;
	unsigned short sector_count = tfr[2];
	if (sector_count == 0) sector_count = 0x100;
	hdd_debugf("IDE%d: read_multi %d.%d.%d, %d", unit, cylinder, head, sector, sector_count);

	if (hdf[unit].enabled && ((lba = chs2lba(cylinder, head, sector, unit)) >= 0))
	{
		if (hdf[unit].file.size) HardFileSeek(&hdf[unit], (lba + hdf[unit].offset) < 0 ? 0 : lba + hdf[unit].offset);

		// FIXME - READM could cross the fake RDB -> real disk boundary.
		// FIXME - but first we should make some attempt to generate fake RGB in multiple mode.
		while (sector_count)
		{
			while (!(GetDiskStatus() & CMD_IDECMD)); // wait for empty sector buffer
			unsigned short block_count = sector_count;
			if (block_count > hdf[unit].sectors_per_block) block_count = hdf[unit].sectors_per_block;
			WriteStatus(IDE_STATUS_IRQ);
			while (block_count--)
			{
				if (hdf[unit].file.size)
				{
					FileReadSec(&hdf[unit].file, sector_buffer);
					SendSector();
				}
				if (sector_count != 1)
				{
					if (sector == hdf[unit].sectors)
					{
						sector = 1;
						head++;
						if (head == hdf[unit].heads)
						{
							head = 0;
							cylinder++;
						}
					}
					else
					{
						sector++;
					}
				}
				sector_count--;
			}
			WriteTaskFile(0, tfr[2], sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);
			//WriteTaskFile(0, 0, sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);
		}
		//WriteTaskFile(0, 0, sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);
	}
	WriteStatus(IDE_STATUS_END);
}

static void ATA_WriteSectors(unsigned char* tfr, unsigned char unit)
{
	WriteStatus(IDE_STATUS_REQ); // pio out (class 2) command type

	long lba;
	unsigned short sector = tfr[3];
	unsigned short cylinder = tfr[4] | (tfr[5] << 8);
	unsigned short head = tfr[6] & 0x0F;
	unsigned short sector_count = tfr[2];
	if (sector_count == 0) sector_count = 0x100;

	if (hdf[unit].enabled && ((lba = chs2lba(cylinder, head, sector, unit)) >= 0))
	{
		lba += hdf[unit].offset;
		if (hdf[unit].file.size)
		{
			// File size will be 0 in direct card modes
			HardFileSeek(&hdf[unit], (lba > -1) ? lba : 0);
		}

		while (sector_count)
		{
			while (!(GetDiskStatus() & CMD_IDEDAT)); // wait for full write buffer
													 // decrease sector count
			if (sector_count != 1)
			{
				if (sector == hdf[unit].sectors)
				{
					sector = 1;
					head++;
					if (head == hdf[unit].heads)
					{
						head = 0;
						cylinder++;
					}
				}
				else
				{
					sector++;
				}
			}
			WriteTaskFile(0, tfr[2], sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);
			RecvSector();
			sector_count--; // decrease sector count
			if (sector_count)
			{
				WriteStatus(IDE_STATUS_IRQ);
			}
			else
			{
				WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
			}

			// Don't attempt to write to fake RDB
			if (hdf[unit].file.size && (lba > -1)) FileWriteSec(&hdf[unit].file, sector_buffer);
			lba++;
		}
	}
}

static void ATA_WriteMultiple(unsigned char* tfr, unsigned char unit)
{
	// write sectors
	WriteStatus(IDE_STATUS_REQ); // pio out (class 2) command type

	long lba;
	unsigned short sector = tfr[3];
	unsigned short cylinder = tfr[4] | (tfr[5] << 8);
	unsigned short head = tfr[6] & 0x0F;
	unsigned short sector_count = tfr[2];
	if (sector_count == 0) sector_count = 0x100;

	if (hdf[unit].enabled && ((lba = chs2lba(cylinder, head, sector, unit)) >= 0))
	{
		//if (hdf[unit].type>=HDF_CARDPART0)
		lba += hdf[unit].offset;
		if (hdf[unit].file.size)
		{
			// File size will be 0 in direct card modes
			HardFileSeek(&hdf[unit], (lba > -1) ? lba : 0);
		}

		while (sector_count)
		{
			unsigned short block_count = sector_count;
			if (block_count > hdf[unit].sectors_per_block) block_count = hdf[unit].sectors_per_block;
			while (block_count)
			{
				while (!(GetDiskStatus() & CMD_IDEDAT)); // wait for full write buffer
														 // decrease sector count
				if (sector_count != 1)
				{
					if (sector == hdf[unit].sectors)
					{
						sector = 1;
						head++;
						if (head == hdf[unit].heads)
						{
							head = 0;
							cylinder++;
						}
					}
					else
					{
						sector++;
					}
				}
				//WriteTaskFile(0, tfr[2], sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);
				RecvSector();
				if (hdf[unit].file.size && (lba > -1)) FileWriteSec(&hdf[unit].file, sector_buffer);
				lba++;

				block_count--;  // decrease block count
				sector_count--; // decrease sector count
			}
			WriteTaskFile(0, tfr[2], sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);
			if (sector_count)
			{
				WriteStatus(IDE_STATUS_IRQ);
			}
			else
			{
				WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
			}
		}
	}
}

void HandleHDD(unsigned char c1, unsigned char c2)
{
	if (c1 & CMD_IDECMD)
	{
		unsigned char  unit = 0;
		unsigned char  tfr[8];
		DISKLED_ON;

		EnableFpga();
		spi_w(CMD_IDE_REGS_RD<<8); // read task file registers
		spi_w(0);
		spi_w(0);

		//printf("SPI:");
		for (int i = 0; i < 8; i++)
		{
			uint16_t tmp = spi_w(0);
			tfr[i] = (uint8_t)tmp;
			if (i == 6) unit = ((tmp >> 7) & 2) | ((tmp >> 4) & 1);
			//printf(" %03X", tmp);
		}
		//printf(" -> unit: %d\n", unit);
		DisableFpga();

		//printf("IDE%d: %02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X\n", unit, tfr[0], tfr[1], tfr[2], tfr[3], tfr[4], tfr[5], tfr[6], tfr[7]);

		if ((tfr[7] & 0xF0) == ACMD_RECALIBRATE)       ATA_Recalibrate(tfr, unit);
		else if (tfr[7] == ACMD_DIAGNOSTIC)            ATA_Diagnostic(tfr);
		else if (tfr[7] == ACMD_IDENTIFY_DEVICE)       ATA_IdentifyDevice(tfr, unit);
		else if (tfr[7] == ACMD_INITIALIZE_PARAMETERS) ATA_Initialize(tfr, unit);
		else if (tfr[7] == ACMD_SET_MULTIPLE_MODE)     ATA_SetMultipleMode(tfr, unit);
		else if (tfr[7] == ACMD_READ_SECTORS)          ATA_ReadSectors(tfr, unit);
		else if (tfr[7] == ACMD_READ_MULTIPLE)         ATA_ReadMultiple(tfr, unit);
		else if (tfr[7] == ACMD_WRITE_SECTORS)         ATA_WriteSectors(tfr, unit);
		else if (tfr[7] == ACMD_WRITE_MULTIPLE)        ATA_WriteMultiple(tfr, unit);
		else
		{
			printf("Unknown ATA command: IDE%d: %02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X\n", unit, tfr[0], tfr[1], tfr[2], tfr[3], tfr[4], tfr[5], tfr[6], tfr[7]);
			WriteTaskFile(0x04, tfr[2], tfr[3], tfr[4], tfr[5], tfr[6]);
			WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ | IDE_STATUS_ERR);
		}
		DISKLED_OFF;
	}
}

static void SetHardfileGeometry(hdfTYPE *pHDF, int isHDF)
{
	struct RigidDiskBlock *rdb = (struct RigidDiskBlock *)sector_buffer;
	uint8_t flg = 0;

	pHDF->offset = 0;

	for (int i = 0; i<16; ++i)
	{
		if (!FileReadSec(&pHDF->file, sector_buffer)) break;
		for (int i = 0; i < 512; i++) flg |= sector_buffer[i];

		if (rdb->rdb_ID == 0x4B534452)
		{
			printf("Found RDB header -> native Amiga image.\n");

			pHDF->heads = SWAP(rdb->rdb_Heads);
			pHDF->sectors = SWAP(rdb->rdb_Sectors);
			pHDF->cylinders = SWAP(rdb->rdb_Cylinders);
			if (pHDF->sectors > 255)
			{
				printf("ATTN: Too many sectors per track %d.", pHDF->sectors);
				if (pHDF->sectors & 1)
				{
					printf(" Odd number of sectors, Cannot translate. Give up! 8-E\n");
					return;
				}

				if (pHDF->sectors > 511)
				{
					printf(" Really, too many! Give up! 8-E\n");
					return;
				}

				if (pHDF->heads > 8)
				{
					printf(" Too many heads (%d). Cannot translate. Give up! 8-E\n", pHDF->heads);
					return;
				}

				printf(" Translate: sectors %d->%d, heads %d->%d.\n", pHDF->sectors, pHDF->sectors/2, pHDF->heads, pHDF->heads*2);

				pHDF->sectors /= 2;
				pHDF->heads *= 2;
			}
			return;
		}
	}

	unsigned long head, cyl, spt;
	unsigned long sptt[] = { 63, 127, 255, 0 };
	uint32_t total = pHDF->file.size / 512;

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
				if (cyl <= 65535) break; // Should there some head constraint here?
			}
		}
		if (head <= 16) break;
	}
	pHDF->cylinders = (unsigned short)cyl;
	pHDF->heads = (unsigned short)head;
	pHDF->sectors = (unsigned short)spt;

	if (isHDF && flg)
	{
		printf("No RDB header found in HDF image. Assume it's image of single partition. Use Virtual RDB header.\n");
		pHDF->offset = -(pHDF->heads * pHDF->sectors);
	}
	else
	{
		printf("No RDB header found. Possible non-Amiga or empty image.\n");
	}
}

unsigned char OpenHardfile(unsigned char unit)
{
	hdf[unit].enabled = 0;
	if (config.enable_ide && config.hardfile[unit].enabled)
	{
		printf("\nChecking HDD %d\n", unit);
		if (config.hardfile[unit].filename[0])
		{
			if (FileOpenEx(&hdf[unit].file, config.hardfile[unit].filename, FileCanWrite(config.hardfile[unit].filename) ? O_RDWR : O_RDONLY))
			{
				hdf[unit].enabled = 1;
				printf("file: \"%s\": ", hdf[unit].file.name);
				SetHardfileGeometry(&hdf[unit], !strcasecmp(".hdf", config.hardfile[unit].filename + strlen(config.hardfile[unit].filename) - 4));
				printf("size: %llu (%llu MB)\n", hdf[unit].file.size, hdf[unit].file.size >> 20);
				printf("CHS: %u/%u/%u", hdf[unit].cylinders, hdf[unit].heads, hdf[unit].sectors);
				printf(" (%lu MB), ", ((((unsigned long)hdf[unit].cylinders) * hdf[unit].heads * hdf[unit].sectors) >> 11));
				printf("Offset: %d\n", hdf[unit].offset);
				return 1;
			}
		}
		printf("HDD %d: not present\n", unit);
	}

	// close opened before.
	FileClose(&hdf[unit].file);
	return 0;
}
