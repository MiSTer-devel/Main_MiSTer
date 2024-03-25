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
#include "miniz.h"
#include "osd.h"
#include "cheats.h"
#include "support.h"

struct cheat_rec_t
{
	bool enabled;
	char name[256];
	int cheatSize;
	char *cheatData;

	cheat_rec_t()
	{
		this->enabled = false;
		this->cheatData = NULL;
		this->cheatSize = 0;
		memset(name, 0, sizeof(name));
	}

	~cheat_rec_t()
	{
		if (this->cheatData)
		{
			delete[] this->cheatData;
		}
	}
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

static int find_by_crc(uint32_t romcrc)
{
	if (!romcrc) return 0;

	sprintf(cheat_zip, "%s/cheats/%s", getRootDir(), CoreName2);
	DIR *d = opendir(cheat_zip);
	if (!d)
	{
		printf("Couldn't open dir: %s\n", cheat_zip);
		return 0;
	}

	struct dirent *de;
	while ((de = readdir(d)))
	{
		if (de->d_type == DT_REG)
		{
			int len = strlen(de->d_name);
			if (len >= 14 && de->d_name[len - 14] == '[' && !strcasecmp(de->d_name + len - 5, "].zip"))
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

static int find_in_same_dir(const char *name)
{
	sprintf(cheat_zip, "%s/%s", getRootDir(), name);
	char *p = strrchr(cheat_zip, '/'); //impossible to fail
	*p = 0;

	DIR *d = opendir(cheat_zip);
	if (!d)
	{
		printf("Couldn't open dir: %s\n", cheat_zip);
		return 0;
	}

	struct dirent *de;
	while ((de = readdir(d)))
	{
		if (de->d_type == DT_REG)
		{
			int len = strlen(de->d_name);
			if (len >= 4 && !strcasecmp(de->d_name + len - 4, ".zip"))
			{
				strcat(cheat_zip, "/");
				strcat(cheat_zip, de->d_name);
				closedir(d);
				return 1;
			}
		}
	}

	closedir(d);
	return 0;
}


bool cheat_init_psx(mz_zip_archive* _z, const char *rom_path)
{
	// lookup based on file name
	const char *rom_name = strrchr(rom_path, '/');
	if (rom_name)
	{
		sprintf(cheat_zip, "%s/cheats/%s%s", getRootDir(), CoreName2, rom_name);
		char *p = strrchr(cheat_zip, '.');
		if (p) *p = 0;
		strcat(cheat_zip, ".zip");
		printf("Trying cheat file: %s\n", cheat_zip);

		memset(_z, 0, sizeof(mz_zip_archive));
		if (mz_zip_reader_init_file(_z, cheat_zip, 0)) return true;
	}

	// lookup based on game ID
	const char *game_id = psx_get_game_id();
	if (game_id && game_id[0])
	{
		sprintf(cheat_zip, "%s/cheats/%s/%s.zip", getRootDir(), CoreName2, psx_get_game_id());
		printf("Trying cheat file: %s\n", cheat_zip);
		memset(_z, 0, sizeof(mz_zip_archive));
		if (mz_zip_reader_init_file(_z, cheat_zip, 0)) return true;
	}

	return false;
}

void cheats_init(const char *rom_path, uint32_t romcrc)
{
	cheats.clear();
	loaded = 0;
	cheat_zip[0] = 0;

	// reset cheats
	if (!is_n64())
	{
		user_io_set_index(255);
		user_io_set_download(1);
		user_io_file_tx_data((const uint8_t*)&loaded, 2);
		user_io_set_download(0);
	}

	if (!strcasestr(rom_path, ".zip"))
	{
		sprintf(cheat_zip, "%s/%s", getRootDir(), rom_path);
		char *p = strrchr(cheat_zip, '.');
		if (p) *p = 0;
		strcat(cheat_zip, ".zip");
	}

	mz_zip_archive _z = {};

	if (is_psx() && !mz_zip_reader_init_file(&_z, cheat_zip, 0))
	{
		if (!cheat_init_psx(&_z, rom_path))
		{
			printf("no cheat file found\n");
			return;
		}
	}
	else if (!mz_zip_reader_init_file(&_z, cheat_zip, 0))
	{
		memset(&_z, 0, sizeof(_z));
		if (!(pcecd_using_cd() || is_megacd()) || !find_in_same_dir(rom_path) || !mz_zip_reader_init_file(&_z, cheat_zip, 0))
		{
			memset(&_z, 0, sizeof(_z));
			const char *rom_name = strrchr(rom_path, '/');
			if (rom_name)
			{
				sprintf(cheat_zip, "%s/cheats/%s%s%s", getRootDir(), CoreName2, pcecd_using_cd() ? "CD" : "", rom_name);
				char *p = strrchr(cheat_zip, '.');
				if (p) *p = 0;
				if (pcecd_using_cd() || is_megacd()) strcat(cheat_zip, " []");
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

		if (mode == SCANF_END || (mode == SCANF_PREV && iSelectedEntry <= 0))
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
			else
			{
				// jump to first visible item
				iFirstEntry = 0;
				iSelectedEntry = 0;
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

	static char s[256 + 4];

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
	static uint8_t buff[CHEAT_SIZE];
	int pos = 0;

	for (int i = 0; i < cheats_available(); i++)
	{
		if (cheats[i].enabled)
		{
			if (cheats[i].cheatData)
			{
				memcpy(&buff[pos], cheats[i].cheatData, cheats[i].cheatSize);
				pos += cheats[i].cheatSize;
			}
			else
			{
				printf("Consistency error, memory for cheat not allocated, but cheat was enabled -> disable.\n");
				cheats[i].cheatSize = 0;
				cheats[i].enabled = false;
			}
		}
	}

	loaded = pos / 16;
	printf("Cheat codes: %d\n", loaded);

	if (is_n64())
	{
		n64_cheats_send(buff, loaded);
	}
	else
	{
		user_io_set_index(255);
		user_io_set_download(1);
		user_io_file_tx_data(buff, pos ? pos : 2);
		user_io_set_download(0);
	}
}

void cheats_toggle()
{
	bool changedCheats = false;

	if (cheats[iSelectedEntry].enabled == true)
	{
		/* disabled loaded cheat, free data */
		if (cheats[iSelectedEntry].cheatData)
		{
			delete[] cheats[iSelectedEntry].cheatData;
			cheats[iSelectedEntry].cheatData = NULL;
		}

		cheats[iSelectedEntry].enabled = false;
		cheats[iSelectedEntry].cheatSize = 0;
		changedCheats = true;
	}
	else
	{
		/* enabled cheat, load data */
		static char filename[1024];
		fileTYPE f = {};

		if (cheats[iSelectedEntry].cheatData)
		{
			printf("Consistency error, memory for cheat already allocated -> cleanup.\n");
			delete[] cheats[iSelectedEntry].cheatData;
			cheats[iSelectedEntry].cheatData = NULL;
			cheats[iSelectedEntry].cheatSize = 0;
		}

		snprintf(filename, sizeof(filename), "%s/%s", cheat_zip, cheats[iSelectedEntry].name);
		if (FileOpen(&f, filename))
		{
			int len = f.size;
			if (!len || (len & 15))
			{
				printf("Cheat file %s has incorrect length %d -> skipping.\n", filename, len);
			}
			else if ((len + cheats_loaded() * 16) <= CHEAT_SIZE)
			{
				cheats[iSelectedEntry].cheatData = new char[len];
				if (cheats[iSelectedEntry].cheatData)
				{
					if (FileReadAdv(&f, cheats[iSelectedEntry].cheatData, len) == len)
					{
						cheats[iSelectedEntry].cheatSize = len;
						cheats[iSelectedEntry].enabled = true;
						changedCheats = true;
					}
					else
					{
						printf("Cannot read cheat file %s.\n", filename);
						delete[] cheats[iSelectedEntry].cheatData;
						cheats[iSelectedEntry].cheatData = NULL;
						cheats[iSelectedEntry].cheatSize = 0;
					}
				}
				else
				{
					printf("Could not allocate required memory (%d) for cheat file %s.\n", len, filename);
				}
			}
			else
			{
				printf("No more room in current selection for cheat file %s.\n", filename);
			}
			FileClose(&f);
		}
		else
		{
			printf("Cannot open cheat file %s.\n", filename);
		}
	}

	if (changedCheats)
	{
		cheats_send();
	}
}

int cheats_loaded()
{
	return loaded;
}