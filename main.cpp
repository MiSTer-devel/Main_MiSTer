/*
Copyright 2005, 2006, 2007 Dennis van Weeren
Copyright 2008, 2009 Jakub Bednarski
Copyright 2012 Till Harbaum

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

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sched.h>
#include <inttypes.h>
#include <ctype.h>
#include <string.h>
#include "menu.h"
#include "user_io.h"
#include "input.h"
#include "fpga_io.h"

const char *version = "$VER:HPS" VDATE;

int main(int argc, char *argv[])
{
	/*
	//placeholder for CPU1 dedicated process
	if (!fork())
	{
		cpu_set_t set;
		CPU_ZERO(&set);
		CPU_SET(1, &set);
		sched_setaffinity(0, sizeof(set), &set);

		while (1)
		{
			sleep(2);
			printf("Tick\n");
		}
	}
	*/

	fpga_io_init();
	fpga_gpo_write(0);

	DISKLED_OFF;

	printf("\nMinimig by Dennis van Weeren");
	printf("\nARM Controller by Jakub Bednarski");
	printf("\nMiSTer code by Sorgelig\n\n");

	printf("Version %s\n\n", version + 5);

	if (argc > 1) printf("Core path: %s\n", argv[1]);

	if (!is_fpga_ready(1))
	{
		printf("\nGPI[31]==1. FPGA is uninitialized or incompatible core loaded.\n");
		printf("Quitting. Bye bye...\n");
		exit(0);
	}

	FindStorage();
	user_io_init((argc > 1) ? argv[1] : "");

	while(1)
	{
		if(!is_fpga_ready(1))
		{
			printf("FPGA is not ready. JTAG uploading?\n");
			printf("Waiting for FPGA to be ready...\n");

			//enable reset in advance
			fpga_core_reset(1);

			while (!is_fpga_ready(0))
			{
				sleep(1);
			}
			reboot(0);
		}

		user_io_poll();
		input_poll(0);
		HandleUI();
		// Sleep for low CPU usage
		usleep(500);
	}
	return 0;
}
