/*
 * Copyright (c) 2014, Aleksander Osman
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>

#include "../../spi.h"
#include "../../user_io.h"
#include "../../file_io.h"
#include "../../fpga_io.h"
#include "x86_share.h"

#define FLOPPY0_BASE_OLD     0x8800
#define HDD0_BASE_OLD        0x8840
#define FLOPPY1_BASE_OLD     0x9800
#define HDD1_BASE_OLD        0x9840
#define PC_BUS_BASE_OLD      0x88a0
#define SOUND_BASE_OLD       0x9000
#define PIT_BASE_OLD         0x8880
#define VGA_BASE_OLD         0x8900
#define RTC_BASE_OLD         0x8c00
#define SD_BASE_OLD          0x0A00

#define IMG_TYPE_FDD0_OLD    0x0800
#define IMG_TYPE_FDD1_OLD    0x1800
#define IMG_TYPE_HDD0_OLD    0x0000
#define IMG_TYPE_HDD1_OLD    0x1000

#define HDD0_BASE_NEW        0xF000
#define HDD1_BASE_NEW        0xF100
#define FDD0_BASE_NEW        0xF200
#define FDD1_BASE_NEW        0xF300
#define RTC_BASE_NEW         0xF400

#define CFG_VER          2

#define SHMEM_ADDR      0x30000000
#define BIOS_SIZE       0x10000

static int newcore = 0;

#define IOWR(base, reg, value) dma_set((base) + (newcore ? (reg) : ((reg)<<2)), value)

typedef struct
{
	uint32_t ver;
	char fdd_name[1024];
	char hdd0_name[1024];
	char hdd1_name[1024];
} x86_config;

static x86_config config;

static uint32_t old_cpu_clock = 0;
static uint32_t cpu_get_clock()
{
	uint32_t clock;

	EnableIO();
	spi8(UIO_DMA_WRITE);
	clock = spi_w(0);
	clock = (spi_w(0) << 16) | clock;
	DisableIO();

	return clock ? clock : 90500000;
}

static uint16_t dma_sdio(int status)
{
	uint16_t res;
	EnableIO();
	res = spi_w(UIO_DMA_SDIO);
	if(status || !res) res = (uint8_t)spi_w((uint16_t)status);
	DisableIO();
	return res;
}

/*
static uint32_t dma_get(uint32_t address)
{
	EnableIO();
	spi8(UIO_DMA_READ);
	spi32w(address);
	uint32_t res = spi32w(0);
	DisableIO();
	return res;
}
*/

static void dma_set(uint32_t address, uint32_t data)
{
	EnableIO();
	spi8(UIO_DMA_WRITE);
	spi32_w(address);
	spi32_w(data);
	DisableIO();
}

static void dma_sendbuf(uint32_t address, uint32_t length, uint32_t *data)
{
	EnableIO();
	fpga_spi_fast(UIO_DMA_WRITE);
	fpga_spi_fast(address);
	fpga_spi_fast(0);
	if (newcore)
	{
		if(address < FDD0_BASE_NEW) fpga_spi_fast_block_write((uint16_t*)data, length * 2);
		else
		{
			uint8_t *buf = (uint8_t*)data;
			length *= 4;
			while (length--) spi32_w(*buf++);
		}
	}
	else while (length--) spi32_w(*data++);
	DisableIO();
}

static void dma_recvbuf(uint32_t address, uint32_t length, uint32_t *data)
{
	EnableIO();
	fpga_spi_fast(UIO_DMA_READ);
	fpga_spi_fast(address);
	fpga_spi_fast(0);
	if (newcore)
	{
		if (address <= FDD0_BASE_NEW) fpga_spi_fast_block_read((uint16_t*)data, length * 2);
		else
		{
			uint8_t *buf = (uint8_t*)data;
			length *= 4;
			while (length--) *buf++ = spi32_w(0);
		}
	}
	else while (length--) *data++ = spi32_w(0);
	DisableIO();
}

static void* shmem_init(int offset, int size)
{
	int fd;
	if ((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1) return 0;

	void *shmem = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, SHMEM_ADDR + offset);
	close(fd);

	if (shmem == (void *)-1)
	{
		printf("ao486 share_init: Unable to mmap(/dev/mem)\n");
		return 0;
	}

	return shmem;
}

static int load_bios(const char* name, uint8_t index)
{
	printf("BIOS: %s\n", name);

	void *buf = shmem_init(index ? 0xC0000 : 0xF0000, BIOS_SIZE);
	if (!buf) return 0;

	memset(buf, 0, BIOS_SIZE);
	FileLoad(name, buf, BIOS_SIZE);
	munmap(buf, BIOS_SIZE);

	return 1;
}

static bool floppy_is_160k = false;
static bool floppy_is_180k = false;
static bool floppy_is_320k = false;
static bool floppy_is_360k = false;
static bool floppy_is_720k = false;
static bool floppy_is_1_2m = false;
static bool floppy_is_1_44m= false;
static bool floppy_is_1_68m= false;
static bool floppy_is_2_88m= false;

#define CMOS_FDD_TYPE ((floppy_is_2_88m) ? 0x50 : (floppy_is_1_44m || floppy_is_1_68m) ? 0x40 : (floppy_is_720k) ? 0x30 : (floppy_is_1_2m) ? 0x20 : 0x10)

struct image_t
{
	fileTYPE f;
	uint32_t cached_lba;
	uint32_t last_lba;
	uint8_t buf[512 * 8];
};

static image_t fdd0_image = {};
static image_t hdd0_image = {};
static image_t hdd1_image = {};
static bool boot_from_floppy = 1;

static int img_mount(image_t *img, char *name)
{
	FileClose(&img->f);
	img->cached_lba = UINT32_MAX;
	img->last_lba = 0;

	int writable = 0, ret = 0;

	if (strlen(name))
	{
		writable = FileCanWrite(name);
		ret = FileOpenEx(&img->f, name, writable ? (O_RDWR | O_SYNC) : O_RDONLY);
		if (!ret) printf("Failed to open file %s\n", name);
	}

	if (!ret)
	{
		img->f.size = 0;
		return 0;
	}

	printf("Mount %s as %s\n", name, writable ? "read-write" : "read-only");
	return 1;
}

static uint32_t* img_read(image_t *img, uint32_t lba, uint32_t cnt)
{
	if (img->cached_lba == UINT32_MAX || lba < img->cached_lba || (lba + cnt) > img->cached_lba + (sizeof(img->buf)/512))
	{
		img->cached_lba = UINT32_MAX;
		if (!FileSeekLBA(&img->f, lba)) return 0;
		if (!FileReadAdv(&img->f, img->buf, sizeof(img->buf))) return 0;
		img->cached_lba = lba;
	}

	return (uint32_t *)(img->buf + ((lba - img->cached_lba) * 512));
}

static uint32_t img_write(image_t *img, uint32_t lba, void *buf, uint32_t cnt)
{
	if (img->cached_lba != UINT32_MAX && lba >= img->cached_lba && lba < img->cached_lba + (sizeof(img->buf) / 512))
	{
		img->cached_lba = UINT32_MAX;
	}

	if (!FileSeekLBA(&img->f, lba)) return 0;
	return FileWriteAdv(&img->f, buf, cnt * 512);
}

static uint32_t cmos[128];
static int floppy_wait_cycles;

static void set_clock()
{
	uint32_t cpu_clock = cpu_get_clock();
	old_cpu_clock = cpu_clock;

	IOWR(FLOPPY0_BASE_OLD, 0x7, (int)(floppy_wait_cycles / (1000000000.0 / cpu_clock)));
	IOWR(FLOPPY0_BASE_OLD, 0x8, (int)(1000000.0 / (1000000000.0 / cpu_clock)));
	IOWR(FLOPPY0_BASE_OLD, 0x9, (int)(1666666.0 / (1000000000.0 / cpu_clock)));
	IOWR(FLOPPY0_BASE_OLD, 0xA, (int)(2000000.0 / (1000000000.0 / cpu_clock)));
	IOWR(FLOPPY0_BASE_OLD, 0xB, (int)(500000.0 / (1000000000.0 / cpu_clock)));

	IOWR(VGA_BASE_OLD, 0, cpu_clock);

	//-------------------------------------------------------------------------- sound
	/*
	0-255.[15:0]: cycles in period
	256.[12:0]:  cycles in 80us
	257.[9:0]:   cycles in 1 sample: 96000 Hz
	*/

	double cycle_in_ns = (1000000000.0 / cpu_clock); //33.333333;
	for (int i = 0; i < 256; i++)
	{
		double f = 1000000.0 / (256.0 - i);

		double cycles_in_period = 1000000000.0 / (f * cycle_in_ns);
		IOWR(SOUND_BASE_OLD, i, (int)cycles_in_period);
	}

	IOWR(SOUND_BASE_OLD, 256, (int)(80000.0 / (1000000000.0 / cpu_clock)));
	IOWR(SOUND_BASE_OLD, 257, (int)((1000000000.0 / 96000.0) / (1000000000.0 / cpu_clock)));

	//-------------------------------------------------------------------------- pit
	/*
	0.[7:0]: cycles in sysclock 1193181 Hz
	*/

	IOWR(PIT_BASE_OLD, 0, (int)((1000000000.0 / 1193181.0) / (1000000000.0 / cpu_clock)));

	/*
	128.[26:0]: cycles in second
	129.[12:0]: cycles in 122.07031 us
	*/

	IOWR(RTC_BASE_OLD, 128, (int)(1000000000.0 / (1000000000.0 / cpu_clock)));
	IOWR(RTC_BASE_OLD, 129, (int)(122070.0 / (1000000000.0 / cpu_clock)));
}

static int fdd_set(char* filename)
{
	floppy_is_160k = false;
	floppy_is_180k = false;
	floppy_is_320k = false;
	floppy_is_360k = false;
	floppy_is_720k = false;
	floppy_is_1_2m = false;
	floppy_is_1_44m = false;
	floppy_is_1_68m = false;
	floppy_is_2_88m = false;

	uint32_t base = newcore ? FDD0_BASE_NEW : FLOPPY0_BASE_OLD;

	int floppy = img_mount(&fdd0_image, filename);
	uint32_t size = fdd0_image.f.size/512;
	printf("floppy size: %d blks\n", size);
	if (floppy && size)
	{
		if (size >= 5760) floppy_is_2_88m = true;
		else if (size >= 3360) floppy_is_1_68m = true;
		else if (size >= 2880) floppy_is_1_44m = true;
		else if (size >= 2400) floppy_is_1_2m = true;
		else if (size >= 1440) floppy_is_720k = true;
		else if (size >= 720) floppy_is_360k = true;
		else if (size >= 640) floppy_is_320k = true;
		else if (size >= 360) floppy_is_180k = true;
		else floppy_is_160k = true;
	}
	else
	{
		floppy = 0;
		floppy_is_1_44m = true;
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

	int floppy_spt =
		(floppy_is_160k) ? 8 :
		(floppy_is_180k) ? 9 :
		(floppy_is_320k) ? 8 :
		(floppy_is_360k) ? 9 :
		(floppy_is_720k) ? 9 :
		(floppy_is_1_2m) ? 15 :
		(floppy_is_1_44m) ? 18 :
		(floppy_is_1_68m) ? 21 :
		(floppy_is_2_88m) ? 36 :
		0;

	int floppy_cylinders     = (floppy_is_2_88m || floppy_is_1_68m || floppy_is_1_44m || floppy_is_1_2m || floppy_is_720k) ? 80 : 40;
	int floppy_heads         = (floppy_is_160k || floppy_is_180k) ? 1 : 2;
	int floppy_total_sectors = floppy_spt * floppy_heads * floppy_cylinders;
	floppy_wait_cycles       = 200000000 / floppy_spt;

	printf("floppy:\n");
	printf("  cylinders:     %d\n", floppy_cylinders);
	printf("  heads:         %d\n", floppy_heads);
	printf("  spt:           %d\n", floppy_spt);
	printf("  total_sectors: %d\n\n", floppy_total_sectors);

	int floppy_media =
		(!floppy) ? 0x20 :
		(floppy_is_160k) ? 0x00 :
		(floppy_is_180k) ? 0x00 :
		(floppy_is_320k) ? 0x00 :
		(floppy_is_360k) ? 0x00 :
		(floppy_is_720k) ? 0xC0 :
		(floppy_is_1_2m) ? 0x00 :
		(floppy_is_1_44m) ? 0x80 :
		(floppy_is_2_88m) ? 0x40 :
		0x20;

	IOWR(base, 0x0, floppy ? 1 : 0);
	IOWR(base, 0x1, (floppy && (fdd0_image.f.mode & O_RDWR)) ? 0 : 1);
	IOWR(base, 0x2, floppy_cylinders);
	IOWR(base, 0x3, floppy_spt);
	IOWR(base, 0x4, floppy_total_sectors);
	IOWR(base, 0x5, floppy_heads);
	IOWR(base, 0x6, 0); // base LBA
	IOWR(base, 0xC, floppy_media);

	if(!newcore) set_clock();

	//cmos_set(0x10, CMOS_FDD_TYPE);
	return floppy;
}

static int hdd_set(uint32_t num, char* filename)
{
	uint32_t hd_cylinders;
	uint32_t hd_heads;
	uint32_t hd_spt;
	uint32_t hd_total_sectors;
	uint32_t present;

	uint32_t base = newcore ? (num ? HDD1_BASE_NEW : HDD0_BASE_NEW) : (num ? HDD1_BASE_OLD : HDD0_BASE_OLD);
	image_t *img = num ? &hdd1_image : &hdd0_image;

	hd_cylinders = 0;
	hd_heads = 0;
	hd_spt = 0;
	hd_total_sectors = 0;

	present = img_mount(img, filename);
	if (!present) return 0;

	hd_heads = 16;
	hd_spt = 63;
	hd_cylinders = img->f.size / (hd_heads * hd_spt * 512);

	//Maximum 8GB images are supported.
	if (hd_cylinders > 16383) hd_cylinders = 16383;
	hd_total_sectors = hd_spt*hd_heads*hd_cylinders;

	/*
	0x00.[31:0]:    identify write
	0x01.[16:0]:    media cylinders
	0x02.[4:0]:     media heads
	0x03.[8:0]:     media spt
	0x04.[13:0]:    media sectors per cylinder = spt * heads
	0x05.[31:0]:    media sectors total
	0x06.[31:0]:    media sd base
	*/

	uint32_t identify[256] =
	{
		0x0040, 										//word 0
		hd_cylinders, 	                                //word 1
		0x0000,											//word 2 reserved
		hd_heads,			               				//word 3
		(uint16_t)(512 * hd_spt),						//word 4
		512,											//word 5
		hd_spt,											//word 6
		0x0000,											//word 7 vendor specific
		0x0000,											//word 8 vendor specific
		0x0000,											//word 9 vendor specific
		('A' << 8) | 'O',								//word 10
		('H' << 8) | 'D',								//word 11
		('0' << 8) | '0',								//word 12
		('0' << 8) | '0',								//word 13
		('0' << 8) | ' ',								//word 14
		(' ' << 8) | ' ',								//word 15
		(' ' << 8) | ' ',								//word 16
		(' ' << 8) | ' ',								//word 17
		(' ' << 8) | ' ',								//word 18
		(' ' << 8) | ' ',								//word 19
		3,   											//word 20 buffer type
		512,											//word 21 cache size
		4,												//word 22 number of ecc bytes
		0,0,0,0,										//words 23..26 firmware revision
		(' ' << 8) | ' ',								//words 27..46 model number
		(' ' << 8) | ' ',
		(' ' << 8) | ' ',
		(' ' << 8) | ' ',
		(' ' << 8) | ' ',
		(' ' << 8) | ' ',
		(' ' << 8) | ' ',
		(' ' << 8) | ' ',
		(' ' << 8) | ' ',
		(' ' << 8) | ' ',
		(' ' << 8) | ' ',
		(' ' << 8) | ' ',
		(' ' << 8) | ' ',
		(' ' << 8) | ' ',
		(' ' << 8) | ' ',
		(' ' << 8) | ' ',
		(' ' << 8) | ' ',
		(' ' << 8) | ' ',
		(' ' << 8) | ' ',
		(' ' << 8) | ' ',
		16,												//word 47 max multiple sectors
		1,												//word 48 dword io
		1 << 9,											//word 49 lba supported
		0x0000,											//word 50 reserved
		0x0200,											//word 51 pio timing
		0x0200,											//word 52 pio timing
		0x0007,											//word 53 valid fields
		hd_cylinders, 									//word 54
		hd_heads,										//word 55
		hd_spt,											//word 56
		hd_total_sectors & 0xFFFF,						//word 57
		hd_total_sectors >> 16,							//word 58
		16,												//word 59 multiple sectors
		hd_total_sectors & 0xFFFF,						//word 60
		hd_total_sectors >> 16,							//word 61
		0x0000,											//word 62 single word dma modes
		0x0000,											//word 63 multiple word dma modes
		0x0000,											//word 64 pio modes
		120,120,120,120,								//word 65..68
		0,0,0,0,0,0,0,0,0,0,0,							//word 69..79
		0x007E,											//word 80 ata modes
		0x0000,											//word 81 minor version number
		1 << 14,  										//word 82 supported commands
		(1 << 14) | (1 << 13) | (1 << 12) | (1 << 10),	//word 83
		1 << 14,	    								//word 84
		1 << 14,	 	    							//word 85
		(1 << 14) | (1 << 13) | (1 << 12) | (1 << 10),	//word 86
		1 << 14,	    								//word 87
		0x0000,											//word 88
		0,0,0,0,										//word 89..92
		1 | (1 << 14) | 0x2000,							//word 93
		0,0,0,0,0,0,									//word 94..99
		hd_total_sectors & 0xFFFF,						//word 100
		hd_total_sectors >> 16,							//word 101
		0,												//word 102
		0,												//word 103

		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,//word 104..127

		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,				//word 128..255
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	};

	if (present)
	{
		char *name = img->f.name;
		for (int i = 0; i < 20; i++)
		{
			if (*name) identify[27 + i] = ((*name++) << 8) | 0x20;
			if (*name) identify[27 + i] = (identify[27 + i] & 0xFF00) | (*name++);
		}
	}

	for (int i = 0; i < 128; i++) IOWR(base, 0, present ? ((unsigned int)identify[2 * i + 1] << 16) | (unsigned int)identify[2 * i + 0] : 0);

	IOWR(base, 1, hd_cylinders);
	IOWR(base, 2, hd_heads);
	IOWR(base, 3, hd_spt);
	IOWR(base, 4, hd_spt * hd_heads);
	IOWR(base, 5, hd_spt * hd_heads * hd_cylinders);
	IOWR(base, 6, 0); // base LBA

	printf("HDD%d:\n  present %d\n  hd_cylinders %d\n  hd_heads %d\n  hd_spt %d\n  hd_total_sectors %d\n\n", num, present, hd_cylinders, hd_heads, hd_spt, hd_total_sectors);
	return present;
}

static uint8_t bin2bcd(unsigned val)
{
	return ((val / 10) << 4) + (val % 10);
}

void x86_init()
{
	user_io_8bit_set_status(UIO_STATUS_RESET, UIO_STATUS_RESET);

	const char *home = HomeDir();

	load_bios(user_io_make_filepath(home, "boot0.rom"), 0);
	load_bios(user_io_make_filepath(home, "boot1.rom"), 1);

	newcore = ((dma_sdio(0) & 0xC000) == 0xC000);

	if (!newcore)
	{
		IOWR(PC_BUS_BASE_OLD, 0, 0x00FFF0EA);
		IOWR(PC_BUS_BASE_OLD, 1, 0x000000F0);
	}

	fdd_set(config.fdd_name);
	hdd_set(0, config.hdd0_name);
	hdd_set(1, config.hdd1_name);

	//-------------------------------------------------------------------------- rtc

	unsigned char translate_mode = 1; //LBA
	translate_mode = (translate_mode << 6) | (translate_mode << 4) | (translate_mode << 2) | translate_mode;

	time_t t = time(NULL);
	struct tm tm = *localtime(&t);

	//rtc contents 0-127
	uint32_t tmp[128] = {
		bin2bcd(tm.tm_sec), //0x00: SEC BCD
		0x00, //0x01: ALARM SEC BCD
		bin2bcd(tm.tm_min), //0x02: MIN BCD
		0x00, //0x03: ALARM MIN BCD
		bin2bcd(tm.tm_hour), //0x04: HOUR BCD 24h
		0x12, //0x05: ALARM HOUR BCD 24h
		(uint32_t)tm.tm_wday + 1, //0x06: DAY OF WEEK Sunday=1
		bin2bcd(tm.tm_mday), //0x07: DAY OF MONTH BCD from 1
		bin2bcd(tm.tm_mon + 1), //0x08: MONTH BCD from 1
		bin2bcd((tm.tm_year < 117) ? 17 : tm.tm_year - 100), //0x09: YEAR BCD
		0x26, //0x0A: REG A
		0x02, //0x0B: REG B
		0x00, //0x0C: REG C
		0x80, //0x0D: REG D
		0x00, //0x0E: REG E - POST status
		0x00, //0x0F: REG F - shutdown status

		(uint32_t)CMOS_FDD_TYPE, //0x10: floppy drive type; 0-none, 1-360K, 2-1.2M, 3-720K, 4-1.44M, 5-2.88M
		0x00, //0x11: configuration bits; not used
		0x00, //0x12: hard disk types; 0-none, 1:E-type, F-type 16+ (unused)
		0x00, //0x13: advanced configuration bits; not used
		0x0D, //0x14: equipment bits
		0x80, //0x15: base memory in 1k LSB
		0x02, //0x16: base memory in 1k MSB
		0x00, //0x17: memory size above 1m in 1k LSB
		0xFC, //0x18: memory size above 1m in 1k MSB
		0x00, //0x19: extended hd types 1/2; type 47d (unused)
		0x00, //0x1A: extended hd types 2/2 (unused)

		//these hd parameters aren't used anymore
		0x00, //0x1B: hd 0 configuration 1/9; cylinders low
		0x00, //0x1C: hd 0 configuration 2/9; cylinders high
		0x00, //0x1D: hd 0 configuration 3/9; heads
		0x00, //0x1E: hd 0 configuration 4/9; write pre-comp low
		0x00, //0x1F: hd 0 configuration 5/9; write pre-comp high
		0x00, //0x20: hd 0 configuration 6/9; retries/bad map/heads>8
		0x00, //0x21: hd 0 configuration 7/9; landing zone low
		0x00, //0x22: hd 0 configuration 8/9; landing zone high
		0x00, //0x23: hd 0 configuration 9/9; sectors/track
		0x00, //0x24: hd 1 configuration 1/9; cylinders low
		0x00, //0x25: hd 1 configuration 2/9; cylinders high
		0x00, //0x26: hd 1 configuration 3/9; heads
		0x00, //0x27: hd 1 configuration 4/9; write pre-comp low
		0x00, //0x28: hd 1 configuration 5/9; write pre-comp high
		0x00, //0x29: hd 1 configuration 6/9; retries/bad map/heads>8
		0x00, //0x2A: hd 1 configuration 7/9; landing zone low
		0x00, //0x2B: hd 1 configuration 8/9; landing zone high
		0x00, //0x2C: hd 1 configuration 9/9; sectors/track

		(fdd0_image.f.size && boot_from_floppy) ? 0x20u : 0x00u, //0x2D: boot sequence

		0x00, //0x2E: checksum MSB
		0x00, //0x2F: checksum LSB

		0x00, //0x30: memory size above 1m in 1k LSB
		0x3C, //0x31: memory size above 1m in 1k MSB

		0x20, //0x32: IBM century
		0x00, //0x33: ?

		0x80, //0x34: memory size above 16m in 64k LSB
		0x0E, //0x35: memory size above 16m in 64k MSB; 256-8 MB

		0x00, //0x36: ?
		0x20, //0x37: IBM PS/2 century

		0x00, 		    //0x38: eltorito boot sequence; not used
		translate_mode, //0x39: ata translation policy 1-4
		0x00,           //0x3A: ata translation policy 5-8

		0x00, //0x3B: ?
		0x00, //0x3C: ?

		0x00, //0x3D: eltorito boot sequence; not used

		0x00, //0x3E: ?
		0x00, //0x3F: ?

		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0
	};

	printf("cmod[2d] = %x\n", tmp[0x2d]);

	memcpy(cmos, tmp, sizeof(cmos));

	//count checksum
	unsigned short sum = 0;
	for (int i = 0x10; i <= 0x2D; i++) sum += cmos[i];

	cmos[0x2E] = sum >> 8;
	cmos[0x2F] = sum & 0xFF;
	for (unsigned int i = 0; i < sizeof(cmos) / sizeof(uint32_t); i++) IOWR(newcore ? RTC_BASE_NEW : RTC_BASE_OLD, i, cmos[i]);

	x86_share_reset();
	user_io_8bit_set_status(0, UIO_STATUS_RESET);
}

static void img_io(image_t *img, uint32_t basereg, uint8_t read, uint8_t next)
{
	struct sd_param_t
	{
		uint32_t lba;
		uint32_t cnt;
	};

	static struct sd_param_t sd_params = {};
	static uint32_t secbuf[128 * 4];

	int res = 0;

	if (img->last_lba && next)
	{
		//printf("%s next: %d\n", read ? "Read" : "Write", img->last_lba);
		sd_params.lba = img->last_lba;
		sd_params.cnt = 1;
	}
	else
	{
		dma_recvbuf(basereg, sizeof(sd_params) >> 2, (uint32_t*)&sd_params);
	}

	img->last_lba = sd_params.lba + sd_params.cnt;

	if (read)
	{
		//printf("Read: 0x%08x, 0x%08x, %d\n", basereg, sd_params.lba, sd_params.cnt);

		if (img->f.size)
		{
			if (sd_params.cnt > 0 && sd_params.cnt <= 4)
			{
				uint32_t *buf = img_read(img, sd_params.lba, sd_params.cnt);
				if (buf)
				{
					dma_sendbuf(basereg + 255, sd_params.cnt * 128, buf);
					img_read(img, sd_params.lba + 1, 1); //prefetch next
					res = 1;
				}
			}
			else
			{
				printf("Error: Block count %d is out of range 1..4.\n", sd_params.cnt);
			}
		}
		else
		{
			printf("Error: image is not ready.\n");
		}

		if (!res)
		{
			memset(secbuf, 0, sd_params.cnt * 512);
			dma_sendbuf(basereg + 255, sd_params.cnt * 128, secbuf);
		}
	}
	else
	{
		//printf("Write: 0x%08x, 0x%08x, %d\n", basereg, sd_params.lba, sd_params.cnt);

		dma_recvbuf(basereg + 255, sd_params.cnt * 128, secbuf);
		if (img->f.size)
		{
			if (sd_params.cnt > 0 && sd_params.cnt <= 4)
			{
				if (img->f.mode & O_RDWR)
				{
					if (img_write(img, sd_params.lba, secbuf, sd_params.cnt))
					{
						res = 1;
					}
				}
				else
				{
					printf("Error: image is read-only.\n");
				}
			}
			else
			{
				printf("Error: Block count %d is out of range 1..4.\n", sd_params.cnt);
			}
		}
		else
		{
			printf("Error: image is not ready.\n");
		}
	}
}

void img_io_old(uint8_t sd_req)
{
	struct sd_param_t
	{
		uint32_t addr;
		uint32_t lba;
		uint32_t bl_cnt;
	};

	static struct sd_param_t sd_params = {};
	static uint32_t secbuf[128 * 4];
	dma_recvbuf(SD_BASE_OLD + (4 << 2), sizeof(sd_params) >> 2, (uint32_t*)&sd_params);

	image_t *img;
	switch (sd_params.addr)
	{
		case IMG_TYPE_HDD0_OLD:
			//printf("HDD0 req\n");
			img = &hdd0_image;
			break;
		case IMG_TYPE_HDD1_OLD:
			//printf("HDD1 req\n");
			img = &hdd1_image;
			break;
		default:
			//printf("FDD req\n");
			img = &fdd0_image;
			break;
	}

	int res = 0;
	if (sd_req == 1)
	{
		//printf("Read(old): 0x%08x, 0x%08x, %d\n", sd_params.addr, sd_params.lba, sd_params.bl_cnt);

		if (img->f.size)
		{
			if (sd_params.bl_cnt > 0 && sd_params.bl_cnt <= 4)
			{
				uint32_t *buf = img_read(img, sd_params.lba, sd_params.bl_cnt);
				if (buf)
				{
					dma_sendbuf(sd_params.addr, sd_params.bl_cnt * 128, buf);
					res = 1;
				}
			}
			else
			{
				printf("Error: Block count %d is out of range 1..4.\n", sd_params.bl_cnt);
			}
		}
		else
		{
			printf("Error: image is not ready.\n");
		}

		dma_sdio(res ? 1 : 2);
	}
	else if (sd_req == 2)
	{
		//printf("Write(old): 0x%08x, 0x%08x, %d\n", sd_params.addr, sd_params.lba, sd_params.bl_cnt);

		if (img->f.size)
		{
			if (sd_params.bl_cnt > 0 && sd_params.bl_cnt <= 4)
			{
				if (img->f.mode & O_RDWR)
				{
					dma_recvbuf(sd_params.addr, sd_params.bl_cnt * 128, secbuf);
					if (img_write(img, sd_params.lba, secbuf, sd_params.bl_cnt))
					{
						res = 1;
					}
				}
				else
				{
					printf("Error: image is read-only.\n");
				}
			}
			else
			{
				printf("Error: Block count %d is out of range 1..4.\n", sd_params.bl_cnt);
			}
		}
		else
		{
			printf("Error: image is not ready.\n");
		}

		dma_sdio(res ? 1 : 2);
	}
}

void x86_poll()
{
	if (!newcore)
	{
		uint32_t cpu_clock = cpu_get_clock();
		if (cpu_clock != old_cpu_clock) set_clock();
	}

	x86_share_poll();

	uint16_t sd_req = dma_sdio(0);
	if (sd_req)
	{
		if (sd_req & 0x8000)
		{
			if (sd_req & 3) img_io(&hdd0_image, HDD0_BASE_NEW, sd_req & 1, sd_req & 4);
			sd_req >>= 3;
			if (sd_req & 3) img_io(&hdd1_image, HDD1_BASE_NEW, sd_req & 1, sd_req & 4);
			sd_req >>= 3;
			if (sd_req & 3) img_io(&fdd0_image, FDD0_BASE_NEW, sd_req & 1, 0);
		}
		else
		{
			img_io_old((uint8_t)sd_req);
		}
	}
}

void x86_set_image(int num, char *filename)
{
	switch (num)
	{
	case 0:
		memset(config.fdd_name, 0, sizeof(config.fdd_name));
		strcpy(config.fdd_name, filename);
		fdd_set(filename);
		break;

	case 2:
		memset(config.hdd0_name, 0, sizeof(config.hdd0_name));
		strcpy(config.hdd0_name, filename);
		break;

	case 3:
		memset(config.hdd1_name, 0, sizeof(config.hdd1_name));
		strcpy(config.hdd1_name, filename);
		break;
	}
}

void x86_config_save()
{
	config.ver = CFG_VER;
	FileSaveConfig("ao486sys.cfg", &config, sizeof(config));
}

void x86_config_load()
{
	newcore = ((dma_sdio(0) & 0xC000) == 0xC000);
	static x86_config tmp;
	memset(&config, 0, sizeof(config));
	if (FileLoadConfig("ao486sys.cfg", &tmp, sizeof(tmp)) && (tmp.ver == CFG_VER))
	{
		memcpy(&config, &tmp, sizeof(config));
	}
}

void x86_set_fdd_boot(uint32_t boot)
{
	boot_from_floppy = (boot != 0);
}

void x86_set_uart_mode(int mode)
{
	dma_sdio(mode ? 0x40 : 0x80);
}
