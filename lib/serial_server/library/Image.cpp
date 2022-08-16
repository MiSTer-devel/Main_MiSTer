//======================================================================
//
// Project:     XTIDE Universal BIOS, Serial Port Server
//
// File:        image.cpp - Abstract base class for disk image support
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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

unsigned int hdd_table[128][3] = {
	{  306,  4, 17 },		/* 0 - 7 */
	{  615,  2, 17 },
	{  306,  4, 26 },
	{ 1024,  2, 17 },
	{  697,  3, 17 },
	{  306,  8, 17 },
	{  614,  4, 17 },
	{  615,  4, 17 },

	{  670,  4, 17 },		/* 8 - 15 */
	{  697,  4, 17 },
	{  987,  3, 17 },
	{  820,  4, 17 },
	{  670,  5, 17 },
	{  697,  5, 17 },
	{  733,  5, 17 },
	{  615,  6, 17 },

	{  462,  8, 17 },		/* 016-023 */
	{  306,  8, 26 },
	{  615,  4, 26 },
	{ 1024,  4, 17 },
	{  855,  5, 17 },
	{  925,  5, 17 },
	{  932,  5, 17 },
	{ 1024,  2, 40 },

	{  809,  6, 17 },		/* 024-031 */
	{  976,  5, 17 },
	{  977,  5, 17 },
	{  698,  7, 17 },
	{  699,  7, 17 },
	{  981,  5, 17 },
	{  615,  8, 17 },
	{  989,  5, 17 },

	{  820,  4, 26 },		/* 032-039 */
	{ 1024,  5, 17 },
	{  733,  7, 17 },
	{  754,  7, 17 },
	{  733,  5, 26 },
	{  940,  6, 17 },
	{  615,  6, 26 },
	{  462,  8, 26 },

	{  830,  7, 17 },		/* 040-047 */
	{  855,  7, 17 },
	{  751,  8, 17 },
	{ 1024,  4, 26 },
	{  918,  7, 17 },
	{  925,  7, 17 },
	{  855,  5, 26 },
	{  977,  7, 17 },

	{  987,  7, 17 },		/* 048-055 */
	{ 1024,  7, 17 },
	{  823,  4, 38 },
	{  925,  8, 17 },
	{  809,  6, 26 },
	{  976,  5, 26 },
	{  977,  5, 26 },
	{  698,  7, 26 },

	{  699,  7, 26 },		/* 056-063 */
	{  940,  8, 17 },
	{  615,  8, 26 },
	{ 1024,  5, 26 },
	{  733,  7, 26 },
	{ 1024,  8, 17 },
	{  823, 10, 17 },
	{  754, 11, 17 },

	{  830, 10, 17 },		/* 064-071 */
	{  925,  9, 17 },
	{ 1224,  7, 17 },
	{  940,  6, 26 },
	{  855,  7, 26 },
	{  751,  8, 26 },
	{ 1024,  9, 17 },
	{  965, 10, 17 },

	{  969,  5, 34 },		/* 072-079 */
	{  980, 10, 17 },
	{  960,  5, 35 },
	{  918, 11, 17 },
	{ 1024, 10, 17 },
	{  977,  7, 26 },
	{ 1024,  7, 26 },
	{ 1024, 11, 17 },

	{  940,  8, 26 },		/* 080-087 */
	{  776,  8, 33 },
	{  755, 16, 17 },
	{ 1024, 12, 17 },
	{ 1024,  8, 26 },
	{  823, 10, 26 },
	{  830, 10, 26 },
	{  925,  9, 26 },

	{  960,  9, 26 },		/* 088-095 */
	{ 1024, 13, 17 },
	{ 1224, 11, 17 },
	{  900, 15, 17 },
	{  969,  7, 34 },
	{  917, 15, 17 },
	{  918, 15, 17 },
	{ 1524,  4, 39 },

	{ 1024,  9, 26 },		/* 096-103 */
	{ 1024, 14, 17 },
	{  965, 10, 26 },
	{  980, 10, 26 },
	{ 1020, 15, 17 },
	{ 1023, 15, 17 },
	{ 1024, 15, 17 },
	{ 1024, 16, 17 },

	{ 1224, 15, 17 },		/* 104-111 */
	{  755, 16, 26 },
	{  903,  8, 46 },
	{  984, 10, 34 },
	{  900, 15, 26 },
	{  917, 15, 26 },
	{ 1023, 15, 26 },
	{  684, 16, 38 },

	{ 1930,  4, 62 },		/* 112-119 */
	{  967, 16, 31 },
	{ 1013, 10, 63 },
	{ 1218, 15, 36 },
	{  654, 16, 63 },
	{  659, 16, 63 },
	{  702, 16, 63 },
	{ 1002, 13, 63 },

	{  854, 16, 63 },		/* 119-127 */
	{  987, 16, 63 },
	{  995, 16, 63 },
	{ 1024, 16, 63 },
	{ 1036, 16, 63 },
	{ 1120, 16, 59 },
	{ 1054, 16, 63 },
	{    0,  0,  0 }
};

struct floppyInfo floppyInfos[] =
{
	{ 1, 2949120 / 512, 6, 80, 2, 36 },   		// 2.88MB 3.5"
	{ 0, 2867200 / 512, 6, 80, 2, 36 },   		// 2.88MB 3.5" (alternate spelling with 2.8)
	{ 0, 2969600 / 512, 6, 80, 2, 36 },   		// 2.88MB 3.5" (alternate spelling with 2.9)
	{ 1, 1474560 / 512, 4, 80, 2, 18 },         // 1.44MB 3.5"
	{ 0, 1433600 / 512, 4, 80, 2, 18 },         // 1.44MB 3.5" (alternate spelling with 1.4)
	{ 1, 1228800 / 512, 2, 80, 2, 15 },     	// 1.2MB 5.25"
	{ 1, 737280 / 512, 3, 80, 1, 18 },    		// 720KB 3.5"
	{ 1, 368640 / 512, 1, 40, 2, 9 }, 			// 360KB 5.25"
	{ 1, 327680 / 512, 0, 40, 2, 8 }, 			// 320KB 5.25"
	{ 1, 184320 / 512, 0, 40, 1, 9 }, 			// 180KB 5.25" single sided
	{ 1, 163840 / 512, 0, 40, 1, 8 }, 			// 160KB 5.25" single sided
	{ 0, 0, 0, 0, 0, 0 }
};

struct hddInfo hddInfos[] = { { 0, 0, 0, 0 } };

struct floppyInfo *FindFloppyInfoBySize( double size )
{
	struct floppyInfo *fi;

	for( fi = floppyInfos; fi->size != 0 && !(size+5 > fi->size && size-5 < fi->size); fi++ ) ;

	if( fi->size == 0 )
		fi = NULL;

	return( fi );
}

struct hddInfo* FindHDDInfoBySize(unsigned long size)
{
	struct hddInfo* fi;
	unsigned long size_chs;
	bool is_chs = false;
	

	for (int i = 0; i < 127; i++) {
		size_chs = ((unsigned long)hdd_table[i][0]) * (unsigned long)hdd_table[i][1] * (unsigned long)hdd_table[i][2] * 512;		
		if (size == size_chs)
		{
			fi = hddInfos;
			fi->size = size;
			fi->cylinders = hdd_table[i][0];			
			fi->heads = hdd_table[i][1];
			fi->sectors = hdd_table[i][2];			
			is_chs = true;
			break;
		}
	}

	if (!is_chs)
		fi = NULL;
		
	return(fi);
}

void flipEndian( unsigned short *buff, unsigned int len )
{
	for( unsigned int t = 0; t < len/2; t++ )
		buff[t] = (buff[t] & 0xff) << 8 | (buff[t] & 0xff00) >> 8;
}

Image::Image( const char *, int, int, int, unsigned long, unsigned long, unsigned long, int)
{
}


void Image::init( const char *name, int p_readOnly, int p_drive, unsigned long p_cyl, unsigned long p_head, unsigned long p_sect, int p_useCHS )
{
	double sizef;
	char sizeChar;
	struct floppyInfo *f;

	for( const char *c = shortFileName = name; *c; c++ )
		if( *c == '\\' || *c == '/' || *c == ':' )
			shortFileName = c+1;

	if( *(shortFileName) == 0 )
	{
		log( 1, "Can't parse '%s' for short file name\n\n", name );
		shortFileName = "SerDrive";
	}

	readOnly = p_readOnly;
	drive = p_drive;

	if( totallba > 0xfffffff )     // lba28 limit - 28 bits
		log( -1, "'%s', Image size larger than LBA28 maximum of 137,438,952,960 bytes, %lu", name, totallba );

	if( totallba == 0 )
		log( -1, "'%s', Image size zero?" );

	floppy = 0;
	for( f = floppyInfos; f->size && !(f->size == totallba && f->real); f++ ) ;
	if( f->size )
	{
		floppy = 1;
		floppyType = f->type;
		p_useCHS = 1;
		p_cyl = f->cylinders;
		p_head = f->heads;
		p_sect = f->sectors;
		totallba = p_cyl * p_head * p_sect;
	}

	if( p_cyl )
	{
		if( (p_sect > 255 || p_sect < 1) || (p_head > 16 || p_head < 1) || (p_cyl > 65536 || p_cyl < 1) )
			log( -1, "'%s', parts of the CHS geometry (%lu:%lu:%lu) are out of the range (1-65536:1-16:1-255)", name, p_cyl, p_head, p_sect );
		else if( totallba != (p_sect * p_head * p_cyl) )
			log( -1, "'%s', file size does not match geometry", name );
		sect = p_sect;
		head = p_head;
		cyl = p_cyl;
	}
	else
	{
		if( totallba > 65536*16*63 )
		{
			log( 0, "'%s': Warning: Image size is greater than derived standard CHS maximum, limiting CHS to 65535:16:63, consider using -g to specify geometry", name );
			cyl = 65536;
			head = 16;
			sect = 63;
		}
		else if( (totallba % 16) != 0 || ((totallba/16) % 63) != 0 )
		{
			log( -1, "'%s', file size does not match standard CHS geometry (x:16:63), please specify geometry explicitly with -g", name );
		}
		else
		{
			sect = 63;
			head = 16;
			cyl = (totallba / sect / head);
			if( cyl > 65536 )
			{
				log( -1, "'%s', derived standard CHS geometry of %lu:16:63 is has more cylinders than 65536, please specify geometry explicitly with -g", name, cyl, head, sect );
			}
		}
	}

	useCHS = p_useCHS;

	sizef = totallba/2048.0;
	sizeChar = 'M';
	if( sizef < 1 )
	{
		sizef *= 1024;
		sizeChar = 'K';
	}
	if( useCHS )
		log( 0, "%s: %s with CHS geometry %u:%u:%u, size %.2lf %cB",
			 name, (floppy ? "Floppy Disk" : "Hard Disk"), cyl, head, sect, sizef, sizeChar );
	else
		log( 0, "%s: %s with %lu LBA sectors, size %.2lf %cB (CHS geometry %u:%u:%u)",
			 name, (floppy ? "Floppy Disk" : "Hard Disk"), totallba, sizef, sizeChar, cyl, head, sect );
}

int Image::parseGeometry( char *str, unsigned long *p_cyl, unsigned long *p_head, unsigned long *p_sect )
{
	char *c, *s, *h;
	unsigned long cyl, sect, head;

	c = str;
	for( h = c; *h && *h != ':' && *h != 'x' && *h != 'X'; h++ ) ;
	if( !*h )
		return( 0 );

	*h = '\0';
	h++;
	for( s = h+1; *s && *s != ':' && *s != 'x' && *s != 'X'; s++ ) ;
	if( !*s )
		return( 0 );

	*s = '\0';
	s++;

	cyl = atol(c);
	head = atol(h);
	sect = atol(s);

	if( cyl == 0 || sect == 0 || head == 0 )
		return( 0 );

	*p_cyl = cyl;
	*p_head = head;
	*p_sect = sect;

	return( 1 );
}

#define ATA_wGenCfg 0
#define ATA_wCylCnt 1
#define ATA_wHeadCnt 3
#define ATA_wBpTrck 4
#define ATA_wBpSect 5
#define ATA_wSPT 6

#define ATA_strSerial 10
#define ATA_strSerial_Length 20

#define ATA_strFirmware 23
#define ATA_strFirmware_Length 8

#define ATA_strModel 27
#define ATA_strModel_Length 40                 // Maximum allowable length of the string according to the ATA spec
#define XTIDEBIOS_strModel_Length 30           // Maximum length copied out of the ATA information by the BIOS

#define ATA_wCaps 49
#define ATA_wCurCyls 54
#define ATA_wCurHeads 55
#define ATA_wCurSPT 56
#define ATA_dwCurSCnt 57
#define ATA_dwLBACnt 60

// Words carved out of the vendor specific area for our use
//
#define ATA_wSerialServerVersion 157
#define ATA_wSerialDriveFlags 158
#define ATA_wSerialPortAndBaud 159

// Defines used in the words above
//
#define ATA_wCaps_LBA 0x200

#define ATA_wGenCfg_FIXED 0x40

// These are all shifted by 1 bit to the right, so that SerialDPT_Finalize can shift them into proper position
// and shift the high order bit into the carry flag to indicate a floppy drive is present.
//
#define ATA_wSerialDriveFlags_Floppy    0x88
#define ATA_wSerialDriveFlags_Present   0x02
#define ATA_wSerialDriveFlags_FloppyType_FieldPosition   4

struct comPorts {
	unsigned long port;
	unsigned char com;
};
struct comPorts supportedComPorts[] =
{
  { 0x3f8, '1' },
  { 0x2f8, '2' },
  { 0x3e8, '3' },
  { 0x2e8, '4' },
  { 0x2f0, '5' },
  { 0x3e0, '6' },
  { 0x2e0, '7' },
  { 0x260, '8' },
  { 0x368, '9' },
  { 0x268, 'A' },
  { 0x360, 'B' },
  { 0x270, 'C' },
  { 0, 0 }
};

void Image::respondInquire( unsigned short *buff, unsigned short originalPortAndBaud, struct baudRate *baudRate, unsigned short port, unsigned char scan )
{
	char formatBuff[ 128 ];
	char speedBuff[XTIDEBIOS_strModel_Length];

	memset( &buff[0], 0, 514 );

	if( scan )
	{
		unsigned short comPort = 0;
		struct comPorts *cp;

		if( port )
		{
			for( cp = supportedComPorts; cp->port && cp->port != port; cp++ ) ;
			if( cp->port )
				comPort = cp->com;
		}

		if( comPort )
			sprintf( speedBuff, " (COM%c/%s)", comPort, baudRate->display );
		else
			sprintf( speedBuff, " %s (%s baud)", shortFileName, baudRate->display );

		sprintf( formatBuff, "%.*s%s ", XTIDEBIOS_strModel_Length - strlen(speedBuff), shortFileName, speedBuff );
	}
	else
		sprintf( formatBuff, "%.*s ", XTIDEBIOS_strModel_Length, shortFileName );
	strncpy( (char *) &buff[ATA_strModel], formatBuff, ATA_strModel_Length );
	flipEndian( &buff[ATA_strModel], ATA_strModel_Length );

	strncpy( (char *) &buff[ATA_strSerial], "SerialDrive ", ATA_strSerial_Length );
	flipEndian( &buff[ATA_strSerial], ATA_strSerial_Length );

	sprintf( formatBuff, "%d.%d ", SERIAL_SERVER_MAJORVERSION, SERIAL_SERVER_MINORVERSION );
	strncpy( (char *) &buff[ATA_strFirmware], formatBuff, ATA_strFirmware_Length );
	flipEndian( &buff[ATA_strFirmware], ATA_strFirmware_Length );

	buff[ ATA_wCylCnt ] = cyl;
	buff[ ATA_wHeadCnt ] = head;
	buff[ ATA_wSPT ] = sect;

	if( !useCHS )
	{
		buff[ ATA_wCaps ] = ATA_wCaps_LBA;
		buff[ ATA_dwLBACnt ] = (unsigned short) (totallba & 0xffff);
		buff[ ATA_dwLBACnt+1 ] = (unsigned short) (totallba >> 16);
	}

	// We echo back the port and baud that we were called on from the client,
	// the client then uses this value to finalize the DPT.
	//
	buff[ ATA_wSerialPortAndBaud ] = originalPortAndBaud;

	// In case the client requires a specific server version...
	//
	buff[ ATA_wSerialServerVersion ] = (SERIAL_SERVER_MAJORVERSION << 8) | SERIAL_SERVER_MINORVERSION;

	buff[ ATA_wSerialDriveFlags ] = ATA_wSerialDriveFlags_Present;
	if( floppy )
		buff[ ATA_wSerialDriveFlags ] |=
			ATA_wSerialDriveFlags_Floppy | (floppyType << ATA_wSerialDriveFlags_FloppyType_FieldPosition);

	// we always set this, so that the bulk of the BIOS will consider this disk as a hard disk
	//
	buff[ ATA_wGenCfg ] = ATA_wGenCfg_FIXED;
}

