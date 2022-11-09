//======================================================================
//
// Project:     XTIDE Universal BIOS, Serial Port Server
//
// File:        process.cpp - Processes commands received over the serial port
//

//
// XTIDE Universal BIOS and Associated Tools
// Copyright (C) 2009-2010 by Tomi Tilli, 2011-2013 by XTIDE Universal BIOS Team.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// Visit http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
//

#include "Library.h"
#include <memory.h>
#include <string.h>
#include <stdio.h>

union _buff {
	struct {
		unsigned char command;
		unsigned char driveAndHead;
		unsigned char count;
		unsigned char sector;
		unsigned short cylinder;
	} chs;
	struct {
		unsigned char command;
		unsigned char bits24;
		unsigned char count;
		unsigned char bits00;
		unsigned char bits08;
		unsigned char bits16;
	} lba;
	struct {
		unsigned char command;
		unsigned char driveAndHead;
		unsigned char count;
		unsigned char scan;
		unsigned char port;
		unsigned char baud;
	} inquire;
	struct {
		unsigned char command;
		unsigned char driveAndHead;
		unsigned char count;
		unsigned char scan;
		unsigned short PackedPortAndBaud;
	} inquirePacked;
	unsigned char b[514];
	unsigned short w[257];
} buff;

#define SERIAL_COMMAND_HEADER 0xa0

#define SERIAL_COMMAND_WRITE 1
#define SERIAL_COMMAND_READWRITE 2
#define SERIAL_COMMAND_RWMASK 3
#define SERIAL_COMMAND_INQUIRE 0

#define SERIAL_COMMAND_MASK 0xe3
#define SERIAL_COMMAND_HEADERMASK 0xe0

#define ATA_COMMAND_LBA 0x40
#define ATA_COMMAND_HEADMASK 0xf

#define ATA_DriveAndHead_Drive 0x10

void logBuff( const char *message, unsigned long buffoffset, unsigned long readto, int verboseLevel )
{
	char logBuff[ 514*9 + 10 ];
	unsigned long logCount;

	if( verboseLevel == 5 || (verboseLevel >= 3 && buffoffset == readto) )
	{
		if( verboseLevel == 3 && buffoffset > 11 )
			logCount = 11;
		else
			logCount = buffoffset;

		for(unsigned long t = 0; t < logCount; t++ )
			sprintf( &logBuff[t*9], "[%3lu:%02x] ", t, buff.b[t] );
		if( logCount != buffoffset )
			sprintf( &logBuff[logCount*9], "... " );

		log( 3, "%s%s", message, logBuff );
	}
}

void processRequests( SerialAccess *serial, Image *image0, Image *image1, int timeoutEnabled, int verboseLevel )
{
	unsigned char workCommand;
	int workOffset, workCount;

	unsigned long mylba = 0;
	unsigned long readto;
	unsigned long buffoffset;
	unsigned long lasttick;
	unsigned short crc;
	unsigned long GetTime_Timeout_Local;
	unsigned long len;
	Image *img = NULL;
	unsigned long cyl = 0, sect = 0, head = 0;
	unsigned long perfTimer = 0;
	unsigned char lastScan;

	GetTime_Timeout_Local = GetTime_Timeout();

	buffoffset = 0;
	readto = 0;
	workCount = workOffset = workCommand = 0;
	lastScan = 0;

	//
	// Floppy disks must come after any hard disks
	//
	if( (image0 && image0->floppy) && (image1 && !image1->floppy) )
	{
		img = image0;
		image0 = image1;
		image1 = img;
	}

	lasttick = GetTime();

	while( (len = serial->readCharacters( &buff.b[buffoffset], (readto ? readto-buffoffset : 1) )) )
	{
		buffoffset += len;

		//
		// For debugging, look at the incoming packet
		//
		if( verboseLevel >= 3 )
			logBuff( "    Received: ", buffoffset, readto, verboseLevel );

		if( timeoutEnabled && readto && GetTime() > lasttick + GetTime_Timeout_Local )
		{
			log( 1, "Timeout waiting on data from client, aborting previous command" );

			workCount = workOffset = workCommand = 0;
			readto = 0;

			if( len <= 8 && (buff.b[buffoffset-len] & SERIAL_COMMAND_HEADERMASK) == SERIAL_COMMAND_HEADER )
			{
				// assume that we are at the front of a new command
				//
				memcpy( &buff.b[0], &buff.b[buffoffset-len], len );
				buffoffset = len;
				readto = 8;
				// fall through to normal processing
			}
			else if( len == 1 )
			{
				// one new character, treat it like any other new character received, discarding the buffer
				//
				buff.b[0] = buff.b[buffoffset-1];
				buffoffset = 1;
				// fall through to normal processing
			}
			else
			{
				// discard even the newly received data and start listening anew
				//
				buffoffset = 0;
				continue;
			}
		}

		lasttick = GetTime();

		//
		// No work currently to do, look at each character as they come in...
		//
		if( !readto )
		{
			if( (buff.b[0] & SERIAL_COMMAND_HEADERMASK) == SERIAL_COMMAND_HEADER )
			{
				//
				// Found our command header byte to start a commnad sequence, read the next 7 and evaluate
				//
				readto = 8;
				continue;
			}
			else
			{
				//
				// Spurious characters, discard
				//
				if( verboseLevel >= 2 )
				{
					if( buff.b[0] >= 0x20 && buff.b[0] <= 0x7e )
						log( 2, "Spurious: [%d:%c]", buff.b[0], buff.b[0] );
					else
						log( 2, "Spurious: [%d]", buff.b[0] );
				}
				buffoffset = 0;
				continue;
			}
		}

		//
		// Partial packet received, keep reading...
		//
		if( readto && buffoffset < readto )
			continue;

		//
		// Read 512 bytes from serial port, only one command reads that many characters: Write Sector
		//
		if( buffoffset == readto && readto == 514 )
		{
			buffoffset = readto = 0;
			if( (crc = checksum( &buff.w[0], 256 )) != buff.w[256] )
			{
				log( 0, "Bad Write Sector Checksum" );
				continue;
			}

			if( img->readOnly )
			{
				log( 1, "Attempt to write to read-only image" );
				continue;
			}

			img->seekSector( mylba + workOffset );
			img->writeSector( &buff.w[0] );

			//
			// Echo back the CRC
			//
			if( !serial->writeCharacters( &buff.w[256], 2 ) )
				break;

			workOffset++;
			workCount--;

			if( workCount )
				readto = 1;           // looking for continuation ACK
		}

		//
		// 8 byte command received, or a continuation of the previous command
		//
		else if( (buffoffset == readto && readto == 8) ||
				 (buffoffset == readto && readto == 1 && workCount) )
		{
			buffoffset = readto = 0;
			if( workCount )
			{
				if( verboseLevel > 1 )
					log( 2, "    Continuation: Offset=%u, Checksum=%04x", workOffset-1, buff.w[256] );

				//
				// Continuation...
				//
				if( buff.b[0] != (workCount-0) )
				{
					log( 0, "Continue Fault: Received=%d, Expected=%d", buff.b[0], workCount );
					workCount = 0;
					continue;
				}
			}
			else
			{
				//
				// New Command...
				//
				if( (crc = checksum( &buff.w[0], 3 )) != buff.w[3] )
				{
					log( 0, "Bad Command Checksum: %02x %02x %02x %02x %02x %02x %02x %02x, Checksum=%04x",
						 buff.b[0], buff.b[1], buff.b[2], buff.b[3], buff.b[4], buff.b[5], buff.b[6], buff.b[7], crc);
					continue;
				}

				img = (buff.inquire.driveAndHead & ATA_DriveAndHead_Drive) ? image1 : image0;

				workCommand = buff.chs.command & SERIAL_COMMAND_RWMASK;

				if( (workCommand != SERIAL_COMMAND_INQUIRE) && (buff.chs.driveAndHead & ATA_COMMAND_LBA) )
				{
					mylba = ((((unsigned long) buff.lba.bits24) & ATA_COMMAND_HEADMASK) << 24)
						| (((unsigned long) buff.lba.bits16) << 16)
						| (((unsigned long) buff.lba.bits08) << 8)
						| ((unsigned long) buff.lba.bits00);
				}
				else
				{
					cyl = buff.chs.cylinder;
					sect = buff.chs.sector;
					head = (buff.chs.driveAndHead & ATA_COMMAND_HEADMASK);
					mylba = img ? (((cyl*img->head + head)*img->sect) + sect-1) : 0;
				}

				workOffset = 0;
				workCount = buff.chs.count;

				if( verboseLevel > 0 )
				{
					const char *comStr = (workCommand & SERIAL_COMMAND_WRITE ? "Write" : "Read");

					if( workCommand == SERIAL_COMMAND_INQUIRE )
						log( 1, "Inquire %d: Client Port=0x%x, Client Baud=%s", img == image0 ? 0 : 1,
							 ((unsigned short) buff.inquire.port) << 2,
							 baudRateMatchDivisor( buff.inquire.baud )->display );
					else if( buff.chs.driveAndHead & ATA_COMMAND_LBA )
						log( 1, "%s %d: LBA=%u, Count=%u", comStr, img == image0 ? 0 : 1,
							 mylba, workCount );
					else
						log( 1, "%s %d: Cylinder=%u, Sector=%u, Head=%u, Count=%u, LBA=%u", comStr, img == image0 ? 0 : 1,
							 cyl, sect, head, workCount, mylba );
				}

				if( !img )
				{
					log( 1, "    No slave drive provided" );
					workCount = 0;
					continue;
				}

				if( (workCommand & SERIAL_COMMAND_WRITE) && img->readOnly )
				{
					log( 1, "    Write attempt to Read Only disk" );
					workCount = 0;
					continue;
				}

				if( verboseLevel > 0 && workCount > 100 )
					perfTimer = GetTime();
			}

			if( workCount && (workCommand == (SERIAL_COMMAND_WRITE | SERIAL_COMMAND_READWRITE)) )
			{
				//
				// Write command...   Setup to receive a sector
				//
				readto = 514;
			}
			else
			{
				//
				// Inquire command...
				//
				if( workCommand == SERIAL_COMMAND_INQUIRE )
				{
					unsigned char localScan;

					if( serial->speedEmulation &&
						buff.inquire.baud != serial->baudRate->divisor )
					{
						log( 1, "    Ignoring Inquire with wrong baud rate" );
						workCount = 0;
						continue;
					}

					localScan = buff.inquire.scan;         // need to do this before the call to
					                                       // img->respondInquire, as it will clear the buff
					img->respondInquire( &buff.w[0], buff.inquirePacked.PackedPortAndBaud,
										 serial->baudRate,
										 ((unsigned short) buff.inquire.port) << 2,
										 (img == image1 && lastScan) || buff.inquire.scan );
					lastScan = localScan;
				}
				//
				// Read command...
				//
				else
				{
					img->seekSector( mylba + workOffset );
					img->readSector( &buff.w[0] );
					lastScan = 0;
				}

				buff.w[256] = checksum( &buff.w[0], 256 );

				if( !serial->writeCharacters( &buff.w[0], 514 ) )
					break;

				if( verboseLevel >= 3 )
					logBuff( "    Sending: ", 514, 514, verboseLevel );

				workCount--;
				workOffset++;

				if( workCount )
					readto = 1;           // looking for continuation ACK
			}
		}

		if( workCount == 0 && workOffset > 100 )
			log( 1, "    Performance: %.2lf bytes per second", (512.0 * workOffset) / (GetTime() - perfTimer) * 1000.0 );
	}
}


