// config.c

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <dirent.h>

#include "../../hardware.h"
#include "minimig_boot.h"
#include "../../file_io.h"
#include "../../osd.h"
#include "minimig_fdd.h"
#include "minimig_hdd.h"
#include "../../menu.h"
#include "minimig_config.h"
#include "../../user_io.h"
#include "../../input.h"

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
	hardfileTYPE  hardfile[2];
	unsigned char cpu;
	unsigned char autofire;
} configTYPE_old;

configTYPE config = { 0 };
unsigned char romkey[3072];

static void SendFileV2(fileTYPE* file, unsigned char* key, int keysize, int address, int size)
{
	static uint8_t buf[512];
	unsigned int keyidx = 0;
	printf("File size: %dkB\n", size >> 1);
	printf("[");
	if (keysize)
	{
		// read header
		FileReadAdv(file, buf, 0xb);
	}

	for (int i = 0; i<size; i++)
	{
		if (!(i & 31)) printf("*");
		FileReadAdv(file, buf, 512);

		if (keysize)
		{
			// decrypt ROM
			for (int j = 0; j<512; j++)
			{
				buf[j] ^= key[keyidx++];
				if (keyidx >= keysize) keyidx -= keysize;
			}
		}
		EnableOsd();
		unsigned int adr = address + i * 512;
		spi8(OSD_CMD_WR);
		spi8(adr & 0xff); adr = adr >> 8;
		spi8(adr & 0xff); adr = adr >> 8;
		spi8(adr & 0xff); adr = adr >> 8;
		spi8(adr & 0xff); adr = adr >> 8;
		for (int j = 0; j<512; j = j + 4)
		{
			spi8(buf[j + 0]);
			spi8(buf[j + 1]);
			spi8(buf[j + 2]);
			spi8(buf[j + 3]);
		}
		DisableOsd();
	}

	printf("]\n");
}

static char UploadKickstart(char *name)
{
	fileTYPE file = { 0 };
	int keysize = 0;

	BootPrint("Checking for Amiga Forever key file:");
	if (FileOpen(&file, "Amiga/ROM.KEY") || FileOpen(&file, "ROM.KEY")) {
		keysize = file.size;
		if (file.size<sizeof(romkey))
		{
			FileReadAdv(&file, romkey, keysize);
			BootPrint("Loaded Amiga Forever key file");
		}
		else
		{
			BootPrint("Amiga Forever keyfile is too large!");
		}
		FileClose(&file);
	}
	BootPrint("Loading file: ");
	BootPrint(name);

	if (FileOpen(&file, name)) {
		if (file.size == 0x100000) {
			// 1MB Kickstart ROM
			BootPrint("Uploading 1MB Kickstart ...");
			SendFileV2(&file, NULL, 0, 0xe00000, file.size >> 10);
			SendFileV2(&file, NULL, 0, 0xf80000, file.size >> 10);
			FileClose(&file);
			return(1);
		}
		else if (file.size == 0x80000) {
			// 512KB Kickstart ROM
			BootPrint("Uploading 512KB Kickstart ...");
			SendFileV2(&file, NULL, 0, 0xf80000, file.size >> 9);
			FileClose(&file);
			FileOpen(&file, name);
			SendFileV2(&file, NULL, 0, 0xe00000, file.size >> 9);
			FileClose(&file);
			return(1);
		}
		else if ((file.size == 0x8000b) && keysize) {
			// 512KB Kickstart ROM
			BootPrint("Uploading 512 KB Kickstart (Probably Amiga Forever encrypted...)");
			SendFileV2(&file, romkey, keysize, 0xf80000, file.size >> 9);
			FileClose(&file);
			FileOpen(&file, name);
			SendFileV2(&file, romkey, keysize, 0xe00000, file.size >> 9);
			FileClose(&file);
			return(1);
		}
		else if (file.size == 0x40000) {
			// 256KB Kickstart ROM
			BootPrint("Uploading 256 KB Kickstart...");
			SendFileV2(&file, NULL, 0, 0xf80000, file.size >> 9);
			FileClose(&file);
			FileOpen(&file, name); // TODO will this work
			SendFileV2(&file, NULL, 0, 0xfc0000, file.size >> 9);
			FileClose(&file);
			return(1);
		}
		else if ((file.size == 0x4000b) && keysize) {
			// 256KB Kickstart ROM
			BootPrint("Uploading 256 KB Kickstart (Probably Amiga Forever encrypted...");
			SendFileV2(&file, romkey, keysize, 0xf80000, file.size >> 9);
			FileClose(&file);
			FileOpen(&file, name); // TODO will this work
			SendFileV2(&file, romkey, keysize, 0xfc0000, file.size >> 9);
			FileClose(&file);
			return(1);
		}
		else {
			BootPrint("Unsupported ROM file size!");
		}
		FileClose(&file);
	}
	else {
		printf("No \"%s\" file!\n", name);
	}
	return(0);
}

static char UploadActionReplay()
{
	fileTYPE file = { 0 };
	if(FileOpen(&file, "Amiga/HRTMON.ROM") || FileOpen(&file, "HRTMON.ROM"))
	{
		int adr, data;
		puts("Uploading HRTmon ROM... ");
		SendFileV2(&file, NULL, 0, 0xa10000, (file.size + 511) >> 9);
		// HRTmon config
		adr = 0xa10000 + 20;
		spi_osd_cmd32le_cont(OSD_CMD_WR, adr);
		data = 0x00800000; // mon_size, 4 bytes
		spi8((data >> 24) & 0xff); spi8((data >> 16) & 0xff);
		spi8((data >> 8) & 0xff); spi8((data >> 0) & 0xff);
		data = 0x00; // col0h, 1 byte
		spi8((data >> 0) & 0xff);
		data = 0x5a; // col0l, 1 byte
		spi8((data >> 0) & 0xff);
		data = 0x0f; // col1h, 1 byte
		spi8((data >> 0) & 0xff);
		data = 0xff; // col1l, 1 byte
		spi8((data >> 0) & 0xff);
		data = 0x01; // right, 1 byte
		spi8((data >> 0) & 0xff);
		data = 0x00; // keyboard, 1 byte
		spi8((data >> 0) & 0xff);
		data = 0x01; // key, 1 byte
		spi8((data >> 0) & 0xff);
		data = config.enable_ide ? 1 : 0; // ide, 1 byte
		spi8((data >> 0) & 0xff);
		data = 0x01; // a1200, 1 byte
		spi8((data >> 0) & 0xff);
		data = config.chipset&CONFIG_AGA ? 1 : 0; // aga, 1 byte
		spi8((data >> 0) & 0xff);
		data = 0x01; // insert, 1 byte
		spi8((data >> 0) & 0xff);
		data = 0x0f; // delay, 1 byte
		spi8((data >> 0) & 0xff);
		data = 0x01; // lview, 1 byte
		spi8((data >> 0) & 0xff);
		data = 0x00; // cd32, 1 byte
		spi8((data >> 0) & 0xff);
		data = config.chipset&CONFIG_NTSC ? 1 : 0; // screenmode, 1 byte
		spi8((data >> 0) & 0xff);
		data = 1; // novbr, 1 byte
		spi8((data >> 0) & 0xff);
		data = 0; // entered, 1 byte
		spi8((data >> 0) & 0xff);
		data = 1; // hexmode, 1 byte
		spi8((data >> 0) & 0xff);
		DisableOsd();
		adr = 0xa10000 + 68;
		spi_osd_cmd32le_cont(OSD_CMD_WR, adr);
		data = ((config.memory & 0x3) + 1) * 512 * 1024; // maxchip, 4 bytes TODO is this correct?
		spi8((data >> 24) & 0xff); spi8((data >> 16) & 0xff);
		spi8((data >> 8) & 0xff); spi8((data >> 0) & 0xff);
		DisableOsd();

		FileClose(&file);
		return(1);
	}
	else {
		puts("\nhrtmon.rom not found!\n");
		return(0);
	}
	return(0);
}

static char* GetConfigurationName(int num)
{
	static char path[128];
	sprintf(path, "%s/%s", getRootDir(), CONFIG_DIR);

	DIR *d;
	struct dirent *dir;
	d = opendir(path);
	if (d)
	{
		if(num) sprintf(path, "minimig%d", num);
		else sprintf(path, "minimig.cfg", num);

		while ((dir = readdir(d)) != NULL)
		{
			int len = strlen(dir->d_name);
			if (len>10 && !strncasecmp(dir->d_name, path, strlen(path)) && !strcasecmp(dir->d_name+len-4, ".cfg"))
			{
				closedir(d);
				strcpy(path, dir->d_name);
				return path;
			}
		}
		closedir(d);
	}

	return NULL;
}

unsigned char SaveConfiguration(int num)
{
	const char *filename = GetConfigurationName(num);
	if (!filename)
	{
		static char name[32];
		if (num) sprintf(name, "minimig%d.cfg", num);
		else sprintf(name, "minimig.cfg");

		filename = name;
	}

	return FileSaveConfig(filename, &config, sizeof(config));
}

const char* GetConfigDisplayName(int num)
{
	char *filename = GetConfigurationName(num);
	if (!filename) return NULL;

	filename[strlen(filename) - 4] = 0;
	char *p = strchr(filename, '_');

	if (p) return p+1;

	return "";
}

static int force_reload_kickstart = 0;
static void ApplyConfiguration(char reloadkickstart)
{
	if (force_reload_kickstart) reloadkickstart = 1;
	force_reload_kickstart = 0;

	ConfigCPU(config.cpu);

	if (!reloadkickstart)
	{
		ConfigChipset(config.chipset);
		ConfigFloppy(config.floppy.drives, config.floppy.speed);
	}

	printf("CPU clock     : %s\n", config.chipset & 0x01 ? "turbo" : "normal");
	printf("Chip RAM size : %s\n", config_memory_chip_msg[config.memory & 0x03]);
	printf("Slow RAM size : %s\n", config_memory_slow_msg[config.memory >> 2 & 0x03]);
	printf("Fast RAM size : %s\n", config_memory_fast_msg[config.memory >> 4 & 0x03]);

	printf("Floppy drives : %u\n", config.floppy.drives + 1);
	printf("Floppy speed  : %s\n", config.floppy.speed ? "fast" : "normal");

	printf("\n");

	printf("\nIDE state: %s.\n", config.enable_ide ? "enabled" : "disabled");
	if (config.enable_ide)
	{
		printf("Primary Master HDD is %s.\n", config.hardfile[0].enabled ? "enabled" : "disabled");
		printf("Primary Slave HDD is %s.\n", config.hardfile[1].enabled ? "enabled" : "disabled");
		printf("Secondary Master HDD is %s.\n", config.hardfile[2].enabled ? "enabled" : "disabled");
		printf("Secondary Slave HDD is %s.\n", config.hardfile[3].enabled ? "enabled" : "disabled");
	}

	rstval = SPI_CPU_HLT;
	spi_osd_cmd8(OSD_CMD_RST, rstval);
	spi_osd_cmd8(OSD_CMD_HDD, (config.enable_ide ? 1 : 0) | (OpenHardfile(0) ? 2 : 0) | (OpenHardfile(1) ? 4 : 0) | (OpenHardfile(2) ? 8 : 0) | (OpenHardfile(3) ? 16 : 0));

	ConfigMemory(config.memory);
	ConfigCPU(config.cpu);

	ConfigChipset(config.chipset);
	ConfigFloppy(config.floppy.drives, config.floppy.speed);

	if (config.memory & 0x40) UploadActionReplay();

	if (reloadkickstart)
	{
		printf("Reloading kickstart ...\n");
		rstval |= (SPI_RST_CPU | SPI_CPU_HLT);
		spi_osd_cmd8(OSD_CMD_RST, rstval);
		if (!UploadKickstart(config.kickstart))
		{
			strcpy(config.kickstart, "Amiga/KICK.ROM");
			if (!UploadKickstart(config.kickstart))
			{
				strcpy(config.kickstart, "KICK.ROM");
				if (!UploadKickstart(config.kickstart))
				{
					BootPrintEx("No Kickstart loaded. Press F12 for settings.");
					BootPrintEx("** Halted! **");
					return;
				}
			}
		}
		rstval |= (SPI_RST_USR | SPI_RST_CPU);
		spi_osd_cmd8(OSD_CMD_RST, rstval);
	}
	else
	{
		printf("Resetting ...\n");
		rstval |= (SPI_RST_USR | SPI_RST_CPU);
		spi_osd_cmd8(OSD_CMD_RST, rstval);
	}

	rstval = 0;
	spi_osd_cmd8(OSD_CMD_RST, rstval);

	ConfigVideo(config.filter.hires, config.filter.lores, config.scanlines);
	ConfigAudio(config.audio);
	ConfigAutofire(config.autofire, 0xC);
}

unsigned char LoadConfiguration(int num)
{
	static const char config_id[] = "MNMGCFG0";
	char updatekickstart = 0;
	char result = 0;
	unsigned char key, i;

	const char *filename = GetConfigurationName(num);

	// load configuration data
	int size;
	if(filename && (size = FileLoadConfig(filename, 0, 0))>0)
	{
		BootPrint("Opened configuration file\n");
		printf("Configuration file size: %s, %lu\n", filename, size);
		if (size == sizeof(config))
		{
			static configTYPE tmpconf;
			if (FileLoadConfig(filename, &tmpconf, sizeof(tmpconf)))
			{
				// check file id and version
				if (strncmp(tmpconf.id, config_id, sizeof(config.id)) == 0) {
					// A few more sanity checks...
					if (tmpconf.floppy.drives <= 4) {
						// If either the old config and new config have a different kickstart file,
						// or this is the first boot, we need to upload a kickstart image.
						if (strcmp(tmpconf.kickstart, config.kickstart) != 0) {
							updatekickstart = true;
						}
						memcpy((void*)&config, (void*)&tmpconf, sizeof(config));
						result = 1; // We successfully loaded the config.
					}
					else BootPrint("Config file sanity check failed!\n");
				}
				else BootPrint("Wrong configuration file format!\n");
			}
			else printf("Cannot load configuration file\n");
		}
		else if (size == sizeof(configTYPE_old))
		{
			static configTYPE_old tmpconf;
			printf("Old Configuration file.\n");
			if (FileLoadConfig(filename, &tmpconf, sizeof(tmpconf)))
			{
				// check file id and version
				if (strncmp(tmpconf.id, config_id, sizeof(config.id)) == 0) {
					// A few more sanity checks...
					if (tmpconf.floppy.drives <= 4) {
						// If either the old config and new config have a different kickstart file,
						// or this is the first boot, we need to upload a kickstart image.
						if (strcmp(tmpconf.kickstart, config.kickstart) != 0) {
							updatekickstart = true;
						}
						memcpy((void*)&config, (void*)&tmpconf, sizeof(config));
						config.cpu = tmpconf.cpu;
						config.autofire = tmpconf.autofire;
						memset(&config.hardfile[2], 0, sizeof(config.hardfile[2]));
						memset(&config.hardfile[3], 0, sizeof(config.hardfile[3]));
						result = 1; // We successfully loaded the config.
					}
					else BootPrint("Config file sanity check failed!\n");
				}
				else BootPrint("Wrong configuration file format!\n");
			}
			else printf("Cannot load configuration file\n");
		}
		else printf("Wrong configuration file size: %lu (expected: %lu)\n", size, sizeof(config));
	}
	if (!result) {
		BootPrint("Can not open configuration file!\n");
		BootPrint("Setting config defaults\n");
		// set default configuration
		memset((void*)&config, 0, sizeof(config));  // Finally found default config bug - params were reversed!
		strncpy(config.id, config_id, sizeof(config.id));
		strcpy(config.kickstart, "Amiga/KICK.ROM");
		config.memory = 0x11;
		config.cpu = 0;
		config.chipset = 0;
		config.floppy.speed = CONFIG_FLOPPY2X;
		config.floppy.drives = 1;
		config.enable_ide = 0;
		config.hardfile[0].enabled = 1;
		config.hardfile[0].filename[0] = 0;
		config.hardfile[1].enabled = 1;
		config.hardfile[1].filename[0] = 0;
		updatekickstart = true;
		BootPrintEx(">>> No config found. Using defaults. <<<");
	}

	for (int i = 0; i < 4; i++)
	{
		df[i].status = 0;
		FileClose(&df[i].file);
	}

	// print config to boot screen
	char cfg_str[256];
	sprintf(cfg_str, "CPU: %s, Chipset: %s, ChipRAM: %s, FastRAM: %s, SlowRAM: %s",
			config_cpu_msg[config.cpu & 0x03], config_chipset_msg[(config.chipset >> 2) & 7],
			config_memory_chip_msg[(config.memory >> 0) & 0x03], config_memory_fast_msg[(config.memory >> 4) & 0x03], config_memory_slow_msg[(config.memory >> 2) & 0x03]
			);
	BootPrintEx(cfg_str);

	input_poll(0);
	if (is_key_pressed(59))
	{
		BootPrintEx("Forcing NTSC video ...");
		//force NTSC mode if F1 pressed
		config.chipset |= CONFIG_NTSC;
	}
	else if (is_key_pressed(60))
	{
		BootPrintEx("Forcing PAL video ...");
		// force PAL mode if F2 pressed
		config.chipset &= ~CONFIG_NTSC;
	}

	ApplyConfiguration(updatekickstart);
	return(result);
}

void MinimigReset()
{
	ApplyConfiguration(0);
	user_io_rtc_reset();
}

void SetKickstart(char *name)
{
	int len = strlen(name);
	if (len > (sizeof(config.kickstart) - 1)) len = sizeof(config.kickstart) - 1;
	memcpy(config.kickstart, name, len);
	config.kickstart[len] = 0;
	force_reload_kickstart = 1;
}
