
#ifndef __MINIMIG_CONFIG_H__
#define __MINIMIG_CONFIG_H__

#include "../../file_io.h"

typedef struct
{
	unsigned char lores;
	unsigned char hires;
} mm_filterTYPE;

typedef struct
{
	unsigned char speed;
	unsigned char drives;
} mm_floppyTYPE;

typedef struct
{
	unsigned char enabled;
	unsigned char reserved;
	char filename[1024];
} mm_hardfileTYPE;

typedef struct
{
	char            id[8];
	unsigned long   version;
	char            kickstart[1024];
	mm_filterTYPE   filter;
	unsigned char   memory;
	unsigned char   chipset;
	mm_floppyTYPE   floppy;
	unsigned char   disable_ar3;
	unsigned char   enable_ide;
	unsigned char   scanlines;
	unsigned char   audio;
	mm_hardfileTYPE hardfile[4];
	unsigned char   cpu;
	unsigned char   autofire;
	char            info[64];
} mm_configTYPE;

extern mm_configTYPE minimig_config;

int minimig_cfg_load(int num);
int minimig_cfg_save(int num);
const char* minimig_get_cfg_info(int num);

void minimig_reset();
void minimig_set_kickstart(char *name);

void minimig_set_adjust(char n);
char minimig_get_adjust();

#endif
