//======================================================================
//
// Project:     XTIDE Universal BIOS, Serial Port Server
//
// File:        library.h - Include file for users of the library
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

#ifndef LIBRARY_H_INCLUDED
#define LIBRARY_H_INCLUDED

#define SERIAL_SERVER_MAJORVERSION 1
#define SERIAL_SERVER_MINORVERSION 0

#include <termios.h>

void log( int level, const char *message, ... );

unsigned long GetTime(void);
unsigned long GetTime_Timeout(void);

unsigned short checksum( unsigned short *wbuff, int wlen );

struct floppyInfo {
	unsigned char real;
	unsigned long size;
	unsigned char type;
	unsigned char cylinders;
	unsigned char heads;
	unsigned char sectors;
};

struct hddInfo {
	unsigned long size;
	unsigned long cylinders;
	unsigned long heads;
	unsigned long sectors;
};

struct floppyInfo *FindFloppyInfoBySize( double size );
struct hddInfo* FindHDDInfoBySize(unsigned long size);

class Image
{
public:
	virtual void seekSector( unsigned long lba ) = 0;

	virtual void writeSector( void *buff ) = 0;

	virtual void readSector( void *buff ) = 0;

	Image( const char *name, int p_readOnly, int p_drive );
	Image( const char *name, int p_readOnly, int p_drive, int p_create, unsigned long p_lba );
	Image( const char *name, int p_readOnly, int p_drive, int p_create, unsigned long p_cyl, unsigned long p_head, unsigned long p_sect, int p_useCHS );

	virtual ~Image() {};

	unsigned long cyl, sect, head;
	unsigned char floppy, floppyType;
	int useCHS;

	unsigned long totallba;

	const char *shortFileName;
	int readOnly;
	int drive;

	static int parseGeometry( char *str, unsigned long *p_cyl, unsigned long *p_head, unsigned long *p_sect );

	void respondInquire( unsigned short *buff, unsigned short originalPortAndBaud, struct baudRate *baudRate, unsigned short port, unsigned char scan );

	void init( const char *name, int p_readOnly, int p_drive, unsigned long p_cyl, unsigned long p_head, unsigned long p_sect, int p_useCHS );
};

struct baudRate {
	unsigned long rate;
	unsigned char divisor;
	const char *display;
	speed_t speed;
};
struct baudRate *baudRateMatchString( const char *str );
struct baudRate *baudRateMatchDivisor( unsigned char divisor );

#include <LinuxSerial.h>
#include <LinuxFile.h>

void processRequests( SerialAccess *serial, Image *image0, Image *image1, int timeoutEnabled, int verboseLevel );

#endif
