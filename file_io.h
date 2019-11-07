#ifndef _FAT16_H_INCLUDED
#define _FAT16_H_INCLUDED

#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include "spi.h"

struct fileZipArchive;

typedef struct
{
	FILE           *filp;
	int             mode;
	int             type;
	fileZipArchive *zip;
	__off64_t       size;
	__off64_t       offset;
	char            path[1024];
	char            name[261];
}  fileTYPE;

struct direntext_t
{
	dirent de;
	int  cookie;
	char altname[256];
};

int flist_nDirEntries();
int flist_iFirstEntry();
int flist_iSelectedEntry();
direntext_t* flist_DirItem(int n);
direntext_t* flist_SelectedItem();

// scanning flags
#define SCANF_INIT       0 // start search from beginning of directory
#define SCANF_NEXT       1 // find next file in directory
#define SCANF_PREV      -1 // find previous file in directory
#define SCANF_NEXT_PAGE  2 // find next 16 files in directory
#define SCANF_PREV_PAGE -2 // find previous 16 files in directory
#define SCANF_SET_ITEM   3 // find exact item
#define SCANF_END        4 // find last file in directory

// options flags
#define SCANO_DIR        1 // include subdirectories
#define SCANO_UMOUNT     2 // allow backspace key
#define SCANO_CORES      4 // only include subdirectories with prefix '_'
#define SCANO_TXT        8
#define SCANO_NEOGEO     16
#define SCANO_NOENTER    32

void FindStorage();
int  getStorage(int from_setting);
void setStorage(int dev);
int  isUSBMounted();

int  FileOpenEx(fileTYPE *file, const char *name, int mode, char mute = 0);
int  FileOpen(fileTYPE *file, const char *name, char mute = 0);
void FileClose(fileTYPE *file);

__off64_t FileGetSize(fileTYPE *file);

int FileSeek(fileTYPE *file, __off64_t offset, int origin);
int FileSeekLBA(fileTYPE *file, uint32_t offset);

int FileReadAdv(fileTYPE *file, void *pBuffer, int length);
int FileReadSec(fileTYPE *file, void *pBuffer);
int FileWriteAdv(fileTYPE *file, void *pBuffer, int length);
int FileWriteSec(fileTYPE *file, void *pBuffer);
void FileCreatePath(char *dir);

int FileExists(const char *name);
int FileCanWrite(const char *name);

#define SAVE_DIR "saves"
void FileGenerateSavePath(const char *name, char* out_name);

#define SCREENSHOT_DIR "screenshots"
#define SCREENSHOT_DEFAULT "screen"
void FileGenerateScreenshotName(const char *name, char *out_name, int buflen);

int FileSave(const char *name, void *pBuffer, int size);
int FileLoad(const char *name, void *pBuffer, int size); // supply pBuffer = 0 to get the file size without loading

//save/load from config dir
#define CONFIG_DIR "config"
int FileSaveConfig(const char *name, void *pBuffer, int size);
int FileLoadConfig(const char *name, void *pBuffer, int size); // supply pBuffer = 0 to get the file size without loading
int FileSaveJoymap(const char *name, void *pBuffer, int size);
int FileLoadJoymap(const char *name, void *pBuffer, int size); // supply pBuffer = 0 to get the file size without loading


void AdjustDirectory(char *path);
int ScanDirectory(char* path, int mode, const char *extension, int options, const char *prefix = NULL);

void prefixGameDir(char *dir, size_t dir_len);

const char *getStorageDir(int dev);
const char *getRootDir();
const char *getFullPath(const char *name);

uint32_t getFileType(const char *name);

#define COEFF_DIR "filters"
#define GAMMA_DIR "gamma"
#define GAMES_DIR "games"

#endif
