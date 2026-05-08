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
#include "str_util.h"
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

	cheat_rec_t(const cheat_rec_t& other)
	{
		memcpy(this->name, other.name, sizeof(other.name));
		this->enabled = other.enabled;
		this->cheatSize = other.cheatSize;
		if (other.cheatData)
		{
			this->cheatData = new char [this->cheatSize];
			memcpy(this->cheatData, other.cheatData, this->cheatSize);
		}
		else
		{
			this->cheatData = nullptr;
		}
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

#define CHEAT_SIZE (128*16) // 128 codes max

static int iSelectedEntry = 0;
static int iFirstEntry = 0;
static int loaded = 0;
static int cheat_unit_size = 16;
static int cheat_max_active = 128;

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

static int cheat_zip_validator(const char *path, void *ctx)
{
	mz_zip_archive *z = (mz_zip_archive *)ctx;
	memset(z, 0, sizeof(mz_zip_archive));
	return mz_zip_reader_init_file(z, path, 0);
}

void cheats_init_arcade(int unit_size, int max_active)
{
	cheats.clear();
	loaded = 0;
	cheat_unit_size = unit_size > 0 ? unit_size : 16;
	cheat_max_active = max_active > 0 ? max_active : 128;
	if ((cheat_max_active * cheat_unit_size) > CHEAT_SIZE)
	{
		cheat_max_active = CHEAT_SIZE / cheat_unit_size;
	}

	cheat_zip[0] = 0;
}

void cheats_add_arcade(const char *name, const char *cheatData, int cheatSize)
{
	cheat_rec_t cheat = {};

	if ((cheatSize % cheat_unit_size) != 0)
	{
		printf("Arcade cheat \'%s\' has incorrect length %d -> skipping.\n", name, cheatSize);
		return;
	}

	strcpyz(cheat.name, name);
	cheat.cheatSize = cheatSize;
	cheat.cheatData = new char [cheatSize];
	memcpy(cheat.cheatData, cheatData, cheatSize);
	cheats.push_back(cheat);
}

void cheats_finalize_arcade()
{
	printf("MRA cheats: %d\n", cheats_available());
	cheats_scan(SCANF_INIT);
}


void cheats_init(const char *rom_path, uint32_t romcrc)
{
	cheats.clear();
	loaded = 0;
	cheat_unit_size = 16;
	cheat_max_active = 128;
	cheat_zip[0] = 0;

	// reset cheats
	if (!is_n64())
	{
		user_io_set_index(255);
		user_io_set_download(1);
		user_io_file_tx_data((const uint8_t*)&loaded, 2);
		user_io_set_download(0);
	}

	char core_dir[1024];
	snprintf(core_dir, sizeof(core_dir), "%s/cheats/%s", getRootDir(), CoreName2);

	const char *pcecd_dir = NULL;
	char pcecd_cheats_dir[1024];
	if (pcecd_using_cd())
	{
		snprintf(pcecd_cheats_dir, sizeof(pcecd_cheats_dir), "%s/cheats/%sCD", getRootDir(), CoreName2);
		pcecd_dir = pcecd_cheats_dir;
	}

	mz_zip_archive _z = {};
	gameAssetValidator validator = { cheat_zip_validator, &_z };

	if (!findGameAsset(cheat_zip, sizeof(cheat_zip), rom_path, romcrc, ".zip", core_dir, pcecd_dir, &validator))
	{
		printf("no cheat file found\n");
		return;
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

	loaded = pos / cheat_unit_size;
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
		cheats[iSelectedEntry].enabled = false;
		changedCheats = true;
	}
	else
	{
		/* enabled cheat, load data */
		static char filename[1024];
		fileTYPE f = {};

		/* lazy load cheat data */
		if (cheats[iSelectedEntry].cheatData == NULL)
		{
			snprintf(filename, sizeof(filename), "%s/%s", cheat_zip, cheats[iSelectedEntry].name);
			if (FileOpen(&f, filename))
			{
				int len = f.size;
				if (!len || (len % cheat_unit_size))
				{
					printf("Cheat file %s has incorrect length %d -> skipping.\n", filename, len);
				}
				else if (((len / cheat_unit_size) + cheats_loaded()) <= cheat_max_active)
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

		if (cheats[iSelectedEntry].cheatData)
		{
			cheats[iSelectedEntry].enabled = true;
			changedCheats = true;
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
