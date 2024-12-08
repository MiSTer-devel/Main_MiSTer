
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "../../file_io.h"
#include "../../user_io.h"
#include "../../spi.h"
#include "../../hardware.h"
#include "../../menu.h"
#include "cdi.h"
#include "../../cd.h"
#include "../chd/mister_chd.h"
#include <libchdr/chd.h>

static char buf[1024];
#define CD_SECTOR_LEN 2352

static uint8_t *chd_hunkbuf = NULL;
static int chd_hunknum;

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

		if (*instr == 10)
			instr++;
		*in = instr;
	} while (!*out && **in);

	return *out;
}

static void unload_chd(toc_t *table)
{
	if (table->chd_f)
	{
		chd_close(table->chd_f);
	}
	if (chd_hunkbuf)
		free(chd_hunkbuf);
	memset(table, 0, sizeof(toc_t));
	chd_hunknum = -1;
}

static void unload_cue(toc_t *table)
{
	for (int i = 0; i < table->last; i++)
	{
		FileClose(&table->tracks[i].f);
	}
	memset(table, 0, sizeof(toc_t));
}

static int load_chd(const char *filename, toc_t *table)
{

	unload_chd(table);
	chd_error err = mister_load_chd(filename, table);
	if (err != CHDERR_NONE)
	{
		return 0;
	}

	/* CD-i core expects the TOC values for track start/end to not take into account
	 * pregap, unlike some other cores. Adjust the CHD toc to reflect this
	 */

	for (int i = 0; i < table->last; i++)
	{
		if (i == 0) // First track fakes a pregap even if it doesn't exist
		{
			table->tracks[i].indexes[1] = 150;
			table->tracks[i].start = 150;
			table->tracks[i].end += 150 - 1;
		}
		else
		{
			int frame_cnt = table->tracks[i].end - table->tracks[i].start;
			frame_cnt += table->tracks[i].indexes[1];
			table->tracks[i].start = table->tracks[i - 1].end + 1;
			table->tracks[i].end = table->tracks[i].start + frame_cnt - 1;
		}
	}

	table->end = table->tracks[table->last - 1].end + 1;

	chd_hunkbuf = (uint8_t *)malloc(table->chd_hunksize);
	chd_hunknum = -1;

	return 1;
}

static int load_cue(const char *filename, toc_t *table)
{
	static char fname[1024 + 10];
	static char line[128];
	char *ptr, *lptr;
	static char toc[100 * 1024];

	unload_cue(table);
	printf("\x1b[32mCDI: Open CUE: %s\n\x1b[0m", fname);

	strcpy(fname, filename);

	memset(toc, 0, sizeof(toc));
	if (!FileLoad(fname, toc, sizeof(toc) - 1))
	{
		printf("\x1b[32mCDI: cannot load file: %s\n\x1b[0m", fname);
		return 0;
	}

	int mm, ss, bb;
	int pregap = 0;

	char *buf = toc;
	while (sgets(line, sizeof(line), &buf))
	{
		lptr = line;
		while (*lptr == 0x20)
			lptr++;

		/* decode FILE commands */
		if (!(memcmp(lptr, "FILE", 4)))
		{
			ptr = fname + strlen(fname) - 1;
			while ((ptr - fname) && (*ptr != '/') && (*ptr != '\\'))
				ptr--;
			if (ptr - fname)
				ptr++;

			lptr += 4;
			while (*lptr == 0x20)
				lptr++;

			if (*lptr == '\"')
			{
				lptr++;
				while ((*lptr != '\"') && (lptr <= (line + 128)) && (ptr < (fname + 1023)))
					*ptr++ = *lptr++;
			}
			else
			{
				while ((*lptr != 0x20) && (lptr <= (line + 128)) && (ptr < (fname + 1023)))
					*ptr++ = *lptr++;
			}
			*ptr = 0;

			if (!FileOpen(&table->tracks[table->last].f, fname))
				return 0;

			printf("\x1b[32mCDI: Open track file: %s\n\x1b[0m", fname);

			table->tracks[table->last].offset = 0;

			if (!strstr(lptr, "BINARY"))
			{
				FileClose(&table->tracks[table->last].f);
				printf("\x1b[32mCDI: unsupported file: %s\n\x1b[0m", fname);
				return 0;
			}
		}

		/* decode PREGAP commands */
		else if (sscanf(lptr, "PREGAP %02d:%02d:%02d", &mm, &ss, &bb) == 3)
		{
			// Single bin specific, add pregab but subtract inherent pregap
			pregap += bb + ss * 75 + mm * 60 * 75;
			table->tracks[table->last].pregap = 1;
		}
		/* decode TRACK commands */
		else if ((sscanf(lptr, "TRACK %02d %*s", &bb)) || (sscanf(lptr, "TRACK %d %*s", &bb)))
		{
			pregap = 0;
			if (bb != (table->last + 1))
			{
				FileClose(&table->tracks[table->last].f);
				printf("\x1b[32mCDI: missing tracks: %s\n\x1b[0m", fname);
				return 0;
			}

			if (strstr(lptr, "MODE1/2352") || strstr(lptr, "MODE2/2352") || strstr(lptr, "CDI/2352"))
			{
				table->tracks[table->last].sector_size = CD_SECTOR_LEN;
				table->tracks[table->last].type = 1;
				if (!table->last)
					table->end = 150; // implicit 2 seconds pregap for track 1
			}
			else if (strstr(lptr, "AUDIO"))
			{
				table->tracks[table->last].sector_size = CD_SECTOR_LEN;
				table->tracks[table->last].type = 0;
			}
			else
			{
				FileClose(&table->tracks[table->last].f);
				printf("\x1b[32mCDI: unsupported track type: %s\n\x1b[0m", lptr);
				return 0;
			}
		}

		/* decode INDEX commands */
		else if ((sscanf(lptr, "INDEX 00 %02d:%02d:%02d", &mm, &ss, &bb) == 3) ||
				 (sscanf(lptr, "INDEX 0 %02d:%02d:%02d", &mm, &ss, &bb) == 3))
		{
			// Single bin specific
			if (!table->tracks[table->last].f.opened())
			{

				pregap = bb + ss * 75 + mm * 60 * 75;
			}
		}
		else if ((sscanf(lptr, "INDEX 01 %02d:%02d:%02d", &mm, &ss, &bb) == 3) ||
				 (sscanf(lptr, "INDEX 1 %02d:%02d:%02d", &mm, &ss, &bb) == 3))
		{
			if (!table->tracks[table->last].f.opened())
			{
				table->tracks[table->last].start = bb + ss * 75 + mm * 60 * 75;
				if (table->tracks[table->last].pregap)
					table->tracks[table->last].start += pregap;
				// Subtract the fake 150 sector pregap used for the first data track
				table->tracks[table->last].offset = table->tracks[table->last].start * table->tracks[table->last].sector_size;
				if (table->last)
				{
					table->tracks[table->last - 1].end = table->tracks[table->last].start - 1;
					if (pregap)
					{
						table->tracks[table->last].indexes[1] = table->tracks[table->last].start - pregap;
						if (!table->tracks[table->last].pregap)
						{
							table->tracks[table->last].offset -= CD_SECTOR_LEN * table->tracks[table->last].indexes[1];
							table->tracks[table->last].indexes[1] = table->tracks[table->last].start - pregap;
						}
						else
						{
							table->tracks[table->last].indexes[1] = pregap;
						}
					}
				}
				else if (table->tracks[table->last].type)
				{
					table->tracks[table->last].indexes[1] = 150;
				}
			}
			else
			{
				table->tracks[table->last].indexes[1] = bb + ss * 75 + mm * 60 * 75;
				if (table->tracks[table->last].type && !table->last)
					table->tracks[table->last].indexes[1] = 150;
				table->tracks[table->last].start = table->end;
				table->end += (table->tracks[table->last].f.size / table->tracks[table->last].sector_size);
				table->tracks[table->last].offset = 0;
			}
			table->tracks[table->last].end = table->end - 1;
			table->last++;
			if (table->last >= 99)
				break;
		}
	}

	for (int i = 0; i < table->last; i++)
	{
		printf("\x1b[32mCDI: Track = %u, start = %u, end = %u, offset = %d, sector_size=%d, type = %u\n\x1b[0m", i, table->tracks[i].start, table->tracks[i].end, table->tracks[i].offset, table->tracks[i].sector_size, table->tracks[i].type);
		if (table->tracks[i].indexes[1])
			printf("\x1b[32mCDI: Track = %u,Index1 = %u seconds\n\x1b[0m", i, table->tracks[i].indexes[1] / 75);
	}

	return 1;
}

static int load_cd_image(const char *filename, toc_t *table)
{

	const char *ext = strrchr(filename, '.');
	if (!ext)
		return 0;

	if (!strncasecmp(".chd", ext, 4))
	{
		return load_chd(filename, table);
	}
	else if (!strncasecmp(".cue", ext, 4))
	{
		return load_cue(filename, table);
	}

	return 0;
}

struct track_t
{
	uint32_t start_lba;
	uint32_t end_lba;
	uint32_t bcd;
	uint32_t reserved;
};

struct disk_t
{
	uint32_t track_count;
	uint32_t total_lba;
	uint32_t total_bcd;
	uint16_t libcrypt_mask;
	uint16_t metadata; // lower 2 bits encode the region, 3rd bit is reset request, the other bits are reseved
	track_t track[99];
};

#define TIMEKEEPER_SIZE (8 * 1024)

static void cdi_mount_save(const char *filename)
{
	user_io_set_index(1);
	if (strlen(filename))
	{
		FileGenerateSavePath(filename, buf, 0);
		user_io_file_mount(buf, 1, 1, TIMEKEEPER_SIZE);
		StoreIdx_S(1, buf);
	}
	else
	{
		user_io_file_mount("", 1);
		StoreIdx_S(1, "");
	}
}

static toc_t toc = {};

int cdi_chd_hunksize()
{
	if (toc.chd_f)
		return toc.chd_hunksize;

	return 0;
}

void cdi_read_cd(uint8_t *buffer, int lba, int cnt)
{
	// printf("req lba=%d, cnt=%d\n", lba, cnt);

	while (cnt > 0)
	{
		if (lba < toc.tracks[0].start || !toc.last)
		{
			memset(buffer, 0, CD_SECTOR_LEN);
		}
		else
		{
			memset(buffer, 0xAA, CD_SECTOR_LEN);

			for (int i = 0; i < toc.last; i++)
			{
				if (lba >= toc.tracks[i].start && lba <= toc.tracks[i].end)
				{
					if (!toc.chd_f)
					{
						if (toc.tracks[i].offset)
						{
							FileSeek(&toc.tracks[0].f, toc.tracks[i].offset + ((lba - toc.tracks[i].start) * CD_SECTOR_LEN), SEEK_SET);
						}
						else
						{
							FileSeek(&toc.tracks[i].f, (lba - toc.tracks[i].start) * CD_SECTOR_LEN, SEEK_SET);
						}
					}
					while (cnt)
					{
						if (toc.tracks[i + 1].pregap && lba > (toc.tracks[i + 1].start - toc.tracks[i + 1].indexes[1]))
						{
							// The TOC is setup so that pregap sectors are actually part of the
							// PREVIOUS track. If the pregap field is set the file doesn't contain
							// this data, so we have to fake it.
							// Check the next track's pregap and indexes[1] values to determine
							// if we're reading pregap sectors

							memset(buffer, 0x0, CD_SECTOR_LEN);
						}
						else if (toc.chd_f)
						{

							// The "fake" 150 sector pregap moves all the LBAs up by 150, so adjust here to read where the core actually wants data from
							int read_lba = lba - toc.tracks[0].indexes[1];
							if (mister_chd_read_sector(toc.chd_f, (read_lba + toc.tracks[i].offset), 0, 0, CD_SECTOR_LEN, buffer, chd_hunkbuf, &chd_hunknum) == CHDERR_NONE)
							{
								if (!toc.tracks[i].type) // CHD requires byteswap of audio data
								{
									for (int swapidx = 0; swapidx < CD_SECTOR_LEN; swapidx += 2)
									{
										uint8_t temp = buffer[swapidx];
										buffer[swapidx] = buffer[swapidx + 1];
										buffer[swapidx + 1] = temp;
									}
								}
							}
							else
							{
								printf("\x1b[32mCDI: CHD read error: %d\n\x1b[0m", lba);
							}
						}
						else
						{
							if (toc.tracks[i].offset)
								FileReadAdv(&toc.tracks[0].f, buffer, CD_SECTOR_LEN);
							else
								FileReadAdv(&toc.tracks[i].f, buffer, CD_SECTOR_LEN);
						}
						if ((lba + 1) > toc.tracks[i].end)
							break;
						buffer += CD_SECTOR_LEN;
						cnt--;
						lba++;
					}
					break;
				}
			}
		}

		buffer += CD_SECTOR_LEN;
		cnt--;
		lba++;
	}
}

static void mount_cd(int size, int index)
{
	spi_uio_cmd_cont(UIO_SET_SDINFO);
	spi32_w(size);
	spi32_w(0);
	DisableIO();
	spi_uio_cmd8(UIO_SET_SDSTAT, (1 << index) | 0x80);
	user_io_bufferinvalidate(1);
}

void cdi_mount_cd(int s_index, const char *filename)
{
	int loaded = 0;

	if (strlen(filename))
	{
		if (load_cd_image(filename, &toc) && toc.last)
		{
			cdi_mount_save(filename);
			user_io_set_index(0);
			mount_cd(toc.end * CD_SECTOR_LEN, s_index);
			loaded = 1;
		}
	}

	if (!loaded)
	{
		printf("Unmount CD\n");
		unload_cue(&toc);
		unload_chd(&toc);
		mount_cd(0, s_index);
	}
}

void cdi_poll()
{
	spi_uio_cmd(UIO_CD_GET);
}
