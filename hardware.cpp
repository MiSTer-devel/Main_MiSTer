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

#include <ctype.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include "hardware.h"
#include "user_io.h"

uint8_t rstval = 0;

void hexdump(void *data, uint16_t size, uint16_t offset)
{
	uint8_t i, b2c;
	uint16_t n = 0;
	char *ptr = (char*)data;

	if (!size) return;

	while (size>0) {
		printf("%04x: ", n + offset);

		b2c = (size>16) ? 16 : size;
		for (i = 0; i<b2c; i++) printf("%02x ", 0xff & ptr[i]);
		printf("  ");
		for (i = 0; i<(16 - b2c); i++) printf("   ");
		for (i = 0; i<b2c; i++) printf("%c", isprint(ptr[i]) ? ptr[i] : '.');
		printf("\n");
		ptr += b2c;
		size -= b2c;
		n += b2c;
	}
}

unsigned long GetTimer(unsigned long offset)
{
	struct timespec tp;

  	clock_gettime(CLOCK_BOOTTIME, &tp);

	uint64_t res;

	res = tp.tv_sec;
	res *= 1000;
	res += (tp.tv_nsec / 1000000);

	return (unsigned long)(res + offset);
}

unsigned long CheckTimer(unsigned long time)
{
	return (!time) || (GetTimer(0) >= time);
}

void WaitTimer(unsigned long time)
{
	time = GetTimer(time);
	while (!CheckTimer(time));
}
