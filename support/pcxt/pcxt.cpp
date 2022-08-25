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
#include "../../ide.h"
#include "pcxt.h"

int verbose = 0;

int timeoutEnabled = 1;
int createFile = 0;
int useCHS = 0;

pthread_t uart_thread;
SerialAccess serial;
bool in_process;

#define FDD0_BASE   0xF200
#define FDD1_BASE   0xF300
#define CFG_VER     1

#define FDD_TYPE_NONE 0
#define FDD_TYPE_160  1
#define FDD_TYPE_180  2
#define FDD_TYPE_320  3
#define FDD_TYPE_360  4
#define FDD_TYPE_720  5
#define FDD_TYPE_1200 6
#define FDD_TYPE_1440 7
#define FDD_TYPE_1680 8
#define FDD_TYPE_2880 9

static char floppy_type[2] = { FDD_TYPE_NONE, FDD_TYPE_NONE };

static fileTYPE fdd0_image = {};
static fileTYPE fdd1_image = {};

#define IOWR(base, reg, value) x86_dma_set((base) + (reg), value)

typedef struct
{
	uint32_t ver;
	char img_name[3][1024];
} pcxt_config;

static pcxt_config config;

static void x86_dma_set(uint32_t address, uint32_t data)
{
	EnableIO();
	spi8(UIO_DMA_WRITE);
	spi32_w(address);
	spi_w((uint16_t)data);
	DisableIO();
}

void pcxt_init()
{	
	user_io_status_set("[0]", 1);
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

	// HDD CHS Calculator

	unsigned long hdd_cyl = 0, hdd_sect = 0, hdd_head = 0;
	bool is_hdd = false;
	struct hddInfo *hdd_fi;

	if (strlen(config.img_name[2]))
	{
		fd = fopen(config.img_name[2], "r");
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
		images[0] = new FlatImage(config.img_name[2], 0, 0, createFile, hdd_cyl, hdd_head, hdd_sect, useCHS);		
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

static void fdd_set(int num, char* filename)
{
	floppy_type[num] = FDD_TYPE_1440;

	fileTYPE* fdd_image = num ? &fdd1_image : &fdd0_image;

	int floppy = ide_img_mount(fdd_image, filename, 1);
	uint32_t size = fdd_image->size / 512;
	printf("floppy size: %d blks\n", size);
	if (floppy && size)
	{
		if (size >= 8000)
		{
			floppy = 0;
			FileClose(fdd_image);
			printf("Image size is too large for floppy. Closing...\n");
		}
		else if (size >= 5760) floppy_type[num] = FDD_TYPE_2880;
		else if (size >= 3360) floppy_type[num] = FDD_TYPE_1680;
		else if (size >= 2880) floppy_type[num] = FDD_TYPE_1440;
		else if (size >= 2400) floppy_type[num] = FDD_TYPE_1200;
		else if (size >= 1440) floppy_type[num] = FDD_TYPE_720;
		else if (size >= 720) floppy_type[num] = FDD_TYPE_360;
		else if (size >= 640) floppy_type[num] = FDD_TYPE_320;
		else if (size >= 360) floppy_type[num] = FDD_TYPE_180;
		else floppy_type[num] = FDD_TYPE_160;
	}
	else
	{
		floppy = 0;
	}

	/*
	0x00.[0]:      media present
	0x01.[0]:      media writeprotect
	0x02.[7:0]:    media cylinders
	0x03.[7:0]:    media sectors per track
	0x04.[31:0]:   media total sector count
	0x05.[1:0]:    media heads
	0x06.[31:0]:   media sd base
	0x07.[15:0]:   media wait cycles: 200000 us / spt
	0x08.[15:0]:   media wait rate 0: 1000 us
	0x09.[15:0]:   media wait rate 1: 1666 us
	0x0A.[15:0]:   media wait rate 2: 2000 us
	0x0B.[15:0]:   media wait rate 3: 500 us
	0x0C.[7:0]:    media type: 8'h20 none; 8'h00 old; 8'hC0 720k; 8'h80 1_44M; 8'h40 2_88M
	*/

	int floppy_spt = 0;
	int floppy_cylinders = 0;
	int floppy_heads = 0;

	switch (floppy_type[num])
	{
	case FDD_TYPE_160:  floppy_spt = 8;  floppy_cylinders = 40; floppy_heads = 1; break;
	case FDD_TYPE_180:  floppy_spt = 9;  floppy_cylinders = 40; floppy_heads = 1; break;
	case FDD_TYPE_320:  floppy_spt = 8;  floppy_cylinders = 40; floppy_heads = 2; break;
	case FDD_TYPE_360:  floppy_spt = 9;  floppy_cylinders = 40; floppy_heads = 2; break;
	case FDD_TYPE_720:  floppy_spt = 9;  floppy_cylinders = 80; floppy_heads = 2; break;
	case FDD_TYPE_1200: floppy_spt = 15; floppy_cylinders = 80; floppy_heads = 2; break;
	case FDD_TYPE_1440: floppy_spt = 18; floppy_cylinders = 80; floppy_heads = 2; break;
	case FDD_TYPE_1680: floppy_spt = 21; floppy_cylinders = 80; floppy_heads = 2; break;
	case FDD_TYPE_2880: floppy_spt = 36; floppy_cylinders = 80; floppy_heads = 2; break;
	}

	int floppy_total_sectors = floppy_spt * floppy_heads * floppy_cylinders;

	printf("floppy:\n");
	printf("  cylinders:     %d\n", floppy_cylinders);
	printf("  heads:         %d\n", floppy_heads);
	printf("  spt:           %d\n", floppy_spt);
	printf("  total_sectors: %d\n\n", floppy_total_sectors);

	uint32_t subaddr = num << 7;

	IOWR(FDD0_BASE + subaddr, 0x0, 0); // Always eject floppy before insertion
	usleep(100000);

	IOWR(FDD0_BASE + subaddr, 0x0, floppy ? 1 : 0);
	IOWR(FDD0_BASE + subaddr, 0x1, (floppy && (fdd_image->mode & O_RDWR)) ? 0 : 1);
	IOWR(FDD0_BASE + subaddr, 0x2, floppy_cylinders);
	IOWR(FDD0_BASE + subaddr, 0x3, floppy_spt);
	IOWR(FDD0_BASE + subaddr, 0x4, floppy_total_sectors);
	IOWR(FDD0_BASE + subaddr, 0x5, floppy_heads);
	IOWR(FDD0_BASE + subaddr, 0x6, 0); // base LBA
	IOWR(FDD0_BASE + subaddr, 0xC, 0);
}

void pcxt_set_image(int num, char* filename)
{
	memset(config.img_name[num], 0, sizeof(config.img_name[0]));
	strcpy(config.img_name[num], filename);
	if (num < 2) fdd_set(num, filename);
	else hdd_set(num, filename);
}

void hdd_set(int num, char* filename)
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
