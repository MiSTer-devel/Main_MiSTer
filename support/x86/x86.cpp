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
#include "x86_ide.h"
#include "x86_cdrom.h"

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

#define CFG_VER         3

#define SHMEM_ADDR      0x30000000
#define BIOS_SIZE       0x10000

static int newcore = 0;
static int v3 = 0;

#define IOWR(base, reg, value) x86_dma_set((base) + (newcore ? (reg) : ((reg)<<2)), value)

typedef struct
{
	uint32_t ver;
	char img_name[6][1024];
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

void x86_dma_set(uint32_t address, uint32_t data)
{
	EnableIO();
	spi8(UIO_DMA_WRITE);
	spi32_w(address);
	if (v3) spi_w((uint16_t)data); else spi32_w(data);
	DisableIO();
}

void x86_dma_sendbuf(uint32_t address, uint32_t length, uint32_t *data)
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
			while (length--) if (v3) spi_w(*buf++); else spi32_w(*buf++);
		}
	}
	else while (length--) spi32_w(*data++);
	DisableIO();
}

void x86_dma_recvbuf(uint32_t address, uint32_t length, uint32_t *data)
{
	EnableIO();
	fpga_spi_fast(UIO_DMA_READ);
	fpga_spi_fast(address);
	fpga_spi_fast(0);
	if (newcore)
	{
		if (address < FDD0_BASE_NEW || (!v3 && address == FDD0_BASE_NEW)) fpga_spi_fast_block_read((uint16_t*)data, length * 2);
		else if (v3 && address == FDD0_BASE_NEW)
		{
			while (length--) *data++ = spi_w(0);
		}
		else
		{
			uint8_t *buf = (uint8_t*)data;
			length *= 4;
			while (length--) *buf++ = v3 ? spi_w(0) : spi32_w(0);
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

static int img_mount(fileTYPE *f, const char *name, int rw)
{
	FileClose(f);
	int writable = 0, ret = 0;

	int len = strlen(name);
	if (len)
	{
		const char *ext = name+len-4;
		if (!strncasecmp(".chd", ext, 4))
		{
			ret = 1;
		} else {
			writable = rw && FileCanWrite(name);
			ret = FileOpenEx(f, name, writable ? (O_RDWR | O_SYNC) : O_RDONLY);
			if (!ret) printf("Failed to open file %s\n", name);
		}
	}

	if (!ret)
	{
		f->size = 0;
		return 0;
	}

	printf("Mount %s as %s\n", name, writable ? "read-write" : "read-only");
	return 1;
}

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

static void fdd_set(int num, char* filename)
{
	if (!newcore && num) return;

	floppy_type[num] = FDD_TYPE_1440;

	uint32_t base = newcore ? FDD0_BASE_NEW : FLOPPY0_BASE_OLD;
	fileTYPE *fdd_image = num ? &fdd1_image : &fdd0_image;

	int floppy = img_mount(fdd_image, filename, 1);
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
	floppy_wait_cycles       = 200000000 / floppy_spt;

	printf("floppy:\n");
	printf("  cylinders:     %d\n", floppy_cylinders);
	printf("  heads:         %d\n", floppy_heads);
	printf("  spt:           %d\n", floppy_spt);
	printf("  total_sectors: %d\n\n", floppy_total_sectors);

	uint32_t subaddr = num << 7;
	IOWR(base + subaddr, 0x0, floppy ? 1 : 0);
	IOWR(base + subaddr, 0x1, (floppy && (fdd_image->mode & O_RDWR)) ? 0 : 1);
	IOWR(base + subaddr, 0x2, floppy_cylinders);
	IOWR(base + subaddr, 0x3, floppy_spt);
	IOWR(base + subaddr, 0x4, floppy_total_sectors);
	IOWR(base + subaddr, 0x5, floppy_heads);
	IOWR(base + subaddr, 0x6, 0); // base LBA
	IOWR(base + subaddr, 0xC, 0);

	if(!newcore) set_clock();
}

static void hdd_set(int num, char* filename)
{
	if (!v3 && num > 1) return;
	uint32_t base = newcore ? ((num & (v3 ? 2 : 1)) ? HDD1_BASE_NEW : HDD0_BASE_NEW) : (num ? HDD1_BASE_OLD : HDD0_BASE_OLD);

	int present = 0;
	int cd = 0;

	int len = strlen(filename);
	int vhd = (len > 4 && !strcasecmp(filename + len - 4, ".vhd"));

	if (num > 1 && !vhd)
	{
		const char *img_name = cdrom_parse(num, filename);
		if (img_name) present = img_mount(&ide_image[num], img_name, 0);
		if (present) cd = 1;
	}

	if(!present && vhd) present = img_mount(&ide_image[num], filename, 1);
	x86_ide_set(num, base, present ? &ide_image[num] : 0, v3 ? 3 : newcore ? 2 : 0, cd);
}

static uint8_t bin2bcd(unsigned val)
{
	return ((val / 10) << 4) + (val % 10);
}

static void check_ver()
{
	uint16_t flg = dma_sdio(0);
	newcore = ((flg & 0xC000) == 0xC000);
	v3 = ((flg & 0xF000) == 0xE000);
}

void x86_init()
{
	user_io_8bit_set_status(UIO_STATUS_RESET, UIO_STATUS_RESET);

	const char *home = HomeDir();

	load_bios(user_io_make_filepath(home, "boot0.rom"), 0);
	load_bios(user_io_make_filepath(home, "boot1.rom"), 1);

	check_ver();

	if (!newcore)
	{
		IOWR(PC_BUS_BASE_OLD, 0, 0x00FFF0EA);
		IOWR(PC_BUS_BASE_OLD, 1, 0x000000F0);
	}

	x86_ide_reset(((dma_sdio(0)>>8) & 3) ^ 1);
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

		(uint8_t)((get_fdd_bios_type(floppy_type[0])<<4) | (newcore ? get_fdd_bios_type(floppy_type[1]) : 0)),
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
	for (unsigned int i = 0; i < sizeof(cmos) / sizeof(cmos[0]); i++) IOWR(newcore ? RTC_BASE_NEW : RTC_BASE_OLD, i, cmos[i]);

	x86_share_reset();
	user_io_8bit_set_status(0, UIO_STATUS_RESET);
}

static void img_io(fileTYPE *img, uint32_t basereg, uint8_t read, int sz)
{
	struct sd_param_t
	{
		uint32_t lba;
		uint32_t cnt;
	};

	static struct sd_param_t sd_params = {};
	static uint32_t secbuf[128 * 16];

	x86_dma_recvbuf(basereg, sizeof(sd_params) >> 2, (uint32_t*)&sd_params);

	if (sz == 1 && (sd_params.lba >> 15))
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
			if (img_read(img, sd_params.lba, &secbuf, sz))
			{
				x86_dma_sendbuf(basereg + 255, sz * 128, secbuf);
				res = 1;
			}
		}
		else
		{
			printf("Error: image is not ready.\n");
		}

		if (!res)
		{
			memset(secbuf, 0, sz * 512);
			x86_dma_sendbuf(basereg + 255, sz * 128, secbuf);
		}
	}
	else
	{
		//printf("Write: 0x%08x, 0x%08x, %d\n", basereg, sd_params.lba, sd_params.cnt);

		x86_dma_recvbuf(basereg + 255, sd_params.cnt * 128, secbuf);
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
	x86_dma_recvbuf(SD_BASE_OLD + (4 << 2), sizeof(sd_params) >> 2, (uint32_t*)&sd_params);

	fileTYPE *img;
	switch (sd_params.addr)
	{
		case IMG_TYPE_HDD0_OLD:
			//printf("HDD0 req\n");
			img = &ide_image[0];
			break;
		case IMG_TYPE_HDD1_OLD:
			//printf("HDD1 req\n");
			img = &ide_image[1];
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

		if (img->size)
		{
			if (sd_params.bl_cnt > 0 && sd_params.bl_cnt <= 4)
			{
				if (img_read(img, sd_params.lba, secbuf, sd_params.bl_cnt))
				{
					x86_dma_sendbuf(sd_params.addr, sd_params.bl_cnt * 128, secbuf);
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

		if (img->size)
		{
			if (sd_params.bl_cnt > 0 && sd_params.bl_cnt <= 4)
			{
				if (img->mode & O_RDWR)
				{
					x86_dma_recvbuf(sd_params.addr, sd_params.bl_cnt * 128, secbuf);
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
			if (v3) x86_ide_io(0, sd_req & 7);
			else if (sd_req & 3) img_io(&ide_image[0], HDD0_BASE_NEW, sd_req & 1, 16);

			sd_req >>= 3;
			if (v3) x86_ide_io(1, sd_req & 7);
			else if (sd_req & 3) img_io(&ide_image[1], HDD1_BASE_NEW, sd_req & 1, 16);

			sd_req >>= 3;
			if (sd_req & 3) img_io(&fdd0_image, FDD0_BASE_NEW, sd_req & 1, 1);
		}
		else
		{
			img_io_old((uint8_t)sd_req);
		}
	}
}

void x86_set_image(int num, char *filename)
{
	memset(config.img_name[num], 0, sizeof(config.img_name[0]));
	strcpy(config.img_name[num], filename);
	if (num < 2) fdd_set(num, filename);
	else if (v3 && x86_ide_is_placeholder(num - 2)) hdd_set(num - 2, filename);
}

void x86_config_save()
{
	config.ver = CFG_VER;
	FileSaveConfig("ao486sys.cfg", &config, sizeof(config));
}

void x86_config_load()
{
	check_ver();

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
