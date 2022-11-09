//======================================================================
//
// Project:     XTIDE Universal BIOS, Serial Port Server
//
// File:        checksum.cpp - Checksum function and test routines

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

//
// This file implements Fletcher's Checksum.  The serial code uses this checksum, as it is very quick
// to calculate in assembly and offers reasonable error detection.
// For more information, see http://en.wikipedia.org/wiki/Fletcher%27s_checksum.
//
// Since it is faster in 8088 assembly code to deal with 16-bit quantities than 8-bit quantities,
// Fletcher's Checksum has been modified to calculate the 32-bit checksum, and then "fold" the result into a
// 16-bit quantity.  Fletcher's 32-bit Checksum consists of two parts: concatenated 16-bit accumulators.
// To "fold" to 16-bits, The upper and lower 8-bits of each of these accumulators is XOR'd independently, and then
// the two results concatenated together, resulting in 16-bits.  Although simpler, an early attempt to XOR the
// 16-bit accumulators results in poorer error detection behavior.  Folding as described here results in error
// detection on par with Fletcher's 16-bit Checksum.
//
// With #define CHECKSUM_TEST, this file becomes a self-contained command line program that runs
// some statistical tests comparing various checksum algorithms with random 512-byte sectors and various
// levels of errors introduced.
//

#include "Library.h"

unsigned short checksum( unsigned short *wbuff, int wlen )
{
	unsigned long a = 0xffff;
	unsigned long b = 0xffff;
	int t;

	for( t = 0; t < wlen; t++ )
	{
		a += wbuff[t];
		b += a;
	}

	a = (a & 0xffff) + (a >> 16);
	b = (b & 0xffff) + (b >> 16);
	a = (a & 0xffff) + (a >> 16);
	b = (b & 0xffff) + (b >> 16);

// Although tempting to use, for its simplicity and size/speed in assembly, the following folding
// algorithm results in many undetected single bit errors and therefore should not be used.
//	return( (unsigned short) (a ^ b) );

	return( (unsigned short) (((a & 0xff) << 8) ^ (a & 0xff00)) + (((b & 0xff00) >> 8) ^ (b & 0xff)) );
}

#ifdef CHECKSUM_TEST

//====================================================================================================
//
// Test Code
//

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

#define BUCKETS 65536
#define BITTEST 16

unsigned char bit[] = { 1, 2, 4, 8, 16, 32, 64, 128 };

class algorithm
{
public:
	virtual unsigned short checksum( unsigned char *data, int len ) = 0;
	char *title;
	unsigned long *found;
	unsigned long zero;
	unsigned long total;
	unsigned long empty;
	unsigned long min;
	unsigned long max;
	double stdev;
	unsigned long bittest[ BITTEST ];
	unsigned long missed[ BITTEST ];
	algorithm *next;
	algorithm( algorithm *last, char *new_title );
};

algorithm::algorithm( algorithm *last, char *new_title )
{
	zero = total = empty = min = max = 0;
	stdev = 0.0;
	for( int t = 0; t < BITTEST; t++ )
	{
		bittest[t] = missed[t] = 0;
	}
	title = new_title;
	next = last;
}

//----------------------------------------------------------------------------------------------------
//
// Standard CRC-16
//
// http://sanity-free.org/134/standard_crc_16_in_csharp.html
//

static unsigned short crc16_table[256];

class crc16_algorithm : public algorithm
{
public:
	crc16_algorithm( algorithm *last ) : algorithm( last, (char *) "crc-16" )
	{
		unsigned short value;
		unsigned short temp;
		unsigned short i;
		unsigned short j;

		for(i = 0; i < 256; ++i)
		{
			value = 0;
			temp = i;
			for(j = 0; j < 8; ++j) {
				if(((value ^ temp) & 0x0001) != 0) {
					value = (unsigned short)((value >> 1) ^ this->crc16_polynomial);
				}else {
					value >>= 1;
				}
				temp >>= 1;
			}
			crc16_table[i] = value;
		}
	}

	unsigned short checksum( unsigned char *data, int len );

private:
	static const unsigned short crc16_polynomial = 0xA001;
};

unsigned short crc16_algorithm::checksum( unsigned char *data, int len )
{
	unsigned short crc = 0;
	int i;

	for(i = 0; i < len; ++i)
	{
		unsigned char index = (unsigned char)(crc ^ data[i]);
		crc = (unsigned short)((crc >> 8) ^ crc16_table[index]);
	}

	return( crc );
}

//----------------------------------------------------------------------------------------------------
//
// Basic checksum (just add up the bytes)
//

class basic_algorithm : public algorithm
{
public:
	unsigned short checksum( unsigned char *data, int len );
	basic_algorithm( algorithm *last ) : algorithm( last, (char *) "basic" ) { };
};

unsigned short basic_algorithm::checksum( unsigned char *bbuff, int blen )
{
	unsigned short sum = 0;
	int i;
	for( i = 0; i < blen; i++ )
	{
		sum += bbuff[ i ];
	}
	return( sum );
}

class fletcher16_algorithm : public algorithm
{
public:
	unsigned short checksum( unsigned char *data, int len );
	fletcher16_algorithm( algorithm *last ) : algorithm( last, (char *) "f-16" ) { }
};

unsigned short fletcher16_algorithm::checksum( unsigned char* data, int count )
{
	unsigned short sum1 = 0;
	unsigned short sum2 = 0;
	int index;

	for( index = 0; index < count; ++index )
	{
		sum1 = (sum1 + data[index]) % 255;
		sum2 = (sum2 + sum1) % 255;
	}

	return (sum2 << 8) | sum1;
}

//----------------------------------------------------------------------------------------------------
//
// Folded Fletcher's Checksum (what we use in the serial code, from the top of this file)
//

class folded_fletcher32_algorithm : public algorithm
{
public:
	unsigned short checksum( unsigned char *data, int len );
	folded_fletcher32_algorithm( algorithm *last ) : algorithm( last, (char *) "fold-f-32" ) { }
};

unsigned short folded_fletcher32_algorithm::checksum( unsigned char* data, int count )
{
	return( ::checksum( (unsigned short *) data, count/2 ) );
}

//----------------------------------------------------------------------------------------------------
//
// Test Driver and Support routines
//

void randomize_buff( unsigned char *bbuff, int blen )
{
	int i;
	for( i = 0; i < blen; i++ )
		bbuff[i] = rand() % 255;
}

#define BBUFF_LENGTH 512

unsigned char bbuff[ BBUFF_LENGTH ];

int main( int argc, char *argv[] )
{
	algorithm *a, *algorithms;

	unsigned short c;

	double p;
	double average;

	unsigned long iterations;

	time_t now;

	algorithms = new folded_fletcher32_algorithm( NULL );
	algorithms = new fletcher16_algorithm( algorithms );
	algorithms = new crc16_algorithm( algorithms );
	algorithms = new basic_algorithm( algorithms );

	time( &now );
	srand((unsigned int)now);

	if( argc != 2 )
	{
		fprintf( stderr, "usage: checksum number_of_iterations\n" );
		exit( 1 );
	}
	else
		iterations = atol( argv[1] );

#define PRINTROW( E, F, G ) { printf( E ); for( a = algorithms; a; a = a->next ) printf( F, G ); printf( "\n" ); }

	printf( "\nnumber of iterations: %d\n\n", iterations );
	PRINTROW( "       ", "%10s  ", a->title );
	PRINTROW( "=======", "============", NULL );

	for( a = algorithms; a; a = a->next )
	{
		a->found = (unsigned long *) calloc( BUCKETS, sizeof(long) );

		a->zero = (unsigned long) a->checksum( bbuff, BBUFF_LENGTH );

		a->min = iterations+1;
	}

	printf( "\n" );
	PRINTROW( "zero   ", "%10d  ", a->zero );

	for( int t = 0; t < iterations; t++ )
	{
		randomize_buff( bbuff, BBUFF_LENGTH );

		for( a = algorithms; a; a = a->next )
			a->found[ a->checksum( bbuff, BBUFF_LENGTH ) ]++;
	}

	average = iterations / 65536.0;

	for( int t = 0; t < 65536; t++ )
	{
		for( a = algorithms; a; a = a->next )
		{
			a->total += a->found[ t ];
			if( !a->found[ t ] )
				a->empty++;
			if( a->found[ t ] > a->max )
				a->max = a->found[ t ];
			if( a->found[ t ] < a->min )
				a->min = a->found[ t ];
			p = a->found[ t ] - average;
			a->stdev += p*p;
		}
	}

	p = 1.0 / (65536.0-1.0);
	for( a = algorithms; a; a = a->next )
	{
		a->stdev = sqrt( p * a->stdev );
		if( a->total != iterations )
			fprintf( stderr, "Bad %s\n", a->title );
	}

	printf( "\nchecksum distribution test:\n" );
	PRINTROW( "empty  ", "%10d  ", a->empty );
	PRINTROW( "min    ", "%10d  ", a->min );
	PRINTROW( "max    ", "%10d  ", a->max );
	PRINTROW( "stdev  ", "%10.4lf  ", a->stdev );

	for( int t = 0; t < iterations; t++ )
	{
		randomize_buff( bbuff, BBUFF_LENGTH );

		for( int b = 0; b < BITTEST; b++ )
		{
			for( a = algorithms; a; a = a->next )
			{
			  a->bittest[ b ] = (a->checksum)( bbuff, BBUFF_LENGTH );
			}

			bbuff[ rand() % 512 ] ^= bit[ rand() % 8 ];

			if( b > 0 )
			{
				for( a = algorithms; a; a = a->next )
				{
					if( a->bittest[ 0 ] == a->bittest[ b ] )
						a->missed[ b ]++;
				}
			}
		}
	}

	printf( "\nbit change test:\n" );
	for( int t = 1; t < BITTEST; t++ )
	{
		printf( "%2d        ", t );
		for( a = algorithms; a; a = a->next )
			printf( "%7d     ", a->missed[ t ] );
		printf( "\n" );
	}
}

#endif




