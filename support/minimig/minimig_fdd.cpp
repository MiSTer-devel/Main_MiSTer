/*
Copyright 2005, 2006, 2007 Dennis van Weeren
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

// 2009-11-14   - adapted gap size
// 2009-12-24   - updated sync word list
//              - fixed sector header generation
// 2010-01-09   - support for variable number of tracks

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include "../../hardware.h"
#include "../../file_io.h"
#include "minimig_fdd.h"
#include "minimig_config.h"
#include "../../debug.h"
#include "../../user_io.h"
#include "../../menu.h"
#include "adflib.h"

unsigned char drives = 0; // number of active drives reported by FPGA (may change only during reset)
adfTYPE *pdfx;            // drive select pointer
adfTYPE df[4] = {};    // drive information structure

static uint8_t sector_buffer[512];

unsigned char Error;

#define TRACK_SIZE 12668
#define HEADER_SIZE 0x40
#define DATA_SIZE 0x400
#define SECTOR_SIZE (HEADER_SIZE + DATA_SIZE)
#define SECTOR_COUNT 11
#define LAST_SECTOR (SECTOR_COUNT - 1)
#define GAP_SIZE (TRACK_SIZE - SECTOR_COUNT * SECTOR_SIZE)

#define B2W(a,b) (((((uint16_t)(a))<<8) & 0xFF00) | ((uint16_t)(b) & 0x00FF))

						  // sends the data in the sector buffer to the FPGA, translated into an Amiga floppy format sector
						  // note that we do not insert clock bits because they will be stripped by the Amiga software anyway
void SendSector(unsigned char *pData, unsigned char sector, unsigned char track, unsigned char dsksynch, unsigned char dsksyncl)
{
	unsigned char checksum[4];
	unsigned short i;
	unsigned char x,y;
	unsigned char *p;

	// preamble
	spi_w(0xAAAA);
	spi_w(0xAAAA);

	// synchronization
	spi_w(B2W(dsksynch, dsksyncl));
	spi_w(B2W(dsksynch, dsksyncl));

	// odd bits of header
	x = 0x55;
	checksum[0] = x;
	y = (track >> 1) & 0x55;
	checksum[1] = y;
	spi_w(B2W(x,y));

	x = (sector >> 1) & 0x55;
	checksum[2] = x;
	y = ((11 - sector) >> 1) & 0x55;
	checksum[3] = y;
	spi_w(B2W(x, y));

	// even bits of header
	x = 0x55;
	checksum[0] ^= x;
	y = track & 0x55;
	checksum[1] ^= y;
	spi_w(B2W(x, y));

	x = sector & 0x55;
	checksum[2] ^= x;
	y = (11 - sector) & 0x55;
	checksum[3] ^= y;
	spi_w(B2W(x, y));

	// sector label and reserved area (changes nothing to checksum)
	i = 0x10;
	while (i--) spi_w(0xAAAA);

	// send header checksum
	spi_w(0xAAAA);
	spi_w(0xAAAA);
	spi_w(B2W(checksum[0] | 0xAA, checksum[1] | 0xAA));
	spi_w(B2W(checksum[2] | 0xAA, checksum[3] | 0xAA));

	// calculate data checksum
	checksum[0] = 0;
	checksum[1] = 0;
	checksum[2] = 0;
	checksum[3] = 0;

	p = pData;
	i = DATA_SIZE / 2 / 4;
	while (i--)
	{
		x = *p++;
		checksum[0] ^= x ^ x >> 1;
		x = *p++;
		checksum[1] ^= x ^ x >> 1;
		x = *p++;
		checksum[2] ^= x ^ x >> 1;
		x = *p++;
		checksum[3] ^= x ^ x >> 1;
	}

	// send data checksum
	spi_w(0xAAAA);
	spi_w(0xAAAA);
	spi_w(B2W(checksum[0] | 0xAA, checksum[1] | 0xAA));
	spi_w(B2W(checksum[2] | 0xAA, checksum[3] | 0xAA));

	// odd bits of data field
	i = DATA_SIZE / 4;
	p = pData;
	while (i--)
	{
		x = (*p++ >> 1) | 0xAA;
		y = (*p++ >> 1) | 0xAA;
		spi_w(B2W(x, y));
	}

	// even bits of data field
	i = DATA_SIZE / 4;
	p = pData;
	while (i--)
	{
		x = *p++ | 0xAA;
		y = *p++ | 0xAA;
		spi_w(B2W(x, y));
	}
}

void SendGap(void)
{
	unsigned short i = GAP_SIZE/2;
	while (i--) spi_w(0xAAAA);
}

// read a track from disk
void ReadTrack(adfTYPE *drive)
{
	// track number is updated in drive struct before calling this function

	unsigned char sector;
	unsigned char status;
	unsigned char track;
	unsigned short dsksync;
	uint16_t tmp;

	if (drive->track >= drive->tracks)
	{
		fdd_debugf("Illegal track read: %d\n", drive->track);
		drive->track = drive->tracks - 1;
	}

	unsigned long lba;

	if (drive->track != drive->track_prev)
	{ // track step or track 0, start at beginning of track
		drive->track_prev = drive->track;
		sector = 0;
		drive->sector_offset = sector;
		lba = drive->track * SECTOR_COUNT;
	}
	else
	{ // same track, start at next sector in track
		sector = drive->sector_offset;
		lba = (drive->track * SECTOR_COUNT) + sector;
	}

	if (!FileSeekLBA(&drive->file, lba))
	{
		return;
	}

	EnableFpga();
	tmp = spi_w(0);
	status = (uint8_t)(tmp>>8); // read request signal
	track = (uint8_t)tmp; // track number (cylinder & head)
	dsksync = spi_w(0); // disk sync
	spi_w(0); // mfm words to transfer
	DisableFpga();

	if (track >= drive->tracks)
		track = drive->tracks - 1;

	while (1)
	{
		FileReadSec(&drive->file, sector_buffer);

		EnableFpga();

		// check if FPGA is still asking for data
		tmp = spi_w(0);
		status = (uint8_t)(tmp >> 8); // read request signal
		track = (uint8_t)tmp; // track number (cylinder & head)
		dsksync = spi_w(0); // disk sync
		spi_w(0); // mfm words to transfer

		if (track >= drive->tracks)
			track = drive->tracks - 1;

		// workaround for Copy Lock in Wiz'n'Liz and North&South (might brake other games)
		if (dsksync == 0x0000 || dsksync == 0x8914 || dsksync == 0xA144)
			dsksync = 0x4489;

		// North&South: $A144
		// Wiz'n'Liz (Copy Lock): $8914
		// Prince of Persia: $4891
		// Commando: $A245

		// some loaders stop dma if sector header isn't what they expect
		// because we don't check dma transfer count after sending a word
		// the track can be changed while we are sending the rest of the previous sector
		// in this case let's start transfer from the beginning
		if (track == drive->track)
		{
			// send sector if fpga is still asking for data
			if (status & CMD_RDTRK)
			{
				//GenerateHeader(sector_header, sector_buffer, sector, track, dsksync);
				//SendSector(sector_header, sector_buffer);
				SendSector(sector_buffer, sector, track, (unsigned char)(dsksync >> 8), (unsigned char)dsksync);

				if (sector == LAST_SECTOR)
					SendGap();
			}
		}

		// we are done accessing FPGA
		DisableFpga();

		// track has changed
		if (track != drive->track)
			break;

		// read dma request
		if (!(status & CMD_RDTRK))
			break;

		sector++;
		if (sector >= SECTOR_COUNT)
		{
			// go to the start of current track
			sector = 0;
			lba = drive->track * SECTOR_COUNT;
			if (!FileSeekLBA(&drive->file, lba))
			{
				return;
			}
		}

		// remember current sector
		drive->sector_offset = sector;
	}
}

unsigned char FindSync(adfTYPE *drive)
// reads data from fifo till it finds sync word or fifo is empty and dma inactive (so no more data is expected)
{
	unsigned char  c1, c2;
	unsigned short n;
	uint16_t tmp;

	while (1)
	{
		EnableFpga();
		tmp = spi_w(0);
		c1 = (uint8_t)(tmp >> 8); // write request signal
		c2 = (uint8_t)tmp; // track number (cylinder & head)
		if (!(c1 & CMD_WRTRK))
			break;
		if (c2 != drive->track)
			break;
		spi_w(0); //disk sync word

		n = spi_w(0) & 0xBFFF; // mfm words to transfer

		if (n == 0)
			break;

		n &= 0x3FFF;

		while (n--)
		{
			if (spi_w(0) == 0x4489)
			{
				DisableFpga();
				return 1;
			}
		}
		DisableFpga();
	}
	DisableFpga();
	return 0;
}

unsigned char GetHeader(unsigned char *pTrack, unsigned char *pSector)
// this function reads data from fifo till it finds sync word or dma is inactive
{
	unsigned char c, c1, c2, c3, c4;
	unsigned char i;
	unsigned char checksum[4];
	uint16_t tmp;

	Error = 0;
	while (1)
	{
		EnableFpga();
		c1 = (uint8_t)(spi_w(0)>>8); // write request signal, track number (cylinder & head)
		if (!(c1 & CMD_WRTRK))
			break;
		spi_w(0); //disk sync word
		tmp = spi_w(0); // mfm words to transfer

		if ((tmp & 0x3F00) != 0 || (tmp & 0xFF) > 24)// remaining header data is 25 mfm words
		{
			tmp = spi_w(0); // second sync
			if (tmp != 0x4489)
			{
				Error = 21;
				fdd_debugf("\nSecond sync word missing...\n");
				break;
			}

			tmp = spi_w(0);
			c = (uint8_t)(tmp >> 8);
			checksum[0] = c;
			c1 = (c & 0x55) << 1;
			c = (uint8_t)tmp;
			checksum[1] = c;
			c2 = (c & 0x55) << 1;

			tmp = spi_w(0);
			c = (uint8_t)(tmp >> 8);
			checksum[2] = c;
			c3 = (c & 0x55) << 1;
			c = (uint8_t)tmp;
			checksum[3] = c;
			c4 = (c & 0x55) << 1;

			tmp = spi_w(0);
			c = (uint8_t)(tmp >> 8);
			checksum[0] ^= c;
			c1 |= c & 0x55;
			c = (uint8_t)tmp;
			checksum[1] ^= c;
			c2 |= c & 0x55;

			tmp = spi_w(0);
			c = (uint8_t)(tmp >> 8);
			checksum[2] ^= c;
			c3 |= c & 0x55;
			c = (uint8_t)tmp;
			checksum[3] ^= c;
			c4 |= c & 0x55;

			if (c1 != 0xFF) // always 0xFF
				Error = 22;
			else if (c2 > 159) // Track number (0-159)
				Error = 23;
			else if (c3 > 10) // Sector number (0-10)
				Error = 24;
			else if (c4 > 11 || c4 == 0) // Number of sectors to gap (1-11)
				Error = 25;

			if (Error)
			{
				fdd_debugf("\nWrong header: %u.%u.%u.%u\n", c1, c2, c3, c4);
				break;
			}

			*pTrack = c2;
			*pSector = c3;

			for (i = 0; i < 8; i++)
			{
				tmp = spi_w(0);
				checksum[0] ^= (uint8_t)(tmp >> 8);
				checksum[1] ^= (uint8_t)tmp;
				tmp = spi_w(0);
				checksum[2] ^= (uint8_t)(tmp >> 8);
				checksum[3] ^= (uint8_t)tmp;
			}

			checksum[0] &= 0x55;
			checksum[1] &= 0x55;
			checksum[2] &= 0x55;
			checksum[3] &= 0x55;

			tmp = (spi_w(0) & 0x5555) << 1;
			c1 = (uint8_t)(tmp >> 8);
			c2 = (uint8_t)tmp;
			tmp = (spi_w(0) & 0x5555) << 1;
			c3 = (uint8_t)(tmp >> 8);
			c4 = (uint8_t)tmp;

			tmp = spi_w(0) & 0x5555;
			c1 |= (uint8_t)(tmp >> 8);
			c2 |= (uint8_t)tmp;
			tmp = spi_w(0) & 0x5555;
			c3 |= (uint8_t)(tmp >> 8);
			c4 |= (uint8_t)tmp;

			if (c1 != checksum[0] || c2 != checksum[1] || c3 != checksum[2] || c4 != checksum[3])
			{
				Error = 26;
				break;
			}

			DisableFpga();
			return 1;
		}
		else if ((tmp & 0x8000) == 0) // not enough data for header and write dma is not active
		{
			Error = 20;
			break;
		}

		DisableFpga();
	}

	DisableFpga();
	return 0;
}

unsigned char GetData(void)
{
	unsigned char c, c1, c2, c3, c4;
	unsigned char i;
	unsigned char *p;
	unsigned short n;
	unsigned char checksum[4];
	uint16_t tmp;

	Error = 0;
	while (1)
	{
		EnableFpga();
		c1 = (uint8_t)(spi_w(0) >> 8); // write request signal, track number (cylinder & head)
		if (!(c1 & CMD_WRTRK))
			break;
		spi_w(0);
		tmp = spi_w(0); // mfm words to transfer

		n = tmp & 0x3FFF;

		if (n >= 0x204)
		{
			tmp = (spi_w(0) & 0x5555) << 1;
			c1 = (uint8_t)(tmp >> 8);
			c2 = (uint8_t)tmp & 0x55;
			tmp = (spi_w(0) & 0x5555) << 1;
			c3 = (uint8_t)(tmp >> 8);
			c4 = (uint8_t)tmp;

			tmp = spi_w(0) & 0x5555;
			c1 |= (uint8_t)(tmp >> 8);
			c2 |= (uint8_t)tmp;
			tmp = spi_w(0) & 0x5555;
			c3 |= (uint8_t)(tmp >> 8);
			c4 |= (uint8_t)tmp;

			checksum[0] = 0;
			checksum[1] = 0;
			checksum[2] = 0;
			checksum[3] = 0;

			// odd bits of data field
			i = 128;
			p = sector_buffer;
			do
			{
				tmp = spi_w(0);
				c = (uint8_t)(tmp >> 8);
				checksum[0] ^= c;
				*p++ = (c & 0x55) << 1;
				c = (uint8_t)tmp;
				checksum[1] ^= c;
				*p++ = (c & 0x55) << 1;
				tmp = spi_w(0);
				c = (uint8_t)(tmp >> 8);
				checksum[2] ^= c;
				*p++ = (c & 0x55) << 1;
				c = (uint8_t)tmp;
				checksum[3] ^= c;
				*p++ = (c & 0x55) << 1;
			} while (--i);

			// even bits of data field
			i = 128;
			p = sector_buffer;
			do
			{
				tmp = spi_w(0);
				c = (uint8_t)(tmp >> 8);
				checksum[0] ^= c;
				*p++ |= c & 0x55;
				c = (uint8_t)tmp;
				checksum[1] ^= c;
				*p++ |= c & 0x55;
				tmp = spi_w(0);
				c = (uint8_t)(tmp >> 8);
				checksum[2] ^= c;
				*p++ |= c & 0x55;
				c = (uint8_t)tmp;
				checksum[3] ^= c;
				*p++ |= c & 0x55;
			} while (--i);

			checksum[0] &= 0x55;
			checksum[1] &= 0x55;
			checksum[2] &= 0x55;
			checksum[3] &= 0x55;

			if (c1 != checksum[0] || c2 != checksum[1] || c3 != checksum[2] || c4 != checksum[3])
			{
				Error = 29;
				break;
			}

			DisableFpga();
			return 1;
		}
		else if ((tmp & 0x8000) == 0) // not enough data in fifo and write dma is not active
		{
			Error = 28;
			break;
		}

		DisableFpga();
	}
	DisableFpga();
	return 0;
}

void WriteTrack(adfTYPE *drive)
{
	unsigned char Track;
	unsigned char Sector;

	unsigned long lba = drive->track * SECTOR_COUNT;

	//    drive->track_prev = drive->track + 1; // This causes a read that directly follows a write to the previous track to return bad data.
	drive->track_prev = -1; // just to force next read from the start of current track

	while (FindSync(drive))
	{
		if (GetHeader(&Track, &Sector))
		{
			if (Track == drive->track)
			{
				if (!FileSeekLBA(&drive->file, lba+Sector))
				{
					return;
				}

				if (GetData())
				{
					if (drive->status & DSK_WRITABLE)
					{
						FileWriteSec(&drive->file, sector_buffer);
					}
					else
					{
						Error = 30;
						fdd_debugf("Write attempt to protected disk!\n");
					}
				}
			}
			else
				Error = 27; //track number reported in sector header is not the same as current drive track
		}
		if (Error)
		{
			fdd_debugf("WriteTrack: error %u\n", Error);
			ErrorMessage("  WriteTrack", Error);
		}
	}
}

void UpdateDriveStatus(void)
{
	EnableFpga();
	spi_w(0x1000 | df[0].status | (df[1].status << 1) | (df[2].status << 2) | (df[3].status << 3));
	DisableFpga();
}

void HandleFDD(unsigned char c1, unsigned char c2)
{
	unsigned char sel;
	drives = (c1 >> 4) & 0x03; // number of active floppy drives

	if (c1 & CMD_RDTRK)
	{
		DISKLED_ON;
		sel = (c1 >> 6) & 0x03;
		df[sel].track = c2;
		ReadTrack(&df[sel]);
		DISKLED_OFF;
	}
	else if (c1 & CMD_WRTRK)
	{
		DISKLED_ON;
		sel = (c1 >> 6) & 0x03;
		df[sel].track = c2;
		WriteTrack(&df[sel]);
		DISKLED_OFF;
	}
}

// insert floppy image pointed to to by global <file> into <drive>
// this was hakked to this defree in a stint, co the code really need a checkup
void InsertFloppy(adfTYPE *drive, char* path)
{
	if(strcasestr(path, ".exe")) { // should use magic number
		// max length of the gerated tmp file name
		char adfpath[L_tmpnam];
		char tmpstr[32];
		char targetpath[1024];
		unsigned char bootsector[1024] = {0x44, 0x4F, 0x53, 0x00, 0xC0, 0x20, 0x0F ,0x19, 0x00 ,0x00 ,0x03 \
			, 0x70, 0x43 ,0xFA, 0x00, 0x18, 0x4E, 0xAE, 0xFF, 0xA0, 0x4A, 0x80, 0x67, 0x0A, 0x20, 0x40 \
			, 0x20, 0x68, 0x00, 0x16, 0x70, 0x00, 0x4E, 0x75, 0x70, 0xFF, 0x60, 0xFA, 0x64, 0x6F, 0x73 \
			, 0x2E, 0x6C, 0x69, 0x62, 0x72, 0x61, 0x72, 0x79, 0x00 };
		struct stat st;
		struct File *amifile;
		struct File *exefile;
		struct Volume *vol;
		char *tmpname = tmpnam_r(adfpath);
		// create adf dump file
		struct Device *dev = adfCreateDumpDevice(adfpath, 80, 2, 11);
		adfCreateFlop(dev, tmpstr, 0);
		vol = adfMount(dev, 0, FALSE);
		adfInstallBootBlock(vol, bootsector);
		adfCreateDir(vol, vol->curDirPtr, "S"); // we assume current dir is the root of the ADF	
		adfCreateDir(vol, vol->curDirPtr, "L"); //
		adfChangeDir(vol, "S");
		amifile = adfOpenFile(vol, "Startup-Sequence", "w");
		char* seq = ":exe";
		adfWriteFile(amifile, 4, (uint8_t *)seq);
		adfCloseFile(amifile);
		adfToRootDir(vol);

		// generate working path to the actual exe
		getcwd(targetpath, 1024);
		if (path[0] != '/')
		{
			sprintf(targetpath, "%s/%s", getRootDir(), path);
		}
		else
		{
			sprintf(targetpath, "%s", path);
		}

		FILE* targetfile = fopen(targetpath, "r");
		if (targetfile != NULL)
		{
			if (fstat(fileno(targetfile), &st) != 0)
			{
				printf("Error in stat(): %i (%s)", errno, strerror(errno));
				return;
			}
			long size = (long)st.st_size;		
			// transfer the exe to the target ADF
			exefile = adfOpenFile(vol, "exe", "w");
			unsigned char *exebin = (unsigned char *)malloc(size);		
			if (fread(exebin, 1, size, targetfile) != size)
			{
				printf("Error reading executable: %i (%s)", errno, strerror(errno));
				return;
			}
			adfWriteFile(exefile, size, exebin);
			adfCloseFile(exefile);
			fclose(targetfile);
			free(exebin);
			path = adfpath;
		} else {
			printf("Error %i (%s)", errno, strerror(errno));
			return;
		}
	}

	int writable = FileCanWrite(path);

	if (!FileOpenEx(&drive->file, path, writable ? O_RDWR | O_SYNC : O_RDONLY))
	{
		return;
	}

	unsigned long tracks;

	// calculate number of tracks in the ADF image file
	tracks = drive->file.size / (512 * 11);
	if (tracks > MAX_TRACKS)
	{
		menu_debugf("UNSUPPORTED ADF SIZE!!! Too many tracks: %lu\n", tracks);
		tracks = MAX_TRACKS;
	}
	drive->tracks = (unsigned char)tracks;
	strcpy(drive->name, path);

	// initialize the rest of drive struct
	drive->status = DSK_INSERTED;
	if (writable) // read-only attribute
		drive->status |= DSK_WRITABLE;

	drive->sector_offset = 0;
	drive->track = 0;
	drive->track_prev = -1;

	menu_debugf("Inserting floppy: \"%s\"\n", path);
	menu_debugf("file writable: %d\n", writable);
	menu_debugf("file size: %lu (%lu KB)\n", drive->file.size, drive->file.size >> 10);
	menu_debugf("drive tracks: %u\n", drive->tracks);
	menu_debugf("drive status: 0x%02X\n", drive->status);
}
