#include "file_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
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
#include "osd.h"
#include "fpga_io.h"
#include "menu.h"
#include "errno.h"
#include "DiskImage.h"
#include "user_io.h"
#include "cfg.h"
#include "input.h"
#include "miniz_zip.h"
#include "scheduler.h"
#include "video.h"
#include "support.h"

#define MIN(a,b) (((a)<(b)) ? (a) : (b))

typedef std::vector<direntext_t> DirentVector;

static const size_t YieldIterations = 128;

DirentVector DirItem;
static int iSelectedEntry = 0;       // selected entry index
static int iFirstEntry = 0;

static char full_path[2100];

struct fileZipArchive
{
	mz_zip_archive                    archive;
	int                               index;
	mz_zip_reader_extract_iter_state* iter;
	__off64_t                         offset;
};

static bool FileIsZipped(char* path, char** zip_path, char** file_path)
{
	char* z = strcasestr(path, ".zip");
	if (z)
	{
		z += 4;
		if (!z[0]) z[1] = 0;
		*z++ = 0;

		if (zip_path) *zip_path = path;
		if (file_path) *file_path = z;
		return true;
	}
	return false;
}

static char* make_fullpath(const char *path, int mode = 0)
{
	const char *root = getRootDir();
	if (strncasecmp(getRootDir(), path, strlen(root)))
	{
		sprintf(full_path, "%s/%s", (mode == -1) ? "" : root, path);
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

static bool isPathDirectory(char *path)
{
	make_fullpath(path);

	char *zip_path, *file_path;
	if (FileIsZipped(full_path, &zip_path, &file_path))
	{
		mz_zip_archive z{};
		if (!mz_zip_reader_init_file(&z, zip_path, 0))
		{
			printf("isPathDirectory(mz_zip_reader_init_file) Zip:%s, error:%s\n", zip_path,
			       mz_zip_get_error_string(mz_zip_get_last_error(&z)));
			return false;
		}

		if (!*file_path)
		{
			mz_zip_reader_end(&z);
			return true;
		}

		// Folder names always end with a slash in the zip
		// file central directory.
		strcat(file_path, "/");
		const int file_index = mz_zip_reader_locate_file(&z, file_path, NULL, 0);
		if (file_index < 0)
		{
			printf("isPathDirectory(mz_zip_reader_locate_file) Zip:%s, file:%s, error: %s\n",
					 zip_path, file_path,
					 mz_zip_get_error_string(mz_zip_get_last_error(&z)));
			mz_zip_reader_end(&z);
			return false;
		}

		if (mz_zip_reader_is_file_a_directory(&z, file_index))
		{
			mz_zip_reader_end(&z);
			return true;
		}
		mz_zip_reader_end(&z);
	}
	else
	{
		int stmode = get_stmode(full_path);
		if (!stmode)
		{
			printf("isPathDirectory(stat) path:%s, error:%s.\n", full_path, strerror(errno));
			return false;
		}

		if (stmode & S_IFDIR) return true;
	}

	return false;
}

static bool isPathRegularFile(const char *path)
{
	make_fullpath(path);

	char *zip_path, *file_path;
	if (FileIsZipped(full_path, &zip_path, &file_path))
	{
		mz_zip_archive z{};
		if (!mz_zip_reader_init_file(&z, zip_path, 0))
		{
			//printf("isPathRegularFile(mz_zip_reader_init_file) Zip:%s, error:%s\n", zip_path,
			//       mz_zip_get_error_string(mz_zip_get_last_error(&z)));
			return false;
		}

		if (!*file_path)
		{
			mz_zip_reader_end(&z);
			return false;
		}

		const int file_index = mz_zip_reader_locate_file(&z, file_path, NULL, 0);
		if (file_index < 0)
		{
			//printf("isPathRegularFile(mz_zip_reader_locate_file) Zip:%s, file:%s, error: %s\n",
			//		 zip_path, file_path,
			//		 mz_zip_get_error_string(mz_zip_get_last_error(&z)));
			mz_zip_reader_end(&z);
			return false;
		}

		if (!mz_zip_reader_is_file_a_directory(&z, file_index) && mz_zip_reader_is_file_supported(&z, file_index))
		{
			mz_zip_reader_end(&z);
			return true;
		}
		mz_zip_reader_end(&z);
	}
	else
	{
		if (get_stmode(full_path) & S_IFREG) return true;
	}

	return false;
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
		file->zip = nullptr;
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
	file->filp = nullptr;
}

int FileOpenEx(fileTYPE *file, const char *name, int mode, char mute)
{
	make_fullpath((char*)name, mode);

	FileClose(file);
	file->mode = 0;
	file->type = 0;

	char *p = strrchr(full_path, '/');
	strcpy(file->name, (mode == -1) ? full_path : p + 1);

	char *zip_path, *file_path;
	if ((mode != -1) && FileIsZipped(full_path, &zip_path, &file_path))
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
		int fd = (mode == -1) ? shm_open("/vtrd", O_CREAT | O_RDWR | O_TRUNC, 0777) : open(full_path, mode, 0777);
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
		offset = fseeko64(file->filp, offset, origin);
		if(offset<0)
		{
			printf("Fail to seek the file.\n");
			return 0;
		}
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

		char buf[512];
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
int FileReadAdv(fileTYPE *file, void *pBuffer, int length)
{
	ssize_t ret = 0;

	if (file->filp)
	{
		ret = fread(pBuffer, 1, length, file->filp);
		if (ret < 0)
		{
			printf("FileReadAdv error(%d).\n", ret);
			return 0;
		}
	}
	else if (file->zip)
	{
		ret = mz_zip_reader_extract_iter_read(file->zip->iter, pBuffer, length);
		if (!ret)
		{
			printf("FileReadEx(mz_zip_reader_extract_iter_read) Failed to read, error:%s\n",
			       mz_zip_get_error_string(mz_zip_get_last_error(&file->zip->archive)));
			return 0;
		}
		file->zip->offset += ret;
	}
	else
	{
		printf("FileReadAdv error(unknown file type).\n");
		return 0;
	}

	file->offset += ret;
	return ret;
}

int FileReadSec(fileTYPE *file, void *pBuffer)
{
	return FileReadAdv(file, pBuffer, 512);
}

// Write with offset advancing
int FileWriteAdv(fileTYPE *file, void *pBuffer, int length)
{
	int ret;

	if (file->filp)
	{
		ret = fwrite(pBuffer, 1, length, file->filp);
		fflush(file->filp);

		if (ret < 0)
		{
			printf("FileWriteAdv error(%d).\n", ret);
			return 0;
		}
	}
	else if (file->zip)
	{
		printf("FileWriteAdv error(not supported for zip).\n");
		return 0;
	}
	else
	{
		printf("FileWriteAdv error(unknown file type).\n");
		return 0;
	}

	file->offset += ret;
	return ret;
}

int FileWriteSec(fileTYPE *file, void *pBuffer)
{
	return FileWriteAdv(file, pBuffer, 512);
}

int FileSave(const char *name, void *pBuffer, int size)
{
	if(name[0] != '/') sprintf(full_path, "%s/%s", getRootDir(), name);
	else strcpy(full_path, name);

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

int FileSaveConfig(const char *name, void *pBuffer, int size)
{
	char path[256] = { CONFIG_DIR"/" };
	strcat(path, name);
	return FileSave(path, pBuffer, size);
}

int FileSaveJoymap(const char *name, void *pBuffer, int size)
{
	char path[256] = { CONFIG_DIR"/inputs/" };
	FileCreatePath(path);
	strcat(path, name);
	return FileSave(path, pBuffer, size);
}

int FileLoad(const char *name, void *pBuffer, int size)
{
	if (name[0] != '/') sprintf(full_path, "%s/%s", getRootDir(), name);
	else strcpy(full_path, name);

	int fd = open(full_path, O_RDONLY);
	if (fd < 0)
	{
		printf("FileLoad(open) File:%s, error: %d.\n", full_path, fd);
		return 0;
	}

	struct stat64 st;
	int ret = fstat64(fd, &st);
	if (ret < 0)
	{
		printf("FileLoad(fstat) File:%s, error: %d.\n", full_path, ret);
		close(fd);
		return 0;
	}

	if (!pBuffer)
	{
		close(fd);
		return (int)st.st_size;
	}

	ret = read(fd, pBuffer, size ? size : st.st_size);
	close(fd);

	if (ret < 0)
	{
		printf("FileLoad(read) File:%s, error: %d.\n", full_path, ret);
		return 0;
	}

	return ret;
}

int FileLoadConfig(const char *name, void *pBuffer, int size)
{
	char path[256] = { CONFIG_DIR"/" };
	strcat(path, name);
	return FileLoad(path, pBuffer, size);
}

int FileLoadJoymap(const char *name, void *pBuffer, int size)
{
	char path[256] = { CONFIG_DIR"/inputs/" };
	strcat(path, name);
	int ret = FileLoad(path, pBuffer, size);
	if (!ret)
		return FileLoadConfig(name, pBuffer, size);
	return ret;
}

int FileExists(const char *name)
{
	return isPathRegularFile(name);
}

int FileCanWrite(const char *name)
{
	sprintf(full_path, "%s/%s", getRootDir(), name);

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

void FileCreatePath(char *dir)
{
	if (!isPathDirectory(dir)) {
		make_fullpath(dir);
		mkdir(full_path, S_IRWXU | S_IRWXG | S_IRWXO);
	}
}

void FileGenerateScreenshotName(const char *name, char *out_name, int buflen)
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

void FileGenerateSavePath(const char *name, char* out_name)
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
	if (e)
	{
		strcpy(e,".sav");
	}
	else
	{
		strcat(fname, ".sav");
	}

	printf("SavePath=%s\n", out_name);
}

uint32_t getFileType(const char *name)
{
	sprintf(full_path, "%s/%s", getRootDir(), name);

	struct stat64 st;
	if (stat64(full_path, &st)) return 0;

	return st.st_mode;
}

void prefixGameDir(char *dir, size_t dir_len)
{
	if (isPathDirectory(dir)) {
		printf("Found existing: %s\n", dir);
		return;
	}

	FileCreatePath((char *) GAMES_DIR);
	static char temp_dir[1024];
	snprintf(temp_dir, 1024, "%s/%s", GAMES_DIR, dir);
	strncpy(dir, temp_dir, dir_len);
	printf("Prefixed dir to %s\n", temp_dir);
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
	sprintf(full_path, "%s/%s", getRootDir(), name);
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
		int saveddev = device;
		device = 0;
		MiSTer_ini_parse();
		device = saveddev;
		video_mode_load();
		user_io_send_buttons(1);

		printf("Waiting for USB...\n");
		int btn = 0;
		int done = 0;
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
		if (++iterations % YieldIterations == 0)
		{
			scheduler_yield();
		}

		if ((de1.de.d_type == DT_DIR) && !strcmp(de1.altname, "..")) return true;
		if ((de2.de.d_type == DT_DIR) && !strcmp(de2.altname, "..")) return false;

		if ((de1.de.d_type == DT_DIR) && (de2.de.d_type == DT_REG)) return true;
		if ((de1.de.d_type == DT_REG) && (de2.de.d_type == DT_DIR)) return false;

		if (de1.de.d_type == de2.de.d_type)
		{
			int len1 = strlen(de1.altname);
			int len2 = strlen(de2.altname);
			if ((len1 > 4) && (de1.altname[len1 - 4] == '.')) len1 -= 4;
			if ((len2 > 4) && (de2.altname[len2 - 4] == '.')) len2 -= 4;

			int len = (len1 < len2) ? len1 : len2;
			int ret = strncasecmp(de1.altname, de2.altname, len);
			if (!ret)
			{
				return len1 < len2;
			}

			return ret < 0;
		}

		return strcasecmp(de1.altname, de2.altname) < 0;
	}

	size_t iterations = 0;
};

void AdjustDirectory(char *path)
{
	if (isPathDirectory(path)) return;

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

int ScanDirectory(char* path, int mode, const char *extension, int options, const char *prefix)
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

	//printf("scan dir\n");

	if (mode == SCANF_INIT)
	{
		file_name[0] = 0;
		if ((options & SCANO_NOENTER) || !isPathDirectory(path))
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

		if (!isPathDirectory(path))
		{
			path[0] = 0;
			file_name[0] = 0;
		}

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

		iFirstEntry = 0;
		iSelectedEntry = 0;
		DirItem.clear();

		DIR *d = nullptr;
		mz_zip_archive *z = nullptr;
		if (is_zipped)
		{
			mz_zip_archive _z = {};
			if (!mz_zip_reader_init_file(&_z, zip_path, 0))
			{
				printf("Couldn't open zip file %s: %s\n", full_path, mz_zip_get_error_string(mz_zip_get_last_error(z)));
				return 0;
			}
			z = new mz_zip_archive(_z);
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

		struct dirent *de = nullptr;
		for (size_t i = 0; (d && (de = readdir(d)))
				 || (z && i < mz_zip_reader_get_num_files(z)); i++)
		{
			if (0 < i && i % YieldIterations == 0)
			{
				scheduler_yield();
			}

			struct dirent _de = {};
			if (z) {
				mz_zip_reader_get_filename(z, i, &_de.d_name[0], sizeof(_de.d_name));
				if (!IsInSameFolder(file_path_in_zip, _de.d_name))
				{
					continue;
				}
				// Remove leading folders.
				const char* subpath = _de.d_name + strlen(file_path_in_zip);
				if (*subpath == '/')
				{
					subpath++;
				}
				strcpy(_de.d_name, subpath);

				_de.d_type = mz_zip_reader_is_file_a_directory(z, i) ? DT_DIR : DT_REG;
				if (_de.d_type == DT_DIR)
				{
					// Remove trailing slash.
					_de.d_name[strlen(_de.d_name) - 1] = '\0';
				}
				de = &_de;
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

				direntext_t dext = { *de, 0, "" };
				memcpy(dext.altname, de->d_name, sizeof(dext.altname));
				if (!strcasecmp(dext.altname + strlen(dext.altname) - 4, ".zip")) dext.altname[strlen(dext.altname) - 4] = 0;

				full_path[path_len] = 0;
				char *altname = neogeo_get_altname(full_path, &dext);
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
						if (!found && !strcasecmp(de->d_name + strlen(de->d_name) - 4, ".zip"))
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
					direntext_t dext = { *de, 0, "" };
					memcpy(dext.altname, de->d_name, sizeof(dext.altname));
					DirItem.push_back(dext);
				}
			}
		}

		if (z)
		{
			// Since zip files aren't actually folders the entry to
			// exit the zip file must be added manually.
			dirent up;
			up.d_type = DT_DIR;
			strcpy(up.d_name, "..");
			direntext_t dext = { up, 0, "" };
			memcpy(dext.altname, up.d_name, sizeof(dext.altname));
			DirItem.push_back(dext);

			mz_zip_reader_end(z);
			delete z;
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
			for (int i = 0; i < flist_nDirEntries(); i++)
			{
				if (!strcmp(file_name, DirItem[i].de.d_name))
				{
					iSelectedEntry = i;
					if (iSelectedEntry + (OsdGetSize() / 2) - 1 >= flist_nDirEntries()) iFirstEntry = flist_nDirEntries() - OsdGetSize();
					else iFirstEntry = iSelectedEntry - (OsdGetSize() / 2) + 1;
					if (iFirstEntry < 0) iFirstEntry = 0;
					break;
				}
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
			if (iSelectedEntry < iFirstEntry + OsdGetSize() - 1)
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
			for (int i = 0; i < flist_nDirEntries(); i++)
			{
				if((DirItem[i].de.d_type == DT_DIR) && !strcmp(DirItem[i].altname, extension))
				{
					iSelectedEntry = i;
					if (iSelectedEntry + (OsdGetSize() / 2) - 1 >= flist_nDirEntries()) iFirstEntry = flist_nDirEntries() - OsdGetSize();
					else iFirstEntry = iSelectedEntry - (OsdGetSize() / 2) + 1;
					if (iFirstEntry < 0) iFirstEntry = 0;
					break;
				}
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

int flist_nDirEntries()
{
	return DirItem.size();
}

int flist_iFirstEntry()
{
	return iFirstEntry;
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
