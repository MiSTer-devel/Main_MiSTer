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
#include "string.h"
#include "hardware.h"
#include "file_io.h"
#include "osd.h"
#include "fdd.h"
#include "hdd.h"
#include "menu.h"
#include "user_io.h"
#include "tos.h"
#include "debug.h"
#include "mist_cfg.h"
#include "input.h"
#include "fpga_io.h"
#include "boot.h"

const char version[] = { "$VER:HPS" VDATE };

void HandleDisk(void)
{
	unsigned char  c1, c2;

	EnableFpga();
	uint16_t tmp = spi_w(0);
	c1 = (uint8_t)(tmp>>8); // cmd request and drive number
	c2 = (uint8_t)tmp;      // track number
	spi_w(0);
	spi_w(0);
	DisableFpga();

	HandleFDD(c1, c2);
	HandleHDD(c1, c2);

	UpdateDriveStatus();
}

void core_init()
{
	user_io_detect_core_type();

	if (user_io_core_type() == CORE_TYPE_MINIMIG2)
	{
		BootInit();

	} // end of minimig setup

	if (user_io_core_type() == CORE_TYPE_MIST)
	{
		puts("Running mist setup");
		tos_upload(NULL);

		// end of mist setup
	}

	if (user_io_core_type() == CORE_TYPE_ARCHIE)
	{
		puts("Running archimedes setup");
	} // end of archimedes setup
}

int main(int argc, char *argv[])
{
	fpga_io_init();
	fpga_gpo_write(0);

	DISKLED_OFF;

	iprintf("\nMinimig by Dennis van Weeren");
	iprintf("\nARM Controller by Jakub Bednarski\n\n");
	iprintf("Version %s\n\n", version + 5);

	if (!is_fpga_ready(1))
	{
		printf("\nGPI[31]==1. FPGA is uninitialized or incompatible core loaded.\n");
		printf("Quitting. Bye bye...\n");
		exit(0);
	}

	FindStorage();

	user_io_init();
	tos_config_init();
	core_init();

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

		switch (user_io_core_type())
		{
		// MIST (atari) core supports the same UI as Minimig
		case CORE_TYPE_MIST:
			HandleUI();
			break;

		// call original minimig handlers if minimig core is found
		case CORE_TYPE_MINIMIG2:
			HandleDisk();
			HandleUI();
			break;

		// 8 bit cores can also have a ui if a valid config string can be read from it
		case CORE_TYPE_8BIT:
			HandleUI();
			break;

		// Archie core will get its own treatment one day ...
		case CORE_TYPE_ARCHIE:
			HandleUI();
			break;
		}
	}
	return 0;
}
