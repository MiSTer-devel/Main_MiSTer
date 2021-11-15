#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <limits.h>
#include <ctype.h>

#include "file_io.h"
#include "user_io.h"
#include "osd.h"
#include "cfg.h"
#include "recent.h"

#define RECENT_MAX 16

struct recent_rec_t
{
	char dir[1024];
	char name[256];
	char label[256];
};

static recent_rec_t recents[RECENT_MAX];
static char ena[RECENT_MAX];

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
	sprintf(str, "cores_recent.cfg");
	if (idx >= 0) sprintf(str, "%s_recent_%d.cfg", user_io_get_core_name(), idx);
	return str;
}

static const char* recent_path(char* dir, char* name)
{
	static char path[1024];
	if(strlen(dir)) snprintf(path, sizeof(path), "%s/%s", dir, name);
	else snprintf(path, sizeof(path), "%s", name);
	return path;
}

static void recent_load(int idx)
{
	// initialize recent to empty strings
	memset(recents, 0, sizeof(recents));

	// load the config file into memory
	FileLoadConfig(recent_create_config_name(idx), recents, sizeof(recents));

	for (numlast = 0; numlast < (int)(sizeof(recents)/sizeof(recents[0])) && strlen(recents[numlast].name); numlast++) {}

	// check the items
	for (int i = 0; i < recent_available(); i++)
	{
		ena[i] = FileExists(recent_path(recents[i].dir, recents[i].name));
		if (idx >= 0 && is_neogeo() && !ena[i]) ena[i] = PathIsDir(recent_path(recents[i].dir, recents[i].name));
	}
}

int recent_init(int idx)
{
	if (!cfg.recents) return 0;

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

		if (mode == SCANF_END || (mode == SCANF_PREV && iSelectedEntry <= 0))
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

void recent_scroll_name()
{
	// this function is called periodically when file selection window is displayed
	// it checks if predefined period of time has elapsed and scrolls the name if necessary
	int len;
	int max_len;
	static char name[256 + 4];

	// don't scroll if the file doesn't exist
	if (!ena[iSelectedEntry]) return;

	name[0] = 32;
	strcpy(name + 1, recents[iSelectedEntry].label);

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
			char* name = recents[k].label;
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

			d = ena[k] ? 0 : 1;
		}
		else
		{
			memset(s, ' ', 32);
		}

		OsdWriteOffset(i, s, i == (iSelectedEntry - iFirstEntry) && recent_available(), d, 0, leftchar);
	}
}

int recent_select(char *dir, char *path, char *label)
{
	// copy directory and file name over
	dir[0] = 0;
	path[0] = 0;

	if (!recent_available()) return 0;

	if (strlen(recents[iSelectedEntry].name))
	{
		strcpy(dir, recents[iSelectedEntry].dir);
		strcpy(path, recent_path(recents[iSelectedEntry].dir, recents[iSelectedEntry].name));
		strcpy(label, recents[iSelectedEntry].label);
	}

	return ena[iSelectedEntry];
}

void recent_update(char* dir, char* path, char* label, int idx)
{
	if (!cfg.recents || !strlen(path)) return;

	// separate the path into directory and filename
	char* name = strrchr(path, '/');
	if (name) name++; else name = path;

	// load the current state.  this is necessary because we may have started a ROM from multiple sources
	recent_load(idx);

	// update the selection
	int indexToErase = RECENT_MAX - 1;
	recent_rec_t rec = {};
	strncpy(rec.dir, dir, sizeof(rec.dir)-1);
	strncpy(rec.name, name, sizeof(rec.name)-1);
	strncpy(rec.label, label ? label : name, sizeof(rec.label)-1);

	for (unsigned i = 0; i < sizeof(recents)/sizeof(recents[0]); i++)
	{
		if (!strcmp(recents[i].dir, dir) && !strcmp(recents[i].name, name))
		{
			indexToErase = i;
			break;
		}
	}

	if(indexToErase) memmove(recents + 1, recents, sizeof(recents[0])*indexToErase);
	memcpy(recents, &rec, sizeof(recents[0]));

	// store the config file to storage
	FileSaveConfig(recent_create_config_name(idx), recents, sizeof(recents));
}

void recent_clear(int idx)
{
	memset(recents, 0, sizeof(recents));

	// store the config file to storage
	FileSaveConfig(recent_create_config_name(idx), recents, sizeof(recents));
}
