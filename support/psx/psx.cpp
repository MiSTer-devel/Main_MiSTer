
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

static int sgets(char *out, int sz, char **in)
{
	*out = 0;
	do
	{
		char *instr = *in;
		int cnt = 0;

		while (*instr && *instr != 10)
		{
			if (*instr == 13)
			{
				instr++;
				continue;
			}

			if (cnt < sz - 1)
			{
				out[cnt++] = *instr;
				out[cnt] = 0;
			}

			instr++;
		}

		if (*instr == 10) instr++;
		*in = instr;
	} while (!*out && **in);

	return *out;
}

static int get_bin(const char *cue)
{
	static char line[128];
	char *ptr, *lptr;
	int bb;
	int res = 0;
	buf[0] = 0;

	int sz = FileLoad(cue, 0, 0);
	if (sz)
	{
		char *toc = new char[sz + 1];
		if (toc)
		{
			if (FileLoad(cue, toc, sz))
			{
				toc[sz] = 0;

				char *tbuf = toc;
				while (sgets(line, sizeof(line), &tbuf))
				{
					lptr = line;
					while (*lptr == 0x20) lptr++;

					/* decode FILE commands */
					if (!(memcmp(lptr, "FILE", 4)))
					{
						strcpy(buf, cue);
						ptr = strrchr(buf, '/');
						if (!ptr) ptr = buf;
						else ptr++;

						lptr += 4;
						while (*lptr == 0x20) lptr++;
						char stp = 0x20;

						if (*lptr == '\"')
						{
							lptr++;
							stp = '\"';
						}

						while ((*lptr != stp) && (lptr <= (line + 128)) && (ptr < (buf + 1023))) *ptr++ = *lptr++;
						*ptr = 0;
					}

					/* decode TRACK commands */
					else if ((sscanf(lptr, "TRACK %02d %*s", &bb)) || (sscanf(lptr, "TRACK %d %*s", &bb)))
					{
						if (buf[0] && (strstr(lptr, "MODE1") || strstr(lptr, "MODE2")))
						{
							res = 1;
							break;
						}
					}
				}
			}

			delete(toc);
		}
	}

	return res;
}

void psx_mount_cd(int f_index, int s_index, const char *filename)
{
	static char last_dir[1024] = {};

	const char *p = strrchr(filename, '/');
	int cur_len = p ? p - filename : 0;
	int old_len = strlen(last_dir);

	int name_len = strlen(filename);
	int is_cue = (name_len > 4) && !strcasecmp(filename + name_len - 4, ".cue");

	int same_game = old_len && (cur_len == old_len) && !strncmp(last_dir, filename, old_len);
	int loaded = 1;

	if (!same_game)
	{
		loaded = 0;

		strcpy(last_dir, filename);
		char *p = strrchr(last_dir, '/');
		if (p) *p = 0;
		else *last_dir = 0;

		strcpy(buf, last_dir);
		if (!is_cue && buf[0]) strcat(buf, "/");

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

	if (loaded)
	{
		user_io_set_index(f_index);
		process_ss(filename, name_len != 0);

		if (is_cue && get_bin(filename))
		{
			user_io_file_mount(buf, s_index);
		}
		else
		{
			user_io_file_mount(filename, s_index);
		}
	}
}
