//======================================================================
//
// Project:     XTIDE Universal BIOS, Serial Port Server
//
// File:        Serial.cpp - Generic functions for dealing with serial communications
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
#include <stdlib.h>
#include <string.h>

struct baudRate supportedBaudRates[] =
{
  {   2400,  0x30,    "2400",   B2400 },
  {   4800,  0x18,    "4800",   B4800 },
  {   9600,   0xc,    "9600",   B9600 },
  {  19200,  0xff,   "19.2K",  B19200 },
//{  28800,   0x4,   "28.8K",  B28800 },
  {  38400,  0xff,   "38.4K",  B38400 },
  {  57600,   0x2,   "57.6K",  B57600 },
//{  76800,  0xff,   "76.8K",  B76800 },
  { 115200,   0x1,  "115.2K", B115200 },
//{ 153600,  0xff,  "153.6K", B153600 },
  { 230400,  0xff,  "230.4K", B230400 },
  { 460800,  0xff,  "460.8K", B460800 },
  { 921600,  0xff,  "921.6K", B921600 },
  {      0,     0, "Unknown", 0 },
};

struct baudRate *baudRateMatchString( const char *str )
{
	struct baudRate *b = NULL;

	unsigned long a = atol( str );
	if( a )
	{
		for( b = supportedBaudRates; b->rate; b++ )
			if( b->rate == a || (b->rate / 1000) == a || ((b->rate + 500) / 1000) == a )
				return( b );
	}

	return( b );
}

struct baudRate *baudRateMatchDivisor( unsigned char divisor )
{
	struct baudRate *b;

	for( b = supportedBaudRates; b->rate && b->divisor != divisor; b++ )
		;

	return( b );
}


