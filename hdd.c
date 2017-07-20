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
#include "hdd.h"
#include "hdd_internal.h"
#include "menu.h"
#include "config.h"
#include "debug.h"
#include "fpga_io.h"


#define SWAP(a)  ((((a)&0x000000ff)<<24)|(((a)&0x0000ff00)<<8)|(((a)&0x00ff0000)>>8)|(((a)&0xff000000)>>24))
#define SWAPW(a) ((((a)<<8)&0xff00)|(((a)>>8)&0x00ff))

// hardfile structure
hdfTYPE hdf[2] = { 0 };

static uint8_t sector_buffer[512];

unsigned char GetDiskStatus(void)
{
	unsigned char status;

	EnableFpga();
	status = (uint8_t)(spi_w(0) >> 8);
	spi_w(0);
	spi_w(0);
	DisableFpga();

	return status;
}

// RDBChecksum()
static void RDBChecksum(unsigned long *p)
{
	unsigned long count = p[1];
	unsigned long c2;
	long result = 0;
	p[2] = 0;
	for (c2 = 0; c2<count; ++c2) result += p[c2];
	p[2] = (unsigned long)-result;
}


// FakeRDB()
// if the hardfile doesn't have a RigidDiskBlock, we synthesize one
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
		strcpy(pb->pb_DriveName, unit ? "1HD\003" : "0HD\003");  // "DH0"/"DH1" BCPL string
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


// IdentifiyDevice()
// builds Identify Device struct
void IdentifyDevice(unsigned short *pBuffer, unsigned char unit)
{
	char *p, i, x;
	unsigned long total_sectors = hdf[unit].cylinders * hdf[unit].heads * hdf[unit].sectors;
	memset(pBuffer, 0, 512);

	switch (hdf[unit].type) {
	case HDF_FILE | HDF_SYNTHRDB:
	case HDF_FILE:
		pBuffer[0] = 1 << 6; // hard disk
		pBuffer[1] = hdf[unit].cylinders; // cyl count
		pBuffer[3] = hdf[unit].heads; // head count
		pBuffer[6] = hdf[unit].sectors; // sectors per track
										// FIXME - can get serial no from card itself.
		memcpy((char*)&pBuffer[10], "MiSTMiniMigHardfile ", 20); // serial number - byte swapped
		memcpy((char*)&pBuffer[23], ".100    ", 8); // firmware version - byte swapped
		p = (char*)&pBuffer[27];
		// FIXME - likewise the model name can be fetched from the card.
		if (hdf[unit].type & HDF_SYNTHRDB) {
			memcpy(p, "DON'T                                   ", 40);
			p += 8;
			memcpy(p, "REPARTITION!    ", 16);
		}
		else {
			memcpy(p, "YAQUBE                                  ", 40); // model name - byte swapped
			p += 8;
			for (i = 0; (x = config.hardfile[unit].long_name[i]) && i < 16; i++) // copy file name as model name
				p[i] = x;
		}
		// SwapBytes((char*)&pBuffer[27], 40); //not for 68000
		break;
	}

	pBuffer[47] = 0x8010; // maximum sectors per block in Read/Write Multiple command
	pBuffer[53] = 1;
	pBuffer[54] = hdf[unit].cylinders;
	pBuffer[55] = hdf[unit].heads;
	pBuffer[56] = hdf[unit].sectors;
	pBuffer[57] = (unsigned short)total_sectors;
	pBuffer[58] = (unsigned short)(total_sectors >> 16);
}


// chs2lba()
unsigned long chs2lba(unsigned short cylinder, unsigned char head, unsigned short sector, unsigned char unit)
{
	return(cylinder * hdf[unit].heads + head) * hdf[unit].sectors + sector - 1;
}


// WriteTaskFile()
void WriteTaskFile(unsigned char error, unsigned char sector_count, unsigned char sector_number, unsigned char cylinder_low, unsigned char cylinder_high, unsigned char drive_head)
{
	EnableFpga();

	spi_w(CMD_IDE_REGS_WR<<8); // write task file registers command
	spi_w(0); // dummy
	spi_w(0); // dummy
	spi_w(0); // dummy

	spi_w(error);         // error
	spi_w(sector_count);  // sector count
	spi_w(sector_number); // sector number
	spi_w(cylinder_low);  // cylinder low
	spi_w(cylinder_high); // cylinder high
	spi_w(drive_head);    // drive/head

	DisableFpga();
}


// WriteStatus()
void WriteStatus(unsigned char status)
{
	EnableFpga();
	spi_w((CMD_IDE_STATUS_WR<<8) | status);
	spi_w(0);
	spi_w(0);
	DisableFpga();
}


// ATA_Recalibrate()
static void ATA_Recalibrate(unsigned char* tfr, unsigned char unit)
{
	// Recalibrate 0x10-0x1F (class 3 command: no data)
	hdd_debugf("IDE%d: Recalibrate", unit);
	WriteTaskFile(0, 0, 1, 0, 0, tfr[6] & 0xF0);
	WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
}


// ATA_Diagnostic()
static void ATA_Diagnostic(unsigned char* tfr)
{
	// Execute Drive Diagnostic (0x90)
	hdd_debugf("IDE: Drive Diagnostic");
	WriteTaskFile(1, 0, 0, 0, 0, 0);
	WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
}


// ATA_IdentifyDevice()
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


// ATA_Initialize()
static void ATA_Initialize(unsigned char* tfr, unsigned char unit)
{
	// Initialize Device Parameters (0x91)
	hdd_debugf("Initialize Device Parameters");
	hdd_debugf("IDE%d: %02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X", unit, tfr[0], tfr[1], tfr[2], tfr[3], tfr[4], tfr[5], tfr[6], tfr[7]);
	WriteTaskFile(0, tfr[2], tfr[3], tfr[4], tfr[5], tfr[6]);
	WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
}


// ATA_SetMultipleMode()
static void ATA_SetMultipleMode(unsigned char* tfr, unsigned char unit)
{
	// Set Multiple Mode (0xc6)
	hdd_debugf("Set Multiple Mode");
	hdd_debugf("IDE%d: %02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X", unit, tfr[0], tfr[1], tfr[2], tfr[3], tfr[4], tfr[5], tfr[6], tfr[7]);
	hdf[unit].sectors_per_block = tfr[2];
	WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
}


// ATA_ReadSectors()
static void ATA_ReadSectors(unsigned char* tfr, unsigned short sector, unsigned short cylinder, unsigned char head, unsigned char unit, unsigned short sector_count)
{
	// Read Sectors (0x20)
	long lba;
	sector = tfr[3];
	cylinder = tfr[4] | (tfr[5] << 8);
	head = tfr[6] & 0x0F;
	sector_count = tfr[2];
	if (sector_count == 0) sector_count = 0x100;
	hdd_debugf("IDE%d: read %d.%d.%d, %d", unit, cylinder, head, sector, sector_count);
	switch (hdf[unit].type) {
	case HDF_FILE | HDF_SYNTHRDB:
	case HDF_FILE:
		lba = chs2lba(cylinder, head, sector, unit);
		if (hdf[unit].file.size) HardFileSeek(&hdf[unit], (lba + hdf[unit].offset) < 0 ? 0 : lba + hdf[unit].offset);
		while (sector_count) {
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
			if (((lba + hdf[unit].offset)<0) || ((unit == 0) && (hdf[unit].type == HDF_FILE | HDF_SYNTHRDB) && (lba == 0)))
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
				spi_w(CMD_IDE_DATA_WR << 8); // write data command
				spi_w(0);
				spi_w(0);
				spi_block_write_16be((uint16_t*)sector_buffer);
				DisableFpga();

				WriteStatus(sector_count == 1 ? IDE_STATUS_IRQ | IDE_STATUS_END : IDE_STATUS_IRQ);
			}
			else
			{
				while (!(GetDiskStatus() & CMD_IDECMD)); // wait for empty sector buffer
				WriteStatus(IDE_STATUS_IRQ);
				if (hdf[unit].file.size)
				{
					FileReadSec(&hdf[unit].file, sector_buffer);
					EnableFpga();
					spi_w(CMD_IDE_DATA_WR << 8); // write data command
					spi_w(0);
					spi_w(0);
					spi_block_write_16be((uint16_t*)sector_buffer);
					DisableFpga();
				}
			}
			lba++;
			sector_count--; // decrease sector count
		}
		break;
	}
}


// HandleHDD()
void HandleHDD(unsigned char c1, unsigned char c2)
{
	unsigned char  tfr[8];
	unsigned short i;
	unsigned short sector;
	unsigned short cylinder;
	unsigned char  head;
	unsigned char  unit;
	unsigned short sector_count;
	unsigned short block_count;

	if (c1 & CMD_IDECMD)
	{
		DISKLED_ON;
		EnableFpga();
		spi_w(CMD_IDE_REGS_RD<<8); // read task file registers
		spi_w(0);
		spi_w(0);
		for (i = 0; i < 8; i++) tfr[i] = (uint8_t)spi_w(0);
		DisableFpga();
		unit = tfr[6] & 0x10 ? 1 : 0; // master/slave selection
		if (0) hdd_debugf("IDE%d: %02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X", unit, tfr[0], tfr[1], tfr[2], tfr[3], tfr[4], tfr[5], tfr[6], tfr[7]);

		if ((tfr[7] & 0xF0) == ACMD_RECALIBRATE) {
			ATA_Recalibrate(tfr, unit);
		}
		else if (tfr[7] == ACMD_DIAGNOSTIC) {
			ATA_Diagnostic(tfr);
		}
		else if (tfr[7] == ACMD_IDENTIFY_DEVICE) {
			ATA_IdentifyDevice(tfr, unit);
		}
		else if (tfr[7] == ACMD_INITIALIZE_DEVICE_PARAMETERS) {
			ATA_Initialize(tfr, unit);
		}
		else if (tfr[7] == ACMD_SET_MULTIPLE_MODE) {
			ATA_SetMultipleMode(tfr, unit);
		}
		else if (tfr[7] == ACMD_READ_SECTORS) {
			ATA_ReadSectors(tfr, sector, cylinder, head, unit, sector_count);
		}
		else if (tfr[7] == ACMD_READ_MULTIPLE) {
			// Read Multiple Sectors (multiple sector transfer per IRQ)
			long lba;
			WriteStatus(IDE_STATUS_RDY); // pio in (class 1) command type
			sector = tfr[3];
			cylinder = tfr[4] | (tfr[5] << 8);
			head = tfr[6] & 0x0F;
			sector_count = tfr[2];
			if (sector_count == 0) sector_count = 0x100;
			hdd_debugf("IDE%d: read_multi %d.%d.%d, %d", unit, cylinder, head, sector, sector_count);

			switch (hdf[unit].type) {
			case HDF_FILE | HDF_SYNTHRDB:
			case HDF_FILE:
				lba = chs2lba(cylinder, head, sector, unit);
				if (hdf[unit].file.size) HardFileSeek(&hdf[unit], (lba + hdf[unit].offset) < 0 ? 0 : lba + hdf[unit].offset);
				// FIXME - READM could cross the fake RDB -> real disk boundary.
				// FIXME - but first we should make some attempt to generate fake RGB in multiple mode.

				while (sector_count) {
					while (!(GetDiskStatus() & CMD_IDECMD)); // wait for empty sector buffer
					block_count = sector_count;
					if (block_count > hdf[unit].sectors_per_block) block_count = hdf[unit].sectors_per_block;
					WriteStatus(IDE_STATUS_IRQ);
					while (block_count--)
					{
						if (hdf[unit].file.size)
						{
							FileReadSec(&hdf[unit].file, sector_buffer);
							EnableFpga();
							spi_w(CMD_IDE_DATA_WR << 8); // write data command
							spi_w(0);
							spi_w(0);
							spi_block_write_16be((uint16_t*)sector_buffer);
							DisableFpga();
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
							else {
								sector++;
							}
						}
						sector_count--;
					}
					WriteTaskFile(0, tfr[2], sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);
					//WriteTaskFile(0, 0, sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);
				}
				//WriteTaskFile(0, 0, sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);
				break;
			}
			WriteStatus(IDE_STATUS_END);
		}
		else if (tfr[7] == ACMD_WRITE_SECTORS) {
			// write sectors
			WriteStatus(IDE_STATUS_REQ); // pio out (class 2) command type
			sector = tfr[3];
			cylinder = tfr[4] | (tfr[5] << 8);
			head = tfr[6] & 0x0F;
			sector_count = tfr[2];
			if (sector_count == 0) sector_count = 0x100;
			long lba = chs2lba(cylinder, head, sector, unit);
			//if (hdf[unit].type>=HDF_CARDPART0)
			lba += hdf[unit].offset;
			if (hdf[unit].file.size) {
				// File size will be 0 in direct card modes
				HardFileSeek(&hdf[unit], (lba>-1) ? lba : 0);
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
				EnableFpga();
				spi_w(CMD_IDE_DATA_RD<<8); // read data command
				spi_w(0);
				spi_w(0);
				spi_block_read_16be((uint16_t*)sector_buffer);
				DisableFpga();
				sector_count--; // decrease sector count
				if (sector_count)
				{
					WriteStatus(IDE_STATUS_IRQ);
				}
				else
				{
					WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
				}

				switch (hdf[unit].type)
				{
				case HDF_FILE | HDF_SYNTHRDB:
				case HDF_FILE:
					// Don't attempt to write to fake RDB
					if (hdf[unit].file.size && (lba>-1))
					{
						FileWriteSec(&hdf[unit].file, sector_buffer);
					}
					lba++;
					break;
				}
			}
		}
		else if (tfr[7] == ACMD_WRITE_MULTIPLE) {
			// write sectors
			WriteStatus(IDE_STATUS_REQ); // pio out (class 2) command type
			sector = tfr[3];
			cylinder = tfr[4] | (tfr[5] << 8);
			head = tfr[6] & 0x0F;
			sector_count = tfr[2];
			if (sector_count == 0) sector_count = 0x100;
			long lba = chs2lba(cylinder, head, sector, unit);
			//if (hdf[unit].type>=HDF_CARDPART0)
			lba += hdf[unit].offset;
			if (hdf[unit].file.size) {
				// File size will be 0 in direct card modes
				HardFileSeek(&hdf[unit], (lba>-1) ? lba : 0);
			}
			while (sector_count) {
				block_count = sector_count;
				if (block_count > hdf[unit].sectors_per_block) block_count = hdf[unit].sectors_per_block;
				while (block_count) {
					while (!(GetDiskStatus() & CMD_IDEDAT)); // wait for full write buffer
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
					//WriteTaskFile(0, tfr[2], sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);
					EnableFpga();
					spi_w(CMD_IDE_DATA_RD<<8); // read data command
					spi_w(0);
					spi_w(0);
					spi_block_read_16be((uint16_t*)sector_buffer);
					DisableFpga();
					switch (hdf[unit].type)
					{
					case HDF_FILE | HDF_SYNTHRDB:
					case HDF_FILE:
						if (hdf[unit].file.size && (lba>-1))
						{
							FileWriteSec(&hdf[unit].file, sector_buffer);
						}
						lba++;
						break;
					}
					block_count--;  // decrease block count
					sector_count--; // decrease sector count
				}
				WriteTaskFile(0, tfr[2], sector, (unsigned char)cylinder, (unsigned char)(cylinder >> 8), (tfr[6] & 0xF0) | head);
				if (sector_count) {
					WriteStatus(IDE_STATUS_IRQ);
				}
				else {
					WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ);
				}
			}
		}
		else {
			hdd_debugf("Unknown ATA command");
			hdd_debugf("IDE%d: %02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X", unit, tfr[0], tfr[1], tfr[2], tfr[3], tfr[4], tfr[5], tfr[6], tfr[7]);
			WriteTaskFile(0x04, tfr[2], tfr[3], tfr[4], tfr[5], tfr[6]);
			WriteStatus(IDE_STATUS_END | IDE_STATUS_IRQ | IDE_STATUS_ERR);
		}
		DISKLED_OFF;
	}
}


// GetHardfileGeometry()
// this function comes from WinUAE, should return the same CHS as WinUAE
void GetHardfileGeometry(hdfTYPE *pHDF)
{
	unsigned long total = 0;
	unsigned long i, head, cyl, spt;
	unsigned long sptt[] = { 63, 127, 255, 0 };

	switch (pHDF->type) {
	case (HDF_FILE | HDF_SYNTHRDB) :
		if (pHDF->file.size == 0) return;
		total = pHDF->file.size / 512;
		pHDF->heads = 1;
		pHDF->sectors = 32;
		pHDF->cylinders = total / 32 + 1;  // Add a cylinder for the fake RDB.
		return;
	case HDF_FILE:
		if (pHDF->file.size == 0) return;
		total = pHDF->file.size / 512;
		break;
	}

	for (i = 0; sptt[i] != 0; i++) {
		spt = sptt[i];
		for (head = 4; head <= 16; head++) {
			cyl = total / (head * spt);
			if (total <= 1024 * 1024) {
				if (cyl <= 1023) break;
			}
			else {
				if (cyl < 16383)
					break;
				if (cyl < 32767 && head >= 5)
					break;
				if (cyl <= 65535)  // Should there some head constraint here?
					break;
			}
		}
		if (head <= 16) break;
	}
	pHDF->cylinders = (unsigned short)cyl;
	pHDF->heads = (unsigned short)head;
	pHDF->sectors = (unsigned short)spt;
}


// HardFileSeek()
unsigned char HardFileSeek(hdfTYPE *pHDF, unsigned long lba)
{
	return FileSeekLBA(&pHDF->file, lba);
}

// OpenHardfile()
unsigned char OpenHardfile(unsigned char unit)
{
	unsigned long time;
	printf("\nChecking HDD %d\n", unit);

	switch (config.hardfile[unit].enabled)
	{
	case HDF_FILE | HDF_SYNTHRDB:
	case HDF_FILE:
		hdf[unit].type = config.hardfile[unit].enabled;
		if (config.hardfile[unit].long_name[0])
		{
			if(FileOpenEx(&hdf[unit].file, config.hardfile[unit].long_name, FileCanWrite(config.hardfile[unit].long_name) ? O_RDWR : O_RDONLY))
			{
				GetHardfileGeometry(&hdf[unit]);
				printf("HARDFILE %d%s:\n", unit, (config.hardfile[unit].enabled&HDF_SYNTHRDB) ? " (with fake RDB)" : "");
				printf("file: \"%s\"\n", hdf[unit].file.name);
				printf("size: %lu (%lu MB)\n", hdf[unit].file.size, hdf[unit].file.size >> 20);
				printf("CHS: %u/%u/%u", hdf[unit].cylinders, hdf[unit].heads, hdf[unit].sectors);
				printf(" (%lu MB), ", ((((unsigned long)hdf[unit].cylinders) * hdf[unit].heads * hdf[unit].sectors) >> 11));
				if (config.hardfile[unit].enabled & HDF_SYNTHRDB) {
					hdf[unit].offset = -(hdf[unit].heads*hdf[unit].sectors);
				}
				else {
					hdf[unit].offset = 0;
				}
				printf("Offset: %d\n\n", hdf[unit].offset);
				config.hardfile[unit].present = 1;
				return 1;
			}
		}
	}

	FileClose(&hdf[unit].file);
	printf("HDD %d: not present\n\n", unit);
	config.hardfile[unit].present = 0;
	return 0;
}

// GetHDFFileType()
unsigned char GetHDFFileType(char *filename)
{
	uint8_t type = HDF_FILETYPE_NOTFOUND;
	fileTYPE rdbfile = { 0 };

	if(FileOpen(&rdbfile, filename))
	{
		type = HDF_FILETYPE_UNKNOWN;
		for (int i = 0; i<16; ++i)
		{
			FileReadSec(&rdbfile, sector_buffer);
			if (sector_buffer[0] == 'R' && sector_buffer[1] == 'D' && sector_buffer[2] == 'S' && sector_buffer[3] == 'K')
			{
				type = HDF_FILETYPE_RDB;
				break;
			}
			if (sector_buffer[0] == 'D' && sector_buffer[1] == 'O' && sector_buffer[2] == 'S')
			{
				type = HDF_FILETYPE_DOS;
				break;
			}
			if (sector_buffer[0] == 'P' && sector_buffer[1] == 'F' && sector_buffer[2] == 'S')
			{
				type = HDF_FILETYPE_DOS;
				break;
			}
			if (sector_buffer[0] == 'S' && sector_buffer[1] == 'F' && sector_buffer[2] == 'S')
			{
				type = HDF_FILETYPE_DOS;
				break;
			}
		}

		FileClose(&rdbfile);
	}

	return type;
}
