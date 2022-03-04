#ifndef _FAT16_H_INCLUDED
#define _FAT16_H_INCLUDED

#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include "spi.h"

struct fileZipArchive;

struct fileTYPE
{
	fileTYPE();
	~fileTYPE();
	int opened();

	FILE           *filp;
	int             mode;
	int             type;
	fileZipArchive *zip;
	__off64_t       size;
	__off64_t       offset;
	char            path[1024];
	char            name[261];
};

struct direntext_t
{
	dirent de;
	int  cookie;
	char datecode[16];
	char altname[256];
};

struct fileTextReader
{
	fileTextReader();
	~fileTextReader();

	size_t size;
	char *buffer;
	char *pos;
};

int flist_nDirEntries();
int flist_iFirstEntry();
void flist_iFirstEntryInc();
int flist_iSelectedEntry();
direntext_t* flist_DirItem(int n);
direntext_t* flist_SelectedItem();
char* flist_Path();
char* flist_GetPrevNext(const char* base_path, const char* file, const char* ext, int next);

// scanning flags
#define SCANF_INIT       0 // start search from beginning of directory
#define SCANF_NEXT       1 // find next file in directory
#define SCANF_PREV      -1 // find previous file in directory
#define SCANF_NEXT_PAGE  2 // find next 16 files in directory
#define SCANF_PREV_PAGE -2 // find previous 16 files in directory
#define SCANF_SET_ITEM   3 // find exact item
#define SCANF_END        4 // find last file in directory

// options flags
#define SCANO_DIR        0b000000001 // include subdirectories
#define SCANO_UMOUNT     0b000000010 // allow backspace key
#define SCANO_CORES      0b000000100 // only include subdirectories with prefix '_'
#define SCANO_TXT        0b000001000
#define SCANO_NEOGEO     0b000010000
#define SCANO_NOENTER    0b000100000
#define SCANO_NOZIP      0b001000000
#define SCANO_CLEAR      0b010000000 // allow backspace key, clear FC option
#define SCANO_SAVES      0b100000000

void FindStorage();
int  getStorage(int from_setting);
void setStorage(int dev);
int  isUSBMounted();

int  FileOpenZip(fileTYPE *file, const char *name, uint32_t crc32);
int  FileOpenEx(fileTYPE *file, const char *name, int mode, char mute = 0, int use_zip = 1);
int  FileOpen(fileTYPE *file, const char *name, char mute = 0);
void FileClose(fileTYPE *file);

__off64_t FileGetSize(fileTYPE *file);

int FileSeek(fileTYPE *file, __off64_t offset, int origin);
int FileSeekLBA(fileTYPE *file, uint32_t offset);

int FileReadAdv(fileTYPE *file, void *pBuffer, int length, int failres = 0);
int FileReadSec(fileTYPE *file, void *pBuffer);
int FileWriteAdv(fileTYPE *file, void *pBuffer, int length, int failres = 0);
int FileWriteSec(fileTYPE *file, void *pBuffer);
int FileCreatePath(const char *dir);

int FileExists(const char *name, int use_zip = 1);
int FileCanWrite(const char *name);
int PathIsDir(const char *name, int use_zip = 1);
struct stat64* getPathStat(const char *path);

#define SAVE_DIR "saves"
void FileGenerateSavePath(const char *name, char* out_name, int ext_replace = 1);

#define SAVESTATE_DIR "savestates"
void FileGenerateSavestatePath(const char *name, char* out_name, int sufx);

#define SCREENSHOT_DIR "screenshots"
#define SCREENSHOT_DEFAULT "screen"
void FileGenerateScreenshotName(const char *name, char *out_name, int buflen);

int FileSave(const char *name, void *pBuffer, int size);
int FileLoad(const char *name, void *pBuffer, int size); // supply pBuffer = 0 to get the file size without loading
int FileDelete(const char *name);
int DirDelete(const char *name);

//save/load from config dir
#define CONFIG_DIR "config"
int FileSaveConfig(const char *name, void *pBuffer, int size);
int FileLoadConfig(const char *name, void *pBuffer, int size); // supply pBuffer = 0 to get the file size without loading
int FileDeleteConfig(const char *name);

void AdjustDirectory(char *path);
int ScanDirectory(char* path, int mode, const char *extension, int options, const char *prefix = NULL, const char *filter = NULL);

void prefixGameDir(char *dir, size_t dir_len);
int findPrefixDir(char *dir, size_t dir_len);

const char *getStorageDir(int dev);
const char *getRootDir();
const char *getFullPath(const char *name);

uint32_t getFileType(const char *name);
int isXmlName(const char *path); // 1 - MRA, 2 - MGL

bool FileOpenTextReader(fileTextReader *reader, const char *path);
const char* FileReadLine(fileTextReader *reader);

#define LOADBUF_SZ (1024*1024)

#define COEFF_DIR "filters"
#define GAMMA_DIR "gamma"
#define AFILTER_DIR "filters_audio"
#define SMASK_DIR "shadow_masks"
#define PRESET_DIR "presets"
#define GAMES_DIR "games"
#define CIFS_DIR "cifs"
#define DOCS_DIR "docs"

#endif
