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
	unsigned char enabled;	// 0: Disabled, 1: Hard file, 2: MMC (entire card), 3-6: Partition 1-4 of MMC card
	unsigned char present;
	char long_name[1024];
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
	unsigned char pad1;
	hardfileTYPE  hardfile[2];
	unsigned char cpu;
	unsigned char autofire;
} configTYPE;

extern configTYPE config;
extern char DebugMode;

char UploadKickstart(char *name);
char UploadActionReplay();
void SetConfigurationFilename(int config);	// Set configuration filename by slot number
unsigned char LoadConfiguration(char *filename);	// Can supply NULL to use filename previously set by slot number
unsigned char SaveConfiguration(char *filename);	// Can supply NULL to use filename previously set by slot number
unsigned char ConfigurationExists(char *filename);
void ApplyConfiguration(char reloadkickstart);

