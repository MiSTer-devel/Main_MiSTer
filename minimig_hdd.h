// hdd.h

#ifndef __MINIMIG_HDD_H__
#define __MINIMIG_HDD_H__

#define HDF_DISABLED  0
#define HDF_FILE      1
#define HDF_TYPEMASK  15
#define HDF_SYNTHRDB  128 // flag to indicate whether we should auto-synthesize a RigidDiskBlock

#define HDF_FILETYPE_UNKNOWN  0
#define HDF_FILETYPE_NOTFOUND 1
#define HDF_FILETYPE_RDB      2
#define HDF_FILETYPE_DOS      3

// functions
void HandleHDD(unsigned char c1, unsigned char c2);
unsigned char OpenHardfile(unsigned char unit);
unsigned char GetHDFFileType(char *filename);

#endif // __HDD_H__

