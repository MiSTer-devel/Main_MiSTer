
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "../../file_io.h"
#include "../../user_io.h"
#include "../../spi.h"
#include "../../hardware.h"
#include "../../menu.h"
#include "psx.h"

static char buf[1024];

void psx_mount_cd(int f_index, int s_index, const char *filename)
{
	static char last_dir[1024] = {};

	int same_game = *filename && *last_dir && !strncmp(last_dir, filename, strlen(last_dir));
	int loaded = 1;

	if (!same_game)
	{
		loaded = 0;

		strcpy(last_dir, filename);
		char *p = strrchr(last_dir, '/');
		if (p) *p = 0;

		strcpy(buf, last_dir);
		p = strrchr(buf, '/');
		if (p)
		{
			strcpy(p + 1, "cd_bios.rom");
			loaded = user_io_file_tx(buf);
		}

		if (!loaded)
		{
			sprintf(buf, "%s/boot.rom", HomeDir());
			loaded = user_io_file_tx(buf);
		}

		if (!loaded) Info("CD BIOS not found!", 4000);
	}

	if (loaded && *filename)
	{
		user_io_set_index(f_index);
		user_io_file_mount(filename, s_index);
	}
}
