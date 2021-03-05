
#ifndef __MINIMIG_CONFIG_H__
#define __MINIMIG_CONFIG_H__

#include "../../file_io.h"

#define CONFIG_TURBO     1
#define CONFIG_NTSC      2
#define CONFIG_A1000     4
#define CONFIG_ECS       8
#define CONFIG_AGA       16

#define CONFIG_FLOPPY1X  0
#define CONFIG_FLOPPY2X  1

extern const char *config_memory_chip_msg[];
extern const char *config_memory_slow_msg[];
extern const char *config_memory_fast_msg[][8];
extern const char *config_cpu_msg[];
extern const char *config_chipset_msg[];

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
	unsigned short  version;
	unsigned short  ext_cfg2;
	char            kickstart[992];
	char            label[32];
	unsigned short  ext_cfg;
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
const char* minimig_get_cfg_info(int num, int label);

void minimig_reset();
void minimig_set_kickstart(char *name);

void minimig_set_adjust(char n);
char minimig_get_adjust();
void minimig_adjust_vsize(char force);

void minimig_ConfigVideo(unsigned char scanlines);
void minimig_ConfigAudio(unsigned char audio);
void minimig_ConfigMemory(unsigned char memory);
void minimig_ConfigCPU(unsigned char cpu);
void minimig_ConfigChipset(unsigned char chipset);
void minimig_ConfigFloppy(unsigned char drives, unsigned char speed);
void minimig_ConfigAutofire(unsigned char autofire, unsigned char mask);

void minimig_set_extcfg(unsigned int ext_cfg);
unsigned int minimig_get_extcfg();

#endif
