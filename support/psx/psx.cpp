
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
#include "../../cd.h"
#include "../chd/mister_chd.h"
#include <libchdr/chd.h>

static char buf[1024];
static uint8_t chd_hunkbuf[CD_FRAME_SIZE * CD_FRAMES_PER_HUNK];
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

		if (*instr == 10) instr++;
		*in = instr;
	} while (!*out && **in);

	return *out;
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

static uint16_t libCryptMask(fileTYPE* sbi_file)
{
	int sz;
	uint16_t mask = 0;
	if ((sz = FileReadAdv(sbi_file, buf, sizeof(buf))))
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

static void unload_chd(toc_t *table)
{
	if (table->chd_f)
	{
		chd_close(table->chd_f);
	}
	memset(chd_hunkbuf, 0, sizeof(chd_hunkbuf));
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

	for (int i = 0; i < table->last; i++)
	{
		if (i == 0) //First track fakes a pregap even if it doesn't exist
		{
			table->tracks[i].index1 = 150;
			table->tracks[i].start = 150;
		}
		table->tracks[i].end += (table->tracks[i].index1 - 1);
	}
	table->end = table->tracks[table->last - 1].end + 1;

	memset(chd_hunkbuf, 0, sizeof(chd_hunkbuf));
	chd_hunknum = -1;

	return 1;

	//Need to store hunkbuf, hunknum and chd_f
}

static int load_cue(const char* filename, toc_t *table)
{
	static char fname[1024 + 10];
	static char line[128];
	char *ptr, *lptr;
	static char toc[100 * 1024];

	unload_cue(table);
	printf("\x1b[32mPSX: Open CUE: %s\n\x1b[0m", fname);

	strcpy(fname, filename);

	memset(toc, 0, sizeof(toc));
	if (!FileLoad(fname, toc, sizeof(toc) - 1))
	{
		printf("\x1b[32mPSX: cannot load file: %s\n\x1b[0m", fname);
		return 0;
	}

	int mm, ss, bb;
	int pregap = 150;

	char *buf = toc;
	while (sgets(line, sizeof(line), &buf))
	{
		lptr = line;
		while (*lptr == 0x20) lptr++;

		/* decode FILE commands */
		if (!(memcmp(lptr, "FILE", 4)))
		{
			ptr = fname + strlen(fname) - 1;
			while ((ptr - fname) && (*ptr != '/') && (*ptr != '\\')) ptr--;
			if (ptr - fname) ptr++;

			lptr += 4;
			while (*lptr == 0x20) lptr++;

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

			if (!FileOpen(&table->tracks[table->last].f, fname)) return 0;

			printf("\x1b[32mPSX: Open track file: %s\n\x1b[0m", fname);

			table->tracks[table->last].offset = 0;

			if (!strstr(lptr, "BINARY"))
			{
				FileClose(&table->tracks[table->last].f);
				printf("\x1b[32mPSX: unsupported file: %s\n\x1b[0m", fname);
				return 0;
			}
		}

		/* decode PREGAP commands */
		else if (sscanf(lptr, "PREGAP %02d:%02d:%02d", &mm, &ss, &bb) == 3)
		{
			// Single bin specific, add pregab but subtract inherent pregap
			pregap += bb + ss * 75 + mm * 60 * 75;
		}
		/* decode TRACK commands */
		else if ((sscanf(lptr, "TRACK %02d %*s", &bb)) || (sscanf(lptr, "TRACK %d %*s", &bb)))
		{
			if (bb != (table->last + 1))
			{
				FileClose(&table->tracks[table->last].f);
				printf("\x1b[32mPSX: missing tracks: %s\n\x1b[0m", fname);
				return 0;
			}

			if (strstr(lptr, "MODE1/2352") || strstr(lptr, "MODE2/2352"))
			{
				table->tracks[table->last].sector_size = 2352;
				table->tracks[table->last].type = 1;
				if (!table->last) table->end = 150; // implicit 2 seconds pregap for track 1
			}
			else if (strstr(lptr, "AUDIO"))
			{
				table->tracks[table->last].sector_size = 2352;
				table->tracks[table->last].type = 0;
			}
			else
			{
				FileClose(&table->tracks[table->last].f);
				printf("\x1b[32mPSX: unsupported track type: %s\n\x1b[0m", lptr);
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
				table->tracks[table->last].start = (bb + ss * 75 + mm * 60 * 75) + pregap;

				//if (!table->last && table->tracks[table->last - 1].end == 0)

				table->tracks[table->last - 1].end = (bb + ss * 75 + mm * 60 * 75) + pregap - 1;
			}
		}
		else if ((sscanf(lptr, "INDEX 01 %02d:%02d:%02d", &mm, &ss, &bb) == 3) ||
			(sscanf(lptr, "INDEX 1 %02d:%02d:%02d", &mm, &ss, &bb) == 3))
		{
			if (!table->tracks[table->last].f.opened())
			{
				// Single bin specific

				// store the end of this track in index1, and use this to calculate the lenght of the track with next track
				// Index1 does not seem to really be used by the core, but I found some code that seems to want to check the length of a track with it
				table->tracks[table->last].index1 = bb + ss * 75 + mm * 60 * 75;

				// Set start if its not set yet (some cue files have both Index 0 and Index 1) else set the end of this track.
				if (!table->tracks[table->last].start)
					table->tracks[table->last].start = bb + ss * 75 + mm * 60 * 75 + pregap;
				else
					table->tracks[table->last].end = bb + ss * 75 + mm * 60 * 75 + pregap;

				int lasttrackend = table->tracks[table->last - 1].index1;
				table->tracks[table->last - 1].index1 = (bb + ss * 75 + mm * 60 * 75) - lasttrackend;

				if (table->tracks[table->last].type && !table->last) table->tracks[table->last].index1 = 150;

				// Set the offset to be the pregap.
				table->tracks[table->last].offset = (pregap * table->tracks[table->last].sector_size);

				table->end += (table->tracks[table->last].f.size / table->tracks[table->last].sector_size);

				// Code for cue files that only has index 1 for each track
				if (table->last > 0 && table->tracks[table->last - 1].end == 0)
				{
					table->tracks[table->last - 1].end = table->tracks[table->last].start - 1;
				}
				// Check if the data track is set to be the full disc
				if (table->last == 1 && table->tracks[0].end == table->end - 1)
				{
					table->tracks[table->last - 1].end = table->tracks[table->last].start - 1;
				}
			}
			else
			{
				table->tracks[table->last].index1 = bb + ss * 75 + mm * 60 * 75;
				if (table->tracks[table->last].type && !table->last) table->tracks[table->last].index1 = 150;
				table->tracks[table->last].start = table->end;
				table->end += (table->tracks[table->last].f.size / table->tracks[table->last].sector_size);
				table->tracks[table->last].end = table->end - 1;
				table->tracks[table->last].offset = 0;
			}
			table->last++;
			if (table->last >= 99) break;
		}
	}

	for (int i = 0; i < table->last; i++)
	{
		printf("\x1b[32mPSX: Track = %u, start = %u, end = %u, offset = %d, sector_size=%d, type = %u\n\x1b[0m", i, table->tracks[i].start, table->tracks[i].end, table->tracks[i].offset, table->tracks[i].sector_size, table->tracks[i].type);
		if (table->tracks[i].index1)
			printf("\x1b[32mPSX: Track = %u,Index1 = %u seconds\n\x1b[0m", i, table->tracks[i].index1 / 75);

	}

	return 1;
}

static int load_cd_image(const char *filename, toc_t *table)
{

	const char *ext = strrchr(filename, '.');
	if (!ext) return 0;

	if (!strncasecmp(".chd", ext, 4))
	{
		return load_chd(filename, table);
	}
	else if (!strncasecmp(".cue", ext, 4)) {
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
	uint32_t reserved;
	track_t  track[99];
};

#define BCD(v) ((uint8_t)((((v)/10) << 4) | ((v)%10)))

void send_cue(toc_t *table)
{
	disk_t *disk = new disk_t;
	if (disk)
	{
		memset(disk, 0, sizeof(disk_t));
		disk->track_count = (BCD(table->last) << 8) | table->last;
		disk->total_lba = table->end;
		int m = (disk->total_lba / 75) / 60;
		int s = (disk->total_lba / 75) % 60;
		disk->total_bcd = (BCD(m) << 8) | BCD(s);

		for (int i = 0; i < table->last; i++)
		{
			disk->track[i].start_lba = i ? table->tracks[i].start : 0;
			disk->track[i].end_lba = table->tracks[i].end;
			m = ((disk->track[i].start_lba + table->tracks[i].index1) / 75) / 60;
			s = ((disk->track[i].start_lba + table->tracks[i].index1) / 75) % 60;
			disk->track[i].bcd = ((BCD(m) << 8) | BCD(s)) | ((table->tracks[i].type ? 0 : 1) << 16);
		}

		user_io_set_index(251);
		user_io_set_download(1);
		user_io_file_tx_data((uint8_t *)disk, sizeof(disk_t));
		user_io_set_download(0);
		delete(disk);
	}
}

#define MCD_SIZE (128*1024)

static void psx_mount_save(const char *filename)
{
	user_io_set_index(2);
	if (strlen(filename))
	{
		FileGenerateSavePath(filename, buf, 0);
		user_io_file_mount(buf, 2, 1, MCD_SIZE);
		StoreIdx_S(2, buf);
	}
	else
	{
		user_io_file_mount("", 2);
		StoreIdx_S(2, "");
	}
}

void psx_fill_blanksave(uint8_t *buffer, uint32_t lba, int cnt)
{
	uint32_t offset = lba * 1024;
	uint32_t size = cnt * 1024;

	if ((offset + size) <= sizeof(mcdheader))
	{
		memcpy(buffer, mcdheader + offset, size);
	}
	else
	{
		memset(buffer, 0, size);
	}
}

static toc_t toc = {};
#define CD_SECTOR_LEN 2352

void psx_read_cd(uint8_t *buffer, int lba, int cnt)
{
	//printf("req lba=%d, cnt=%d\n", lba, cnt);

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
							FileSeek(&toc.tracks[0].f, ((lba * CD_SECTOR_LEN) - toc.tracks[i].offset), SEEK_SET);
						else
							FileSeek(&toc.tracks[i].f, (lba - toc.tracks[i].start) * CD_SECTOR_LEN, SEEK_SET);
					}
					while (cnt)
					{
						if (toc.chd_f)
						{
							if (mister_chd_read_sector(toc.chd_f, (lba - toc.tracks[i].index1) + toc.tracks[i].offset, 0, 0, CD_SECTOR_LEN, buffer, chd_hunkbuf, &chd_hunknum) == CHDERR_NONE)
							{
								if (!toc.tracks[i].type) //CHD requires byteswap of audio data
								{
									for (int swapidx = 0; swapidx < CD_SECTOR_LEN; swapidx += 2)
									{
										uint8_t temp = buffer[swapidx];
										buffer[swapidx] = buffer[swapidx + 1];
										buffer[swapidx + 1] = temp;
									}
								}
							}
							else {
								printf("\x1b[32mPSX: CHD read error: %d\n\x1b[0m", lba);
							}
						}
						else {
							if (toc.tracks[i].offset)
								FileReadAdv(&toc.tracks[0].f, buffer, CD_SECTOR_LEN);
							else
								FileReadAdv(&toc.tracks[i].f, buffer, CD_SECTOR_LEN);
						}
						if ((lba + 1) > toc.tracks[i].end) break;
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

#define ROOT_FOLDER_LBA 150 + 22

const char* game_id_prefixes[]
{
	"SCES",
	"SLES",
	"SCUS",
	"SLUS",
	"SCPM",
	"SLPM",
	"SCPS",
	"SLPS",
};

const char* psx_get_game_id()
{
	uint8_t buffer[CD_SECTOR_LEN];

	static char game_id[11];
	memset(game_id, 0, sizeof(game_id));

	for (int sector = ROOT_FOLDER_LBA; sector < ROOT_FOLDER_LBA + 3; ++sector)
	{
		psx_read_cd(buffer, sector, 1);
		//hexdump(buffer, CD_SECTOR_LEN);
		char* start = nullptr;

		for (const char* prefix : game_id_prefixes)
		{
			start = (char*)memmem(buffer, CD_SECTOR_LEN, prefix, 4);
			if (start) break;
		}

		if (!start) continue;

		const size_t start_pos = start - (char*)buffer;
		char* end = (char*)memmem(start, CD_SECTOR_LEN - start_pos, ";1", 2);

		if (!end) continue;

		size_t size = end - start;

		// file is usually in CCCC_DDD.DD format, normalize to CCCC-DDDDD
		if (size == 11)
		{
			if (start[4] == '_') start[4] = '-';
			if (start[8] == '.')
			{
				start[8] = start[9];
				start[9] = start[10];
				--size;
			}
		}

		const size_t max_length = sizeof(game_id) - 1;
		if (size > max_length) size = max_length;

		return (char*)memcpy(game_id, start, size);
	}

	return game_id;
}

static void mount_cd(int size, int index)
{
	spi_uio_cmd_cont(UIO_SET_SDINFO);
	spi32_w(size);
	spi32_w(0);
	DisableIO();
	spi_uio_cmd8(UIO_SET_SDSTAT, (1 << index) | 0x80);
}

static int load_bios(const char* filename)
{
	int sz = FileLoad(filename, 0, 0);
	if (sz != 512 * 1024) return 0;
	return user_io_file_tx(filename);
}

void psx_mount_cd(int f_index, int s_index, const char *filename)
{
	static char last_dir[1024] = {};

	int loaded = 0;

	if (strlen(filename))
	{
		if (load_cd_image(filename, &toc) && toc.last)
		{
			printf("GAME ID: %s\n", psx_get_game_id());

			int name_len = strlen(filename);

			if (toc.tracks[0].type) // is first track a data?
			{
				const char *p = strrchr(filename, '/');
				int cur_len = p ? p - filename : 0;
				int old_len = strlen(last_dir);

				int same_game = old_len && (cur_len == old_len) && !strncmp(last_dir, filename, old_len);

				if (!same_game)
				{
					int reset = 1;
					if (old_len)
					{
						strcat(last_dir, "/noreset.txt");
						reset = !FileExists(last_dir);
					}

					strcpy(last_dir, filename);
					char *p = strrchr(last_dir, '/');
					if (p) *p = 0;
					else *last_dir = 0;

					if (reset)
					{
						int bios_loaded = 0;

						strcpy(buf, last_dir);
						p = strrchr(buf, '/');
						if (p)
						{
							strcpy(p + 1, "cd_bios.rom");
							bios_loaded = load_bios(buf);
						}

						if (!bios_loaded)
						{
							sprintf(buf, "%s/boot.rom", HomeDir());
							bios_loaded = load_bios(buf);
						}

						if (!bios_loaded) Info("CD BIOS not found!", 4000);
					}

					if (!(user_io_status(0, 0, 1) >> 31)) psx_mount_save(last_dir);
				}
			}

			send_cue(&toc);

			uint16_t mask = 0;

			fileTYPE sbi_file = {};
			bool has_sbi_file = false;

			// search for .sbi file in PSX/sbi.zip
			sprintf(buf, "%s/sbi.zip/%s.sbi", HomeDir(), psx_get_game_id());
			has_sbi_file = (FileOpen(&sbi_file, buf, 1));

			if (!has_sbi_file)
			{
				// search for .sbi file base on image name
				strcpy(buf, filename);
				strcpy((name_len > 4) ? buf + name_len - 4 : buf + name_len, ".sbi");
				has_sbi_file = (FileOpen(&sbi_file, buf, 1));
			}

			if (has_sbi_file)
			{
				printf("Found SBI file: %s\n", buf);
				mask = libCryptMask(&sbi_file);
			}

			user_io_set_index(250);
			user_io_set_download(1);
			user_io_file_tx_data((const uint8_t*)&mask, 2);
			user_io_set_download(0);

			user_io_set_index(f_index);
			process_ss(filename, name_len != 0);

			mount_cd(toc.end*CD_SECTOR_LEN, s_index);
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
