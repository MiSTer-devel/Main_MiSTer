// config.c

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <dirent.h>

#include "../../hardware.h"
#include "../../file_io.h"
#include "../../menu.h"
#include "../../user_io.h"
#include "../../input.h"
#include "minimig_boot.h"
#include "minimig_fdd.h"
#include "minimig_hdd.h"
#include "minimig_config.h"

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
	mm_hardfileTYPE hardfile[2];
	unsigned char   cpu;
	unsigned char   autofire;
} configTYPE_old;

mm_configTYPE minimig_config = { };
static unsigned char romkey[3072];

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
				if ((int)keyidx >= keysize) keyidx -= keysize;
			}
		}
		EnableIO();
		unsigned int adr = address + i * 512;
		spi8(UIO_MM2_WR);
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
		DisableIO();
	}

	printf("]\n");
}


static char UploadKickstart(char *name)
{
	fileTYPE file = {};
	int keysize = 0;

	BootPrint("Checking for Amiga Forever key file:");
	if (FileOpen(&file, user_io_make_filepath(HomeDir, "ROM.KEY")) || FileOpen(&file, "ROM.KEY")) {
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
		else if ((file.size == 8203) && keysize) {
		        // Cloanto encrypted A1000 boot ROM
		        BootPrint("Uploading encrypted A1000 boot ROM");
			SendFileV2(&file, romkey, keysize, 0xf80000, file.size >> 9);
			FileClose(&file);
			//clear tag (write 0 to $fc0000) to force bootrom to load Kickstart from disk
			//and not use one which was already there.
			spi_uio_cmd32le_cont(UIO_MM2_WR, 0xfc0000);
			spi8(0x00);spi8(0x00);
			DisableIO();
			return(1);
		  }
		else if (file.size == 0x2000) {
		        // 8KB A1000 boot ROM
		        BootPrint("Uploading A1000 boot ROM");
		        SendFileV2(&file, NULL, 0, 0xf80000, file.size >> 9);
			FileClose(&file);
			spi_uio_cmd32le_cont(UIO_MM2_WR, 0xfc0000);
			spi8(0x00);spi8(0x00);
			DisableIO();
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
	fileTYPE file = {};
	if(FileOpen(&file, user_io_make_filepath(HomeDir, "HRTMON.ROM")) || FileOpen(&file, "HRTMON.ROM"))
	{
		int adr, data;
		puts("Uploading HRTmon ROM... ");
		SendFileV2(&file, NULL, 0, 0xa10000, (file.size + 511) >> 9);
		// HRTmon config
		adr = 0xa10000 + 20;
		spi_uio_cmd32le_cont(UIO_MM2_WR, adr);
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
		data = 0xff; // right, 1 byte
		spi8((data >> 0) & 0xff);
		data = 0x00; // keyboard, 1 byte
		spi8((data >> 0) & 0xff);
		data = 0xff; // key, 1 byte
		spi8((data >> 0) & 0xff);
		data = minimig_config.enable_ide ? 0xff : 0; // ide, 1 byte
		spi8((data >> 0) & 0xff);
		data = 0xff; // a1200, 1 byte
		spi8((data >> 0) & 0xff);
		data = minimig_config.chipset&CONFIG_AGA ? 0xff : 0; // aga, 1 byte
		spi8((data >> 0) & 0xff);
		data = 0xff; // insert, 1 byte
		spi8((data >> 0) & 0xff);
		data = 0x0f; // delay, 1 byte
		spi8((data >> 0) & 0xff);
		data = 0xff; // lview, 1 byte
		spi8((data >> 0) & 0xff);
		data = 0x00; // cd32, 1 byte
		spi8((data >> 0) & 0xff);
		data = minimig_config.chipset&CONFIG_NTSC ? 1 : 0; // screenmode, 1 byte
		spi8((data >> 0) & 0xff);
		data = 0xff; // novbr, 1 byte
		spi8((data >> 0) & 0xff);
		data = 0; // entered, 1 byte
		spi8((data >> 0) & 0xff);
		data = 1; // hexmode, 1 byte
		spi8((data >> 0) & 0xff);
		DisableIO();
		adr = 0xa10000 + 68;
		spi_uio_cmd32le_cont(UIO_MM2_WR, adr);
		data = ((minimig_config.memory & 0x3) + 1) * 512 * 1024; // maxchip, 4 bytes TODO is this correct?
		spi8((data >> 24) & 0xff); spi8((data >> 16) & 0xff);
		spi8((data >> 8) & 0xff); spi8((data >> 0) & 0xff);
		DisableIO();

		FileClose(&file);
		return(1);
	}
	else {
		puts("\nhrtmon.rom not found!\n");
		return(0);
	}
	return(0);
}

static char* GetConfigurationName(int num, int chk)
{
	static char name[128];
	if (num) sprintf(name, CONFIG_DIR "/minimig%d.cfg", num);
	else sprintf(name, CONFIG_DIR "/minimig.cfg");

	if (chk && !S_ISREG(getFileType(name))) return 0;
	return name+strlen(CONFIG_DIR)+1;
}

int minimig_cfg_save(int num)
{
	return FileSaveConfig(GetConfigurationName(num, 0), &minimig_config, sizeof(minimig_config));
}

const char* minimig_get_cfg_info(int num)
{
	char *filename = GetConfigurationName(num, 1);
	if (!filename) return NULL;

	static mm_configTYPE tmpconf;
	memset(&tmpconf, 0, sizeof(tmpconf));

	if (FileLoadConfig(filename, &tmpconf, sizeof(tmpconf))) return tmpconf.info;
	return "";
}

static int force_reload_kickstart = 0;
static void ApplyConfiguration(char reloadkickstart)
{
	if (force_reload_kickstart) reloadkickstart = 1;
	force_reload_kickstart = 0;

	minimig_ConfigCPU(minimig_config.cpu);

	if (!reloadkickstart)
	{
		minimig_ConfigChipset(minimig_config.chipset);
		minimig_ConfigFloppy(minimig_config.floppy.drives, minimig_config.floppy.speed);
	}

	printf("CPU clock     : %s\n", minimig_config.chipset & 0x01 ? "turbo" : "normal");
	uint8_t memcfg = minimig_config.memory;

	printf("Chip RAM size : %s\n", config_memory_chip_msg[memcfg & 0x03]);
	printf("Slow RAM size : %s\n", config_memory_slow_msg[memcfg >> 2 & 0x03]);
	printf("Fast RAM size : %s\n", config_memory_fast_msg[((memcfg >> 4) & 0x03) | ((memcfg & 0x80) >> 5)]);

	printf("Floppy drives : %u\n", minimig_config.floppy.drives + 1);
	printf("Floppy speed  : %s\n", minimig_config.floppy.speed ? "fast" : "normal");

	printf("\n");

	printf("\nIDE state: %s.\n", minimig_config.enable_ide ? "enabled" : "disabled");
	if (minimig_config.enable_ide)
	{
		printf("Primary Master HDD is %s.\n", minimig_config.hardfile[0].enabled ? "enabled" : "disabled");
		printf("Primary Slave HDD is %s.\n", minimig_config.hardfile[1].enabled ? "enabled" : "disabled");
		printf("Secondary Master HDD is %s.\n", minimig_config.hardfile[2].enabled ? "enabled" : "disabled");
		printf("Secondary Slave HDD is %s.\n", minimig_config.hardfile[3].enabled ? "enabled" : "disabled");
	}

	rstval = SPI_CPU_HLT;
	spi_uio_cmd8(UIO_MM2_RST, rstval);
	spi_uio_cmd8(UIO_MM2_HDD, (minimig_config.enable_ide ? 1 : 0) | (OpenHardfile(0) ? 2 : 0) | (OpenHardfile(1) ? 4 : 0) | (OpenHardfile(2) ? 8 : 0) | (OpenHardfile(3) ? 16 : 0));

	minimig_ConfigMemory(memcfg);
	minimig_ConfigCPU(minimig_config.cpu);

	minimig_ConfigChipset(minimig_config.chipset);
	minimig_ConfigFloppy(minimig_config.floppy.drives, minimig_config.floppy.speed);

	if (minimig_config.memory & 0x40) UploadActionReplay();

	if (reloadkickstart)
	{
		printf("Reloading kickstart ...\n");
		rstval |= (SPI_RST_CPU | SPI_CPU_HLT);
		spi_uio_cmd8(UIO_MM2_RST, rstval);
		if (!UploadKickstart(minimig_config.kickstart))
		{
			snprintf(minimig_config.kickstart, 1024, "%s/%s", HomeDir, "KICK.ROM");
			if (!UploadKickstart(minimig_config.kickstart))
			{
				strcpy(minimig_config.kickstart, "KICK.ROM");
				if (!UploadKickstart(minimig_config.kickstart))
				{
					BootPrintEx("No Kickstart loaded. Press F12 for settings.");
					BootPrintEx("** Halted! **");
					return;
				}
			}
		}
		rstval |= (SPI_RST_USR | SPI_RST_CPU);
		spi_uio_cmd8(UIO_MM2_RST, rstval);
	}
	else
	{
		printf("Resetting ...\n");
		rstval |= (SPI_RST_USR | SPI_RST_CPU);
		spi_uio_cmd8(UIO_MM2_RST, rstval);
	}

	rstval = 0;
	spi_uio_cmd8(UIO_MM2_RST, rstval);

	minimig_ConfigVideo(minimig_config.scanlines);
	minimig_ConfigAudio(minimig_config.audio);
	minimig_ConfigAutofire(minimig_config.autofire, 0xC);
}

int minimig_cfg_load(int num)
{
	static const char config_id[] = "MNMGCFG0";
	char updatekickstart = 0;
	int result = 0;

	const char *filename = GetConfigurationName(num, 1);

	// load configuration data
	int size;
	if(filename && (size = FileLoadConfig(filename, 0, 0))>0)
	{
		BootPrint("Opened configuration file\n");
		printf("Configuration file size: %s, %d\n", filename, size);
		if (size == sizeof(minimig_config) || size == 5152)
		{
			static mm_configTYPE tmpconf = {};
			if (FileLoadConfig(filename, &tmpconf, sizeof(tmpconf)))
			{
				// check file id and version
				if (strncmp(tmpconf.id, config_id, sizeof(minimig_config.id)) == 0) {
					// A few more sanity checks...
					if (tmpconf.floppy.drives <= 4) {
						// If either the old config and new config have a different kickstart file,
						// or this is the first boot, we need to upload a kickstart image.
						if (strcmp(tmpconf.kickstart, minimig_config.kickstart) != 0) {
							updatekickstart = true;
						}
						memcpy((void*)&minimig_config, (void*)&tmpconf, sizeof(minimig_config));
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
				if (strncmp(tmpconf.id, config_id, sizeof(minimig_config.id)) == 0) {
					// A few more sanity checks...
					if (tmpconf.floppy.drives <= 4) {
						// If either the old config and new config have a different kickstart file,
						// or this is the first boot, we need to upload a kickstart image.
						if (strcmp(tmpconf.kickstart, minimig_config.kickstart) != 0) {
							updatekickstart = true;
						}
						memcpy((void*)&minimig_config, (void*)&tmpconf, sizeof(minimig_config));
						minimig_config.cpu = tmpconf.cpu;
						minimig_config.autofire = tmpconf.autofire;
						memset(&minimig_config.hardfile[2], 0, sizeof(minimig_config.hardfile[2]));
						memset(&minimig_config.hardfile[3], 0, sizeof(minimig_config.hardfile[3]));
						result = 1; // We successfully loaded the config.
					}
					else BootPrint("Config file sanity check failed!\n");
				}
				else BootPrint("Wrong configuration file format!\n");
			}
			else printf("Cannot load configuration file\n");
		}
		else printf("Wrong configuration file size: %d (expected: %u)\n", size, sizeof(minimig_config));
	}
	if (!result) {
		BootPrint("Can not open configuration file!\n");
		BootPrint("Setting config defaults\n");
		// set default configuration
		memset((void*)&minimig_config, 0, sizeof(minimig_config));  // Finally found default config bug - params were reversed!
		strncpy(minimig_config.id, config_id, sizeof(minimig_config.id));
		snprintf(minimig_config.kickstart, 1024, "%s/%s", HomeDir, "KICK.ROM");
		minimig_config.memory = 0x11;
		minimig_config.cpu = 0;
		minimig_config.chipset = 0;
		minimig_config.floppy.speed = CONFIG_FLOPPY2X;
		minimig_config.floppy.drives = 1;
		minimig_config.enable_ide = 0;
		minimig_config.hardfile[0].enabled = 1;
		minimig_config.hardfile[0].filename[0] = 0;
		minimig_config.hardfile[1].enabled = 1;
		minimig_config.hardfile[1].filename[0] = 0;
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
			config_cpu_msg[minimig_config.cpu & 0x03], config_chipset_msg[(minimig_config.chipset >> 2) & 7],
			config_memory_chip_msg[(minimig_config.memory >> 0) & 0x03], config_memory_fast_msg[((minimig_config.memory >> 4) & 0x03) | ((minimig_config.memory & 0x80) >> 5)], config_memory_slow_msg[(minimig_config.memory >> 2) & 0x03]
			);
	BootPrintEx(cfg_str);

	input_poll(0);
	if (is_key_pressed(59))
	{
		BootPrintEx("Forcing NTSC video ...");
		//force NTSC mode if F1 pressed
		minimig_config.chipset |= CONFIG_NTSC;
	}
	else if (is_key_pressed(60))
	{
		BootPrintEx("Forcing PAL video ...");
		// force PAL mode if F2 pressed
		minimig_config.chipset &= ~CONFIG_NTSC;
	}

	ApplyConfiguration(updatekickstart);
	return(result);
}

void minimig_reset()
{
	ApplyConfiguration(0);
	user_io_rtc_reset();
}

void minimig_set_kickstart(char *name)
{
	uint len = strlen(name);
	if (len > (sizeof(minimig_config.kickstart) - 1)) len = sizeof(minimig_config.kickstart) - 1;
	memcpy(minimig_config.kickstart, name, len);
	minimig_config.kickstart[len] = 0;
	force_reload_kickstart = 1;
}

static char minimig_adjust = 0;

typedef struct
{
	uint32_t mode;
	uint32_t hpos;
	uint32_t vpos;
	uint32_t reserved;
} vmode_adjust_t;

vmode_adjust_t vmodes_adj[64] = {};

static void adjust_vsize(char force)
{
	static uint16_t nres = 0;
	spi_uio_cmd_cont(UIO_GET_VMODE);
	uint16_t res = spi_w(0);
	if ((res & 0x8000) && (nres != res || force))
	{
		nres = res;
		uint16_t scr_hsize = spi_w(0);
		uint16_t scr_vsize = spi_w(0);
		DisableIO();

		printf("\033[1;37mVMODE: resolution: %u x %u, mode: %u\033[0m\n", scr_hsize, scr_vsize, res & 255);

		static int loaded = 0;
		if (~loaded)
		{
			FileLoadConfig("minimig_vadjust.dat", vmodes_adj, sizeof(vmodes_adj));
			loaded = 1;
		}

		uint32_t mode = scr_hsize | (scr_vsize << 12) | ((res & 0xFF) << 24);
		if (mode)
		{
			for (uint i = 0; i < sizeof(vmodes_adj) / sizeof(vmodes_adj[0]); i++)
			{
				if (vmodes_adj[i].mode == mode)
				{
					spi_uio_cmd_cont(UIO_SET_VPOS);
					spi_w(vmodes_adj[i].hpos >> 16);
					spi_w(vmodes_adj[i].hpos);
					spi_w(vmodes_adj[i].vpos >> 16);
					spi_w(vmodes_adj[i].vpos);
					printf("\033[1;37mVMODE: set positions: [%u-%u, %u-%u]\033[0m\n", vmodes_adj[i].hpos >> 16, (uint16_t)vmodes_adj[i].hpos, vmodes_adj[i].vpos >> 16, (uint16_t)vmodes_adj[i].vpos);
					DisableIO();
					return;
				}
			}
			printf("\033[1;37mVMODE: preset not found.\033[0m\n");
			spi_uio_cmd_cont(UIO_SET_VPOS); spi_w(0); spi_w(0); spi_w(0); spi_w(0);
			DisableIO();
		}
	}
	else
	{
		DisableIO();
	}
}

static void store_vsize()
{
	Info("Stored");
	minimig_adjust = 0;

	spi_uio_cmd_cont(UIO_GET_VMODE);
	uint16_t res = spi_w(0);
	uint16_t scr_hsize = spi_w(0);
	uint16_t scr_vsize = spi_w(0);
	uint16_t scr_hbl_l = spi_w(0);
	uint16_t scr_hbl_r = spi_w(0);
	uint16_t scr_vbl_t = spi_w(0);
	uint16_t scr_vbl_b = spi_w(0);
	DisableIO();

	printf("\033[1;37mVMODE: store position: [%u-%u, %u-%u]\033[0m\n", scr_hbl_l, scr_hbl_r, scr_vbl_t, scr_vbl_b);

	uint32_t mode = scr_hsize | (scr_vsize << 12) | ((res & 0xFF) << 24);
	if (mode)
	{
		int applied = 0;
		int empty = -1;
		for (int i = 0; (uint)i < sizeof(vmodes_adj) / sizeof(vmodes_adj[0]); i++)
		{
			if (vmodes_adj[i].mode == mode)
			{
				vmodes_adj[i].hpos = (scr_hbl_l << 16) | scr_hbl_r;
				vmodes_adj[i].vpos = (scr_vbl_t << 16) | scr_vbl_b;
				applied = 1;
			}
			if (empty < 0 && !vmodes_adj[i].mode) empty = i;
		}

		if (!applied && empty >= 0)
		{
			vmodes_adj[empty].mode = mode;
			vmodes_adj[empty].hpos = (scr_hbl_l << 16) | scr_hbl_r;
			vmodes_adj[empty].vpos = (scr_vbl_t << 16) | scr_vbl_b;
			applied = 1;
		}

		if (applied)
		{
			FileSaveConfig("minimig_vadjust.dat", vmodes_adj, sizeof(vmodes_adj));
		}
	}
}

// 0 - disable
// 1 - enable
// 2 - cancel
void minimig_set_adjust(char n)
{
	if (minimig_adjust && !n) store_vsize();
	minimig_adjust = (n == 1) ? 1 : 0;
	if (n == 2) adjust_vsize(1);
}

char minimig_get_adjust()
{
	return minimig_adjust;
}

void minimig_ConfigVideo(unsigned char scanlines)
{
	spi_uio_cmd16(UIO_MM2_VID, (((scanlines >> 6) & 0x03) << 10) | (((scanlines >> 4) & 0x03) << 8) | (scanlines & 0x07));
}

void minimig_ConfigAudio(unsigned char audio)
{
	spi_uio_cmd8(UIO_MM2_AUD, audio);
}

void minimig_ConfigMemory(unsigned char memory)
{
	spi_uio_cmd8(UIO_MM2_MEM, memory);
}

void minimig_ConfigCPU(unsigned char cpu)
{
	spi_uio_cmd8(UIO_MM2_CPU, cpu & 0x1f);
}

void minimig_ConfigChipset(unsigned char chipset)
{
	spi_uio_cmd8(UIO_MM2_CHIP, chipset & 0x1f);
}

void minimig_ConfigFloppy(unsigned char drives, unsigned char speed)
{
	spi_uio_cmd8(UIO_MM2_FLP, ((drives & 0x03) << 2) | (speed & 0x03));
}

void minimig_ConfigAutofire(unsigned char autofire, unsigned char mask)
{
	uint16_t param = mask;
	param = (param << 8) | autofire;
	spi_uio_cmd16(UIO_MM2_JOY, param);
}
