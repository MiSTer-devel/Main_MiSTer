#ifndef _FAT16_H_INCLUDED
#define _FAT16_H_INCLUDED

#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include "spi.h"

typedef struct
{
	int       fd;
	int       mode;
	__off64_t size;
	__off64_t offset;
	char      name[261];
}  fileTYPE;

extern int nDirEntries;
extern struct dirent DirItem[1000];
extern int iSelectedEntry;
extern int iFirstEntry;

// scanning flags
#define SCAN_INIT        0 // start search from beginning of directory
#define SCAN_NEXT        1 // find next file in directory
#define SCAN_PREV       -1 // find previous file in directory
#define SCAN_NEXT_PAGE   2 // find next 8 files in directory
#define SCAN_PREV_PAGE  -2 // find previous 8 files in directory
#define SCAN_SET_ITEM    3 // find exact item

// options flags
#define SCAN_DIR   1 // include subdirectories

void FindStorage();
int getStorage(int from_setting);
void setStorage(int dev);
int isUSBMounted();

unsigned char FileOpenEx(fileTYPE *file, const char *name, int mode);
unsigned char FileOpen(fileTYPE *file, const char *name);
void FileClose(fileTYPE *file);

unsigned char FileSeek(fileTYPE *file, __off64_t offset, unsigned long origin);
unsigned char FileSeekLBA(fileTYPE *file, uint32_t offset);

//MiST compatible functions. Avoid to use them.
unsigned char FileRead(fileTYPE *file, void *pBuffer);
unsigned char FileReadEx(fileTYPE *file, void *pBuffer, unsigned long nSize);
unsigned char FileWrite(fileTYPE *file, void *pBuffer);
unsigned char FileNextSector(fileTYPE *file);

//New functions.
unsigned long FileReadAdv(fileTYPE *file, void *pBuffer, unsigned long length);
unsigned long FileReadSec(fileTYPE *file, void *pBuffer);
unsigned long FileWriteAdv(fileTYPE *file, void *pBuffer, unsigned long length);
unsigned long FileWriteSec(fileTYPE *file, void *pBuffer);

int FileCanWrite(char *name);

int FileSave(char *name, void *pBuffer, int size);
int FileLoad(char *name, void *pBuffer, int size); // supply pBuffer = 0 to get the file size without loading

//save/load from config dir
#define CONFIG_DIR "config"
int FileSaveConfig(char *name, void *pBuffer, int size);
int FileLoadConfig(char *name, void *pBuffer, int size); // supply pBuffer = 0 to get the file size without loading

void AdjustDirectory(char *path);
int ScanDirectory(char* path, unsigned long mode, char *extension, unsigned char options);

char *make_name(char *short_name);
char *getRootDir();

#endif
