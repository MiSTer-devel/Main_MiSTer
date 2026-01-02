
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
static uint8_t *chd_hunkbuf = NULL;
static int chd_hunknum;
static int noreset = 0;

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
	if (chd_hunkbuf) free(chd_hunkbuf);
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

	/* PSX core expects the TOC values for track start/end to not take into account
	* pregap, unlike some other cores. Adjust the CHD toc to reflect this
	*/

	for (int i = 0; i < table->last; i++)
	{
		if (i == 0) //First track fakes a pregap even if it doesn't exist
		{
			table->tracks[i].indexes[1] = 150;
			table->tracks[i].start = 150;
			table->tracks[i].end += 150-1;
		} else {
			int frame_cnt = table->tracks[i].end - table->tracks[i].start;
			frame_cnt += table->tracks[i].indexes[1];
			table->tracks[i].start = table->tracks[i-1].end + 1;
			table->tracks[i].end = table->tracks[i].start + frame_cnt - 1;
		}
	}

	table->end = table->tracks[table->last - 1].end + 1;

	chd_hunkbuf = (uint8_t *)malloc(table->chd_hunksize);
	chd_hunknum = -1;

	return 1;
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
	int pregap = 0;

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
      table->tracks[table->last].pregap = 1;

		}
		/* decode TRACK commands */
		else if ((sscanf(lptr, "TRACK %02d %*s", &bb)) || (sscanf(lptr, "TRACK %d %*s", &bb)))
		{
      pregap = 0;
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


        pregap = bb+ss*75+mm*60*75;
			}
		}
		else if ((sscanf(lptr, "INDEX 01 %02d:%02d:%02d", &mm, &ss, &bb) == 3) ||
			(sscanf(lptr, "INDEX 1 %02d:%02d:%02d", &mm, &ss, &bb) == 3))
		{
			if (!table->tracks[table->last].f.opened())
			{
        table->tracks[table->last].start = bb+ss*75+mm*60*75; 
        if (table->tracks[table->last].pregap)
          table->tracks[table->last].start += pregap;
        //Subtract the fake 150 sector pregap used for the first data track
        table->tracks[table->last].offset = table->tracks[table->last].start*table->tracks[table->last].sector_size;
        if (table->last)
        {
          table->tracks[table->last-1].end = table->tracks[table->last].start-1;
          if (pregap)
          {
            table->tracks[table->last].indexes[1] = table->tracks[table->last].start - pregap;
            if (!table->tracks[table->last].pregap)
            {
              table->tracks[table->last].offset -= 2352*table->tracks[table->last].indexes[1];
              table->tracks[table->last].indexes[1] = table->tracks[table->last].start - pregap;
            } else {
              table->tracks[table->last].indexes[1] = pregap;
            }
          }
        } else if (table->tracks[table->last].type) {
          table->tracks[table->last].indexes[1] = 150;
        }
			}
			else
			{
				table->tracks[table->last].indexes[1] = bb + ss * 75 + mm * 60 * 75;
				if (table->tracks[table->last].type && !table->last) table->tracks[table->last].indexes[1] = 150;
				table->tracks[table->last].start = table->end;
				table->end += (table->tracks[table->last].f.size / table->tracks[table->last].sector_size);
				table->tracks[table->last].offset = 0;
			}
			table->tracks[table->last].end = table->end - 1;
			table->last++;
			if (table->last >= 99) break;
		}
	}

	/*
	for (int i = 0; i < table->last; i++)
	{
		printf("\x1b[32mPSX: Track = %u, start = %u, end = %u, offset = %d, sector_size=%d, type = %u\n\x1b[0m", i, table->tracks[i].start, table->tracks[i].end, table->tracks[i].offset, table->tracks[i].sector_size, table->tracks[i].type);
		if (table->tracks[i].indexes[1])
			printf("\x1b[32mPSX: Track = %u,Index1 = %u seconds\n\x1b[0m", i, table->tracks[i].indexes[1] / 75);

	}*/

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
	track_t  track[99];
};

enum region_t
{
	UNKNOWN = 0,
	JP,
	US,
	EU
};

static const char* region_string(region_t region)
{
	switch (region)
	{
		case region_t::JP: return "Japan";
		case region_t::US: return "USA";
		case region_t::EU: return "Europe";
		default: return "Unknown";
	}
}

#define BCD(v) ((uint8_t)((((v)/10) << 4) | ((v)%10)))

static void send_cue_and_metadata(toc_t *table, uint16_t libcrypt_mask, enum region_t region, int reset)
{
	disk_t *disk = new disk_t;
	if (disk)
	{
		for (int i = 0; i < table->last; i++)
		{
			printf("\x1b[32mPSX: Track = %u, start = %u, end = %u, offset = %d, sector_size=%d, type = %u\n\x1b[0m", i, table->tracks[i].start, table->tracks[i].end, table->tracks[i].offset, table->tracks[i].sector_size, table->tracks[i].type);
			if (table->tracks[i].indexes[1]) printf("\x1b[32mPSX: Track = %u,Index1 = %u seconds\n\x1b[0m", i, table->tracks[i].indexes[1] / 75);
		}

		memset(disk, 0, sizeof(disk_t));
		disk->libcrypt_mask = libcrypt_mask;
		disk->metadata = region; // the lower 2 bits of metadata contain the region
		if (reset) disk->metadata |= 4; // 3rd bit is reset request
		disk->track_count = (BCD(table->last) << 8) | table->last;
		disk->total_lba = table->end;
		int m = (disk->total_lba / 75) / 60;
		int s = (disk->total_lba / 75) % 60;
		disk->total_bcd = (BCD(m) << 8) | BCD(s);

		for (int i = 0; i < table->last; i++)
		{
			disk->track[i].start_lba = i ? table->tracks[i].start : 0;
			disk->track[i].end_lba = table->tracks[i].end;
			m = ((disk->track[i].start_lba + table->tracks[i].indexes[1]) / 75) / 60;
			s = ((disk->track[i].start_lba + table->tracks[i].indexes[1]) / 75) % 60;
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

int psx_chd_hunksize()
{
	if (toc.chd_f)
		return toc.chd_hunksize;

	return 0;
}


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
            {
							FileSeek(&toc.tracks[0].f, toc.tracks[i].offset+((lba-toc.tracks[i].start)*CD_SECTOR_LEN), SEEK_SET);

            }else {
							FileSeek(&toc.tracks[i].f, (lba - toc.tracks[i].start) * CD_SECTOR_LEN, SEEK_SET);
            }
					}
					while (cnt)
					{
            if (toc.tracks[i+1].pregap && lba > (toc.tracks[i+1].start-toc.tracks[i+1].indexes[1]))
            {
              //The TOC is setup so that pregap sectors are actually part of the
              //PREVIOUS track. If the pregap field is set the file doesn't contain
              //this data, so we have to fake it. 
              //Check the next track's pregap and indexes[1] values to determine
              //if we're reading pregap sectors
              

              memset(buffer, 0x0, CD_SECTOR_LEN);
            }
            else if (toc.chd_f)
						{

							// The "fake" 150 sector pregap moves all the LBAs up by 150, so adjust here to read where the core actually wants data from
							int read_lba = lba - toc.tracks[0].indexes[1];
							if (mister_chd_read_sector(toc.chd_f, (read_lba + toc.tracks[i].offset), 0, 0, CD_SECTOR_LEN, buffer, chd_hunkbuf, &chd_hunknum) == CHDERR_NONE)
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

struct region_info_t
{
	const char* game_id_prefix;
	enum region_t region;
};

const region_info_t region_info_table[]
{
	{ "SCES", region_t::EU },
	{ "SLES", region_t::EU },
	{ "SCUS", region_t::US },
	{ "SLUS", region_t::US },
	{ "SCPM", region_t::JP },
	{ "SLPM", region_t::JP },
	{ "SCPS", region_t::JP },
	{ "SLPS", region_t::JP },
	{ "SIPS", region_t::JP },
	// for demo disks
	{ "PUPX", region_t::US },
	{ "PEPX", region_t::EU },
	{ "PAPX", region_t::JP },
	{ "PCPX", region_t::JP },
	{ "SCZS", region_t::JP },
	{ "SCED", region_t::EU },
	{ "SLED", region_t::EU },
};

struct game_info_t
{
	const char* game_id;
	region_t region;
};

static region_t psx_get_region()
{
	uint8_t buffer[CD_SECTOR_LEN];
	int license_sector = 154;
	psx_read_cd(buffer, license_sector, 1);
	uint8_t* license_start = (uint8_t*)memmem(buffer, CD_SECTOR_LEN, "          Licensed  by          Sony Computer Entertainment ", 60);
	if (license_start) {
		const uint8_t* region_start = license_start + 60;
		if (memcmp(region_start, "Amer  ica ", 10) == 0)
			return region_t::US;
		if (memcmp(region_start, "Inc.", 4) == 0)
			return region_t::JP;
		if (memcmp(region_start, "Euro pe", 7) == 0)
			return region_t::EU;
	}

	return region_t::UNKNOWN;
}

static game_info_t psx_get_game_info()
{
	uint8_t buffer[CD_SECTOR_LEN];

	static char game_id[11];
	memset(game_id, 0, sizeof(game_id));
	enum region_t game_region = UNKNOWN;

	for (int sector = ROOT_FOLDER_LBA; sector < ROOT_FOLDER_LBA + 25; ++sector)
	{
		psx_read_cd(buffer, sector, 1);
		//hexdump(buffer, CD_SECTOR_LEN);
		char* start = nullptr;

		for (const auto& region_info : region_info_table)
		{
			game_region = region_info.region;
			start = (char*)memmem(buffer, CD_SECTOR_LEN, region_info.game_id_prefix, 4);
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

		return { (const char*)memcpy(game_id, start, size), game_region };
	}

	return { game_id, region_t::UNKNOWN };
}

const char* psx_get_game_id()
{
	return psx_get_game_info().game_id;
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

static int load_bios(const char* filename)
{
	int sz = FileLoad(filename, 0, 0);
	if (sz != 512 * 1024) return 0;
	return user_io_file_tx(filename, 0xC0);
}

void psx_mount_cd(int f_index, int s_index, const char *filename)
{
	static char last_dir[1024] = {};

	int loaded = 0;

	if (strlen(filename))
	{
		if (load_cd_image(filename, &toc) && toc.last)
		{
			int reset = 0;
			game_info_t game_info = psx_get_game_info();
			const char* game_id = game_info.game_id;
			region_t region = psx_get_region();
			if (region == region_t::UNKNOWN)
				region = game_info.region;
			printf("Game ID: %s, region: %s\n", game_id, region_string(region));

			// Write game ID if it's not empty (BIOS check is handled in user_io_write_gameid)
			if (game_id && game_id[0] != '\0')
			{
				user_io_write_gameid(filename, 0, game_id);
			}

			int name_len = strlen(filename);

			if (toc.tracks[0].type) // is first track a data?
			{
				const char *p = strrchr(filename, '/');
				int cur_len = p ? p - filename : 0;
				int old_len = strlen(last_dir);

				int same_game = old_len && (cur_len == old_len) && !strncmp(last_dir, filename, old_len);

				if (!same_game)
				{
					if (!noreset && old_len)
					{
						strcat(last_dir, "/noreset.txt");
						noreset = FileExists(last_dir);
					}
					reset = !noreset;

					strcpy(last_dir, filename);
					char *p = strrchr(last_dir, '/');
					if (p) *p = 0;
					else *last_dir = 0;

					if (reset)
					{
						int bios_loaded = 0;

						// load cd_bios.rom from game directory
						sprintf(buf, "%s/", last_dir);
						p = strrchr(buf, '/');
						if (p)
						{
							strcpy(p + 1, "cd_bios.rom");
							bios_loaded = load_bios(buf);
						}

						// load cd_bios.rom from parent directory
						if (!bios_loaded) {
							strcpy(buf, last_dir);
							p = strrchr(buf, '/');
							if (p)
							{
								strcpy(p + 1, "cd_bios.rom");
								bios_loaded = load_bios(buf);
							}
						}

					}

					if (!(user_io_status_get("[63]"))) psx_mount_save(last_dir);
				}
			}

			uint16_t mask = 0;

			fileTYPE sbi_file = {};
			bool has_sbi_file = false;

			// search for .sbi file in PSX/sbi.zip
			sprintf(buf, "%s/sbi.zip/%s.sbi", HomeDir(), game_id);
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

			process_ss(filename, name_len != 0);
			send_cue_and_metadata(&toc, mask, region, reset);

			user_io_set_index(f_index);

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

void psx_poll()
{
	spi_uio_cmd(UIO_CD_GET);
}

void psx_reset()
{
	noreset = 0;
}
