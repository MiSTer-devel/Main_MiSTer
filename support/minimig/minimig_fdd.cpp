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
#include <ctype.h>
#include "../../hardware.h"
#include "../../file_io.h"
#include "minimig_fdd.h"
#include "minimig_config.h"
#include "../../debug.h"
#include "../../user_io.h"
#include "../../menu.h"
#include "scpfile.h"
#include "ipffile.h"

unsigned char drives = 0; // number of active drives reported by FPGA (may change only during reset)
adfTYPE *pdfx;            // drive select pointer
adfTYPE df[4] = {};       // drive information structure

struct mfm_data_block {
	uint8_t sector_buffer[512];
	uint8_t extra_bufferA[512];    // these three are just to make the flux data transfer more efficient 
	uint8_t extra_bufferB[512];    // The core can only buffer upto 2048 words (4k), and this is 1024 in total
	uint8_t extra_bufferC[512];    // The code only writes to the core if rhere are LESS than 1024 words in the fifo	
};

static mfm_data_block mfm_data;

unsigned char Error;

#define TRACK_SIZE 12668
#define HEADER_SIZE 0x40
#define DATA_SIZE 0x400
#define SECTOR_SIZE (HEADER_SIZE + DATA_SIZE)
#define SECTOR_COUNT 11
#define LAST_SECTOR (SECTOR_COUNT - 1)
#define GAP_SIZE (TRACK_SIZE - SECTOR_COUNT * SECTOR_SIZE)

#define B2W(a,b) (((((uint16_t)(a))<<8) & 0xFF00) | ((uint16_t)(b) & 0x00FF))

// modified from https://github.com/h5n1xp/Omega/issues/6
uint16_t ADDCLOCKBITS(uint16_t value, uint16_t* previous) {
	// Clear all previously set clock bits
	value &= 0x5555;

	// Compute clock bits (clock bit values are inverted)
	uint16_t lShifted = (value << 1);
	uint16_t rShifted = (value >> 1) | (*previous << 15);
	uint16_t cBitsInv = lShifted | rShifted;

	// Reverse the computed clock bits
	uint16_t cBits = cBitsInv ^ 0xAAAA;

	// Return original value with the clock bits added
	value |= cBits;
	*previous = value;
	
	return value;
}

// sends the data in the sector buffer to the FPGA, translated into an Amiga floppy format sector
// *** note that we do not insert clock bits because they will be stripped by the Amiga software anyway
// note: Clock bits now added as they're needed when being written to real drives!
void SendSector(unsigned char* pData, unsigned char sector, unsigned char track, unsigned char dsksynch, unsigned char dsksyncl, uint16_t* lastBit)
{
	unsigned char checksum[4];
	unsigned short i;
	unsigned char x, y;
	unsigned char* p;

	// preamble
	spi_w(ADDCLOCKBITS(0, lastBit));
	spi_w(ADDCLOCKBITS(0, lastBit));

	// synchronization
	spi_w(B2W(dsksynch, dsksyncl));
	spi_w(B2W(dsksynch, dsksyncl));
	*lastBit = B2W(dsksynch, dsksyncl);

	// odd bits of header
	x = 0x55;
	checksum[0] = x;
	y = track >> 1;
	checksum[1] = y;
	spi_w(ADDCLOCKBITS(B2W(x, y), lastBit));

	x = sector >> 1;
	checksum[2] = x;
	y = (11 - sector) >> 1;
	checksum[3] = y;
	spi_w(ADDCLOCKBITS(B2W(x, y), lastBit));

	// even bits of header
	x = 0x55;
	checksum[0] ^= x;
	y = track;
	checksum[1] ^= y;
	spi_w(ADDCLOCKBITS(B2W(x, y), lastBit));

	x = sector;
	checksum[2] ^= x;
	y = 11 - sector;
	checksum[3] ^= y;
	spi_w(ADDCLOCKBITS(B2W(x, y), lastBit));

	// sector label and reserved area (changes nothing to checksum)
	i = 0x10;
	while (i--) spi_w(ADDCLOCKBITS(0, lastBit));

	// send header checksum
	spi_w(ADDCLOCKBITS(0, lastBit));
	spi_w(ADDCLOCKBITS(0, lastBit));
	spi_w(ADDCLOCKBITS(B2W(checksum[0], checksum[1]), lastBit));
	spi_w(ADDCLOCKBITS(B2W(checksum[2], checksum[3]), lastBit));

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
		checksum[0] ^= x ^ (x >> 1);
		x = *p++;
		checksum[1] ^= x ^ (x >> 1);
		x = *p++;
		checksum[2] ^= x ^ (x >> 1);
		x = *p++;
		checksum[3] ^= x ^ (x >> 1);
	}

	// send data checksum
	spi_w(ADDCLOCKBITS(0, lastBit));
	spi_w(ADDCLOCKBITS(0, lastBit));
	spi_w(ADDCLOCKBITS(B2W(checksum[0], checksum[1]), lastBit));
	spi_w(ADDCLOCKBITS(B2W(checksum[2], checksum[3]), lastBit));

	// odd bits of data field
	i = DATA_SIZE / 4;
	p = pData;
	while (i--)
	{
		x = *p++ >> 1;
		y = *p++ >> 1;
		spi_w(ADDCLOCKBITS(B2W(x, y), lastBit));
	}

	// even bits of data field
	i = DATA_SIZE / 4;
	p = pData;
	while (i--)
	{
		x = *p++;
		y = *p++;
		spi_w(ADDCLOCKBITS(B2W(x, y), lastBit));
	}
}

void SendRawFlux(const uint16_t* buffer, uint32_t numWords) {
	// This can fill the fifo (with 2048 words) in around 820uSec! 
	fpga_spi_fast_block_write(buffer, numWords);
}

void SendGap(uint16_t* lastBit)
{
	unsigned short i = GAP_SIZE / 2;
	while (i--) spi_w(ADDCLOCKBITS(0, lastBit));
}

// read a track from disk
void ReadTrack(adfTYPE* drive)
{
	// track number is updated in drive struct before calling this function
	unsigned char sector;
	unsigned char status;
	unsigned char track;
	unsigned short dsksync;
	uint16_t tmp;

	if (drive->track >= drive->tracks)
	{
		fdd_debugf("Illegal track read: %d of %d from %s\n", drive->track, drive->tracks, drive->name);
		drive->track = drive->tracks - 1;
	}

	unsigned long lba;

	const bool fluxMode = drive->fluxFile && drive->fluxFile->fluxReady();

	if (fluxMode) {		
		drive->fluxFile->selectTrack(drive->track);
		sector = 0;
	}
	else {
		if (drive->track != drive->track_prev)
		{ // track step or track 0, start at beginning of track
			drive->track_prev = drive->track;
			drive->lastBit = 0xAAAA;
			sector = 0;
			drive->sector_offset = sector;
			lba = drive->track * SECTOR_COUNT;
		}
		else
		{ // same track, start at next sector in track
			sector = drive->sector_offset;
			lba = (drive->track * SECTOR_COUNT) + sector;
		}

		if (!FileSeekLBA(&drive->file, lba)) return;
	}

	EnableFpga();
	tmp = spi_w(0);
	status = (uint8_t)(tmp >> 8); // read request signal
	track = (uint8_t)tmp; // track number (cylinder & head)
	dsksync = spi_w(0); // disk sync
	spi_w(0); // mfm words to transfer
	DisableFpga();

	if (track >= drive->tracks) track = drive->tracks - 1;	
	
	// Its possible for flux to be needed at such a high rate that the menu freezes. This allows a small amount of breathing
	int fluxModeCounter = 0;
	while (fluxModeCounter < 32)
	{
		if (fluxMode) {
			if (!drive->fluxFile->fluxRead((uint16_t*)&mfm_data, sizeof(mfm_data) / sizeof(uint16_t))) {
				drive->fluxFile->fluxDummyRead((uint16_t*)&mfm_data, sizeof(mfm_data) / sizeof(uint16_t)); // make ups some data
			}
			fluxModeCounter++;
		} else {
			FileReadSec(&drive->file, mfm_data.sector_buffer);
		}
		
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
				if (fluxMode) {
					SendRawFlux((uint16_t*)&mfm_data, sizeof(mfm_data) / sizeof(uint16_t));
				}
				else {
					//GenerateHeader(sector_header, sector_buffer, sector, track, dsksync);
					//SendSector(sector_header, sector_buffer);
					SendSector(mfm_data.sector_buffer, sector, track, (unsigned char)(dsksync >> 8), (unsigned char)dsksync, &drive->lastBit);

					if (sector == LAST_SECTOR)
						SendGap(&drive->lastBit);
				}
			}
		}

		// we are done accessing FPGA
		DisableFpga();

		// track has changed
		if (track != drive->track) break;

		// read dma request
		if (!(status & CMD_RDTRK)) {
			// Ask the flux system to "wind back" to be as if it didnt just read something
			if (fluxMode) drive->fluxFile->unFluxRead((uint16_t*)&mfm_data, sizeof(mfm_data) / sizeof(uint16_t));
			break;
		}

		if (!fluxMode) {
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
}

unsigned char FindSync(adfTYPE* drive)
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

unsigned char GetHeader(unsigned char* pTrack, unsigned char* pSector)
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
		c1 = (uint8_t)(spi_w(0) >> 8); // write request signal, track number (cylinder & head)
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
	unsigned char* p;
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
			p = mfm_data.sector_buffer;
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
			p = mfm_data.sector_buffer;
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

void WriteTrack(adfTYPE* drive)
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
				if (!FileSeekLBA(&drive->file, lba + Sector))
				{
					return;
				}

				if (GetData())
				{
					if (drive->status & DSK_WRITABLE)
					{
						FileWriteSec(&drive->file, mfm_data.sector_buffer);
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
			Info("Write error");
		}
	}
}

void UpdateDriveStatus(void)
{
	EnableFpga();
	spi_w(0x1000 | df[0].status | (df[1].status << 1) | (df[2].status << 2) | (df[3].status << 3));
	DisableFpga();
	EnableFpga();
	spi_w(0x2000 | df[0].ex_status | (df[1].ex_status << 1) | (df[2].ex_status << 2) | (df[3].ex_status << 3));
	DisableFpga();
}

void HandleFDD(unsigned char c1, unsigned char c2)
{
	unsigned char sel;
	drives = (c1 >> 4) & 0x03; // number of active floppy drives

	if (c1 & CMD_RDTRK)
	{
		sel = (c1 >> 6) & 0x03;
		df[sel].track = c2;
		ReadTrack(&df[sel]);
	}
	else if (c1 & CMD_WRTRK)
	{
		sel = (c1 >> 6) & 0x03;
		df[sel].track = c2;
		WriteTrack(&df[sel]);
	}
}

// insert floppy image pointed to to by global <file> into <drive>
void InsertFloppy(adfTYPE* drive, char* path)
{
	FluxFile* tmpFlux = drive->fluxFile;
	drive->fluxFile = NULL;
	if (tmpFlux) delete tmpFlux;
	tmpFlux = NULL;

	int writable = FileCanWrite(path);
	char* fext = strrchr(path, '.');

	if (fext) {
		fext++;
		char tmp[4] = { 0 };
		for (uint16_t i = 0; i < 3; i++) 
			if (fext[i]) tmp[i] = tolower(fext[i]); else break;

		if (strcmp(tmp, "scp") == 0) {
			tmpFlux = new SCPFile();
			menu_debugf("SCP file: \"%s\"\n", path);
		}
		else
		if (strcmp(tmp, "ipf") == 0) {
			tmpFlux = new IPFFile();
			menu_debugf("IPF file: \"%s\"\n", path);
		}
	}
	
	if (tmpFlux) {
		drive->status = 0;
		if (!tmpFlux->openFile(path)) {
			delete tmpFlux;
			return;
		}		
		menu_debugf("Inserting floppy: \"%s\"\n", path);
		menu_debugf("file writable: %d\n", 0);
		menu_debugf("file size: %lu (%lu KB)\n", drive->file.size, drive->file.size >> 10);
		menu_debugf("drive tracks: %u\n", drive->tracks);
		menu_debugf("drive status: 0x%02X\n", drive->status);

		drive->tracks = tmpFlux->lastTrack() - tmpFlux->firstTrack();		
		drive->sector_offset = 0;
		drive->lastBit = 0xAAAA;
		drive->track = 0;
		drive->track_prev = -1;
		strcpy(drive->name, path);
		drive->fluxFile = tmpFlux;
		drive->ex_status = (tmpFlux->fluxMode() == FLUXMODE_DENSITY) ? DSKEX_FLUXDENSITY : 0;
		drive->status = DSK_INSERTED | DSK_FLUXMODE;
		return;
	}

	if (!FileOpenEx(&drive->file, path, writable ? O_RDWR | O_SYNC : O_RDONLY))
	{
		return;
	}
	menu_debugf("ADF file: \"%s\"\n", path);

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
	drive->ex_status = 0;
	drive->status = DSK_INSERTED;
	if (writable) // read-only attribute
		drive->status |= DSK_WRITABLE;

	drive->sector_offset = 0;
	drive->lastBit = 0xAAAA;
	drive->track = 0;
	drive->track_prev = -1;

	menu_debugf("Inserting floppy: \"%s\"\n", path);
	menu_debugf("file writable: %d\n", writable);
	menu_debugf("file size: %lu (%lu KB)\n", drive->file.size, drive->file.size >> 10);
	menu_debugf("drive tracks: %u\n", drive->tracks);
	menu_debugf("drive status: 0x%02X\n", drive->status);
}

FluxFile::FluxFile() {
	tmpFilename[0] = '\0';
}

// Open Flux based file
bool FluxFile::openFile(const char* filename) {
	closeFile();
	// SCP accessed via ZIP is too slow, and the IPF library cant parse a ZIP, so if it's a ZIP we need to extract the file from it.

	char* z = strcasestr((char*)filename, ".zip");
	if (z) {
		// Extract the file
		fileTYPE file;
		if (!FileOpen(&file, filename, 0)) return false;
		if (!file.zip) {
			FileClose(&file);
			return false;
		}
		strcpy(tmpFilename, "/tmp/fluxfile-XXXXXX");
		int fd = mkstemp(tmpFilename);
		if (fd == -1) {
			// Failed
			FileClose(&file);
			tmpFilename[0] = '\0'; // zero out the filename
			return false;
		}

		char buffer[4096];
		int amount;
		do {
			amount = FileReadAdv(&file, buffer, sizeof(buffer), -1);
			if (amount>0) {
				if (write(fd, buffer, amount) != amount) {
					FileClose(&file);
					close(fd);
					unlink(tmpFilename);
					tmpFilename[0] = '\0'; // zero out the filename
					return false;
				}
			}
		} while (amount > 0);
		FileClose(&file);
		close(fd);

		return _openFile(tmpFilename);
	}
	else {
		return _openFile(filename);
	}
}
void FluxFile::closeFile() {
	_closeFile();
	if (strlen(tmpFilename)) unlink(tmpFilename);
	tmpFilename[0] = '\0';
}
