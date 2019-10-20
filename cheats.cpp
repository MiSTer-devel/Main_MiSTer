#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <limits.h>
#include <ctype.h>
#include <vector>
#include <algorithm>

#include "hardware.h"
#include "file_io.h"
#include "user_io.h"
#include "fpga_io.h"
#include "miniz_zip.h"
#include "osd.h"
#include "cheats.h"

struct cheat_rec_t
{
	char enabled;
	char name[256];
};

typedef std::vector<cheat_rec_t> CheatVector;
static CheatVector cheats;

static int iSelectedEntry = 0;
static int iFirstEntry = 0;
static int loaded = 0;

struct CheatComp
{
	bool operator()(const cheat_rec_t& ce1, const cheat_rec_t& ce2)
	{
		int len1 = strlen(ce1.name);
		int len2 = strlen(ce2.name);

		int len = (len1 < len2) ? len1 : len2;
		int ret = strncasecmp(ce1.name, ce2.name, len);
		if (!ret)
		{
			return len1 < len2;
		}

		return ret < 0;
	}
};

static char cheat_zip[1024] = {};

int find_by_crc(uint32_t romcrc)
{
	sprintf(cheat_zip, "%s/cheats/%s", getRootDir(), CoreName);
	DIR *d = opendir(cheat_zip);
	if (!d)
	{
		printf("Couldn't open dir: %s\n", cheat_zip);
		return 0;
	}

	struct dirent *de;
	while((de = readdir(d)))
	{
		if (de->d_type == DT_REG)
		{
			int len = strlen(de->d_name);
			if (len >= 14 && de->d_name[len - 14] == '[' && !strcasecmp(de->d_name+len-5, "].zip"))
			{
				uint32_t crc = 0;
				if (sscanf(de->d_name + len - 14, "[%X].zip", &crc) == 1)
				{
					if (crc == romcrc)
					{
						strcat(cheat_zip, "/");
						strcat(cheat_zip, de->d_name);
						closedir(d);
						return 1;
					}
				}
			}
		}
	}

	closedir(d);
	return 0;
}

void cheats_init(const char *rom_path, uint32_t romcrc)
{
	cheats.clear();
	loaded = 0;
	cheat_zip[0] = 0;

	if (!strcasestr(rom_path, ".zip"))
	{
		sprintf(cheat_zip, "%s/%s", getRootDir(), rom_path);
		char *p = strrchr(cheat_zip, '.');
		if (p) *p = 0;
		strcat(cheat_zip, ".zip");
	}

	mz_zip_archive _z = {};
	if (!mz_zip_reader_init_file(&_z, cheat_zip, 0))
	{
		memset(&_z, 0, sizeof(_z));

		const char *rom_name = strrchr(rom_path, '/');
		if (rom_name)
		{
			sprintf(cheat_zip, "%s/cheats/%s%s", getRootDir(), CoreName, rom_name);
			char *p = strrchr(cheat_zip, '.');
			if (p) *p = 0;
			strcat(cheat_zip, ".zip");

			if (!mz_zip_reader_init_file(&_z, cheat_zip, 0))
			{
				memset(&_z, 0, sizeof(_z));
				if (!find_by_crc(romcrc) || !mz_zip_reader_init_file(&_z, cheat_zip, 0))
				{
					printf("no cheat file found\n");
					return;
				}
			}
		}
		else
		{
			if (!find_by_crc(romcrc) || !mz_zip_reader_init_file(&_z, cheat_zip, 0))
			{
				printf("no cheat file found\n");
				return;
			}
		}
	}

	printf("Using cheat file: %s\n", cheat_zip);

	mz_zip_archive *z = new mz_zip_archive(_z);
	for (size_t i = 0; i < mz_zip_reader_get_num_files(z); i++)
	{
		cheat_rec_t ch = {};
		mz_zip_reader_get_filename(z, i, ch.name, sizeof(ch.name));

		if (mz_zip_reader_is_file_a_directory(z, i))
		{
			continue;
		}

		cheats.push_back(ch);
	}

	mz_zip_reader_end(z);
	delete z;

	std::sort(cheats.begin(), cheats.end(), CheatComp());

	printf("cheats: %d\n", cheats_available());
	cheats_scan(SCANF_INIT);
}

int cheats_available()
{
	return cheats.size();
}

void cheats_scan(int mode)
{
	if (mode == SCANF_INIT)
	{
		iFirstEntry = 0;
		iSelectedEntry = 0;
	}
	else
	{
		if (!cheats_available()) return;

		if (mode == SCANF_END)
		{
			iSelectedEntry = cheats_available() - 1;
			iFirstEntry = iSelectedEntry - OsdGetSize() + 1;
			if (iFirstEntry < 0) iFirstEntry = 0;
		}
		else if (mode == SCANF_NEXT)
		{
			if (iSelectedEntry + 1 < cheats_available()) // scroll within visible items
			{
				iSelectedEntry++;
				if (iSelectedEntry > iFirstEntry + OsdGetSize() - 1) iFirstEntry = iSelectedEntry - OsdGetSize() + 1;
			}
		}
		else if (mode == SCANF_PREV)
		{
			if (iSelectedEntry > 0) // scroll within visible items
			{
				iSelectedEntry--;
				if (iSelectedEntry < iFirstEntry) iFirstEntry = iSelectedEntry;
			}
		}
		else if (mode == SCANF_NEXT_PAGE)
		{
			if (iSelectedEntry < iFirstEntry + OsdGetSize() - 1)
			{
				iSelectedEntry = iFirstEntry + OsdGetSize() - 1;
				if (iSelectedEntry >= cheats_available()) iSelectedEntry = cheats_available() - 1;
			}
			else
			{
				iSelectedEntry += OsdGetSize();
				iFirstEntry += OsdGetSize();
				if (iSelectedEntry >= cheats_available())
				{
					iSelectedEntry = cheats_available() - 1;
					iFirstEntry = iSelectedEntry - OsdGetSize() + 1;
					if (iFirstEntry < 0) iFirstEntry = 0;
				}
				else if (iFirstEntry + OsdGetSize() > cheats_available())
				{
					iFirstEntry = cheats_available() - OsdGetSize();
				}
			}
		}
		else if (mode == SCANF_PREV_PAGE)
		{
			if (iSelectedEntry != iFirstEntry)
			{
				iSelectedEntry = iFirstEntry;
			}
			else
			{
				iFirstEntry -= OsdGetSize();
				if (iFirstEntry < 0) iFirstEntry = 0;
				iSelectedEntry = iFirstEntry;
			}
		}
	}
}

void cheats_scroll_name()
{
	// this function is called periodically when file selection window is displayed
	// it checks if predefined period of time has elapsed and scrolls the name if necessary
	int len;
	int max_len;
	static char name[256 + 4];

	name[0] = 32;
	name[1] = cheats[iSelectedEntry].enabled ? 0x1a : 0x1b;
	name[2] = 32;
	strcpy(name + 3, cheats[iSelectedEntry].name);

	len = strlen(name); // get name length
	if (len > 3 && !strncasecmp(name + len - 3, ".gg", 3)) len -= 3;

	max_len = 30;
	ScrollText(iSelectedEntry - iFirstEntry, name, 3, len, max_len, 1);
}

void cheats_print()
{
	int k;
	int len;

	static char s[256+4];

	ScrollReset();

	for (int i = 0; i < OsdGetSize(); i++)
	{
		char leftchar = 0;
		if (i < cheats_available())
		{
			k = iFirstEntry + i;

			s[0] = 32;
			s[1] = cheats[k].enabled ? 0x1a : 0x1b;
			s[2] = 32;
			strcpy(s + 3, cheats[k].name);

			len = strlen(s); // get name length
			if (len > 3 && !strncasecmp(s + len - 3, ".gg", 3)) len -= 3;
			s[len] = 0;

			if (len > 28)
			{
				len = 27; // trim display length if longer than 30 characters
				s[28] = 22;
			}

			s[29] = 0;

			if (!i && k) leftchar = 17;
			if ((i == OsdGetSize() - 1) && (k < cheats_available() - 1)) leftchar = 16;
		}
		else
		{
			memset(s, ' ', 32);
		}

		OsdWriteOffset(i, s, i == (iSelectedEntry - iFirstEntry), 0, 0, leftchar);
	}
}

#define CHEAT_SIZE (128*16) // 128 codes max

static void cheats_send()
{
	static char filename[1024];
	static uint8_t buff[CHEAT_SIZE];
	int pos = 0;
	for (int i = 0; i < cheats_available(); i++)
	{
		fileTYPE f = {};
		if (cheats[i].enabled)
		{
			sprintf(filename, "%s/%s", cheat_zip, cheats[i].name);
			if (FileOpen(&f, filename))
			{
				int len = f.size;
				if (!len || (len & 15))
				{
					printf("Cheat file %s has incorrect length %d -> skipping.", filename, len);
				}
				else
				{
					if (len + pos > CHEAT_SIZE)
					{
						len = CHEAT_SIZE - pos;
					}

					if (FileReadAdv(&f, buff + pos, len) == len)
					{
						pos += len;
					}
					else
					{
						printf("Cannot read cheat file %s.", filename);
					}
				}
				FileClose(&f);
			}
			else
			{
				printf("Cannot open cheat file %s.", filename);
			}
		}

		if (pos >= CHEAT_SIZE) break;
	}

	loaded = pos / 16;
	printf("Cheat codes: %d\n", loaded);

	user_io_set_index(255);

	// prepare transmission
	EnableFpga();
	spi8(UIO_FILE_TX);
	spi8(0xff);
	DisableFpga();

	EnableFpga();
	spi8(UIO_FILE_TX_DAT);
	spi_write(buff, pos ? pos : 2, fpga_get_fio_size());
	DisableFpga();

	// signal end of transmission
	EnableFpga();
	spi8(UIO_FILE_TX);
	spi8(0x00);
	DisableFpga();
}

void cheats_toggle()
{
	cheats[iSelectedEntry].enabled = !cheats[iSelectedEntry].enabled;
	cheats_send();
}

int cheats_loaded()
{
	return loaded;
}