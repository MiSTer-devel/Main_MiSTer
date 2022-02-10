#include "file_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/vfs.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <linux/magic.h>
#include <algorithm>
#include <vector>
#include <string>
#include <set>
#include "lib/miniz/miniz.h"
#include "osd.h"
#include "fpga_io.h"
#include "menu.h"
#include "errno.h"
#include "DiskImage.h"
#include "user_io.h"
#include "cfg.h"
#include "input.h"
#include "miniz.h"
#include "scheduler.h"
#include "video.h"
#include "support.h"

#define MIN(a,b) (((a)<(b)) ? (a) : (b))

typedef std::vector<direntext_t> DirentVector;
typedef std::set<std::string> DirNameSet;

static const size_t YieldIterations = 128;

DirentVector DirItem;
DirNameSet DirNames;


// Directory scanning can cause the same zip file to be opened multiple times
// due to testing file types to adjust the path
// (and the fact the code path is shared with regular files)
// cache the opened mz_zip_archive so we only open it once
// this has the extra benefit that if a user is navigating through multiple directories
// in a zip archive, the zip will only be opened once and things will be more responsive
// ** We have to open the file outselves with open() so we can set O_CLOEXEC to prevent
// leaking the file descriptor when the user changes cores

static mz_zip_archive last_zip_archive = {};
static int last_zip_fd = -1;
static FILE *last_zip_cfile = NULL;
static char last_zip_fname[256] = {};
static char scanned_path[1024] = {};
static int scanned_opts = 0;

static int iSelectedEntry = 0;       // selected entry index
static int iFirstEntry = 0;

static char full_path[2100];
uint8_t loadbuf[LOADBUF_SZ];

fileTYPE::fileTYPE()
{
	filp = 0;
	mode = 0;
	type = 0;
	zip = 0;
	size = 0;
	offset = 0;
}

fileTYPE::~fileTYPE()
{
	FileClose(this);
}

int fileTYPE::opened()
{
	return filp || zip;
}

struct fileZipArchive
{
	mz_zip_archive                    archive;
	int                               index;
	mz_zip_reader_extract_iter_state* iter;
	__off64_t                         offset;
};


static int OpenZipfileCached(char *path, int flags)
{
  if (last_zip_fname[0] && !strcasecmp(path, last_zip_fname))
  {
    return 1;
  }

  mz_zip_reader_end(&last_zip_archive);
  mz_zip_zero_struct(&last_zip_archive);
  if (last_zip_cfile)
  {
    fclose(last_zip_cfile);
    last_zip_cfile = nullptr;
  }

  last_zip_fname[0] = '\0';
  last_zip_fd = open(path, O_RDONLY|O_CLOEXEC);
  if (last_zip_fd < 0)
  {
    return 0;
  }

  last_zip_cfile = fdopen(last_zip_fd, "r");
  if (!last_zip_cfile)
  {
    close(last_zip_fd);
    last_zip_fd = -1;
    return 0;
  }

  int mz_ret = mz_zip_reader_init_cfile(&last_zip_archive, last_zip_cfile, 0, flags);
  if (mz_ret)
  {
    strncpy(last_zip_fname, path, sizeof(last_zip_fname));
  }
  return mz_ret;
}


static int FileIsZipped(char* path, char** zip_path, char** file_path)
{
	char* z = strcasestr(path, ".zip");
	if (z)
	{
		z += 4;
		if (!z[0]) z[1] = 0;
		*z++ = 0;

		if (zip_path) *zip_path = path;
		if (file_path) *file_path = z;
		return 1;
	}

	return 0;
}

static char* make_fullpath(const char *path, int mode = 0)
{
	if (path[0] != '/')
	{
		sprintf(full_path, "%s/%s", (mode == -1) ? "" : getRootDir(), path);
	}
	else
	{
		sprintf(full_path, "%s",path);
	}

	return full_path;
}

static int get_stmode(const char *path)
{
	struct stat64 st;
	return (stat64(path, &st) < 0) ? 0 : st.st_mode;
}

struct stat64* getPathStat(const char *path)
{
	make_fullpath(path);
	static struct stat64 st;
	return (stat64(full_path, &st) >= 0) ? &st : NULL;
}

static int isPathDirectory(const char *path, int use_zip = 1)
{
	make_fullpath(path);

	char *zip_path, *file_path;
	if (use_zip && FileIsZipped(full_path, &zip_path, &file_path))
	{

      if (!*file_path)
      {
        return 1;
      }

      if (!OpenZipfileCached(full_path, 0))
		  {
			  printf("isPathDirectory(OpenZipfileCached) Zip:%s, error:%s\n", zip_path,
			        mz_zip_get_error_string(mz_zip_get_last_error(&last_zip_archive)));
			  return 0;
		  }

		// Folder names always end with a slash in the zip
		// file central directory.
		strcat(file_path, "/");


    // Some zip files don't have directory entries
    // Use the locate_file call to try and find the directory entry first, since
    // this is a binary search (usually) If that fails then scan for the first
    // entry that starts with file_path

    const int file_index = mz_zip_reader_locate_file(&last_zip_archive, file_path, NULL, 0);
    if (file_index >= 0 && mz_zip_reader_is_file_a_directory(&last_zip_archive, file_index))
    {
      return 1;
    }

    for (size_t i = 0; i < mz_zip_reader_get_num_files(&last_zip_archive); i++) {
      char zip_fname[256];
      mz_zip_reader_get_filename(&last_zip_archive, i, &zip_fname[0], sizeof(zip_fname));
      if (strcasestr(zip_fname, file_path))
      {
        return 1;
      }
    }
    return 0;
  }
	else
	{
		int stmode = get_stmode(full_path);
		if (!stmode)
		{
			//printf("isPathDirectory(stat) path: %s, error: %s.\n", full_path, strerror(errno));
			return 0;
		}

		if (stmode & S_IFDIR) return 1;
	}

	return 0;
}

static int isPathRegularFile(const char *path, int use_zip = 1)
{
	make_fullpath(path);

	char *zip_path, *file_path;
	if (use_zip && FileIsZipped(full_path, &zip_path, &file_path))
	{
    //If there's no path into the zip file, don't bother opening it, we're a "directory"
    if (!*file_path)
    {
      return 0;
    }
		  if (!OpenZipfileCached(full_path, 0))
		  {
			  //printf("isPathRegularFile(mz_zip_reader_init_file) Zip:%s, error:%s\n", zip_path,
			  //       mz_zip_get_error_string(mz_zip_get_last_error(&z)));
			  return 0;
		  }
		const int file_index = mz_zip_reader_locate_file(&last_zip_archive, file_path, NULL, 0);
		if (file_index < 0)
		{
			//printf("isPathRegularFile(mz_zip_reader_locate_file) Zip:%s, file:%s, error: %s\n",
			//		 zip_path, file_path,
			//		 mz_zip_get_error_string(mz_zip_get_last_error(&z)));
			return 0;
		}

		if (!mz_zip_reader_is_file_a_directory(&last_zip_archive, file_index) && mz_zip_reader_is_file_supported(&last_zip_archive, file_index))
		{
			return 1;
		}
	}
	else
	{
		if (get_stmode(full_path) & S_IFREG) return true;
	}

	return 0;
}

void FileClose(fileTYPE *file)
{
	if (file->zip)
	{
		if (file->zip->iter)
		{
			mz_zip_reader_extract_iter_free(file->zip->iter);
		}
		mz_zip_reader_end(&file->zip->archive);

		delete file->zip;
	}

	if (file->filp)
	{
		//printf("closing %p\n", file->filp);
		fclose(file->filp);
		if (file->type == 1)
		{
			if (file->name[0] == '/')
			{
				shm_unlink(file->name);
			}
			file->type = 0;
		}
	}

	file->zip = nullptr;
	file->filp = nullptr;
	file->size = 0;
}

static int zip_search_by_crc(mz_zip_archive *zipArchive, uint32_t crc32)
{
	for (unsigned int file_index = 0; file_index < zipArchive->m_total_files; file_index++)
	{
		mz_zip_archive_file_stat s;
		if (mz_zip_reader_file_stat(zipArchive, file_index, &s))
		{
			if (s.m_crc32 == crc32)
			{
				return file_index;
			}
		}
	}

	return -1;
}

int FileOpenZip(fileTYPE *file, const char *name, uint32_t crc32)
{
	make_fullpath(name);
	FileClose(file);
	file->mode = 0;
	file->type = 0;

	char *p = strrchr(full_path, '/');
	strcpy(file->name, (p) ? p + 1 : full_path);

	char *zip_path, *file_path;
	if (!FileIsZipped(full_path, &zip_path, &file_path))
	{
		printf("FileOpenZip: %s, is not a zip.\n", full_path);
		return 0;
	}

	file->zip = new fileZipArchive{};
	if (!mz_zip_reader_init_file(&file->zip->archive, zip_path, 0))
	{
		printf("FileOpenZip(mz_zip_reader_init_file) Zip:%s, error:%s\n", zip_path,
					mz_zip_get_error_string(mz_zip_get_last_error(&file->zip->archive)));
		return 0;
	}

	file->zip->index = -1;
	if (crc32) file->zip->index = zip_search_by_crc(&file->zip->archive, crc32);
	if (file->zip->index < 0) file->zip->index = mz_zip_reader_locate_file(&file->zip->archive, file_path, NULL, 0);
	if (file->zip->index < 0)
	{
		printf("FileOpenZip(mz_zip_reader_locate_file) Zip:%s, file:%s, error: %s\n",
					zip_path, file_path,
					mz_zip_get_error_string(mz_zip_get_last_error(&file->zip->archive)));
		FileClose(file);
		return 0;
	}

	mz_zip_archive_file_stat s;
	if (!mz_zip_reader_file_stat(&file->zip->archive, file->zip->index, &s))
	{
		printf("FileOpenZip(mz_zip_reader_file_stat) Zip:%s, file:%s, error:%s\n",
					zip_path, file_path,
					mz_zip_get_error_string(mz_zip_get_last_error(&file->zip->archive)));
		FileClose(file);
		return 0;
	}
	file->size = s.m_uncomp_size;

	file->zip->iter = mz_zip_reader_extract_iter_new(&file->zip->archive, file->zip->index, 0);
	if (!file->zip->iter)
	{
		printf("FileOpenZip(mz_zip_reader_extract_iter_new) Zip:%s, file:%s, error:%s\n",
					zip_path, file_path,
					mz_zip_get_error_string(mz_zip_get_last_error(&file->zip->archive)));
		FileClose(file);
		return 0;
	}

	file->zip->offset = 0;
	file->offset = 0;
	file->mode = O_RDONLY;
	return 1;
}

int FileOpenEx(fileTYPE *file, const char *name, int mode, char mute, int use_zip)
{
	make_fullpath((char*)name, mode);
	FileClose(file);
	file->mode = 0;
	file->type = 0;

	char *p = strrchr(full_path, '/');
	strcpy(file->name, (mode == -1) ? full_path : p + 1);

	char *zip_path, *file_path;
	if (use_zip && (mode != -1) && FileIsZipped(full_path, &zip_path, &file_path))
	{
		if (mode & O_RDWR || mode & O_WRONLY)
		{
			if(!mute) printf("FileOpenEx(mode) Zip:%s, writing to zipped files is not supported.\n",
					 full_path);
			return 0;
		}

		file->zip = new fileZipArchive{};
		if (!mz_zip_reader_init_file(&file->zip->archive, zip_path, 0))
		{
			if(!mute) printf("FileOpenEx(mz_zip_reader_init_file) Zip:%s, error:%s\n", zip_path,
					 mz_zip_get_error_string(mz_zip_get_last_error(&file->zip->archive)));
			return 0;
		}

		file->zip->index = mz_zip_reader_locate_file(&file->zip->archive, file_path, NULL, 0);
		if (file->zip->index < 0)
		{
			if(!mute) printf("FileOpenEx(mz_zip_reader_locate_file) Zip:%s, file:%s, error: %s\n",
					 zip_path, file_path,
					 mz_zip_get_error_string(mz_zip_get_last_error(&file->zip->archive)));
			FileClose(file);
			return 0;
		}

		mz_zip_archive_file_stat s;
		if (!mz_zip_reader_file_stat(&file->zip->archive, file->zip->index, &s))
		{
			if(!mute) printf("FileOpenEx(mz_zip_reader_file_stat) Zip:%s, file:%s, error:%s\n",
					 zip_path, file_path,
					 mz_zip_get_error_string(mz_zip_get_last_error(&file->zip->archive)));
			FileClose(file);
			return 0;
		}
		file->size = s.m_uncomp_size;

		file->zip->iter = mz_zip_reader_extract_iter_new(&file->zip->archive, file->zip->index, 0);
		if (!file->zip->iter)
		{
			if(!mute) printf("FileOpenEx(mz_zip_reader_extract_iter_new) Zip:%s, file:%s, error:%s\n",
					 zip_path, file_path,
					 mz_zip_get_error_string(mz_zip_get_last_error(&file->zip->archive)));
			FileClose(file);
			return 0;
		}
		file->zip->offset = 0;
		file->offset = 0;
		file->mode = mode;
	}
	else
	{
		int fd = (mode == -1) ? shm_open("/vdsk", O_CREAT | O_RDWR | O_TRUNC | O_CLOEXEC, 0777) : open(full_path, mode | O_CLOEXEC, 0777);
		if (fd <= 0)
		{
			if(!mute) printf("FileOpenEx(open) File:%s, error: %s.\n", full_path, strerror(errno));
			return 0;
		}
		const char *fmode = mode & O_RDWR ? "w+" : "r";
		file->filp = fdopen(fd, fmode);
		if (!file->filp)
		{
			if(!mute) printf("FileOpenEx(fdopen) File:%s, error: %s.\n", full_path, strerror(errno));
			close(fd);
			return 0;
		}

		if (mode == -1)
		{
			file->type = 1;
			file->size = 0;
			file->offset = 0;
			file->mode = O_CREAT | O_RDWR | O_TRUNC;
		}
		else
		{
			struct stat64 st;
			int ret = fstat64(fileno(file->filp), &st);
			if (ret < 0)
			{
				if (!mute) printf("FileOpenEx(fstat) File:%s, error: %d.\n", full_path, ret);
				FileClose(file);
				return 0;
			}

			file->size = st.st_size;
			if (st.st_rdev && !st.st_size)  //for special files we need an ioctl call to get the correct size
			{
				unsigned long long blksize;
				int ret = ioctl(fd, BLKGETSIZE64, &blksize);
				if (ret < 0)
				{
					if (!mute) printf("FileOpenEx(ioctl) File:%s, error: %d.\n", full_path, ret);
					FileClose(file);
					return 0;
				}
				file->size = blksize;
			}

			file->offset = 0;
			file->mode = mode;
		}
	}

	//printf("opened %s, size %llu\n", full_path, file->size);
	return 1;
}

__off64_t FileGetSize(fileTYPE *file)
{
	if (file->filp)
	{
		struct stat64 st;
		if (fstat64(fileno(file->filp), &st) < 0) return 0;

		if (st.st_rdev && !st.st_size)  //for special files we need an ioctl call to get the correct size
		{
			unsigned long long blksize;
			int ret = ioctl(fileno(file->filp), BLKGETSIZE64, &blksize);
			if (ret < 0) return 0;
			return blksize;
		}

		return st.st_size;
	}
	else if (file->zip)
	{
		return file->size;
	}
	return 0;
}

int FileOpen(fileTYPE *file, const char *name, char mute)
{
	return FileOpenEx(file, name, O_RDONLY, mute);
}

int FileSeek(fileTYPE *file, __off64_t offset, int origin)
{
	if (file->filp)
	{
		__off64_t res = fseeko64(file->filp, offset, origin);
		if (res < 0)
		{
			printf("Fail to seek the file: offset=%lld, %s.\n", offset, file->name);
			return 0;
		}
		offset = ftello64(file->filp);
	}
	else if (file->zip)
	{
		if (origin == SEEK_CUR)
		{
			offset = file->zip->offset + offset;
		}
		else if (origin == SEEK_END)
		{
			offset = file->size - offset;
		}

		if (offset < file->zip->offset)
		{
			mz_zip_reader_extract_iter_state *iter = mz_zip_reader_extract_iter_new(&file->zip->archive, file->zip->index, 0);
			if (!iter)
			{
				printf("FileSeek(mz_zip_reader_extract_iter_new) Failed to rewind iterator, error:%s\n",
				       mz_zip_get_error_string(mz_zip_get_last_error(&file->zip->archive)));
				return 0;
			}

			mz_zip_reader_extract_iter_free(file->zip->iter);
			file->zip->iter = iter;
			file->zip->offset = 0;
		}

		static char buf[4*1024];
		while (file->zip->offset < offset)
		{
			const size_t want_len = MIN((__off64_t)sizeof(buf), offset - file->zip->offset);
			const size_t read_len = mz_zip_reader_extract_iter_read(file->zip->iter, buf, want_len);
			file->zip->offset += read_len;
			if (read_len < want_len)
			{
				printf("FileSeek(mz_zip_reader_extract_iter_read) Failed to advance iterator, error:%s\n",
				       mz_zip_get_error_string(mz_zip_get_last_error(&file->zip->archive)));
				return 0;
			}
		}
	}
	else
	{
		return 0;
	}

	file->offset = offset;
	return 1;
}

int FileSeekLBA(fileTYPE *file, uint32_t offset)
{
	__off64_t off64 = offset;
	off64 <<= 9;
	return FileSeek(file, off64, SEEK_SET);
}

// Read with offset advancing
int FileReadAdv(fileTYPE *file, void *pBuffer, int length, int failres)
{
	ssize_t ret = 0;

	if (file->filp)
	{
		ret = fread(pBuffer, 1, length, file->filp);
		if (ret < 0)
		{
			printf("FileReadAdv error(%d).\n", ret);
			return failres;
		}
	}
	else if (file->zip)
	{
		ret = mz_zip_reader_extract_iter_read(file->zip->iter, pBuffer, length);
		if (!ret)
		{
			printf("FileReadEx(mz_zip_reader_extract_iter_read) Failed to read, error:%s\n",
			       mz_zip_get_error_string(mz_zip_get_last_error(&file->zip->archive)));
			return failres;
		}
		file->zip->offset += ret;
	}
	else
	{
		printf("FileReadAdv error(unknown file type).\n");
		return failres;
	}

	file->offset += ret;
	return ret;
}

int FileReadSec(fileTYPE *file, void *pBuffer)
{
	return FileReadAdv(file, pBuffer, 512);
}

// Write with offset advancing
int FileWriteAdv(fileTYPE *file, void *pBuffer, int length, int failres)
{
	int ret;

	if (file->filp)
	{
		ret = fwrite(pBuffer, 1, length, file->filp);
		fflush(file->filp);

		if (ret < 0)
		{
			printf("FileWriteAdv error(%d).\n", ret);
			return failres;
		}

		file->offset += ret;
		if (file->offset > file->size) file->size = FileGetSize(file);
		return ret;
	}
	else if (file->zip)
	{
		printf("FileWriteAdv error(not supported for zip).\n");
		return failres;
	}
	else
	{
		printf("FileWriteAdv error(unknown file type).\n");
		return failres;
	}
}

int FileWriteSec(fileTYPE *file, void *pBuffer)
{
	return FileWriteAdv(file, pBuffer, 512);
}

int FileSave(const char *name, void *pBuffer, int size)
{
	make_fullpath(name);

	int fd = open(full_path, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, S_IRWXU | S_IRWXG | S_IRWXO);
	if (fd < 0)
	{
		printf("FileSave(open) File:%s, error: %d.\n", full_path, fd);
		return 0;
	}

	int ret = write(fd, pBuffer, size);
	close(fd);

	if (ret < 0)
	{
		printf("FileSave(write) File:%s, error: %d.\n", full_path, ret);
		return 0;
	}

	return ret;
}

int FileDelete(const char *name)
{
	make_fullpath(name);
	printf("delete %s\n", full_path);
	return !unlink(full_path);
}

int DirDelete(const char *name)
{
	make_fullpath(name);
	printf("rmdir %s\n", full_path);
	return !rmdir(full_path);
}

int FileLoad(const char *name, void *pBuffer, int size)
{
	fileTYPE f;
	if (!FileOpen(&f, name)) return 0;

	int ret = f.size;
	if (pBuffer) ret = FileReadAdv(&f, pBuffer, size ? size : f.size);

	FileClose(&f);
	return ret;
}

int FileLoadConfig(const char *name, void *pBuffer, int size)
{
	char path[256] = { CONFIG_DIR"/" };
	strcat(path, name);
	return FileLoad(path, pBuffer, size);
}

int FileSaveConfig(const char *name, void *pBuffer, int size)
{
	char path[256] = { CONFIG_DIR };
	const char *p;
	while ((p = strchr(name, '/')))
	{
		strcat(path, "/");
		strncat(path, name, p - name);
		name = ++p;
		FileCreatePath(path);
	}

	strcat(path, "/");
	strcat(path, name);
	return FileSave(path, pBuffer, size);
}

int FileDeleteConfig(const char *name)
{
	char path[256] = { CONFIG_DIR"/" };
	strcat(path, name);
	return FileDelete(path);
}

int FileExists(const char *name, int use_zip)
{
	return isPathRegularFile(name, use_zip);
}

int PathIsDir(const char *name, int use_zip)
{
	return isPathDirectory(name, use_zip);
}

int FileCanWrite(const char *name)
{
	make_fullpath(name);

	if (FileIsZipped(full_path, nullptr, nullptr))
	{
		return 0;
	}

	struct stat64 st;
	int ret = stat64(full_path, &st);
	if (ret < 0)
	{
		printf("FileCanWrite(stat) File:%s, error: %d.\n", full_path, ret);
		return 0;
	}

	//printf("FileCanWrite: mode=%04o.\n", st.st_mode);
	return ((st.st_mode & S_IWUSR) != 0);
}

static void create_path(const char *base_dir, const char* sub_dir)
{
	make_fullpath(base_dir);
	mkdir(full_path, S_IRWXU | S_IRWXG | S_IRWXO);
	strcat(full_path, "/");
	strcat(full_path, sub_dir);
	mkdir(full_path, S_IRWXU | S_IRWXG | S_IRWXO);
}

int FileCreatePath(const char *dir)
{
	int res = 1;
	if (!isPathDirectory(dir)) {
		make_fullpath(dir);
		res = !mkdir(full_path, S_IRWXU | S_IRWXG | S_IRWXO);
	}
	return res;
}

void FileGenerateScreenshotName(const char *name, char *out_name, int buflen)
{
	// If the name ends with .png then don't modify it
	if( !strcasecmp(name + strlen(name) - 4, ".png") )
	{
		const char *p = strrchr(name, '/');
		make_fullpath(SCREENSHOT_DIR);
		if( p )
		{
			snprintf(out_name, buflen, "%s%s", SCREENSHOT_DIR, p);
		}
		else
		{
			snprintf(out_name, buflen, "%s/%s", SCREENSHOT_DIR, name);
		}
	}
	else
	{
		create_path(SCREENSHOT_DIR, CoreName);

		time_t t = time(NULL);
		struct tm tm = *localtime(&t);
		char datecode[32] = {};
		if (tm.tm_year >= 119) // 2019 or up considered valid time
		{
			strftime(datecode, 31, "%Y%m%d_%H%M%S", &tm);
			snprintf(out_name, buflen, "%s/%s/%s-%s.png", SCREENSHOT_DIR, CoreName, datecode, name[0] ? name : SCREENSHOT_DEFAULT);
		}
		else
		{
			for (int i = 1; i < 10000; i++)
			{
				snprintf(out_name, buflen, "%s/%s/NODATE-%s_%04d.png", SCREENSHOT_DIR, CoreName, name[0] ? name : SCREENSHOT_DEFAULT, i);
				if (!getFileType(out_name)) return;
			}
		}
	}
}

void FileGenerateSavePath(const char *name, char* out_name, int ext_replace)
{
	create_path(SAVE_DIR, CoreName);

	sprintf(out_name, "%s/%s/", SAVE_DIR, CoreName);
	char *fname = out_name + strlen(out_name);

	const char *p = strrchr(name, '/');
	if (p)
	{
		strcat(fname, p+1);
	}
	else
	{
		strcat(fname, name);
	}

	char *e = strrchr(fname, '.');
	if (ext_replace && e)
	{
		strcpy(e,".sav");
	}
	else
	{
		strcat(fname, ".sav");
	}

	printf("SavePath=%s\n", out_name);
}

void FileGenerateSavestatePath(const char *name, char* out_name, int sufx)
{
	create_path(SAVESTATE_DIR, CoreName);

	sprintf(out_name, "%s/%s/", SAVESTATE_DIR, CoreName);
	char *fname = out_name + strlen(out_name);

	const char *p = strrchr(name, '/');
	if (p)
	{
		strcat(fname, p + 1);
	}
	else
	{
		strcat(fname, name);
	}

	char *e = strrchr(fname, '.');
	if (e) e[0] = 0;

	if(sufx) sprintf(e, "_%d.ss", sufx);
	else strcat(e, ".ss");
}

uint32_t getFileType(const char *name)
{
	make_fullpath(name);

	struct stat64 st;
	if (stat64(full_path, &st)) return 0;

	return st.st_mode;
}

int findPrefixDir(char *dir, size_t dir_len)
{
	// Searches for the core's folder in the following order:
	// /media/fat
	// /media/usb<0..5>
	// /media/usb<0..5>/games
	// /media/fat/cifs
	// /media/fat/cifs/games
	// /media/fat/games/
	// if the core folder is not found anywhere,
	// it will be created in /media/fat/games/<dir>
	static char temp_dir[1024];

	for (int x = 0; x < 6; x++) {
		snprintf(temp_dir, 1024, "%s%d/%s", "../usb", x, dir);
		if (isPathDirectory(temp_dir)) {
			printf("Found USB dir: %s\n", temp_dir);
			strncpy(dir, temp_dir, dir_len);
			return 1;
		}

		snprintf(temp_dir, 1024, "%s%d/%s/%s", "../usb", x, GAMES_DIR, dir);
		if (isPathDirectory(temp_dir)) {
			printf("Found USB dir: %s\n", temp_dir);
			strncpy(dir, temp_dir, dir_len);
			return 1;
		}
	}

	snprintf(temp_dir, 1024, "%s/%s", CIFS_DIR, dir);
	if (isPathDirectory(temp_dir)) {
		printf("Found CIFS dir: %s\n", temp_dir);
		strncpy(dir, temp_dir, dir_len);
		return 1;
	}

	snprintf(temp_dir, 1024, "%s/%s/%s", CIFS_DIR, GAMES_DIR, dir);
	if (isPathDirectory(temp_dir)) {
		printf("Found CIFS dir: %s\n", temp_dir);
		strncpy(dir, temp_dir, dir_len);
		return 1;
	}

	if (isPathDirectory(dir)) {
		printf("Found existing: %s\n", dir);
		return 1;
	}

	snprintf(temp_dir, 1024, "%s/%s", GAMES_DIR, dir);
	if (isPathDirectory(temp_dir)) {
		printf("Found dir: %s\n", temp_dir);
		strncpy(dir, temp_dir, dir_len);
		return 1;
	}

	return 0;
}

void prefixGameDir(char *dir, size_t dir_len)
{
	if (!findPrefixDir(dir, dir_len))
	{
		static char temp_dir[1024];

		//FileCreatePath(GAMES_DIR);
		snprintf(temp_dir, 1024, "%s/%s", GAMES_DIR, dir);
		strncpy(dir, temp_dir, dir_len);
		printf("Prefixed dir to %s\n", temp_dir);
	}
}

static int device = 0;
static int usbnum = 0;
const char *getStorageDir(int dev)
{
	static char path[32];
	if (!dev) return "/media/fat";
	sprintf(path, "/media/usb%d", usbnum);
	return path;
}

const char *getRootDir()
{
	return getStorageDir(device);
}

const char *getFullPath(const char *name)
{
	make_fullpath(name);
	return full_path;
}

void setStorage(int dev)
{
	device = 0;
	FileSave(CONFIG_DIR"/device.bin", &dev, sizeof(int));
	fpga_load_rbf("menu.rbf");
}

static int orig_device = 0;
int getStorage(int from_setting)
{
	return from_setting ? orig_device : device;
}

int isPathMounted(int n)
{
	char path[32];
	sprintf(path, "/media/usb%d", n);

	struct stat file_stat;
	struct stat parent_stat;

	if (-1 == stat(path, &file_stat))
	{
		printf("failed to stat %s\n", path);
		return 0;
	}

	if (!(file_stat.st_mode & S_IFDIR))
	{
		printf("%s is not a directory.\n", path);
		return 0;
	}

	if (-1 == stat("/media", &parent_stat))
	{
		printf("failed to stat /media\n");
		return 0;
	}

	if (file_stat.st_dev != parent_stat.st_dev ||
		(file_stat.st_dev == parent_stat.st_dev &&
			file_stat.st_ino == parent_stat.st_ino))
	{
		printf("%s IS a mountpoint.\n", path);
		struct statfs fs_stat;
		if (!statfs(path, &fs_stat))
		{
			printf("%s is FS: 0x%08X\n", path, fs_stat.f_type);
			if (fs_stat.f_type != EXT4_SUPER_MAGIC)
			{
				printf("%s is not EXT2/3/4.\n", path);
				return 1;
			}
		}
	}

	printf("%s is NOT a VFAT mountpoint.\n", path);
	return 0;
}

int isUSBMounted()
{
	for (int i = 0; i < 4; i++)
	{
		if (isPathMounted(i))
		{
			usbnum = i;
			return 1;
		}
	}
	return 0;
}

void FindStorage(void)
{
	char str[128];
	printf("Looking for root device...\n");
	device = 0;
	FileLoad(CONFIG_DIR"/device.bin", &device, sizeof(int));
	orig_device = device;

	if(device && !isUSBMounted())
	{
		uint8_t core_type = (fpga_core_id() & 0xFF);
		if (core_type == CORE_TYPE_8BIT)
		{
			user_io_read_confstr();
			user_io_read_core_name();
		}

		int saveddev = device;
		device = 0;
		cfg_parse();
		device = saveddev;
		video_mode_load();
		user_io_send_buttons(1);

		printf("Waiting for USB...\n");
		int btn = 0;
		int done = 0;

		OsdWrite(16, "", 1);
		OsdWrite(17, "       www.MiSTerFPGA.org       ", 1);
		OsdWrite(18, "", 1);

		for (int i = 30; i >= 0; i--)
		{
			sprintf(str, "\n     Waiting for USB...\n\n             %d   \n\n\n  OSD/USER or ESC to cancel", i);
			InfoMessage(str);
			if (isUSBMounted())
			{
				done = 1;
				break;
			}

			for (int i = 0; i < 10; i++)
			{
				btn = fpga_get_buttons();
				if (!btn) btn = input_poll(1);
				if (btn)
				{
					printf("Button has been pressed %d\n", btn);
					InfoMessage("\n\n         Canceled!\n");
					usleep(500000);
					setStorage(0);
					break;
				}
				usleep(100000);
			}
			if (done) break;
		}

		if (!done)
		{
			InfoMessage("\n\n     No USB storage found\n   Falling back to SD card\n");
			usleep(2000000);
			setStorage(0);
		}
	}

	if (device)
	{
		printf("Using USB as a root device\n");
	}
	else
	{
		printf("Using SD card as a root device\n");
	}

	sprintf(full_path, "%s/" CONFIG_DIR, getRootDir());
	DIR* dir = opendir(full_path);
	if (dir) closedir(dir);
	else if (ENOENT == errno) mkdir(full_path, S_IRWXU | S_IRWXG | S_IRWXO);
}

struct DirentComp
{
	bool operator()(const direntext_t& de1, const direntext_t& de2)
	{

#ifdef USE_SCHEDULER
		if (++iterations % YieldIterations == 0)
		{
			scheduler_yield();
		}
#endif

		if ((de1.de.d_type == DT_DIR) && !strcmp(de1.altname, "..")) return true;
		if ((de2.de.d_type == DT_DIR) && !strcmp(de2.altname, "..")) return false;

		if ((de1.de.d_type == DT_DIR) && (de2.de.d_type != DT_DIR)) return true;
		if ((de1.de.d_type != DT_DIR) && (de2.de.d_type == DT_DIR)) return false;

		int len1 = strlen(de1.altname);
		int len2 = strlen(de2.altname);
		if ((len1 > 4) && (de1.altname[len1 - 4] == '.')) len1 -= 4;
		if ((len2 > 4) && (de2.altname[len2 - 4] == '.')) len2 -= 4;

		int len = (len1 < len2) ? len1 : len2;
		int ret = strncasecmp(de1.altname, de2.altname, len);
		if (!ret)
		{
			if(len1 != len2)
			{
				return len1 < len2;
			}
			ret = strcasecmp(de1.datecode, de2.datecode);
		}

		return ret < 0;
	}

	size_t iterations = 0;
};

void AdjustDirectory(char *path)
{
	if (!FileExists(path)) return;

	char *p = strrchr(path, '/');
	if (p)
	{
		*p = 0;
	}
	else
	{
		path[0] = 0;
	}
}

static const char *GetRelativeFileName(const char *folder, const char *path) {
  if (strcasestr(path, folder) == path) {
    const char *subpath = path + strlen(folder);
    if (*subpath != '\0')
    {
      if (*subpath == '/')
      {
        return subpath+1;
      }
      return subpath;
    }
  }
  return NULL;
}

static bool IsInSameFolder(const char *folder, const char *path)
{
	if (strcasestr(path, folder) == path)
	{
		const char *subpath = path + strlen(folder) + 1;
		if (*subpath != '\0')
		{
			const char *slash = strchr(subpath, '/');
			return !slash || *(slash + 1) == '\0';
		}
	}
	return false;
}

static int names_loaded = 0;
static void get_display_name(direntext_t *dext, const char *ext, int options)
{
	static char *names = 0;
	memcpy(dext->altname, dext->de.d_name, sizeof(dext->altname));
	if (dext->de.d_type == DT_DIR) return;

	int len = strlen(dext->altname);
	int rbf = (len > 4 && !strcasecmp(dext->altname + len - 4, ".rbf"));
	if (rbf)
	{
		dext->altname[len - 4] = 0;
		char *p = strstr(dext->altname, "_20");
		if (p) if (strlen(p + 3) < 6) p = 0;
		if (p)
		{
			*p = 0;
			strncpy(dext->datecode, p + 3, 15);
			dext->datecode[15] = 0;
		}
		else
		{
			strcpy(dext->datecode, "------");
		}

		if (!names_loaded)
		{
			if (names)
			{
				free(names);
				names = 0;
			}

			int size = FileLoad("names.txt", 0, 0);
			if (size)
			{
				names = (char*)malloc(size + 1);
				if (names)
				{
					names[0] = 0;
					FileLoad("names.txt", names, 0);
					names[size] = 0;
				}
			}
			names_loaded = 1;
		}

		if (names)
		{
			strcat(dext->altname, ":");
			len = strlen(dext->altname);
			char *transl = strstr(names, dext->altname);
			if (transl)
			{
				int copy = 0;
				transl += len;
				len = 0;
				while (*transl && len < (int)sizeof(dext->altname) - 1)
				{
					if (!copy && *transl <= 32)
					{
						transl++;
						continue;
					}

					if (copy && *transl < 32) break;

					copy = 1;
					dext->altname[len++] = *transl++;
				}
				len++;
			}

			dext->altname[len - 1] = 0;
		}
		return;
	}

	//do not remove ext if core supplies more than 1 extension and it's not list of cores
	if (!(options & SCANO_CORES) && strlen(ext) > 3) return;
	if (strchr(ext, '*') || strchr(ext, '?')) return;

	/* find the extension on the end of the name*/
	char *fext = strrchr(dext->altname, '.');
	if (fext) *fext = 0;
}

int ScanDirectory(char* path, int mode, const char *extension, int options, const char *prefix, const char *filter)
{
	static char file_name[1024];
	static char full_path[1024];

	int has_trd = 0;
	const char *ext = extension;
	while (*ext)
	{
		if (!strncasecmp(ext, "TRD", 3)) has_trd = 1;
		ext += 3;
	}

	int extlen = strlen(extension);
    int filterlen = filter ? strlen(filter) : 0;
	//printf("scan dir\n");

	if (mode == SCANF_INIT)
	{
		iFirstEntry = 0;
		iSelectedEntry = 0;
		DirItem.clear();
		DirNames.clear();

		file_name[0] = 0;

		if ((options & SCANO_NOENTER) || isPathRegularFile(path))
		{
			char *p = strrchr(path, '/');
			if (p)
			{
				strcpy(file_name, p + 1);
				*p = 0;
			}
			else
			{
				strcpy(file_name, path);
				path[0] = 0;
			}
		}

		if (!isPathDirectory(path)) return 0;
		snprintf(scanned_path, sizeof(scanned_path), "%s", path);
		scanned_opts = options;

		if (options & SCANO_NEOGEO) neogeo_scan_xml(path);

		sprintf(full_path, "%s/%s", getRootDir(), path);
		int path_len = strlen(full_path);

		const char* is_zipped = strcasestr(full_path, ".zip");
		if (is_zipped && strcasestr(is_zipped + 4, ".zip"))
		{
			printf("Nested zip-files are not supported: %s\n", full_path);
			return 0;
		}

		printf("Start to scan %sdir: %s\n", is_zipped ? "zipped " : "", full_path);
		printf("Position on item: %s\n", file_name);

		char *zip_path, *file_path_in_zip = (char*)"";
		FileIsZipped(full_path, &zip_path, &file_path_in_zip);

		DIR *d = nullptr;
		mz_zip_archive *z = nullptr;
		if (is_zipped)
		{
			if (!OpenZipfileCached(full_path, 0))
			{
				printf("Couldn't open zip file %s: %s\n", full_path, mz_zip_get_error_string(mz_zip_get_last_error(&last_zip_archive)));
				return 0;
			}
			z = &last_zip_archive;
		}
		else
		{
			d = opendir(full_path);
			if (!d)
			{
				printf("Couldn't open dir: %s\n", full_path);
				return 0;
			}
		}

		struct dirent64 *de = nullptr;
		for (size_t i = 0; (d && (de = readdir64(d)))
				 || (z && i < mz_zip_reader_get_num_files(z)); i++)
		{
#ifdef USE_SCHEDULER
			if (0 < i && i % YieldIterations == 0)
			{
				scheduler_yield();
			}
#endif
			struct dirent64 _de = {};
			if (z) {
        mz_zip_reader_get_filename(z, i, &_de.d_name[0], sizeof(_de.d_name));
        const char *rname = GetRelativeFileName(file_path_in_zip, _de.d_name);
        if (rname) {
          const char *fslash = strchr(rname, '/');
          if (fslash) {

            char dirname[256] = {};
            strncpy(dirname, rname, fslash - rname);
            if (rname[0] != '/' && !(DirNames.find(dirname) != DirNames.end())) {
              direntext_t dirext;
              memset(&dirext, 0, sizeof(dirext));
              strncpy(dirext.de.d_name, rname, fslash - rname);
              dirext.de.d_type = DT_DIR;
              memcpy(dirext.altname, dirext.de.d_name,
                     sizeof(dirext.de.d_name));
              DirItem.push_back(dirext);
              DirNames.insert(dirname);
            }
          }
        }

        if (!IsInSameFolder(file_path_in_zip, _de.d_name)) {
          continue;
        }
        // Remove leading folders.
        const char *subpath = _de.d_name + strlen(file_path_in_zip);
        if (*subpath == '/') {
          subpath++;
        }
        strcpy(_de.d_name, subpath);

        de = &_de;

        _de.d_type = mz_zip_reader_is_file_a_directory(z, i) ? DT_DIR : DT_REG;
        if (_de.d_type == DT_DIR) {
          // Remove trailing slash.
          if (DirNames.find(_de.d_name) != DirNames.end())
          {
            DirNames.insert(_de.d_name);
            _de.d_name[strlen(_de.d_name) - 1] = '\0';
          } else {
            continue;
          }
        }
			}
			else
			// Handle (possible) symbolic link type in the directory entry
			if (de->d_type == DT_LNK || de->d_type == DT_REG)
			{
				sprintf(full_path+path_len, "/%s", de->d_name);

				struct stat entrystat;

				if (!stat(full_path, &entrystat))
				{
					if (S_ISREG(entrystat.st_mode))
					{
						de->d_type = DT_REG;
					}
					else if (S_ISDIR(entrystat.st_mode))
					{
						de->d_type = DT_DIR;
					}
				}
			}

            if (filter) {
                bool passes_filter = false;

                for(const char *str = de->d_name; *str; str++) {
                    if (strncasecmp(str, filter, filterlen) == 0) {
                        passes_filter = true;
                        break;
                    }
                }

                if (!passes_filter) continue;
            }


			if (options & SCANO_NEOGEO)
			{
				if (de->d_type == DT_REG && !strcasecmp(de->d_name + strlen(de->d_name) - 4, ".zip"))
				{
					de->d_type = DT_DIR;
				}

				if (strcasecmp(de->d_name + strlen(de->d_name) - 4, ".neo"))
				{
					if (de->d_type != DT_DIR) continue;
				}

				if (!strcmp(de->d_name, ".."))
				{
					if (!strlen(path)) continue;
				}
				else
				{
					// skip hidden folders
					if (!strncasecmp(de->d_name, ".", 1)) continue;
				}

				direntext_t dext;
				memset(&dext, 0, sizeof(dext));
				memcpy(&dext.de, de, sizeof(dext.de));
				memcpy(dext.altname, de->d_name, sizeof(dext.altname));
				if (!strcasecmp(dext.altname + strlen(dext.altname) - 4, ".zip")) dext.altname[strlen(dext.altname) - 4] = 0;

				full_path[path_len] = 0;
				char *altname = neogeo_get_altname(full_path, dext.de.d_name, dext.altname);
				if (altname)
				{
					if (altname == (char*)-1) continue;

					dext.de.d_type = DT_REG;
					memcpy(dext.altname, altname, sizeof(dext.altname));
				}

				DirItem.push_back(dext);
			}
			else
			{
				if (de->d_type == DT_DIR)
				{
					// skip System Volume Information folder
					if (!strcmp(de->d_name, "System Volume Information")) continue;
					if (!strcmp(de->d_name, ".."))
					{
						if (!strlen(path)) continue;
					}
					else
					{
						// skip hidden folder
						if (!strncasecmp(de->d_name, ".", 1)) continue;
					}

					if (!(options & SCANO_DIR))
					{
						if (de->d_name[0] != '_' && strcmp(de->d_name, "..")) continue;
						if (!(options & SCANO_CORES)) continue;
					}
				}
				else if (de->d_type == DT_REG)
				{
					// skip hidden files
					if (!strncasecmp(de->d_name, ".", 1)) continue;
					//skip non-selectable files
					if (!strcasecmp(de->d_name, "menu.rbf")) continue;
					if (!strncasecmp(de->d_name, "menu_20", 7)) continue;
					if (!strcasecmp(de->d_name, "boot.rom")) continue;

					//check the prefix if given
					if (prefix && strncasecmp(prefix, de->d_name, strlen(prefix))) continue;

					if (extlen > 0)
					{
						const char *ext = extension;
						int found = (has_trd && x2trd_ext_supp(de->d_name));
						if (!found && !(options & SCANO_NOZIP) && !strcasecmp(de->d_name + strlen(de->d_name) - 4, ".zip") && (options & SCANO_DIR))
						{
							// Fake that zip-file is a directory.
							de->d_type = DT_DIR;
							found = 1;
						}
						if (!found && is_minimig() && !memcmp(extension, "HDF", 3))
						{
							found = !strcasecmp(de->d_name + strlen(de->d_name) - 4, ".iso");
						}

						char *fext = strrchr(de->d_name, '.');
						if (fext) fext++;
						while (!found && *ext && fext)
						{
							char e[4];
							memcpy(e, ext, 3);
							if (e[2] == ' ')
							{
								e[2] = 0;
								if (e[1] == ' ') e[1] = 0;
							}

							e[3] = 0;
							found = 1;
							for (int i = 0; i < 4; i++)
							{
								if (e[i] == '*') break;
								if (e[i] == '?' && fext[i]) continue;

								if (tolower(e[i]) != tolower(fext[i])) found = 0;

								if (!e[i] || !found) break;
							}
							if (found) break;

							if (strlen(ext) < 3) break;
							ext += 3;
						}
						if (!found) continue;
					}
				}
				else
				{
					continue;
				}

        {
			      direntext_t dext;
				    memset(&dext, 0, sizeof(dext));
				    memcpy(&dext.de, de, sizeof(dext.de));
				    get_display_name(&dext, extension, options);
				    DirItem.push_back(dext);
        }
			}
		}

		if (z)
		{
			// Since zip files aren't actually folders the entry to
			// exit the zip file must be added manually.
			direntext_t dext;
			memset(&dext, 0, sizeof(dext));
			dext.de.d_type = DT_DIR;
			strcpy(dext.de.d_name, "..");
			get_display_name(&dext, extension, options);
			DirItem.push_back(dext);
		}

		if (d)
		{
			closedir(d);
		}

		printf("Got %d dir entries\n", flist_nDirEntries());
		if (!flist_nDirEntries()) return 0;

		std::sort(DirItem.begin(), DirItem.end(), DirentComp());
		if (file_name[0])
		{
			int pos = -1;
			for (int i = 0; i < flist_nDirEntries(); i++)
			{
				if (!strcmp(file_name, DirItem[i].de.d_name))
				{
					pos = i;
					break;
				}
				else if (!strcasecmp(file_name, DirItem[i].de.d_name))
				{
					pos = i;
				}
			}

			if(pos>=0)
			{
				iSelectedEntry = pos;
				if (iSelectedEntry + (OsdGetSize() / 2) >= flist_nDirEntries()) iFirstEntry = flist_nDirEntries() - OsdGetSize();
				else iFirstEntry = iSelectedEntry - (OsdGetSize() / 2) + 1;
				if (iFirstEntry < 0) iFirstEntry = 0;
			}
		}
		return flist_nDirEntries();
	}
	else
	{
		if (flist_nDirEntries() == 0) // directory is empty so there is no point in searching for any entry
			return 0;

		if (mode == SCANF_END || (mode == SCANF_PREV && iSelectedEntry <= 0))
		{
			iSelectedEntry = flist_nDirEntries() - 1;
			iFirstEntry = iSelectedEntry - OsdGetSize() + 1;
			if (iFirstEntry < 0) iFirstEntry = 0;
			return 0;
		}
		else if (mode == SCANF_NEXT)
		{
			if(iSelectedEntry + 1 < flist_nDirEntries()) // scroll within visible items
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
            return 0;
		}
		else if (mode == SCANF_PREV)
		{
			if (iSelectedEntry > 0) // scroll within visible items
			{
				iSelectedEntry--;
				if (iSelectedEntry < iFirstEntry) iFirstEntry = iSelectedEntry;
			}
            return 0;
		}
		else if (mode == SCANF_NEXT_PAGE)
		{
			if (iSelectedEntry < iFirstEntry + OsdGetSize() - 2)
			{
				iSelectedEntry = iFirstEntry + OsdGetSize() - 1;
				if (iSelectedEntry >= flist_nDirEntries()) iSelectedEntry = flist_nDirEntries() - 1;
			}
			else
			{
				iSelectedEntry += OsdGetSize();
				iFirstEntry += OsdGetSize();
				if (iSelectedEntry >= flist_nDirEntries())
				{
					iSelectedEntry = flist_nDirEntries() - 1;
					iFirstEntry = iSelectedEntry - OsdGetSize() + 1;
					if (iFirstEntry < 0) iFirstEntry = 0;
				}
				else if (iFirstEntry + OsdGetSize() > flist_nDirEntries())
				{
					iFirstEntry = flist_nDirEntries() - OsdGetSize();
				}
			}
			return 0;
		}
		else if (mode == SCANF_PREV_PAGE)
		{
			if(iSelectedEntry != iFirstEntry)
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
		else if (mode == SCANF_SET_ITEM)
		{
			int pos = -1;
			for (int i = 0; i < flist_nDirEntries(); i++)
			{
				if ((DirItem[i].de.d_type == DT_DIR) && !strcmp(DirItem[i].altname, extension))
				{
					pos = i;
					break;
				}
				else if ((DirItem[i].de.d_type == DT_DIR) && !strcasecmp(DirItem[i].altname, extension))
				{
					pos = i;
				}
			}

			if(pos>=0)
			{
				iSelectedEntry = pos;
				if (iSelectedEntry + (OsdGetSize() / 2) >= flist_nDirEntries()) iFirstEntry = flist_nDirEntries() - OsdGetSize();
				else iFirstEntry = iSelectedEntry - (OsdGetSize() / 2) + 1;
				if (iFirstEntry < 0) iFirstEntry = 0;
			}
		}
		else
		{
			//printf("dir scan for key: %x/%c\n", mode, mode);
			mode = toupper(mode);
			if ((mode >= '0' && mode <= '9') || (mode >= 'A' && mode <= 'Z'))
			{
				int found = -1;
				for (int i = iSelectedEntry+1; i < flist_nDirEntries(); i++)
				{
					if (toupper(DirItem[i].altname[0]) == mode)
					{
						found = i;
						break;
					}
				}

				if (found < 0)
				{
					for (int i = 0; i < flist_nDirEntries(); i++)
					{
						if (toupper(DirItem[i].altname[0]) == mode)
						{
							found = i;
							break;
						}
					}
				}

				if (found >= 0)
				{
					iSelectedEntry = found;
					if (iSelectedEntry + (OsdGetSize() / 2) >= flist_nDirEntries()) iFirstEntry = flist_nDirEntries() - OsdGetSize();
					else iFirstEntry = iSelectedEntry - (OsdGetSize()/2) + 1;
					if (iFirstEntry < 0) iFirstEntry = 0;
				}
			}
		}
	}

	return 0;
}

char* flist_Path()
{
	return scanned_path;
}

int flist_nDirEntries()
{
	return DirItem.size();
}

int flist_iFirstEntry()
{
	return iFirstEntry;
}

void flist_iFirstEntryInc()
{
	iFirstEntry++;
}

int flist_iSelectedEntry()
{
	return iSelectedEntry;
}

direntext_t* flist_DirItem(int n)
{
	return &DirItem[n];
}

direntext_t* flist_SelectedItem()
{
	return &DirItem[iSelectedEntry];
}

char* flist_GetPrevNext(const char* base_path, const char* file, const char* ext, int next)
{
	static char path[1024];
	snprintf(path, sizeof(path), "%s/%s", base_path, file);
	char *p = strrchr(path, '/');
	if (!FileExists(path))
	{
		snprintf(path, sizeof(path), "%s", base_path);
		p = 0;
	}

	int len = (p) ? p - path : strlen(path);
	if (strncasecmp(scanned_path, path, len) || (scanned_opts & SCANO_DIR)) ScanDirectory(path, SCANF_INIT, ext, 0);

	if (!DirItem.size()) return NULL;
	if (p) ScanDirectory(path, next ? SCANF_NEXT : SCANF_PREV, "", 0);
	snprintf(path, sizeof(path), "%s/%s", scanned_path, DirItem[iSelectedEntry].de.d_name);

	return path + strlen(base_path) + 1;
}

bool isMraName(char *path)
{
	char *spl = strrchr(path, '.');
	return (spl && !strcmp(spl, ".mra"));
}

fileTextReader::fileTextReader()
{
	buffer = nullptr;
}

fileTextReader::~fileTextReader()
{
	if( buffer != nullptr )
	{
		free(buffer);
	}
	buffer = nullptr;
}

bool FileOpenTextReader( fileTextReader *reader, const char *filename )
{
	fileTYPE f;

	// ensure buffer is freed if the reader is being reused
	reader->~fileTextReader();

	if (FileOpen(&f, filename))
	{
		char *buf = (char*)malloc(f.size+1);
		if (buf)
		{
			memset(buf, 0, f.size + 1);
			int size;
			if ((size = FileReadAdv(&f, buf, f.size)))
			{
				reader->size = f.size;
				reader->buffer = buf;
				reader->pos = reader->buffer;
				return true;
			}
		}
	}
	return false;
}

#define IS_NEWLINE(c) (((c) == '\r') || ((c) == '\n'))
#define IS_WHITESPACE(c) (IS_NEWLINE(c) || ((c) == ' ') || ((c) == '\t'))

const char *FileReadLine(fileTextReader *reader)
{
	const char *end = reader->buffer + reader->size;
	while (reader->pos < end)
	{
		char *st = reader->pos;
		while ((reader->pos < end) && *reader->pos && !IS_NEWLINE(*reader->pos))
			reader->pos++;
		*reader->pos = 0;
		while (IS_WHITESPACE(*st))
			st++;
		if (*st == '#' || *st == ';' || !*st)
		{
			reader->pos++;
		}
		else
		{
			return st;
		}
	}
	return nullptr;
}
