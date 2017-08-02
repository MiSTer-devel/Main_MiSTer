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
#include <linux/magic.h>
#include "osd.h"
#include "fpga_io.h"
#include "menu.h"
#include "errno.h"

int nDirEntries = 0;
struct dirent DirItem[1000];
int iSelectedEntry = 0;       // selected entry index
int iFirstEntry = 0;

void FileClose(fileTYPE *file)
{
	if (file->fd > 0)
	{
		//printf("closing %d\n", file->fd);
		close(file->fd);
	}
	file->fd = -1;
}

static char full_path[1200];

int FileOpenEx(fileTYPE *file, const char *name, int mode)
{
	sprintf(full_path, "%s/%s", getRootDir(), name);

	FileClose(file);
	file->mode = 0;

	char *p = strrchr(full_path, '/');
	strcpy(file->name, p+1);

	file->fd = open(full_path, mode);
	if (file->fd <= 0)
	{
		printf("FileOpenEx(open) File:%s, error: %d.\n", full_path, file->fd);
		file->fd = -1;
		return 0;
	}

	struct stat64 st;
	int ret = fstat64(file->fd, &st);
	if ( ret < 0)
	{
		printf("FileOpenEx(fstat) File:%s, error: %d.\n", full_path, ret);
		file->fd = -1;
		return 0;
	}

	file->size = st.st_size;
	file->offset = 0;
	file->mode = mode;

	//printf("opened %s, size %lu\n", full_path, file->size);
	return 1;
}

int FileOpen(fileTYPE *file, const char *name)
{
	return FileOpenEx(file, name, O_RDONLY);
}

int FileNextSector(fileTYPE *file)
{
	__off64_t newoff = lseek64(file->fd, file->offset + 512, SEEK_SET);
	if (newoff != file->offset + 512)
	{
		//printf("Fail to seek to next sector. File: %s.\n", file->name);
		lseek64(file->fd, file->offset, SEEK_SET);
		return 0;
	}

	file->offset = newoff;
	return 1;
}

int FileSeek(fileTYPE *file, __off64_t offset, int origin)
{
	__off64_t newoff = lseek64(file->fd, offset, origin);
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
			int ret = read(file->fd, tmpbuff, 512);
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
		int ret = read(file->fd, pBuffer, nSize * 512);
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

	int ret = write(file->fd, pBuffer, 512);
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
	ssize_t ret = read(file->fd, pBuffer, length);
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
	int ret = write(file->fd, pBuffer, length);
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

int FileSave(char *name, void *pBuffer, int size)
{
	sprintf(full_path, "%s/%s", getRootDir(), name);
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

int FileSaveConfig(char *name, void *pBuffer, int size)
{
	char path[256] = { CONFIG_DIR"/" };
	strcat(path, name);
	return FileSave(path, pBuffer, size);
}

int FileLoad(char *name, void *pBuffer, int size)
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

int FileLoadConfig(char *name, void *pBuffer, int size)
{
	char path[256] = { CONFIG_DIR"/" };
	strcat(path, name);
	return FileLoad(path, pBuffer, size);
}

int FileCanWrite(char *name)
{
	sprintf(full_path, "%s/%s", getRootDir(), name);

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

char *make_name(char *short_name)
{
	static char name[16];
	memset(name, 0, sizeof(name));
	memcpy(name, short_name, 8);
	memcpy(name + 10, short_name + 8, 3);

	for (int i = 7; i >= 0; i--)
	{
		if (name[i] <= 0x20) name[i] = 0;
		else break;
	}

	for (int i = 12; i >= 10; i--)
	{
		if (name[i] <= 0x20) name[i] = 0;
		else break;
	}

	if (strlen(name + 10))
	{
		strcat(name, ".");
		strcat(name, name + 10);
	}

	return name;
}

static int device = 0;
static int usbnum = 0;
char *getRootDir()
{
	static char dev[16];
	if(!device) return "/media/fat";
	sprintf(dev, "/media/usb%d", usbnum);
	return dev;
}

void setStorage(int dev)
{
	device = 0;
	FileSave("device.bin", &dev, sizeof(int));
	app_restart();
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
	FileLoad("device.bin", &device, sizeof(int));
	orig_device = device;

	if(device && !isUSBMounted())
	{
		printf("Waiting for USB...\n");
		int btn = 0;
		int done = 0;
		for (int i = 30; i >= 0; i--)
		{
			sprintf(str, "\n\n     Waiting for USB...\n\n             %d   \n", i);
			InfoMessage(str);
			if (isUSBMounted())
			{
				done = 1;
				break;
			}

			for (int i = 0; i < 10; i++)
			{
				btn = fpga_get_buttons();
				if (btn)
				{
					printf("Button has been pressed %d\n", btn);
					InfoMessage("\n\n         Canceled!\n");
					usleep(500000);
					device = 0;
					done = 1;
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
			device = 0;
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

	sprintf(full_path, "%s/"CONFIG_DIR, getRootDir());
	DIR* dir = opendir(full_path);
	if (dir) closedir(dir);
	else if (ENOENT == errno) mkdir(full_path, S_IRWXU | S_IRWXG | S_IRWXO);
}

int de_cmp(const void *e1, const void *e2)
{
	const struct dirent *de1 = e1;
	const struct dirent *de2 = e2;

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

void AdjustDirectory(char *path)
{
	sprintf(full_path, "%s/%s", getRootDir(), path);

	struct stat64 st;
	int ret = stat64(full_path, &st);
	if (ret < 0)
	{
		printf("AdjustDirectory(stat) path:%s, error: %d.\n", full_path, ret);
		path[0] = 0;
		return;
	}

	if (st.st_mode & S_IFDIR) return;

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

int ScanDirectory(char* path, int mode, char *extension, int options)
{
	int extlen = strlen(extension);
	//printf("scan dir\n");

	if (mode == SCAN_INIT)
	{
		sprintf(full_path, "%s/%s", getRootDir(), path);
		printf("Start to scan dir: %s\n", full_path);

		iFirstEntry = 0;
		iSelectedEntry = 0;
		nDirEntries = 0;

		DIR *d = opendir(full_path);
		if (!d)
		{
			printf("Couldn't open dir: %s\n", full_path);
			return 0;
		}

		struct dirent *de;
		while(nDirEntries < 1000)
		{
			de = readdir(d);
			if (de == NULL) break;

			if (de->d_type == DT_DIR)
			{
				if (!strcmp(de->d_name, ".")) continue;
				if (!strcmp(de->d_name, ".."))
				{
					if(!strlen(path)) continue;
				}
				if (!(options & SCAN_DIR)) continue;
			}
			else if (de->d_type == DT_REG)
			{
				//skip non-selectable files
				if (!strcasecmp(de->d_name, "menu.rbf")) continue;
				if (!strcasecmp(de->d_name, "boot.rom")) continue;
				if (!strcasecmp(de->d_name, "boot.vhd")) continue;

				if (extlen > 0)
				{
					int len = strlen(de->d_name);
					char *ext = extension;
					int found = 0;
					while(*ext)
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
						if((len>l) && !strncasecmp(de->d_name + len - l, e, l))
						{
							found = 1;
							break;
						}

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
		closedir(d);

		printf("Got %d dir entries\n", nDirEntries);
		if (!nDirEntries) return 0;

		qsort(DirItem, nDirEntries, sizeof(struct dirent), de_cmp);
	}
	else
	{
		if (nDirEntries == 0) // directory is empty so there is no point in searching for any entry
			return 0;

		if (mode == SCAN_NEXT)
		{
			if(iSelectedEntry + 1 < nDirEntries) // scroll within visible items
			{
				iSelectedEntry++;
				if (iSelectedEntry > iFirstEntry + OsdGetSize() - 1) iFirstEntry = iSelectedEntry - OsdGetSize() + 1;
			}
			return 0;
		}
		else if (mode == SCAN_PREV)
		{
			if (iSelectedEntry > 0) // scroll within visible items
			{
				iSelectedEntry--;
				if (iSelectedEntry < iFirstEntry) iFirstEntry = iSelectedEntry;
			}
			return 0;
		}
		else if (mode == SCAN_NEXT_PAGE)
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
		else if (mode == SCAN_PREV_PAGE)
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
		else if (mode == SCAN_SET_ITEM)
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
