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
#include <linux/magic.h>
#include <algorithm>
#include <string>
#include "osd.h"
#include "fpga_io.h"
#include "menu.h"
#include "errno.h"
#include "DiskImage.h"
#include "user_io.h"
#include "cfg.h"
#include "input.h"
#include "miniz.h"

int nDirEntries = 0;
struct dirent DirItem[10000];
const int maxDirEntries = (int)(sizeof(DirItem)/sizeof(DirItem[0]));
int iSelectedEntry = 0;       // selected entry index
int iFirstEntry = 0;

static char full_path[1200];

static bool FileIsZipped(const std::string path, std::string* zip_path, std::string* file_path)
{
	const std::string zipext(".zip");
	auto it = std::search(path.begin(), path.end(),
						  zipext.begin(), zipext.end(),
						  [](char ch1, char ch2) { return std::toupper(ch1) == std::toupper(ch2); });

	if (it != path.end())
	{
		if (zip_path)
		{
			*zip_path = std::string(path.begin(), it + zipext.length());
		}
		if (file_path)
		{
			*file_path = std::string(it + zipext.length() + 1, path.end());
		}
		return true;
	}
	return false;
}

void FileClose(fileTYPE *file)
{
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
	const char *root = getRootDir();
	if (strncasecmp(getRootDir(), name, strlen(root)))
	{
		sprintf(full_path, "%s/%s", (mode == -1) ? "" : root, name);
	}
	else
	{
		sprintf(full_path, name);
	}

	FileClose(file);
	file->mode = 0;
	file->type = 0;

	std::string zip_path, file_path;
	if (FileIsZipped(full_path, &zip_path, &file_path))
	{
		if (mode & O_RDWR || mode & O_WRONLY)
		{
			if(!mute) printf("FileOpenEx(mode) File:%s, writing to zipped files is not supported.\n",
					 full_path);
			return 0;
		}

		snprintf(file->name, sizeof(file->name), "/%s", file_path.c_str());

		int fd = shm_open(file->name, O_CREAT | O_RDWR | O_TRUNC, 0777);
		if (fd <= 0)
		{
			if(!mute) printf("FileOpenEx(open) File:%s, error: %s.\n", full_path, strerror(errno));
			return 0;
		}
		file->type = 1;
		file->filp = fdopen(fd, "w+");
		if (!file->filp)
		{
			if(!mute) printf("FileOpenEx(fdopen) File:%s, error: %s.\n", full_path, strerror(errno));
			close(fd);
			return 0;
		}
		file->mode = mode;

		mz_zip_archive z = { 0 };
		if (!mz_zip_reader_init_file(&z, zip_path.c_str(), 0))
		{
			if(!mute) printf("FileOpenEx(mz_zip_reader_init_file) Zip: %s, error: %s\n", zip_path.c_str(),
							 mz_zip_get_error_string(mz_zip_get_last_error(&z)));
			FileClose(file);
			return 0;
		}

		const int file_index = mz_zip_reader_locate_file(&z, file_path.c_str(), NULL, 0);
		if (file_index < 0)
		{
			if(!mute) printf("FileOpenEx(mz_zip_reader_locate_file) File:%s, zip:%s, error: %s\n",
							 file_path.c_str(), zip_path.c_str(),
							 mz_zip_get_error_string(mz_zip_get_last_error(&z)));
			FileClose(file);
			mz_zip_reader_end(&z);
			return 0;
		}

		mz_zip_archive_file_stat s;
		if (!mz_zip_reader_file_stat(&z, file_index, &s))
		{
			if(!mute) printf("FileOpenEx(mz_zip_reader_file_stat) File:%s, zip:%s, error:%s\n",
							 file_path.c_str(), zip_path.c_str(),
							 mz_zip_get_error_string(mz_zip_get_last_error(&z)));
			FileClose(file);
			mz_zip_reader_end(&z);
			return 0;
		}
		file->size = s.m_uncomp_size;

		if (!mz_zip_reader_extract_to_cfile(&z, file_index, file->filp, 0))
		{
			if(!mute) printf("FileOpenEx(mz_zip_reader_extract_to_cfile) File:%s, zip:%s, error:%s\n",
							 file_path.c_str(), zip_path.c_str(),
							 mz_zip_get_error_string(mz_zip_get_last_error(&z)));
			FileClose(file);
			mz_zip_reader_end(&z);
			return 0;
		}
		rewind(file->filp);
		file->offset = 0;

		mz_zip_reader_end(&z);
	}
	else
	{
		char *p = strrchr(full_path, '/');
		strcpy(file->name, (mode == -1) ? full_path : p+1);

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
			file->offset = 0;
			file->mode = mode;
		}
	}

	//printf("opened %s, size %lu\n", full_path, file->size);
	return 1;
}

__off64_t FileGetSize(fileTYPE *file)
{
	if (!file->filp) return 0;

	struct stat64 st;
	int ret = fstat64(fileno(file->filp), &st);
	return (ret < 0) ? 0 : st.st_size;
}

int FileOpen(fileTYPE *file, const char *name, char mute)
{
	return FileOpenEx(file, name, O_RDONLY, mute);
}

int FileNextSector(fileTYPE *file)
{
	__off64_t newoff = lseek64(fileno(file->filp), file->offset + 512, SEEK_SET);
	if (newoff != file->offset + 512)
	{
		//printf("Fail to seek to next sector. File: %s.\n", file->name);
		lseek64(fileno(file->filp), file->offset, SEEK_SET);
		return 0;
	}

	file->offset = newoff;
	return 1;
}

int FileSeek(fileTYPE *file, __off64_t offset, int origin)
{
	__off64_t newoff = lseek64(fileno(file->filp), offset, origin);
	if(newoff<0)
	{
		printf("Fail to seek the file.\n");
		return 0;
	}

	file->offset = newoff;
	return 1;
}

int FileSeekLBA(fileTYPE *file, uint32_t offset)
{
	__off64_t off64 = offset;
	off64 <<= 9;
	return FileSeek(file, off64, SEEK_SET);
}

// Read. MiST compatible. Avoid to use it.
int FileRead(fileTYPE *file, void *pBuffer)
{
	return FileReadEx(file, pBuffer, 1);
}

int FileReadEx(fileTYPE *file, void *pBuffer, int nSize)
{
	static uint8_t tmpbuff[512];

	if (!FileSeek(file, file->offset, SEEK_SET))
	{
		printf("FileRead error(seek).\n");
		return 0;
	}

	if (!pBuffer)
	{
		for (int i = 0; i < nSize; i++)
		{
			int ret = fread(tmpbuff, 512, 1, file->filp);
			if (ret < 0)
			{
				printf("FileRead error(%d).\n", ret);
				return 0;
			}

			EnableDMode();
			spi_block_write(tmpbuff, 0);
			DisableDMode();
		}
	}
	else
	{
		int ret = fread(pBuffer, nSize, 512, file->filp);
		if (ret < 0)
		{
			printf("FileRead error(%d).\n", ret);
			return 0;
		}
	}

	return 1;
}

// Write. MiST compatible. Avoid to use it.
int FileWrite(fileTYPE *file, void *pBuffer)
{
	if (!FileSeek(file, file->offset, SEEK_SET))
	{
		printf("FileWrite error(seek).\n");
		return 0;
	}

	int ret = fwrite(pBuffer, 512, 1, file->filp);
	if (ret < 0)
	{
		printf("FileWrite error(%d).\n", ret);
		return 0;
	}

	return 1;
}

// Read with offset advancing
int FileReadAdv(fileTYPE *file, void *pBuffer, int length)
{
	ssize_t ret = fread(pBuffer, length, 1, file->filp);
	if (ret < 0)
	{
		printf("FileReadAdv error(%d).\n", ret);
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
	int ret = fwrite(pBuffer, length, 1, file->filp);
	if (ret < 0)
	{
		printf("FileWriteAdv error(%d).\n", ret);
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
	sprintf(full_path, "%s/%s", getRootDir(), name);
	if (!FileCanWrite(name))
	{
		printf("FileSave(FileCanWrite) File:%s, not writable.\n", full_path);
		return 0;
	}

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

int FileLoad(const char *name, void *pBuffer, int size)
{
	sprintf(full_path, "%s/%s", getRootDir(), name);
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

void FileGenerateSavePath(const char *name, const char* extension, char* out_name, int length)
{
	const char *d = strrchr(name, '.');
	if (d)
	{
		const int l = std::min(d - name, length);
		strncpy(out_name, name, l);
		out_name[l] = '\0';
	}
	else
	{
		strncpy(out_name, name, length);
	}

	char *z = strcasestr(out_name, ".zip");
	if (z)
	{
		// Remove '.' from '.zip' so file logic won't think
		// the file has been compressed.
		*z = '-';
		for (char *p = z; (p = strchr(p, '/')); )
		{
			*p = '-';
		}
	}

	strncat(out_name, ".", length);
	strncat(out_name, extension, length);
}

uint32_t getFileType(const char *name)
{
	sprintf(full_path, "%s/%s", getRootDir(), name);

	struct stat64 st;
	if (stat64(full_path, &st)) return 0;

	return st.st_mode;
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
		parse_video_mode();
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

int de_cmp(const void *e1, const void *e2)
{
	const struct dirent *de1 = (struct dirent *)e1;
	const struct dirent *de2 = (struct dirent *)e2;

	if ((de1->d_type == DT_DIR) && !strcmp(de1->d_name, "..")) return -1;
	if ((de2->d_type == DT_DIR) && !strcmp(de2->d_name, "..")) return  1;

	if ((de1->d_type == DT_DIR) && (de2->d_type == DT_REG)) return -1;
	if ((de1->d_type == DT_REG) && (de2->d_type == DT_DIR)) return  1;

	if ((de1->d_type == DT_REG) && (de2->d_type == DT_REG))
	{
		int len1 = strlen(de1->d_name);
		int len2 = strlen(de2->d_name);
		if ((len1 > 4) && (de1->d_name[len1 - 4] == '.')) len1 -= 4;
		if ((len2 > 4) && (de2->d_name[len2 - 4] == '.')) len2 -= 4;

		int len = (len1 < len2) ? len1 : len2;
		int ret = strncasecmp(de1->d_name, de2->d_name, len);
		if (!ret)
		{
			return len1 - len2;
		}

		return ret;
	}

	return strcasecmp(de1->d_name, de2->d_name);
}

static int get_stmode(const char *path)
{
	sprintf(full_path, "%s/%s", getRootDir(), path);
	struct stat64 st;
	return (stat64(full_path, &st) < 0) ? 0 : st.st_mode;
}

void AdjustDirectory(char *path)
{
	int stmode = get_stmode(path);
	if (!stmode)
	{
		printf("AdjustDirectory(stat) path:%s, error.\n", path);
		path[0] = 0;
		return;
	}

	if (stmode & S_IFDIR) return;

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

int ScanDirectory(char* path, int mode, const char *extension, int options, const char *prefix)
{
	static char file_name[1024];

	int has_trd = 0;
	const char *ext = extension;
	while (*ext)
	{
		if (!strncasecmp(ext, "TRD", 3)) has_trd = 1;
		ext += 3;
	}

	const char *is_zipped = strcasestr(path, ".zip");
	if (is_zipped)
	{
		if (strcasestr(is_zipped + 4, ".zip"))
		{
			printf("Nested zip-files are not supported: %s\n", path);
			return 0;
		}
	}
	int extlen = strlen(extension);

	//printf("scan dir\n");

	if (mode == SCANF_INIT)
	{
		file_name[0] = 0;
		int stmode = get_stmode(path);
		if (!(stmode & S_IFDIR) && !is_zipped)
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

			if (!(stmode & S_IFREG)) file_name[0] = 0;
		}

		if (!(get_stmode(path) & S_IFDIR) && !is_zipped)
		{
			path[0] = 0;
			file_name[0] = 0;
		}

		sprintf(full_path, "%s/%s", getRootDir(), path);
		printf("Start to scan dir: %s\n", full_path);

		iFirstEntry = 0;
		iSelectedEntry = 0;
		nDirEntries = 0;
		memset(&DirItem[0], 0, sizeof(DirItem));

		DIR *d = nullptr;
		mz_zip_archive *z = nullptr;
		if (!strcasecmp(full_path + strlen(full_path) - 4, ".zip"))
		{
			mz_zip_archive _z = { 0 };
			if (!mz_zip_reader_init_file(&_z, full_path, 0))
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
		while(nDirEntries < maxDirEntries)
		{
			struct dirent _de = { 0 };
			if (z) {
				if (nDirEntries >= (int)mz_zip_reader_get_num_files(z)) {
					break;
				}

				_de.d_type = mz_zip_reader_is_file_a_directory(z, nDirEntries) ? DT_DIR : DT_REG;
				mz_zip_reader_get_filename(z, nDirEntries, &_de.d_name[0], sizeof(_de.d_name));
				de = &_de;
			} else {
				de = readdir(d);
				if (de == NULL) break;
			}

			if (de->d_type == DT_DIR)
			{
				if (!strcmp(de->d_name, ".")) continue;
				if (!strcmp(de->d_name, ".."))
				{
					if(!strlen(path)) continue;
				}

				if (!(options & SCANO_DIR))
				{
					if (de->d_name[0] != '_' && strcmp(de->d_name, "..")) continue;
					if (!(options & SCANO_CORES)) continue;
				}
			}
			else if (de->d_type == DT_REG)
			{
				//skip non-selectable files
				if (!strcasecmp(de->d_name, "menu.rbf")) continue;
				if (!strcasecmp(de->d_name, "boot.rom")) continue;

				//check the prefix if given
				if (prefix && strncasecmp(prefix, de->d_name, strlen(prefix))) continue;

				if (extlen > 0)
				{
					int len = strlen(de->d_name);
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

					while(!found && *ext)
					{
						char e[5];
						memcpy(e+1, ext, 3);
						if (e[3] == 0x20)
						{
							e[3] = 0;
							if (e[2] == 0x20)
							{
								e[2] = 0;
							}
						}
						e[0] = '.';
						e[4] = 0;
						int l = strlen(e);
						if (len > l)
						{
							char *p = de->d_name + len - l;
							found = 1;
							for (int i = 0; i < l; i++)
							{
								if (e[i] == '?') continue;
								if (tolower(e[i]) != tolower(p[i])) found = 0;
							}
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
			memcpy(&DirItem[nDirEntries], de, sizeof(struct dirent));
			nDirEntries++;
		}
		if (z)
		{
			// Since zip files aren't actually folders the entry to
			// exit the zip file must be added manually.
			DirItem[nDirEntries++] = {
				.d_type = DT_DIR,
				.d_name = "..",
			};

			mz_zip_reader_end(z);
			delete z;
		}
		if (d)
		{
			closedir(d);
		}

		printf("Got %d dir entries\n", nDirEntries);
		if (!nDirEntries) return 0;

		qsort(DirItem, nDirEntries, sizeof(struct dirent), de_cmp);
		if (file_name[0])
		{
			for (int i = 0; i < nDirEntries; i++)
			{
				if (!strcmp(file_name, DirItem[i].d_name))
				{
					iSelectedEntry = i;
					if (iSelectedEntry + (OsdGetSize() / 2) - 1 >= nDirEntries) iFirstEntry = nDirEntries - OsdGetSize();
					else iFirstEntry = iSelectedEntry - (OsdGetSize() / 2) + 1;
					if (iFirstEntry < 0) iFirstEntry = 0;
					break;
				}
			}
		}
		return nDirEntries;
	}
	else
	{
		if (nDirEntries == 0) // directory is empty so there is no point in searching for any entry
			return 0;

		if (mode == SCANF_NEXT)
		{
			if(iSelectedEntry + 1 < nDirEntries) // scroll within visible items
			{
				iSelectedEntry++;
				if (iSelectedEntry > iFirstEntry + OsdGetSize() - 1) iFirstEntry = iSelectedEntry - OsdGetSize() + 1;
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
				if (iSelectedEntry >= nDirEntries) iSelectedEntry = nDirEntries - 1;
			}
			else
			{
				iSelectedEntry += OsdGetSize();
				iFirstEntry += OsdGetSize();
				if (iSelectedEntry >= nDirEntries)
				{
					iSelectedEntry = nDirEntries - 1;
					iFirstEntry = iSelectedEntry - OsdGetSize() + 1;
					if (iFirstEntry < 0) iFirstEntry = 0;
				}
				else if (iFirstEntry + OsdGetSize() > nDirEntries)
				{
					iFirstEntry = nDirEntries - OsdGetSize();
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
			for (int i = 0; i < nDirEntries; i++)
			{
				if((DirItem[i].d_type == DT_DIR) && !strcmp(DirItem[i].d_name, extension))
				{
					iSelectedEntry = i;
					if (iSelectedEntry + (OsdGetSize() / 2) - 1 >= nDirEntries) iFirstEntry = nDirEntries - OsdGetSize();
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
				for (int i = iSelectedEntry+1; i < nDirEntries; i++)
				{
					if (toupper(DirItem[i].d_name[0]) == mode)
					{
						found = i;
						break;
					}
				}

				if (found < 0)
				{
					for (int i = 0; i < nDirEntries; i++)
					{
						if (toupper(DirItem[i].d_name[0]) == mode)
						{
							found = i;
							break;
						}
					}
				}

				if (found >= 0)
				{
					iSelectedEntry = found;
					if (iSelectedEntry + (OsdGetSize() / 2) >= nDirEntries) iFirstEntry = nDirEntries - OsdGetSize();
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
	return nDirEntries;
}

int flist_iFirstEntry()
{
	return iFirstEntry;
}

int flist_iSelectedEntry()
{
	return iSelectedEntry;
}

dirent* flist_DirItem(int n)
{
	if (maxDirEntries < n) {
		return nullptr;
	}
	return &DirItem[n];
}

dirent* flist_SelectedItem()
{
	return &DirItem[iSelectedEntry];
}
