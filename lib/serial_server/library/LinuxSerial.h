//======================================================================
//
// Project:     XTIDE Universal BIOS, Serial Port Server
//
// File:        Win32Serial.h - Microsoft Windows serial code
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

#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <fcntl.h>
#include "../library/Library.h"

#define PIPENAME "\\\\.\\pipe\\xtide"

class SerialAccess
{
public:
	void Connect( const char *name, struct baudRate *p_baudRate )
	{
//		char buff1[20], buff2[1024];

		baudRate = p_baudRate;

		pipe = -1;

		if( !access(name, R_OK | W_OK) )
		{
			struct termios state;
			  
			log( 0, "Opening %s (%s baud)", name, baudRate->display );

			pipe = open(name, O_RDWR);
			if( pipe < 0 )
				log( -1, "Could not Open \"%s\"", name );

			tcgetattr(pipe, &state);
			cfmakeraw(&state);
			state.c_cflag |= CRTSCTS | CLOCAL;
			state.c_lflag &= ~ECHO;
			cfsetispeed(&state, baudRate->speed);
			cfsetospeed(&state, baudRate->speed);
			tcsetattr(pipe, TCSAFLUSH, &state);
		}
		else
			log( -1, "Serial port '%s' not found", name );
	}

	void Disconnect()
	{
		if( pipe )
		{
			close( pipe );
			pipe = -1;
		}
	}

	unsigned long readCharacters( void *buff, unsigned long len )
	{
		long readLen;
//		int ret;

		readLen = read(pipe, buff, len);

		if( readLen < 0 )
			log( -1, "read serial failed (error code %i)", errno );

		return( readLen );
	}

	int writeCharacters( void *buff, unsigned long len )
	{
		long writeLen;
//		int ret;

		writeLen = write(pipe, buff, len);

		if( writeLen < 0 )
			log( -1, "write serial failed (error code %i)", errno );

		return( 1 );
	}

	SerialAccess()
	{
		pipe = 0;
		speedEmulation = 0;
		resetConnection = 0;
		baudRate = NULL;
	}

	~SerialAccess()
	{
		Disconnect();
	}

	int speedEmulation;
	int resetConnection;

	struct baudRate *baudRate;

private:
	int pipe;
};

