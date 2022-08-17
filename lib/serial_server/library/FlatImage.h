//======================================================================
//
// Project:     XTIDE Universal BIOS, Serial Port Server
//
// File:        FlatImage.h - Header file for basic flat disk image support
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
#include <string.h>

class FlatImage : public Image
{
private:
	class FileAccess fp;

public:
	FlatImage( char *name, int p_readOnly, int p_drive, int p_create, unsigned long p_cyl, unsigned long p_head, unsigned long p_sect, int p_useCHS )   :   Image( name, p_readOnly, p_drive, p_create, p_cyl, p_head, p_sect, p_useCHS )
	{
		//long filesize;

		if( p_create )
		{
			char buff[512];
			unsigned long size;
			double sizef;
			FileAccess cf;
			char sizeChar;

			size = (unsigned long) p_cyl * (unsigned long) p_sect * (unsigned long) p_head;
			if( size > cf.MaxSectors )
				log( -1, "'%s', can't create flat file with size greater than %lu 512-byte sectors", name, cf.MaxSectors );
			sizef = size / 2048.0;   // 512 byte sectors -> MB
			sizeChar = 'M';
			if( sizef < 1 )
			{
				sizef *= 1024;
				sizeChar = 'K';
			}

			if( cf.Create( name ) )
			{
				memset( &buff[0], 0, 512 );
				while( size-- )
					cf.Write( &buff[0], 512 );

				if( p_cyl > 1024 )
					log( 0, "Created file '%s', size %.2lf %cB", name, sizef, sizeChar );
				else
					log( 0, "Created file '%s', geometry %u:%u:%u, size %.2lf %cB", name, p_cyl, p_head, p_sect, sizef, sizeChar );
				cf.Close();
			}
		}

		fp.Open( name );

		totallba = fp.SizeSectors();

		init( name, p_readOnly, p_drive, p_cyl, p_head, p_sect, p_useCHS );
	}

	~FlatImage()
	{
		fp.Close();
	}

	void seekSector( unsigned long lba )
	{
		fp.SeekSectors( lba );
	}

	void writeSector( void *buff )
	{
		fp.Write( buff, 512 );
	}

	void readSector( void *buff )
	{
		fp.Read( buff, 512 );
	}
};

