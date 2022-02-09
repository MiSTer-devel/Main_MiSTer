
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
#include "mcdheader.h"

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

static uint32_t libCryptSectors[16] =
{
	14105,
	14231,
	14485,
	14579,
	14649,
	14899,
	15056,
	15130,
	15242,
	15312,
	15378,
	15628,
	15919,
	16031,
	16101,
	16167,
};

static uint32_t msfToLba(uint32_t m, uint32_t s, uint32_t f)
{
	return (m * 60 + s) * 75 + f;
}

static uint8_t bcdToDec(uint8_t bcd)
{
	return (bcd >> 4) * 10 + (bcd & 0x0F);
}

#define SBI_HEADER_SIZE 4
#define SBI_BLOCK_SIZE  14

static uint16_t libCryptMask(const char *sbifile)
{
	int sz;
	uint16_t mask = 0;
	if ((sz = FileLoad(sbifile, buf, sizeof(buf))))
	{
		for (int i = 0;; i++)
		{
			int pos = SBI_HEADER_SIZE + i * SBI_BLOCK_SIZE;
			if (pos >= sz) break;
			uint32_t lba = msfToLba(bcdToDec(buf[pos]), bcdToDec(buf[pos + 1]), bcdToDec(buf[pos + 2]));
			for (int m = 0; m < 16; m++) if (libCryptSectors[m] == lba) mask |= (1 << (15 - m));
		}
	}

	return mask;
}

#define MCD_SIZE (128*1024)

static void psx_mount_save(const char *filename)
{
	user_io_set_index(2);
	user_io_set_download(1);

	int mounted = 0;
	if (strlen(filename))
	{
		FileGenerateSavePath(filename, buf, 0);
		if(!FileExists(buf))
		{
			uint8_t *mcd = new uint8_t[MCD_SIZE];
			if (mcd)
			{
				memset(mcd, 0, MCD_SIZE);
				memcpy(mcd, mcdheader, sizeof(mcdheader));
				FileSave(buf, mcd, MCD_SIZE);
				delete(mcd);
			}
		}

		if (FileExists(buf))
		{
			user_io_file_mount(buf, 2);
			StoreIdx_S(2, buf);
			mounted = 1;
		}
	}

	if (!mounted)
	{
		user_io_file_mount("", 2);
		StoreIdx_S(2, "");
	}
	user_io_set_download(0);
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

		if(*last_dir) psx_mount_save(last_dir);
	}

	if (loaded)
	{
		strcpy(buf, filename);
		strcpy((name_len > 4) ? buf + name_len - 4 : buf + name_len, ".sbi");

		uint16_t mask = libCryptMask(buf);
		user_io_set_index(250);
		user_io_set_download(1);
		user_io_file_tx_data((const uint8_t*)&mask, 2);
		user_io_set_download(0);

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
