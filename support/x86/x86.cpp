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
#include <time.h>

#include "../../spi.h"
#include "../../user_io.h"
#include "../../file_io.h"
#include "../../fpga_io.h"
#include "../../shmem.h"
#include "../../ide.h"
#include "x86_share.h"

#define FDD0_BASE   0xF200
#define FDD1_BASE   0xF300
#define RTC_BASE    0xF400

#define CFG_VER     3

#define SHMEM_ADDR  0x30000000
#define BIOS_SIZE   0x10000

#define IOWR(base, reg, value) x86_dma_set((base) + (reg), value)

unsigned int hdd_table[128][3] = {
	{  306,  4, 17 },		/* 0 - 7 */
	{  615,  2, 17 },
	{  306,  4, 26 },
	{ 1024,  2, 17 },
	{  697,  3, 17 },
	{  306,  8, 17 },
	{  614,  4, 17 },
	{  615,  4, 17 },

	{  670,  4, 17 },		/* 8 - 15 */
	{  697,  4, 17 },
	{  987,  3, 17 },
	{  820,  4, 17 },
	{  670,  5, 17 },
	{  697,  5, 17 },
	{  733,  5, 17 },
	{  615,  6, 17 },

	{  462,  8, 17 },		/* 016-023 */
	{  306,  8, 26 },
	{  615,  4, 26 },
	{ 1024,  4, 17 },
	{  855,  5, 17 },
	{  925,  5, 17 },
	{  932,  5, 17 },
	{ 1024,  2, 40 },

	{  809,  6, 17 },		/* 024-031 */
	{  976,  5, 17 },
	{  977,  5, 17 },
	{  698,  7, 17 },
	{  699,  7, 17 },
	{  981,  5, 17 },
	{  615,  8, 17 },
	{  989,  5, 17 },

	{  820,  4, 26 },		/* 032-039 */
	{ 1024,  5, 17 },
	{  733,  7, 17 },
	{  754,  7, 17 },
	{  733,  5, 26 },
	{  940,  6, 17 },
	{  615,  6, 26 },
	{  462,  8, 26 },

	{  830,  7, 17 },		/* 040-047 */
	{  855,  7, 17 },
	{  751,  8, 17 },
	{ 1024,  4, 26 },
	{  918,  7, 17 },
	{  925,  7, 17 },
	{  855,  5, 26 },
	{  977,  7, 17 },

	{  987,  7, 17 },		/* 048-055 */
	{ 1024,  7, 17 },
	{  823,  4, 38 },
	{  925,  8, 17 },
	{  809,  6, 26 },
	{  976,  5, 26 },
	{  977,  5, 26 },
	{  698,  7, 26 },

	{  699,  7, 26 },		/* 056-063 */
	{  940,  8, 17 },
	{  615,  8, 26 },
	{ 1024,  5, 26 },
	{  733,  7, 26 },
	{ 1024,  8, 17 },
	{  823, 10, 17 },
	{  754, 11, 17 },

	{  830, 10, 17 },		/* 064-071 */
	{  925,  9, 17 },
	{ 1224,  7, 17 },
	{  940,  6, 26 },
	{  855,  7, 26 },
	{  751,  8, 26 },
	{ 1024,  9, 17 },
	{  965, 10, 17 },

	{  969,  5, 34 },		/* 072-079 */
	{  980, 10, 17 },
	{  960,  5, 35 },
	{  918, 11, 17 },
	{ 1024, 10, 17 },
	{  977,  7, 26 },
	{ 1024,  7, 26 },
	{ 1024, 11, 17 },

	{  940,  8, 26 },		/* 080-087 */
	{  776,  8, 33 },
	{  755, 16, 17 },
	{ 1024, 12, 17 },
	{ 1024,  8, 26 },
	{  823, 10, 26 },
	{  830, 10, 26 },
	{  925,  9, 26 },

	{  960,  9, 26 },		/* 088-095 */
	{ 1024, 13, 17 },
	{ 1224, 11, 17 },
	{  900, 15, 17 },
	{  969,  7, 34 },
	{  917, 15, 17 },
	{  918, 15, 17 },
	{ 1524,  4, 39 },

	{ 1024,  9, 26 },		/* 096-103 */
	{ 1024, 14, 17 },
	{  965, 10, 26 },
	{  980, 10, 26 },
	{ 1020, 15, 17 },
	{ 1023, 15, 17 },
	{ 1024, 15, 17 },
	{ 1024, 16, 17 },

	{ 1224, 15, 17 },		/* 104-111 */
	{  755, 16, 26 },
	{  903,  8, 46 },
	{  984, 10, 34 },
	{  900, 15, 26 },
	{  917, 15, 26 },
	{ 1023, 15, 26 },
	{  684, 16, 38 },

	{ 1930,  4, 62 },		/* 112-119 */
	{  967, 16, 31 },
	{ 1013, 10, 63 },
	{ 1218, 15, 36 },
	{  654, 16, 63 },
	{  659, 16, 63 },
	{  702, 16, 63 },
	{ 1002, 13, 63 },

	{  854, 16, 63 },		/* 119-127 */
	{  987, 16, 63 },
	{  995, 16, 63 },
	{ 1024, 16, 63 },
	{ 1036, 16, 63 },
	{ 1120, 16, 59 },
	{ 1054, 16, 63 },
	{    0,  0,  0 }
};

struct hddInfo {
	unsigned long size;
	unsigned long cylinders;
	unsigned long heads;
	unsigned long sectors;
};

struct hddInfo hddInfos[] = { { 0, 0, 0, 0 } };

typedef struct
{
	uint32_t ver;
	char img_name[6][1024];
} x86_config;

static x86_config config = {};

struct hddInfo* FindHDDInfoBySize(uint64_t size)
{
	struct hddInfo* fi;
	uint64_t size_chs;
	bool is_chs = false;


	for (int i = 0; i < 127; i++)
	{
		size_chs = hdd_table[i][0] * hdd_table[i][1] * hdd_table[i][2] * 512;
		if (size == size_chs)
		{
			fi = hddInfos;
			fi->size = size;
			fi->cylinders = hdd_table[i][0];
			fi->heads = hdd_table[i][1];
			fi->sectors = hdd_table[i][2];
			is_chs = true;
			break;
		}
	}

	if (!is_chs) fi = NULL;
	return(fi);
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

static void x86_dma_set(uint32_t address, uint32_t data)
{
	EnableIO();
	spi8(UIO_DMA_WRITE);
	spi32_w(address);
	spi_w((uint16_t)data);
	DisableIO();
}

static void x86_dma_sendbuf(uint32_t address, uint32_t length, uint32_t *data)
{
	EnableIO();
	fpga_spi_fast(UIO_DMA_WRITE);
	fpga_spi_fast(address);
	fpga_spi_fast(0);

	if(address < FDD0_BASE) fpga_spi_fast_block_write((uint16_t*)data, length * 2);
	else
	{
		uint8_t *buf = (uint8_t*)data;
		length *= 4;
		while (length--) spi_w(*buf++);
	}
	DisableIO();
}

static void x86_dma_recvbuf(uint32_t address, uint32_t length, uint32_t *data)
{
	EnableIO();
	fpga_spi_fast(UIO_DMA_READ);
	fpga_spi_fast(address);
	fpga_spi_fast(0);

	if (address < FDD0_BASE) fpga_spi_fast_block_read((uint16_t*)data, length * 2);
	else if (address == FDD0_BASE)
	{
		while (length--) *data++ = spi_w(0);
	}
	else
	{
		uint8_t *buf = (uint8_t*)data;
		length *= 4;
		while (length--) *buf++ = spi_w(0);
	}
	DisableIO();
}

static int load_bios(const char* name, uint8_t index)
{
	printf("BIOS: %s\n", name);

	void *buf = shmem_map(SHMEM_ADDR + (index ? 0xC0000 : 0xF0000), BIOS_SIZE);
	if (!buf) return 0;

	memset(buf, 0, BIOS_SIZE);
	FileLoad(name, buf, BIOS_SIZE);
	shmem_unmap(buf, BIOS_SIZE);

	return 1;
}

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

static uint8_t get_fdd_bios_type(char type)
{
	switch (type)
	{
	case FDD_TYPE_2880:
		return 0x5;

	case FDD_TYPE_1440:
	case FDD_TYPE_1680:
		return 0x4;

	case FDD_TYPE_720:
		return 0x3;

	case FDD_TYPE_1200:
		return 0x2;
	}

	return 0x1;
}

static fileTYPE fdd0_image = {};
static fileTYPE fdd1_image = {};
static fileTYPE ide_image[4] = {};
static bool boot_from_floppy = 1;

static int img_read(fileTYPE *f, uint32_t lba, void *buf, uint32_t cnt)
{
	if (!FileSeekLBA(f, lba)) return 0;
	return FileReadAdv(f, buf, cnt * 512);
}

static uint32_t img_write(fileTYPE *f, uint32_t lba, void *buf, uint32_t cnt)
{
	if (!FileSeekLBA(f, lba)) return 0;
	return FileWriteAdv(f, buf, cnt * 512);
}

static void fdd_set(int num, char* filename)
{
	floppy_type[num] = FDD_TYPE_1440;

	fileTYPE *fdd_image = num ? &fdd1_image : &fdd0_image;

	int floppy = ide_img_mount(fdd_image, filename, 1);
	uint32_t size = fdd_image->size/512;
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

static void hdd_set(int num, char* filename)
{
	int present = 0;
	int cd = 0;

	int len = strlen(filename);
	int vhd = (len > 4 && !strcasecmp(filename + len - 4, ".vhd"));

	if (num > 1 && !vhd)
	{
		const char *img_name = cdrom_parse(num, filename);
		if (img_name) present = ide_img_mount(&ide_image[num], img_name, 0);
		if (present) cd = 1;
	}

	if(!present && vhd) present = ide_img_mount(&ide_image[num], filename, 1);
	if (!cd && is_pcxt())
	{
		FILE* fd;
		uint64_t size;
		struct hddInfo* hdd_fi;

		const char* path = getFullPath(filename);
		fd = fopen(path, "r");
		if (fd)
		{
			fseek(fd, 0L, SEEK_END);
			size = ftello64(fd);
			if ((hdd_fi = FindHDDInfoBySize(size)))
			{
				ide_img_set(num, present ? &ide_image[num] : 0, cd, hdd_fi->sectors, hdd_fi->heads);
			}
			else
			{
				if (size > 8455200768ULL) // 16383 cylinders * 16 heads * 63 sectors * 512 bytes per sector (Max. CHS)
				{
					ide_img_set(num, present ? &ide_image[num] : 0, cd);
				}
				else
				{
					ide_img_set(num, present ? &ide_image[num] : 0, cd, 63, 16);
				}
			}
		}
	}
	else
	{
		ide_img_set(num, present ? &ide_image[num] : 0, cd);
	}
}

static uint8_t bin2bcd(unsigned val)
{
	return ((val / 10) << 4) + (val % 10);
}

void x86_ide_set()
{
	for (int i = 0; i < 4; i++) hdd_set(i, config.img_name[i + 2]);
}

void x86_init()
{
	user_io_status_set("[0]", 1);

	const char *home = HomeDir();

	if (is_x86())
	{
		load_bios(user_io_make_filepath(home, "boot0.rom"), 0);
		load_bios(user_io_make_filepath(home, "boot1.rom"), 1);
	}

	uint16_t cfg = ide_check();
	uint8_t hotswap[4] = {
		0,
		0,
		(uint8_t)(((cfg >> 8) & 1) ^ 1),
		(uint8_t)((cfg >> 9) & 1),
	};
	ide_reset(hotswap);

	fdd_set(0, config.img_name[0]);
	fdd_set(1, config.img_name[1]);
	for (int i = 0; i < 4; i++) hdd_set(i, config.img_name[i + 2]);

	//-------------------------------------------------------------------------- rtc

	unsigned char translate_mode = 1; //LBA
	translate_mode = (translate_mode << 6) | (translate_mode << 4) | (translate_mode << 2) | translate_mode;

	time_t t = time(NULL);
	struct tm tm = *localtime(&t);

	//rtc contents 0-127
	uint8_t cmos[128] =
	{
		bin2bcd(tm.tm_sec), //0x00: SEC BCD
		0x00, //0x01: ALARM SEC BCD
		bin2bcd(tm.tm_min), //0x02: MIN BCD
		0x00, //0x03: ALARM MIN BCD
		bin2bcd(tm.tm_hour), //0x04: HOUR BCD 24h
		0x12, //0x05: ALARM HOUR BCD 24h
		(uint8_t)(tm.tm_wday + 1), //0x06: DAY OF WEEK Sunday=1
		bin2bcd(tm.tm_mday), //0x07: DAY OF MONTH BCD from 1
		bin2bcd(tm.tm_mon + 1), //0x08: MONTH BCD from 1
		bin2bcd((tm.tm_year < 117) ? 17 : tm.tm_year - 100), //0x09: YEAR BCD
		0x26, //0x0A: REG A
		0x02, //0x0B: REG B
		0x00, //0x0C: REG C
		0x80, //0x0D: REG D
		0x00, //0x0E: REG E - POST status
		0x00, //0x0F: REG F - shutdown status

		(uint8_t)((get_fdd_bios_type(floppy_type[0])<<4) | get_fdd_bios_type(floppy_type[1])),
		0x00, //0x11: configuration bits; not used
		0x00, //0x12: hard disk types; 0-none, 1:E-type, F-type 16+ (unused)
		0x00, //0x13: advanced configuration bits; not used
		0x4D, //0x14: equipment bits
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

		(uint8_t)((fdd0_image.size && boot_from_floppy) ? 0x20 : 0x00), //0x2D: boot sequence

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

		(uint8_t)((fdd0_image.size && boot_from_floppy) ? 0x21 : 0x02), //0x3D: eltorito boot sequence

		0x00, //0x3E: ?
		0x00, //0x3F: ?

		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0
	};

	//count checksum
	unsigned short sum = 0;
	for (int i = 0x10; i <= 0x2D; i++) sum += cmos[i];

	cmos[0x2E] = sum >> 8;
	cmos[0x2F] = sum & 0xFF;
	for (unsigned int i = 0; i < sizeof(cmos) / sizeof(cmos[0]); i++) IOWR(RTC_BASE, i, cmos[i]);

	x86_share_reset();
	user_io_status_set("[0]", 0);
}

static void fdd_io(uint8_t read)
{
	fileTYPE *img = &fdd0_image;

	struct sd_param_t
	{
		uint32_t lba;
		uint32_t cnt;
	};

	static struct sd_param_t sd_params = {};
	static uint32_t secbuf[128 * 16];

	x86_dma_recvbuf(FDD0_BASE, sizeof(sd_params) >> 2, (uint32_t*)&sd_params);

	if (sd_params.lba >> 15)
	{
		// Floppy B:
		sd_params.lba &= 0x7FFF;
		img = &fdd1_image;
	}

	int res = 0;
	if (read)
	{
		//printf("Read: 0x%08x, %d, %d\n", basereg, sd_params.lba, sd_params.cnt);

		if (img->size)
		{
			if (img_read(img, sd_params.lba, &secbuf, 1))
			{
				x86_dma_sendbuf(FDD0_BASE + 255, 128, secbuf);
				res = 1;
			}
		}
		else
		{
			printf("Error: image is not ready.\n");
		}

		if (!res)
		{
			memset(secbuf, 0, 512);
			x86_dma_sendbuf(FDD0_BASE + 255, 128, secbuf);
		}
	}
	else
	{
		//printf("Write: 0x%08x, 0x%08x, %d\n", basereg, sd_params.lba, sd_params.cnt);

		x86_dma_recvbuf(FDD0_BASE + 255, sd_params.cnt * 128, secbuf);
		if (img->size)
		{
			if (sd_params.cnt > 0 && sd_params.cnt <= 16)
			{
				if (img->mode & O_RDWR)
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
				printf("Error: Block count %d is out of range 1..16.\n", sd_params.cnt);
			}
		}
		else
		{
			printf("Error: image is not ready.\n");
		}
	}
}

void x86_poll(int only_ide)
{
	if(!only_ide) x86_share_poll();

	uint16_t sd_req = ide_check();
	if (sd_req)
	{
		if (sd_req & 0x400) ide_cdda_send_sector();

		ide_io(0, sd_req & 7);
		sd_req >>= 3;
		ide_io(1, sd_req & 7);

		sd_req >>= 3;
		if (!only_ide && (sd_req & 3)) fdd_io(sd_req & 1);
	}
}

void x86_set_image(int num, char *filename)
{
	memset(config.img_name[num], 0, sizeof(config.img_name[0]));
	strcpy(config.img_name[num], filename);
	if (num < 2) fdd_set(num, filename);
	else if (ide_is_placeholder(num - 2)) hdd_set(num - 2, filename);
}

static char* get_config_name()
{
	static char str[256];
	snprintf(str, sizeof(str), "%ssys.cfg", user_io_get_core_name());
	return str;
}


void x86_config_save()
{
	config.ver = CFG_VER;
	FileSaveConfig(get_config_name(), &config, sizeof(config));
}

void x86_config_load()
{
	static x86_config tmp;
	memset(&config, 0, sizeof(config));
	if (FileLoadConfig(get_config_name(), &tmp, sizeof(tmp)) && (tmp.ver == CFG_VER))
	{
		memcpy(&config, &tmp, sizeof(config));
	}
}

void x86_set_fdd_boot(uint32_t boot)
{
	boot_from_floppy = (boot != 0);
}

const char* x86_get_image_name(int num)
{
	static char res[32];

	char *name = config.img_name[num];
	if (!name[0]) return NULL;

	char *p = strrchr(name, '/');
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

const char* x86_get_image_path(int num)
{
	return config.img_name[num];
}
