#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <time.h>

#include <map>
#include <string>
#include <vector>

#include "../../hardware.h"
#include "../../file_io.h"
#include "../../user_io.h"
#include "../../spi.h"
#include "../../cfg.h"
#include "../../shmem.h"
#include "miminig_fs_messages.h"

#define SHMEM_ADDR      0x27FF4000
#define SHMEM_SIZE      0x2000
static uint8_t *shmem = 0;

#define REQUEST_FLG     0      // 4B
#define REQUEST_BUFFER  4      // ~512B
#define DATA_BUFFER     0x1000 // 4KB

// Must match device name in MountList and volume name from MiSTerFileSystem
#define DEVICE_NAME     "SHARE"
#define VOLUME_NAME     "MiSTer"

//#define DEBUG

#ifdef DEBUG
	#define dbg_print printf
	#define dbg_hexdump hexdump
#else
	#define dbg_print(x,...) void()
	#define dbg_hexdump(x,...) void()
#endif

#define SWAP_INT(a) ((((a)&0x000000ff)<<24)|(((a)&0x0000ff00)<<8)|(((a)&0x00ff0000)>>8)|(((a)&0xff000000)>>24))

static char basepath[1024] = {};
static int baselen = 0;

struct lock
{
	uint16_t mode;
	std::string path;
	std::vector<dirent64> dir_items;
};

static std::map<uint32_t, lock> locks;
static uint32_t next_key = 1;

static uint32_t get_key()
{
	uint32_t key;

	do
	{
		key = next_key;
		if (next_key == INT32_MAX) next_key = 1; else next_key++;

	} while (locks.find(key) != locks.end());

	return key;
}

static uint32_t add_lock(uint16_t mode, const char* path)
{
	uint32_t key = get_key();
	locks[key] = { mode, path, {} };

	dbg_print("+ add lock: %d, %s\n", key, path);
	return key;
}

static int has_locks(const char* path)
{
	int has = 0;
	for (const auto &pair : locks)
	{
		if (pair.second.path == path)
		{
			if (!has)
			{
				dbg_print("! path %s has locks:", path);
			}

			has = 1;
			dbg_print(" %d", pair.first);
		}
	}

	if (has)
	{
		dbg_print("\n");
	}
	return has;
}

static std::map<uint32_t, fileTYPE> open_file_handles;
static uint32_t next_fp = 1;

static uint32_t get_fp()
{
	uint32_t fp;

	do
	{
		fp = next_fp;
		if (next_fp == INT32_MAX) next_fp = 1; else next_fp++;

	} while (open_file_handles.find(fp) != open_file_handles.end());

	return fp;
}

static char* find_path(uint32_t key, const char *name)
{
	dbg_print("find_path(%d, %s)\n", key, name);

	static char str[1024] = {};
	const char* p = strchr(name, ':');
	if (p)
	{
		size_t root_len = p - name;

		// Don't use lock for relative path if the name contains our root
		if (root_len == 0 || !strncasecmp(name, DEVICE_NAME, root_len) || !strncasecmp(name, VOLUME_NAME, root_len))
			key = 0;

		name = p + 1;
	}

	strcpy(str, basepath);
	if (key)
	{
		auto it = locks.find(key);
		if (it != locks.end()) strcpy(str, it->second.path.c_str());
	}

	if (strlen(name))
	{
		strcat(str, "/");
		strcat(str, name);
	}

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

			if (len && (!strncmp(cur, ".", len) || !strncmp(cur, "..", len)))
			{
				dbg_print("Illegal path\n");
				str[0] = 0;
				break;
			}

			// end
			if (!*next) break;

			if (!len)
			{
				// / alone is used for parent
				cur -= 2;
				while (*cur != '/') cur--;

				if (cur < str + baselen)
				{
					dbg_print("Going above root\n");
					str[0] = 0;
					break;
				}

				// collapse the component
				strcpy(cur, next);
			}
			else
			{
				cur = next;
			}
		}
	}

	// remove trailing /
	int len = strlen(str);
	if (len && str[len-1] == '/') str[len-1] = 0;

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

static void fill_date(time_t time, int date[3])
{
	time_t days = time / 86400;
	time_t left = time - (days * 86400);
	time_t mins = left / 60;
	time_t secs = left - (mins * 60);
	time_t ticks = secs * 50;
	days -= 2922; // Days between 1970 - 01 - 01 and 1978 - 01 - 01
	if (days < 0) days = 0;

	date[0] = SWAP_INT(days);
	date[1] = SWAP_INT(mins);
	date[2] = SWAP_INT(ticks);
}

static int process_request(void *reqres_buffer)
{
	static char buf[1024];
	GenericRequestResponse *reqres = ( GenericRequestResponse *)reqres_buffer;

	int rtype = SWAP_INT(reqres->type);
	int ret = ERROR_ACTION_NOT_KNOWN;

	int sz = SWAP_INT(reqres->sz);
	((uint8_t*)reqres_buffer)[sz] = 0;

	int sz_res = sizeof(GenericRequestResponse);

	dbg_print("request type: %d, struct size: %d\n", rtype, sz);
	dbg_hexdump(reqres_buffer, sz, 0);

	if (!baselen)
	{
		if (strlen(cfg.shared_folder))
		{
			if(cfg.shared_folder[0] == '/') strcpy(basepath, cfg.shared_folder);
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

		if(baselen) FileCreatePath(basepath);
	}

	// no base path => force fail
	if (!baselen) rtype = ACTION_NIL;

	switch (rtype)
	{
		case ACTION_LOCATE_OBJECT:
		{
			dbg_print("> ACTION_LOCATE_OBJECT\n");
			LocateObjectRequest *req = (LocateObjectRequest*)reqres_buffer;
			LocateObjectResponse *res = (LocateObjectResponse*)reqres_buffer;
			sz_res = sizeof(LocateObjectResponse);

			char *str = find_path(SWAP_INT(req->key), req->name + 1);
			if (!str[0])
			{
				ret = ERROR_OBJECT_NOT_FOUND;
				break;
			}

			if (!FileExists(str, 0) && !PathIsDir(str, 0))
			{
				ret = ERROR_OBJECT_NOT_FOUND;
				break;
			}

			uint32_t key = add_lock(req->mode, str);
			res->key = SWAP_INT(key);
			ret = 0;
		}
		break;

		case ACTION_FREE_LOCK:
		{
			dbg_print("> ACTION_FREE_LOCK\n");
			FreeLockRequest *req = (FreeLockRequest*)reqres_buffer;

			uint32_t key = SWAP_INT(req->key);
			locks.erase(key);
			dbg_print("  lock: %d\n", key);

			ret = 0;
		}
		break;

		case ACTION_COPY_LOCK:
		{
			dbg_print("> ACTION_COPY_LOCK\n");
			CopyDirRequest *req = (CopyDirRequest*)reqres_buffer;
			CopyDirResponse *res = (CopyDirResponse*)reqres_buffer;
			sz_res = sizeof(CopyDirResponse);

			uint32_t key = SWAP_INT(req->key);
			if (locks.find(key) == locks.end())
			{
				ret = ERROR_OBJECT_NOT_FOUND;
				break;
			}

			uint32_t new_key = add_lock(locks[key].mode, locks[key].path.c_str());
			dbg_print("CopyDir: %s: %d -> %d\n", locks[new_key].path.c_str(), key, new_key);

			res->key = SWAP_INT(new_key);
			ret = 0;
		}
		break;

		case ACTION_PARENT:
		{
			dbg_print("> ACTION_PARENT\n");
			ParentRequest *req = (ParentRequest*)reqres_buffer;
			ParentResponse *res = (ParentResponse*)reqres_buffer;
			sz_res = sizeof(ParentResponse);

			uint32_t key = SWAP_INT(req->key);
			dbg_print("  current key: %d\n", key);

			if (locks.find(key) == locks.end())
			{
				ret = ERROR_OBJECT_NOT_FOUND;
				break;
			}

			ret = 0;
			res->key = 0;

			char *name = buf;
			strcpy(name, locks[key].path.c_str());
			dbg_print("  current path: %s\n", name);

			if (!strncasecmp(basepath, name, baselen)) name += baselen;

			if (strlen(name))
			{
				char* p = strrchr(name, '/');
				if (p)
				{
					*p = 0;
					uint32_t key = add_lock(SHARED_LOCK, buf);
					res->key = SWAP_INT(key);
					dbg_print("  parent path: %s\n", buf);
				}
			}
		}
		break;

		case ACTION_EXAMINE_NEXT:
		case ACTION_EXAMINE_OBJECT:
		{
			dbg_print("> ACTION_EXAMINE\n");
			ExamineObjectRequest *req = (ExamineObjectRequest*)reqres_buffer;
			ExamineObjectResponse *res = (ExamineObjectResponse*)reqres_buffer;
			sz_res = sizeof(ExamineObjectResponse);

			uint32_t key = SWAP_INT(req->key);
			dbg_print("  key: %d\n", key);

			char *name = buf;
			name[0] = 0;
			if (locks.find(key) != locks.end()) strcpy(name, locks[key].path.c_str());
			if (!strlen(name)) strcpy(name, basepath);

			int disk_key = 666;
			static char fn[256];
			if (rtype == ACTION_EXAMINE_OBJECT)
			{
				dbg_print("  examine first\n");

				if (!strlen(name + baselen)) strcpy(fn, "MiSTer");
				else
				{
					const char *p = strrchr(name, '/');
					strcpy(fn, p ? p + 1 : name);
				}

				locks[key].dir_items.clear();
				if (PathIsDir(name, 0))
				{
					const char* full_path = getFullPath(name);
					DIR *d = opendir(full_path);
					if (!d)
					{
						printf("Couldn't open dir: %s\n", full_path);
						ret = ERROR_OBJECT_WRONG_TYPE;
						break;
					}

					struct dirent64 *de;
					while ((de = readdir64(d)))
					{
						if (!strcmp(de->d_name, "..") || !strcmp(de->d_name, ".")) continue;
						locks[key].dir_items.push_back(*de);
					}
					closedir(d);
				}
			}
			else
			{
				dbg_print("  examine next\n");

				disk_key = SWAP_INT(req->disk_key);

				uint32_t listed = disk_key - 666;
				disk_key++;

				if (listed >= locks[key].dir_items.size())
				{
					locks[key].dir_items.clear();
					ret = ERROR_NO_MORE_ENTRIES;
					break;
				}

				strcat(name, "/");
				strcat(name, locks[key].dir_items[listed].d_name);
				memcpy(fn, locks[key].dir_items[listed].d_name, sizeof(fn));
				ret = 0;
			}

			dbg_print("    name: %s\n", name);
			dbg_print("    fn: %s\n", fn);

			int type = 0;
			if (FileExists(name, 0)) type = ST_FILE;
			else if (PathIsDir(name, 0)) type = ST_USERDIR;
			else
			{
				ret = ERROR_OBJECT_NOT_FOUND;
				break;
			}

			time_t time = 0;
			uint32_t size = 0;

			struct stat64 *st = getPathStat(name);
			if (st)
			{
				time = st->st_mtime;
				if (type == ST_FILE)
				{
					if (st->st_size > UINT32_MAX) size = UINT32_MAX;
					else size = (uint32_t)st->st_size;
				}
			}

			res->disk_key = SWAP_INT(disk_key);
			res->entry_type = SWAP_INT(type);
			res->size = SWAP_INT(size);
			res->protection = 0;
			fill_date(time, res->date);

			res->file_name[0] = strlen(fn);
			strcpy(res->file_name + 1, fn);

			sz_res = sizeof(ExamineObjectResponse) + strlen(fn);
			ret = 0;
		}
		break;

		case ACTION_EXAMINE_FH:
		{
			dbg_print("> ACTION_EXAMINE_FH\n");
			ExamineFhRequest *req = (ExamineFhRequest*)reqres_buffer;
			ExamineFhResponse *res = (ExamineFhResponse*)reqres_buffer;
			sz_res = sizeof(ExamineFhResponse);

			uint32_t key = SWAP_INT(req->arg1);
			dbg_print("  key: %d\n", key);

			if (open_file_handles.find(key) == open_file_handles.end())
			{
				ret = ERROR_OBJECT_NOT_FOUND;
				break;
			}

			const char *fn = open_file_handles[key].name;
			int disk_key = 666;
			int type = 0;
			time_t time = 0;
			uint32_t size = 0;

			struct stat64 st;
			if (fstat64(fileno(open_file_handles[key].filp), &st) == 0)
			{
				time = st.st_mtime;
				if (st.st_mode & S_IFDIR) type = ST_USERDIR;
				else
				{
					type = ST_FILE;
					if (st.st_size > UINT32_MAX) size = UINT32_MAX;
					else size = (uint32_t)st.st_size;
				}
			}
			else
			{
				dbg_print("Couldn't stat %s: %d\n", fn, errno);
				ret = ERROR_OBJECT_NOT_FOUND;
				break;
			}

			dbg_print("    fn: %s\n", fn);
			dbg_print("    size: %lld\n", open_file_handles[key].size);
			dbg_print("    type: %d\n", type);

			res->disk_key = SWAP_INT(disk_key);
			res->entry_type = SWAP_INT(type);
			res->size = SWAP_INT(size);
			res->protection = 0;
			fill_date(time, res->date);

			res->file_name[0] = strlen(fn);
			strcpy(res->file_name + 1, fn);

			sz_res = sizeof(ExamineFhResponse) + strlen(fn);
			ret = 0;
		}
		break;

		case ACTION_FINDINPUT:  // MODE_OLDFILE
		case ACTION_FINDOUTPUT: // MODE_NEWFILE
		case ACTION_FINDUPDATE: // MODE_READWRITE
		{
			dbg_print("> ACTION_FIND\n");
			FindXxxRequest *req = (FindXxxRequest*)reqres_buffer;
			FindXxxResponse *res = (FindXxxResponse*)reqres_buffer;
			sz_res = sizeof(FindXxxResponse);

			char *name = find_path(SWAP_INT(req->key), req->name + 1);

			if (!name[0])
			{
				ret = ERROR_OBJECT_NOT_FOUND;
				break;
			}

			if (PathIsDir(name, 0))
			{
				ret = ERROR_OBJECT_WRONG_TYPE;
				break;
			}

			uint32_t key = get_fp();
			open_file_handles[key] = {};

			int mode = O_RDWR;

			if (rtype == MODE_NEWFILE) mode = O_RDWR | O_CREAT | O_TRUNC;
			if (rtype == MODE_READWRITE) mode = O_RDWR | O_CREAT;

			ret = FileOpenEx(&open_file_handles[key], name, mode, 0, 0);
			if (!ret)
			{
				open_file_handles.erase(key);
				ret = ERROR_OBJECT_NOT_FOUND;
				break;
			}

			res->arg1 = SWAP_INT(key);
			ret = 0;
		}
		break;

		case ACTION_READ:
		{
			dbg_print("> ACTION_READ\n");
			ReadRequest *req = (ReadRequest*)reqres_buffer;
			ReadResponse *res = (ReadResponse*)reqres_buffer;
			sz_res = sizeof(ReadResponse);

			uint32_t key = SWAP_INT(req->arg1);
			if (open_file_handles.find(key) == open_file_handles.end())
			{
				ret = ERROR_OBJECT_NOT_FOUND;
				break;
			}

			uint32_t length = SWAP_INT(req->length);
			length = FileReadAdv(&open_file_handles[key], shmem + DATA_BUFFER, length);

			res->actual = SWAP_INT(length);
			ret = 0;
		}
		break;

		case ACTION_WRITE:
		{
			dbg_print("> ACTION_WRITE\n");
			WriteRequest *req = (WriteRequest*)reqres_buffer;
			WriteResponse *res = (WriteResponse*)reqres_buffer;
			sz_res = sizeof(WriteResponse);

			uint32_t key = SWAP_INT(req->arg1);
			if (open_file_handles.find(key) == open_file_handles.end())
			{
				ret = ERROR_OBJECT_NOT_FOUND;
				break;
			}

			uint32_t length = SWAP_INT(req->length);
			length = FileWriteAdv(&open_file_handles[key], shmem + DATA_BUFFER, length);

			res->actual = SWAP_INT(length);
			ret = 0;
		}
		break;

		case ACTION_SEEK:
		{
			dbg_print("> ACTION_SEEK\n");
			SeekRequest *req = (SeekRequest*)reqres_buffer;
			SeekResponse *res = (SeekResponse*)reqres_buffer;
			sz_res = sizeof(SeekResponse);

			uint32_t key = SWAP_INT(req->arg1);
			if (open_file_handles.find(key) == open_file_handles.end())
			{
				ret = ERROR_OBJECT_NOT_FOUND;
				break;
			}

			int old_pos = open_file_handles[key].offset;

			int new_pos = SWAP_INT(req->new_pos);
			int mode = SWAP_INT(req->mode);

			int origin = SEEK_SET;
			if (mode == OFFSET_CURRENT) origin = SEEK_CUR;
			if (mode == OFFSET_END) origin = SEEK_END;

			ret = FileSeek(&open_file_handles[key], new_pos, origin);

			dbg_print("  mode: %d\n", mode);
			dbg_print("  old_pos: %d\n", old_pos);
			dbg_print("  new_pos: %d\n", new_pos);

			if (!ret)
			{
				ret = ERROR_SEEK_ERROR;
				break;
			}

			res->old_pos = SWAP_INT(old_pos);
			ret = 0;
		}
		break;

		case ACTION_END:
		{
			dbg_print("> ACTION_END\n");
			EndRequest *req = (EndRequest*)reqres_buffer;
			uint32_t key = SWAP_INT(req->arg1);

			if (open_file_handles.find(key) != open_file_handles.end())
			{
				FileClose(&open_file_handles[key]);
				open_file_handles.erase(key);
			}

			ret = 0;
		}
		break;

		case ACTION_DELETE_OBJECT:
		{
			dbg_print("> ACTION_DELETE_OBJECT\n");
			DeleteObjectRequest *req = (DeleteObjectRequest*)reqres_buffer;

			char *name = find_path(SWAP_INT(req->key), req->name + 1);
			if (name[0])
			{
				if (has_locks(name))
				{
					ret = ERROR_OBJECT_IN_USE;
					break;
				}

				if (PathIsDir(name, 0))
				{
					ret = DirDelete(name) ? 0 : ERROR_DIRECTORY_NOT_EMPTY;
					break;
				}

				if (FileExists(name, 0))
				{
					ret = FileDelete(name) ? 0 : ERROR_OBJECT_NOT_FOUND;
					break;
				}
			}

			ret = ERROR_OBJECT_NOT_FOUND;
		}
		break;

		case ACTION_RENAME_OBJECT:
		{
			dbg_print("> ACTION_RENAME_OBJECT\n");
			RenameObjectRequest *req = (RenameObjectRequest*)reqres_buffer;

			strncpy(buf, req->name, req->name_len);
			buf[req->name_len] = 0;

			uint32_t key = SWAP_INT(req->key);
			char *cp1 = find_path(key, buf);
			if (!cp1[0])
			{
				ret = ERROR_OBJECT_NOT_FOUND;
				break;
			}

			if (!FileExists(cp1, 0) && !PathIsDir(cp1, 0))
			{
				ret = ERROR_OBJECT_NOT_FOUND;
				break;
			}

			strcpy(buf, cp1);
			key = SWAP_INT(req->target_dir);
			char *cp2 = find_path(key, req->name + req->name_len);
			if (!cp2[0])
			{
				ret = ERROR_OBJECT_NOT_FOUND;
				break;
			}

			strcpy(buf, getFullPath(buf));
			const char *fp2 = getFullPath(cp2);

			// Identical match; do nothing
			if (!strcmp(buf, fp2))
			{
				ret = 0;
				break;
			}

			if (FileExists(cp2, 0) || PathIsDir(cp2, 0))
			{
				ret = ERROR_OBJECT_EXISTS;
				break;
			}

			if (rename(buf, fp2))
			{
				ret = ERROR_OBJECT_NOT_FOUND;
				break;
			}

			ret = 0;
		}
		break;

		case ACTION_CREATE_DIR:
		{
			dbg_print("> ACTION_CREATE_DIR\n");
			CreateDirRequest *req = (CreateDirRequest*)reqres_buffer;
			CreateDirResponse *res = (CreateDirResponse*)reqres_buffer;
			sz_res = sizeof(CreateDirResponse);

			char *name = find_path(SWAP_INT(req->key), req->name + 1);
			if (!FileCreatePath(name))
			{
				ret = ERROR_OBJECT_NOT_FOUND;
				break;
			}

			uint32_t key = add_lock(SHARED_LOCK, name);
			res->key = SWAP_INT(key);

			ret = 0;
		}
		break;

		case ACTION_DISK_INFO:
		case ACTION_INFO:
		{
			dbg_print("> ACTION_INFO\n");
			DiskInfoResponse *res = (DiskInfoResponse*)reqres_buffer;
			sz_res = sizeof(DiskInfoResponse);

			uint32_t total = 10;
			uint32_t used = 1;

			struct statvfs st;
			if (!statvfs(getFullPath(basepath), &st))
			{
				uint64_t sz    = st.f_bsize * st.f_blocks;
				uint64_t avail = st.f_bsize * st.f_bavail;

				total = sz / 512;
				used = (sz - avail) / 512;
			}

			res->total = SWAP_INT(total);
			res->used = SWAP_INT(used);
			ret = 0;
		}
		break;

		case ACTION_SET_PROTECT:
		{
			dbg_print("> ACTION_SET_PROTECT unimplemented\n");
			ret = 0;
		}
		break;

		case ACTION_SET_COMMENT:
		{
			dbg_print("> ACTION_SET_COMMENT unimplemented\n");
			ret = 0;
		}
		break;

		case ACTION_SAME_LOCK:
		{
			dbg_print("> ACTION_SAME_LOCK\n");
			SameLockRequest *req = (SameLockRequest*)reqres_buffer;

			uint32_t key1 = SWAP_INT(req->key1);
			uint32_t key2 = SWAP_INT(req->key2);

			if ((locks.find(key1) == locks.end()) || (locks.find(key2) == locks.end()))
			{
				ret = LOCK_DIFFERENT;
				break;
			}

			if (locks[key1].path == locks[key2].path)
			{
				ret = LOCK_SAME;
				break;
			}

			ret = LOCK_SAME_VOLUME;
		}
		break;
	}

	int success = ret ? 0 : 1;
	reqres->success = SWAP_INT(success);
	reqres->error_code = SWAP_INT(ret);

	dbg_print("error: %d\n", ret);
	dbg_hexdump(shmem + REQUEST_BUFFER, sz_res, 0);
	dbg_print("\n");

	return sz_res;
}

void minimig_share_poll()
{
	if (!shmem)
	{
		shmem = (uint8_t *)shmem_map(SHMEM_ADDR, SHMEM_SIZE);
		if (!shmem) shmem = (uint8_t *)-1;
	}
	else if(shmem != (uint8_t *)-1)
	{
		static uint32_t old_req_id = 0;
		uint32_t req_id = *(uint32_t*)(shmem + REQUEST_FLG);

		if ((uint16_t)old_req_id != (uint16_t)req_id)
		{
			dbg_print("new req: %08X\n", req_id);
			old_req_id = req_id;
			if (((req_id>>16) & 0xFFFF) == 0x5AA5 && ((req_id - 77) & 0xFF) == ((req_id >> 8) & 0xFF))
			{
				process_request(shmem + REQUEST_BUFFER);
				*(uint16_t*)(shmem + REQUEST_FLG + 2) = (uint16_t)req_id;
			}
		}
	}
}

void minimig_share_reset()
{
	open_file_handles.clear();
	locks.clear();
	next_fp = 1;
	next_key = 1;
}
