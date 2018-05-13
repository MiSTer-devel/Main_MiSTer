
#ifndef __MINIMIG_CONFIG_H__
#define __MINIMIG_CONFIG_H__

#include "file_io.h"

typedef struct
{
	unsigned char lores;
	unsigned char hires;
} filterTYPE;

typedef struct
{
	unsigned char speed;
	unsigned char drives;
} floppyTYPE;

typedef struct
{
	unsigned char enabled;
	unsigned char reserved;
	char filename[1024];
} hardfileTYPE;

typedef struct
{
	char          id[8];
	unsigned long version;
	char          kickstart[1024];
	filterTYPE    filter;
	unsigned char memory;
	unsigned char chipset;
	floppyTYPE    floppy;
	unsigned char disable_ar3;
	unsigned char enable_ide;
	unsigned char scanlines;
	unsigned char audio;
	hardfileTYPE  hardfile[4];
	unsigned char cpu;
	unsigned char autofire;
} configTYPE;

extern configTYPE config;

unsigned char LoadConfiguration(int num);
unsigned char SaveConfiguration(int num);
unsigned char ConfigurationExists(int num);

void MinimigReset();
void SetKickstart(char *name);

#endif
