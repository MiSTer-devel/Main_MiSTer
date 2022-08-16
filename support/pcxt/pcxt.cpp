#include <stdio.h>
#include <string.h>
#include <time.h>

#include <stdlib.h>
#include <fcntl.h>
#include <stdarg.h>
#include <ctype.h>
#include <pthread.h>

#include "../../cfg.h"
#include "../../hardware.h"
#include "../../fpga_io.h"
#include "../../menu.h"
#include "../../debug.h"
#include "../../user_io.h"
#include "../../input.h"
#include "../../support.h"
#include "../../lib/serial_server/library/Library.h"
#include "../../lib/serial_server/library/FlatImage.h"
#include "pcxt.h"

int verbose = 0;

int timeoutEnabled = 1;
int createFile = 0;
int useCHS = 0;

pthread_t uart_thread;
SerialAccess serial;
bool in_process;

#define CFG_VER     1

typedef struct
{
	uint32_t ver;
	char img_name[2][1024];
} pcxt_config;

static pcxt_config config;

void pcxt_init()
{	
	user_io_status_set("[0]", 1);
	const char* home = HomeDir();
	static char mainpath[512];

	int status = user_io_status_get("[3]");
	if (status)
	{
		sprintf(mainpath, "%s/tandy.rom", home);
	}
	else
	{
		sprintf(mainpath, "%s/pcxt.rom", home);
	}	
	
	user_io_file_tx(mainpath);
}

void* OpenUART(void*) {
	
	char ComPortBuff[20];
	sprintf(ComPortBuff, "/dev/ttyS1");
	const char* ComPort = NULL;
	ComPort = &ComPortBuff[0];

	struct baudRate* baudRate = NULL;
	int status;

	status = user_io_status_get("[22:21]");
	switch (status)
	{
	case 1:
		baudRate = baudRateMatchString("230400");
		break;
	case 2:
		baudRate = baudRateMatchString("460800");
		break;
	case 3:
		baudRate = baudRateMatchString("921600");
		break;
	case 0:
	default:
		baudRate = baudRateMatchString("115200");
		break;
	};

	Image* images[2] = { NULL, NULL };
	int timeoutEnabled = 1;
	FILE* fd;
	long size;
	
	// FDD CHS Calculator

	unsigned long fdd_cyl = 0, fdd_sect = 0, fdd_head = 0;
	bool is_fdd = false;
	struct floppyInfo* fdd_fi;
	
	if (strlen(config.img_name[1]))
	{
		fd = fopen(config.img_name[1], "r");
		if (fd)
		{
			is_fdd = true;
			fseek(fd, 0L, SEEK_END);
			size = ftell(fd);

			if ((fdd_fi = FindFloppyInfoBySize(size)))
			{
				fdd_sect = fdd_fi->sectors;
				fdd_head = fdd_fi->heads;
				fdd_cyl = fdd_fi->cylinders;
			}
			else
			{
				fdd_sect = 63;
				fdd_head = 16;
				fdd_cyl = size / (16 * 63);
			}
		}
	}

	// HDD CHS Calculator

	unsigned long hdd_cyl = 0, hdd_sect = 0, hdd_head = 0;
	bool is_hdd = false;
	struct hddInfo *hdd_fi;

	if (strlen(config.img_name[0]))
	{
		fd = fopen(config.img_name[0], "r");
		if (fd)
		{
			is_hdd = true;
			fseek(fd, 0L, SEEK_END);
			size = ftell(fd);

			if ((hdd_fi = FindHDDInfoBySize(size)))
			{
				hdd_sect = hdd_fi->sectors;
				hdd_head = hdd_fi->heads;
				hdd_cyl = hdd_fi->cylinders;
			}
			else
			{
				hdd_sect = 63;
				hdd_head = 16;
				hdd_cyl = size / (16 * 63);
			}
		}
	}
	// Prepare Images

	status = user_io_status_get("[20:19]");

	if (is_hdd)
	{
		images[0] = new FlatImage(config.img_name[0], status & 2, 0, createFile, hdd_cyl, hdd_head, hdd_sect, useCHS);
		
		if (is_fdd)
		{
			images[1] = new FlatImage(config.img_name[1], status & 1, 1, createFile, fdd_cyl, fdd_head, fdd_sect, useCHS);
		}
	}
	else if (is_fdd)
	{
		images[0] = new FlatImage(config.img_name[1], status & 1, 0, createFile, fdd_cyl, fdd_head, fdd_sect, useCHS);
	}

	// Mount Images

	serial.Connect(ComPort, baudRate);	
	processRequests(&serial, images[0], images[1], timeoutEnabled, verbose);

	pthread_exit(NULL);
}



void log(int level, const char* message, ...)
{
	va_list args;

	va_start(args, message);

	if (level < 0)
	{
		fprintf(stderr, "ERROR: ");
		vfprintf(stderr, message, args);
		fprintf(stderr, "\n");
		if (level < -1)
		{
			fprintf(stderr, "\n");
			//usage();
		}
		//exit(1);
	}
	else if (verbose >= level)
	{
		vprintf(message, args);
		printf("\n");
	}

	va_end(args);
}


unsigned long GetTime(void)
{
	struct timespec now;


	if (clock_gettime(CLOCK_MONOTONIC, &now))
		return 0;
	return now.tv_sec * 1000.0 + now.tv_nsec / 1000000.0;
}

unsigned long GetTime_Timeout(void)
{
	return(1000);
}

void pcxt_unmount_images()
{
	void* status;	
	printf("Unmounting Images...");
	if (in_process)
	{
		in_process = false;
		serial.Disconnect();		
		pthread_cancel(uart_thread);
		pthread_join(uart_thread, &status);
		usleep(100000);
		printf("OK\n");
	}
	else
	{
		printf("No serdrive is running\n");
	}
	
}

void pcxt_load_images()
{
	pcxt_unmount_images();	
	pthread_create(&uart_thread, NULL, OpenUART, NULL);
	usleep(100000);
	in_process = true;
}

void pcxt_set_image(int num, char* filename)
{	
	const char* imghome = "/media/fat";

	memset(config.img_name[num], 0, sizeof(config.img_name[num]));

	if (strlen(filename))
	{
		memset(config.img_name[num], 0, sizeof(config.img_name[num]));
		sprintf(config.img_name[num], "%s/%s", imghome, filename);
		pcxt_load_images();
	}

}

static char* get_config_name()
{
	static char str[256];
	snprintf(str, sizeof(str), "%ssys.cfg", user_io_get_core_name());
	return str;
}


void pcxt_config_save()
{
	config.ver = CFG_VER;
	FileSaveConfig(get_config_name(), &config, sizeof(config));
}

void pcxt_config_load()
{
	static pcxt_config tmp;
	memset(&config, 0, sizeof(config));
	if (FileLoadConfig(get_config_name(), &tmp, sizeof(tmp)) && (tmp.ver == CFG_VER))
	{
		memcpy(&config, &tmp, sizeof(config));
		pcxt_load_images();
	}
}

const char* pcxt_get_image_name(int num)
{
	static char res[32];

	char* name = config.img_name[num];
	if (!name[0]) return NULL;

	char* p = strrchr(name, '/');
	if (!p) p = name;
	else p++;

	if (strlen(p) < 19) strcpy(res, p);
	else
	{
		strncpy(res, p, 19);
		res[19] = 0;
	}

	return res;
}

const char* pcxt_get_image_path(int num)
{
	return config.img_name[num];
}
