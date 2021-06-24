/*
 * Copyright (c) 2020, Alexey Melnikov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/statvfs.h>
#include <time.h>

#include <map>
#include <string>
#include <vector>

#include "../../hardware.h"
#include "../../user_io.h"
#include "../../file_io.h"
#include "../../cfg.h"
#include "../../shmem.h"

#define SHMEM_ADDR      0x300CE000
#define SHMEM_SIZE      0x2000
static uint8_t *shmem = 0;

#define REQUEST_FLG     0
#define REQUEST_BUFFER  4
#define HDRLEN          8
#define DATA_BUFFER     (REQUEST_BUFFER+66)

//#define DEBUG

#ifdef DEBUG
	#define dbg_print printf
	#define dbg_hexdump hexdump
#else
	#define dbg_print(x,...) void()
	#define dbg_hexdump(x,...) void()
#endif

enum AL_SUBFUNCTIONS {
	AL_RMDIR      = 0x01,
	AL_MKDIR      = 0x03,
	AL_CHDIR      = 0x05,
	AL_CLOSE      = 0x06,
	AL_READ       = 0x08,
	AL_WRITE      = 0x09,
	AL_LOCK       = 0x0A,
	AL_DISKSPACE  = 0x0C,
	AL_SETATTR    = 0x0E,
	AL_GETATTR    = 0x0F,
	AL_RENAME     = 0x11,
	AL_DELETE     = 0x13,
	AL_OPEN       = 0x16,
	AL_CREATE     = 0x17,
	AL_FINDFIRST  = 0x1B,
	AL_FINDNEXT   = 0x1C,
	AL_SKFMEND    = 0x21,
	AL_QUALIFY    = 0x23,
	AL_SPOPEN     = 0x2E,
	AL_UNKNOWN    = 0xFF
};

#define FAT_RO   1
#define FAT_HID  2
#define FAT_SYS  4
#define FAT_VOL  8
#define FAT_DIR 16
#define FAT_ARC 32
#define FAT_DEV 64

static char basepath[1024] = {};
static int baselen = 0;

struct dir_item_t
{
	dirent64 de;
	stat64 st;
};

struct lock
{
	std::string path;
	std::vector<dir_item_t> dir_items;
};

static std::map<short, lock> locks;
static short next_key = 0;

static short get_key()
{
	short key;

	do
	{
		next_key++;
		if (!next_key) next_key++;
		key = next_key;

	} while (locks.find(key) != locks.end());

	return key;
}

static short get_lock(const char* path)
{
	for (const auto &pair : locks)
	{
		if (pair.second.path == path)
		{
			dbg_print("! path %s has lock: %d\n", path, pair.first);
			return pair.first;
		}
	}
	return 0;
}

static short add_lock(const char* path)
{
	short key = get_lock(path);
	if (key)
	{
		locks[key].dir_items.clear();
	}
	else
	{
		key = get_key();
		locks[key] = { path, {} };
		dbg_print("+ add lock: %d, %s\n", key, path);
	}
	return key;
}

static std::map<short, fileTYPE> open_file_handles;
static short next_fp = 1;

static short get_fp()
{
	short fp;

	do
	{
		next_fp++;
		if (!next_fp) next_fp++;
		fp = next_fp;

	} while (open_file_handles.find(fp) != open_file_handles.end());

	return fp;
}

static char* find_path(const char *name)
{
	dbg_print("find_path(%s)\n", name);

	static char str[1024] = {};
	const char* p = strchr(name, ':');
	if (p)
	{
		name = p + 1;
	}

	strcpy(str, basepath);
	if (strlen(name))
	{
		strcat(str, "/");
		strcat(str, name);
	}

	char *bsl;
	while ((bsl = strchr(str, '\\'))) *bsl = '/';

	dbg_print("Requested path: %s\n", str);

	if (strncmp(basepath, str, baselen))
	{
		dbg_print("Not belonging to shared folder\n");
		str[0] = 0;
	}
	else if (str[baselen] && str[baselen] != '/')
	{
		dbg_print("No / after root\n");
		str[0] = 0;
	}
	else if (str[baselen])
	{
		char *cur = str + baselen;
		while (*cur)
		{
			cur++;
			char *next = strchr(cur, '/');
			if (!next) next = cur + strlen(cur);
			int len = next - cur;

			if (!len && !*next) break;

			if (!len || !strncmp(cur, ".", len))
			{
				strcpy(cur, next + 1);
				cur--;
				continue;
			}

			if (!strncmp(cur, "..", len))
			{
				cur -= 2;
				while (*cur != '/') cur--;

				if (cur < str + baselen)
				{
					printf("Going above root\n");
					str[0] = 0;
					break;
				}

				// collapse the component
				strcpy(cur, next);
				continue;
			}

			cur = next;
		}
	}

	// remove trailing /
	int len = strlen(str);
	if (len && str[len - 1] == '/') str[len - 1] = 0;

	dbg_print("Converted path: %s\n", str);

	if (str[0])
	{
		char *p = strrchr(str, '/');
		if (!p) str[0] = 0;
		else
		{
			*p = 0;
			if (!PathIsDir(str, 0)) str[0] = 0;
			else *p = '/';
		}
	}

	dbg_print("returned path: %s\n", str);
	return str;
}

static void __attribute__((noinline)) memcpyb(void *dst, const void *src, int len)
{
	char *d = (char*)dst;
	char *s = (char*)src;
	while (len--) *d++ = *s++;
}


// | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 | Directory Attribute Flags
//   |   |   |   |   |   |   |   \-- 1 = read only
//   |   |   |   |   |   |   \--- 1 = hidden
//   |   |   |   |   |   \---- 1 = system
//   |   |   |   |   \----- 1 = volume label(exclusive)
//   |   |   |   \------ 1 = subdirectory
//   |   |   \------- 1 = archive
//   \---+-------unused

static uint32_t get_attr(char *path, uint16_t *time, uint16_t *date, uint32_t *size)
{
	if (time) *time = 0;
	if (date) *date = 0;
	if (size) *size = 0;

	stat64 *st = getPathStat(path);
	if (!st) return 0;

	tm *t = localtime(&st->st_mtime);
	if (time) *time = (t->tm_sec / 2) | (t->tm_min << 5) | (t->tm_hour << 11);
	if (date) *date = t->tm_mday | ((t->tm_mon + 1) << 5) | ((t->tm_year - 80) << 9);
	if (size) *size = st->st_size;
	return st->st_mode;
}

static void name83(const char *src, char *dst)
{
	int namelen = 0;
	int extlen = 0;

	const char *p = strrchr(src, '/');
	if (p) src = p + 1;

	if (!strcmp(src, ".") || !strcmp(src, ".."))
	{
		namelen = strlen(src);
	}
	else
	{
		p = strrchr(src, '.');
		if (!p) namelen = strlen(src);
		else
		{
			namelen = p - src;
			extlen = strlen(src) - namelen - 1;
		}
	}


	char ext[4] = { ' ', ' ', ' ', 0 };
	if (p) memcpy(ext, p + 1, extlen);
	for (int i = 0; i < namelen; i++) dst[i] = toupper(src[i]);
	while (namelen < 8) dst[namelen++] = ' ';
	for (int i = 0; i < 3; i++) dst[8 + i] = toupper(ext[i]);
}

static int cmp_name(const char *name, const char *flt)
{
	int namelen = 0;
	int extlen = 0;

	const char *ext = strrchr(name, '.');
	if (!ext)
	{
		namelen = strlen(name);
		ext = name + namelen;
	}
	else
	{
		namelen = ext - name;
		ext++;
		extlen = strlen(ext);
	}

	if (namelen > 8 || extlen > 3) return 0;

	char testname[16];
	char fltname[16];
	name83(name, testname);
	name83(flt, fltname);

	testname[11] = 0;
	fltname[11] = 0;

	char *cmpname = fltname;
	char *cmpend = fltname + 8;
	char *cur = testname;

	while (cmpname < cmpend)
	{
		if (*cmpname == '?')
		{
			cmpname++;
			cur++;
		}
		else if (*cmpname == '*')
		{
			break;
		}
		else if (*cmpname++ != *cur++)
		{
			return 0;
		}
	}

	cmpname = fltname + 8;
	cmpend = fltname + 11;
	cur = testname + 8;

	while (cmpname < cmpend)
	{
		if (*cmpname == '?')
		{
			cmpname++;
			cur++;
		}
		else if (*cmpname == '*')
		{
			break;
		}
		else if (*cmpname++ != *cur++)
		{
			return 0;
		}
	}

	return 1;
}

static int process_request(void *reqres_buffer)
{
	static char str[1024];
	int len = *(unsigned short*)reqres_buffer;
	char func = ((char*)reqres_buffer)[4];

	dbg_hexdump(reqres_buffer, 8, 0);
	if(func != AL_WRITE) dbg_hexdump((char*)reqres_buffer+8, len, 8);

	short res = -1;
	short reslen = 0;

	unsigned short idx = 0;
	short key = 0;

	if (!baselen)
	{
		if (strlen(cfg.shared_folder))
		{
			if (cfg.shared_folder[0] == '/') strcpy(basepath, cfg.shared_folder);
			else
			{
				strcpy(basepath, HomeDir());
				strcat(basepath, "/");
				strcat(basepath, cfg.shared_folder);
			}
		}
		else
		{
			strcpy(basepath, HomeDir());
			strcat(basepath, "/shared");
		}

		baselen = strlen(basepath);
		if (baselen && basepath[baselen - 1] == '/')
		{
			basepath[baselen - 1] = 0;
			baselen--;
		}

		if (baselen) FileCreatePath(basepath);
	}

	char *buf = ((char*)reqres_buffer) + 8;
	buf[len] = 0;

	switch (func)
	{
	case AL_RMDIR:
	{
		dbg_print("> AL_RMDIR\n");

		char *path = find_path(buf);
		if (!*path)
		{
			res = 3;
			break;
		}

		if(!DirDelete(path))
		{
			dbg_print("Cannot delete dir %s\n", path);
			res = 29;
			break;
		}

		res = 0;
	}
	break;

	case AL_MKDIR:
	{
		dbg_print("> AL_MKDIR\n");

		char *path = find_path(buf);
		if (!*path)
		{
			res = 3;
			break;
		}

		if (!FileCreatePath(path))
		{
			res = 29;
			break;
		}

		res = 0;
	}
	break;

	case AL_CHDIR:
	{
		dbg_print("> AL_CHDIR\n");

		char *path = find_path(buf);
		if (!*path || !PathIsDir(path))
		{
			res = 3;
			break;
		}

		res = 0;
	}
	break;

	case AL_OPEN:
	{
		dbg_print("> AL_OPEN\n");

		uint16_t attr = *(uint16_t *)buf;
		char *path = find_path(buf + 6);
		if (!*path)
		{
			res = 3;
			break;
		}

		if (!FileExists(path, 0))
		{
			res = 2;
			break;
		}

		int mode = attr & 3;

		short key = get_fp();
		open_file_handles[key] = {};

		if (!FileOpenEx(&open_file_handles[key], path, mode, 0, 0))
		{
			open_file_handles.erase(key);
			res = 5;
			break;
		}

		dbg_print("opened handle: %d\n", key);

		*buf++ = 0;
		name83(path, buf);
		buf += 11;
		get_attr(path, (uint16_t*)buf, (uint16_t*)(buf + 2), (uint32_t*)(buf + 4));
		buf += 8;
		*buf++ = key;
		*buf++ = key >> 8;
		*buf++ = 0;
		*buf++ = 0;
		*buf++ = mode;

		/*
	   Bit 0-2 access mode
		   000=read
		   001=write
		   010=read/write
	   4-6 sharing mode
		   000=compatibility
		   001=deny read/write
		   010=deny write
		   011=deny read
		   100=deny none
		13 critical error handling
		   0=execute INT 24
		   1=return error code
		14 buffering
		   0=buffer writes
		   1=don't buffer writes
		15 1=FCB SFT
		*/
		reslen = 25;
		res = 0;
	}
	break;

	case AL_CREATE:
	{
		dbg_print("> AL_CREATE\n");

		char *path = find_path(buf + 6);
		if (!*path)
		{
			res = 3;
			break;
		}

		int mode = O_RDWR | O_CREAT | O_TRUNC;

		short key = get_fp();
		open_file_handles[key] = {};

		if (!FileOpenEx(&open_file_handles[key], path, mode, 0, 0))
		{
			open_file_handles.erase(key);
			res = 5;
			break;
		}

		dbg_print("opened handle: %d\n", key);

		*buf++ = 0;
		name83(path, buf);
		buf += 11;
		get_attr(path, (uint16_t*)buf, (uint16_t*)(buf + 2), (uint32_t*)(buf + 4));
		buf += 8;
		*buf++ = key;
		*buf++ = key >> 8;
		*buf++ = 0;
		*buf++ = 0;
		*buf++ = 2;

		reslen = 25;
		res = 0;
	}
	break;

	case AL_SPOPEN:
	{
		dbg_print("> AL_SPOPEN\n");

		/* actioncode contains instructions about how to behave...
		 *   high nibble = action if file does NOT exist:
		 *     0000 fail
		 *     0001 create
		 *   low nibble = action if file DOES exist
		 *     0000 fail
		 *     0001 open
		 *     0010 truncate/open */
		uint16_t attr = *(uint16_t *)buf;
		uint16_t actioncode = *(uint16_t *)(buf + 2);
		uint16_t openmode = *(uint16_t *)(buf + 4);

		char *path = find_path(buf + 6);
		if (!*path)
		{
			res = 3;
			break;
		}

		int mode = openmode & 0x3;
		uint16_t spopres = 0;

		if (FileExists(path, 0))
		{
			if ((actioncode & 0xF) == 1)
			{
				spopres = 1;
			}
			else if ((actioncode & 0xF) == 2)
			{
				mode = O_RDWR | O_TRUNC;
				spopres = 3;
			}
			else
			{
				res = 5;
				break;
			}
		}
		else
		{
			// some copiers create an empty file with hidden attribute and then fail if found non-hidden file
			// so prohibit to create a hidden file.
			if ((actioncode & 0xF0) == 0x10 && !(attr & FAT_HID))
			{
				mode = O_RDWR | O_CREAT;
				spopres = 2;
			}
			else
			{
				res = 2;
				break;
			}
		}

		key = get_fp();
		open_file_handles[key] = {};

		if (!FileOpenEx(&open_file_handles[key], path, mode, 0, 0))
		{
			open_file_handles.erase(key);
			res = 5;
			break;
		}

		dbg_print("opened handle: %d\n", key);

		*buf++ = 0;
		name83(path, buf);
		buf += 11;
		get_attr(path, (uint16_t*)buf, (uint16_t*)(buf + 2), (uint32_t*)(buf + 4)); // 12 14 16
		buf += 8;
		*buf++ = key;
		*buf++ = key >> 8;
		*buf++ = spopres;
		*buf++ = spopres >> 8;
		*buf++ = openmode & 0x7f;

		reslen = 25;
		res = 0;
	}
	break;

	case AL_CLOSE:
	{
		dbg_print("> AL_CLOSE\n");

		key = *(short *)buf;
		if (open_file_handles.find(key) != open_file_handles.end())
		{
			FileClose(&open_file_handles[key]);
			open_file_handles.erase(key);

			dbg_print("closed handle: %d\n", key);
		}

		reslen = 0;
		res = 0;
	}
	break;

	case AL_READ:
	{
		dbg_print("> AL_READ\n");

		key = buf[4] | (buf[5] << 8);
		if (open_file_handles.find(key) == open_file_handles.end())
		{
			res = 5;
			break;
		}

		uint32_t off;
		memcpyb(&off, buf, 4);
		uint16_t sz = buf[6] | (buf[7] << 8);
		dbg_print("  read %d bytes at %d\n", sz, off);

		FileSeek(&open_file_handles[key], off, SEEK_SET);

		int read = FileReadAdv(&open_file_handles[key], buf, sz, -1);
		if (read < 0)
		{
			res = 5;
			break;
		}

		dbg_print("  was read %d\n", read);

		reslen = read;
		res = 0;
	}
	break;

	case AL_WRITE:
	{
		dbg_print("> AL_WRITE\n");

		key = buf[4] | (buf[5] << 8);
		if (open_file_handles.find(key) == open_file_handles.end())
		{
			res = 5;
			break;
		}

		uint32_t off;
		memcpyb(&off, buf, 4);
		uint16_t sz = buf[6] | (buf[7] << 8);
		dbg_print("  write %d bytes at %d\n", sz, off);

		FileSeek(&open_file_handles[key], off, SEEK_SET);

		int written = 0;
		if (sz)
		{
			written = FileWriteAdv(&open_file_handles[key], buf + 8, sz);
			if (!written)
			{
				res = 5;
				break;
			}
		}

		dbg_print("  written %d\n", written);

		*buf++ = written;
		*buf++ = written >> 8;

		reslen = 2;
		res = 0;
	}
	break;

	case AL_LOCK:
	{
		dbg_print("> AL_LOCK\n");

		reslen = 0;
		res = 0;
	}
	break;

	case AL_DISKSPACE:
	{
		dbg_print("> AL_DISKSPACE\n");
		uint32_t total = 10;
		uint32_t avail = 1;
		uint32_t spc = 128;
		uint32_t bps = 512;

		struct statvfs st;
		if (!statvfs(getFullPath(basepath), &st))
		{
			uint64_t sz = st.f_bsize * st.f_blocks;
			uint64_t av = st.f_bsize * st.f_bavail;

			total = sz / (bps * spc);
			avail = av / (bps * spc);
		}

		if (total > UINT16_MAX) total = UINT16_MAX;
		if (avail > UINT16_MAX) avail = UINT16_MAX;

		*buf++ = total;
		*buf++ = total >> 8;
		*buf++ = bps;
		*buf++ = bps >> 8;
		*buf++ = avail;
		*buf++ = avail >> 8;

		reslen = 6;
		res = spc;
	}
	break;

	case AL_SETATTR:
	{
		dbg_print("> AL_SETATTR\n");
		reslen = 0;
		res = 0;
	}
	break;

	case AL_GETATTR:
	{
		dbg_print("> AL_GETATTR\n");

		char *path = find_path(buf);
		if (!*path)
		{
			res = 2;
			break;
		}

		uint16_t time, date;
		uint32_t sz;
		uint32_t mode = get_attr(path, &time, &date, &sz);

		if (!mode)
		{
			res = 2;
			break;
		}

		*buf++ = time;
		*buf++ = time >> 8;
		*buf++ = date;
		*buf++ = date >> 8;
		*buf++ = sz;
		*buf++ = sz >> 8;
		*buf++ = sz >> 16;
		*buf++ = sz >> 24;
		*buf++ = (mode & S_IFDIR) ? FAT_DIR : 0;
		*buf++ = 0;

		res = 0;
		reslen = 10;
	}
	break;

	case AL_RENAME:
	{
		dbg_print("> AL_RENAME\n");

		int srclen = (*buf++) & 0xFF;
		char *path = find_path(buf + srclen);
		if (!*path)
		{
			res = 3;
			break;
		}
		strcpy(str, getFullPath(path));

		buf[srclen] = 0;
		path = find_path(buf);
		if (!*path)
		{
			res = 2;
			break;
		}

		if (rename(getFullPath(path), str))
		{
			res = 5;
			break;
		}

		res = 0;
	}
	break;

	case AL_DELETE:
	{
		dbg_print("> AL_DELETE\n");

		char *path = find_path(buf);
		if (!*path)
		{
			res = 3;
			break;
		}

		if(strchr(path, '?') || strchr(path, '*'))
		{
			res = 2;
			break;
		}

		if (!FileDelete(path))
		{
			res = 2;
			break;
		}

		res = 0;
	}
	break;

	case AL_FINDFIRST:
	{
		dbg_print("> AL_FINDFIRST\n");

		char attr = *buf;

		char *path = find_path(buf+1);
		if (!*path)
		{
			res = 0x12;
			break;
		}

		char *flt = strrchr(path, '/');
		if (!*flt)
		{
			res = 0x12;
			break;
		}

		*flt++ = 0;
		key = add_lock(path);

		const char* full_path = getFullPath(path);
		DIR *d = opendir(full_path);
		if (!d)
		{
			locks.erase(key);
			printf("Couldn't open dir: %s\n", full_path);
			res = 0x12;
			break;
		}

		if (attr == 8)
		{
			struct dirent64 de = {};
			strcpy(de.d_name, "MiSTer");
			locks[key].dir_items.push_back({ de, {} });

			*buf++ = 8;
			memcpyb(buf, "MiSTer     ", 11);

			buf += 11;
			*buf++ = 0; *buf++ = 0; // time;
			*buf++ = 0; *buf++ = 0; // date;
			*buf++ = 0; *buf++ = 0; *buf++ = 0; *buf++ = 0; // size;
			*buf++ = key;
			*buf++ = key >> 8;
			*buf++ = 0; *buf++ = 0;

			res = 0;
			reslen = 24;
			break;
		}
		else
		{
			struct dirent64 de, *de2;
			while ((de2 = readdir64(d)))
			{
				if (de2->d_type == DT_REG || (attr & FAT_DIR))
				{
					memcpy(&de, de2, sizeof(dirent64));
					sprintf(str, "%s/%s", path, de.d_name);
					stat64 *st = getPathStat(str);

					if (st && cmp_name(de.d_name, flt))
					{
						name83(de2->d_name, de.d_name);
						de.d_name[11] = 0;
						locks[key].dir_items.push_back({ de, *st });
					}
				}
			}
			closedir(d);
		}
	}
	// fall through

	case AL_FINDNEXT:
	{
		if (func == AL_FINDNEXT)
		{
			dbg_print("> AL_FINDNEXT\n");

			key = *(short *)buf;
			idx = *(short *)(buf+2);
			idx++;

			if (locks.find(key) == locks.end())
			{
				dbg_print("Key %d not found\n", key);
				res = 0x12;
				break;
			}
		}

		if (idx >= locks[key].dir_items.size())
		{
			if (locks.find(key) != locks.end()) locks.erase(key);

			dbg_print("No more items\n");
			res = 0x12;
			break;
		}

		*buf++ = (locks[key].dir_items[idx].de.d_type == DT_DIR) ? FAT_DIR : 0;
		memcpyb(buf, locks[key].dir_items[idx].de.d_name, 11);
		buf += 11;

		tm *t = localtime(&locks[key].dir_items[idx].st.st_mtime);
		uint16_t time = (t->tm_sec / 2) | (t->tm_min << 5) | (t->tm_hour << 11);
		uint16_t date = t->tm_mday | ((t->tm_mon + 1) << 5) | ((t->tm_year - 80) << 9);

		*buf++ = time;
		*buf++ = time >> 8;
		*buf++ = date;
		*buf++ = date >> 8;

		memcpyb(buf, &locks[key].dir_items[idx].st.st_size, 4);
		buf += 4;
		*buf++ = key;
		*buf++ = key >> 8;
		*buf++ = idx;
		*buf++ = idx >> 8;

		res = 0;
		reslen = 24;
	}
	break;

	case AL_SKFMEND:
	{
		dbg_print("> AL_SKFMEND\n");

		key = buf[4] | (buf[5] << 8);
		if (open_file_handles.find(key) == open_file_handles.end())
		{
			res = 2;
			break;
		}

		int32_t off;
		memcpyb(&off, buf, 4);

		int32_t sz = open_file_handles[key].size;
		off += sz;
		if (off < 0) off = 0;

		memcpyb(buf, &off, 4);

		res = 0;
		reslen = 4;
	}
	break;

	case AL_QUALIFY:
	{
		dbg_print("> AL_QUALIFY\n");

		res = 0;
		reslen = 128;
	}
	break;

	}

	((short *)reqres_buffer)[0] = reslen;
	((short *)reqres_buffer)[2] = res;
	dbg_print("result %d, %d:\n", reslen, res);
	dbg_hexdump(reqres_buffer, 8, 0);
	if (reslen > 0 && func != AL_READ)
	{
		dbg_hexdump((char*)reqres_buffer + 8, reslen, 8);
	}
	return reslen;
}

void x86_share_poll()
{
	if (!shmem)
	{
		shmem = (uint8_t *)shmem_map(SHMEM_ADDR, SHMEM_SIZE);
		if (!shmem) shmem = (uint8_t *)-1;
	}
	else if (shmem != (uint8_t *)-1)
	{
		static uint32_t old_req_id = 0;
		uint32_t req_id = *(uint32_t*)(shmem + REQUEST_FLG);

		if ((uint16_t)old_req_id != (uint16_t)req_id)
		{
			dbg_print("\nnew req: %08X\n", req_id);
			old_req_id = req_id;

			if (((req_id >> 16) & 0xFFFF) == 0xA55A && ((req_id + 77) & 0xFF) == ((req_id >> 8) & 0xFF))
			{
				process_request(shmem + REQUEST_BUFFER);
				*(uint16_t*)(shmem + REQUEST_FLG + 2) = (uint16_t)req_id;
			}
		}
	}
}

void x86_share_reset()
{
	open_file_handles.clear();
	locks.clear();
	next_fp = 1;
	next_key = 1;
}
