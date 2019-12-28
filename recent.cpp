#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <limits.h>
#include <ctype.h>
#include <dirent.h>
#include <vector>
#include <string>

#include "hardware.h"
#include "file_io.h"
#include "user_io.h"
#include "fpga_io.h"
#include "miniz_zip.h"
#include "osd.h"
#include "recent.h"
#include "support.h"

#define RECENT_MAX 16

struct recent_rec_t
{
	char dir[1024];
	char name[256];
};

struct display_name_t
{
	char name[256];
};

typedef std::vector<recent_rec_t> RecentVector;
static RecentVector recents(RECENT_MAX);
typedef std::vector<display_name_t> RecentDisplayNameVector;
static RecentDisplayNameVector displaynames(RECENT_MAX);

static int numlast = 0;

static int iSelectedEntry = 0;
static int iFirstEntry = 0;

static int recent_available()
{
	return numlast;
}

static char* recent_create_config_name(int idx)
{
	static char str[256];
	sprintf(str, "%s_recent_%d.cfg", user_io_get_core_name(), idx);
	return str;
}

static void recent_load(int idx)
{
	// initialize recent to empty strings
	memset(recents.data(), 0, recents.size() * sizeof(recent_rec_t));

	// load the config file into memory
	FileLoadConfig(recent_create_config_name(idx), recents.data(), recents.size() * sizeof(recent_rec_t));

	for (numlast = 0; numlast < (int)recents.size() && strlen(recents[numlast].name); numlast++) {}

	// init display names to file names
	for (int i = 0; i < recent_available(); i++) memcpy(displaynames[i].name, recents[i].name, sizeof(displaynames[i].name));

	if (is_neogeo_core()) {
		for (int i = 0; i < recent_available(); i++) {
			// update display names for neogeo neo files
			char* altname = neogeo_get_altname(recents[i].dir, recents[i].name, recents[i].name);
			if (altname) strcpy(displaynames[i].name, altname);
		}
	}
}

int recent_init(int idx)
{
	recent_load(idx);
	recent_scan(SCANF_INIT);
	return recent_available();
}

void recent_scan(int mode)
{
	if (mode == SCANF_INIT)
	{
		iFirstEntry = 0;
		iSelectedEntry = 0;
	}
	else
	{
		if (!recent_available()) return;

		if (mode == SCANF_END)
		{
			iSelectedEntry = recent_available() - 1;
			iFirstEntry = iSelectedEntry - OsdGetSize() + 1;
			if (iFirstEntry < 0) iFirstEntry = 0;
		}
		else if (mode == SCANF_NEXT)
		{
			if (iSelectedEntry + 1 < recent_available()) // scroll within visible items
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
				if (iSelectedEntry >= recent_available()) iSelectedEntry = recent_available() - 1;
			}
			else
			{
				iSelectedEntry += OsdGetSize();
				iFirstEntry += OsdGetSize();
				if (iSelectedEntry >= recent_available())
				{
					iSelectedEntry = recent_available() - 1;
					iFirstEntry = iSelectedEntry - OsdGetSize() + 1;
					if (iFirstEntry < 0) iFirstEntry = 0;
				}
				else if (iFirstEntry + OsdGetSize() > recent_available())
				{
					iFirstEntry = recent_available() - OsdGetSize();
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

static const char* recent_path(char* dir, char* name)
{
	static std::string fullname;
	fullname = dir;
	fullname += '/';
	fullname += name;
	return fullname.c_str();
}

void recent_scroll_name()
{
	// this function is called periodically when file selection window is displayed
	// it checks if predefined period of time has elapsed and scrolls the name if necessary
	int len;
	int max_len;
	static char name[256 + 4];

	// don't scroll if the file doesn't exist
	if (!FileExists(recent_path(recents[iSelectedEntry].dir, recents[iSelectedEntry].name))) return;

	name[0] = 32;
	strcpy(name + 1, displaynames[iSelectedEntry].name);

	len = strlen(name); // get name length

	max_len = 30;
	ScrollText(iSelectedEntry - iFirstEntry, name, 1, len, max_len, 1);
}

void recent_print()
{
	int k;
	int len;

	static char s[256+4];

	ScrollReset();

	for (int i = 0; i < OsdGetSize(); i++)
	{
		char leftchar = 0;
		unsigned char d = 1;
		if (i < recent_available())
		{
			k = iFirstEntry + i;

			s[0] = 32;
			char* name = displaynames[k].name;
			strcpy(s + 1, name);

			len = strlen(s); // get name length

			s[len] = 0;

			if (len > 28)
			{
				len = 27; // trim display length if longer than 30 characters
				s[28] = 22;
			}

			s[29] = 0;

			if (!i && k) leftchar = 17;
			if ((i == OsdGetSize() - 1) && (k < recent_available() - 1)) leftchar = 16;

			// check if file exists
			d = FileExists(recent_path(recents[k].dir, recents[k].name)) ? 0 : 1;
		}
		else
		{
			memset(s, ' ', 32);
		}

		OsdWriteOffset(i, s, i == (iSelectedEntry - iFirstEntry) && recent_available(), d, 0, leftchar);
	}
}

int recent_select(char *dir, char *path)
{
	// copy directory and file name over
	dir[0] = 0;
	path[0] = 0;

	if (strlen(recents[iSelectedEntry].name))
	{
		strcpy(dir, recents[iSelectedEntry].dir);
		strcpy(path, dir);
		strcat(path, "/");
		strcat(path, recents[iSelectedEntry].name);
	}

	if (!FileExists(path)) return 0;
	else return recent_available();
}

void recent_update(char* dir, char* path, int idx)
{
	if (!strlen(path)) return;

	if (is_neogeo_core())
	{
		// only support neo files for now to simplify name parsing and locating files in recent files menu
		char* ext = strrchr(path, '.');
		if (!ext || strcmp(ext, ".neo")) return;
	}

	// separate the path into directory and filename
	char* name = strrchr(path, '/');
	if (name) name++; else name = path;

	// load the current state.  this is necessary because we may have started a ROM from multiple sources
	recent_load(idx);

	// update the selection
	int indexToErase = RECENT_MAX - 1;
	recent_rec_t rec;
	strcpy(rec.dir, dir);
	strcpy(rec.name, name);

	for (unsigned i = 0; i < recents.size(); i++)
	{
		if (!strcmp(recents[i].dir, dir) && !strcmp(recents[i].name, name))
		{
			indexToErase = i;
			break;
		}
	}
	recents.erase(recents.begin() + indexToErase);
	recents.insert(recents.begin(), rec);

	// store the config file to storage
	FileSaveConfig(recent_create_config_name(idx), recents.data(), recents.size() * sizeof(recent_rec_t));
}
