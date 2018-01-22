#ifndef __MINIMIG_FDD_H__
#define __MINIMIG_FDD_H__

#include "file_io.h"

// floppy disk interface defs
#define CMD_RDTRK 0x01
#define CMD_WRTRK 0x02

// floppy status
#define DSK_INSERTED 0x01 /*disk is inserted*/
#define DSK_WRITABLE 0x10 /*disk is writable*/

#define MAX_TRACKS (83*2)

typedef struct
{
	fileTYPE      file;
	unsigned char status; /*status of floppy*/
	unsigned char tracks; /*number of tracks*/
	unsigned char sector_offset; /*sector offset to handle tricky loaders*/
	unsigned char track; /*current track*/
	unsigned char track_prev; /*previous track*/
	char          name[1024]; /*floppy name*/
} adfTYPE;

void UpdateDriveStatus(void);
void HandleFDD(unsigned char c1, unsigned char c2);

#endif

