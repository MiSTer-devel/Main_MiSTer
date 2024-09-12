
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <limits.h>
#include <glob.h>

#include "../../file_io.h"
#include "../../user_io.h"
#include "../../spi.h"

static uint8_t hdr[512];

enum HeaderField {
	CartName = 0x00,
	Mapper = 0x15,
	RomType = 0x16,
	RomSize = 0x17,
	RamSize = 0x18,
	CartRegion = 0x19,
	Company = 0x1a,
	Version = 0x1b,
	Complement = 0x1c,  //inverse checksum
	Checksum = 0x1e,
	ResetVector = 0x3c,
};

static uint32_t score_header(const uint8_t *data, uint32_t size, uint32_t addr)
{
	if (size < addr + 64) return 0;  //image too small to contain header at this location?
	int score = 0;

	uint16_t resetvector = data[addr + ResetVector] | (data[addr + ResetVector + 1] << 8);
	uint16_t checksum = data[addr + Checksum] | (data[addr + Checksum + 1] << 8);
	uint16_t complement = data[addr + Complement] | (data[addr + Complement + 1] << 8);

	uint8_t resetop = data[(addr & ~0x7fff) | (resetvector & 0x7fff)];  //first opcode executed upon reset
	uint8_t mapper = data[addr + Mapper] & ~0x10;                      //mask off irrelevent FastROM-capable bit

																	   //$00:[0000-7fff] contains uninitialized RAM and MMIO.
																	   //reset vector must point to ROM at $00:[8000-ffff] to be considered valid.
	if (resetvector < 0x8000) return 0;

	//some images duplicate the header in multiple locations, and others have completely
	//invalid header information that cannot be relied upon.
	//below code will analyze the first opcode executed at the specified reset vector to
	//determine the probability that this is the correct header.

	//most likely opcodes
	if (resetop == 0x78  //sei
		|| resetop == 0x18  //clc (clc; xce)
		|| resetop == 0x38  //sec (sec; xce)
		|| resetop == 0x9c  //stz $nnnn (stz $4200)
		|| resetop == 0x4c  //jmp $nnnn
		|| resetop == 0x5c  //jml $nnnnnn
		) score += 8;

	//plausible opcodes
	if (resetop == 0xc2  //rep #$nn
		|| resetop == 0xe2  //sep #$nn
		|| resetop == 0xad  //lda $nnnn
		|| resetop == 0xae  //ldx $nnnn
		|| resetop == 0xac  //ldy $nnnn
		|| resetop == 0xaf  //lda $nnnnnn
		|| resetop == 0xa9  //lda #$nn
		|| resetop == 0xa2  //ldx #$nn
		|| resetop == 0xa0  //ldy #$nn
		|| resetop == 0x20  //jsr $nnnn
		|| resetop == 0x22  //jsl $nnnnnn
		) score += 4;

	//implausible opcodes
	if (resetop == 0x40  //rti
		|| resetop == 0x60  //rts
		|| resetop == 0x6b  //rtl
		|| resetop == 0xcd  //cmp $nnnn
		|| resetop == 0xec  //cpx $nnnn
		|| resetop == 0xcc  //cpy $nnnn
		) score -= 4;

	//least likely opcodes
	if (resetop == 0x00  //brk #$nn
		|| resetop == 0x02  //cop #$nn
		|| resetop == 0xdb  //stp
		|| resetop == 0x42  //wdm
		|| resetop == 0xff  //sbc $nnnnnn,x
		) score -= 8;

	//at times, both the header and reset vector's first opcode will match ...
	//fallback and rely on info validity in these cases to determine more likely header.

	//a valid checksum is the biggest indicator of a valid header.
	if ((checksum + complement) == 0xffff && (checksum != 0) && (complement != 0)) score += 4;

	if (addr == 0x007fc0 && mapper == 0x20) score += 2;  //0x20 is usually LoROM
	if (addr == 0x00ffc0 && mapper == 0x21) score += 2;  //0x21 is usually HiROM
	if (addr == 0x007fc0 && mapper == 0x22) score += 2;  //0x22 is usually SDD1
	if (addr == 0x40ffc0 && mapper == 0x25) score += 2;  //0x25 is usually ExHiROM

	if (data[addr + Company] == 0x33) score += 2;        //0x33 indicates extended header
	if (data[addr + RomType] < 0x08) score++;
	if (data[addr + RomSize] < 0x10) score++;
	if (data[addr + RamSize] < 0x08) score++;
	if (data[addr + CartRegion] < 14) score++;

	if (score < 0) score = 0;
	return score;
}

static uint32_t find_header(const uint8_t *data, uint32_t size)
{
	uint32_t score_lo = score_header(data, size, 0x007fc0);
	uint32_t score_hi = score_header(data, size, 0x00ffc0);
	uint32_t score_ex = score_header(data, size, 0x40ffc0);
	if (score_ex) score_ex += 4;  //favor ExHiROM on images > 32mbits

	if (score_lo >= score_hi && score_lo >= score_ex)
	{
		return score_lo ? 0x007fc0 : 0;
	}
	else if (score_hi >= score_ex)
	{
		return score_hi ? 0x00ffc0 : 0;
	}

	return score_ex ? 0x40ffc0 : 0;
}

uint8_t* snes_get_header(fileTYPE *f)
{
	memset(hdr, 0, sizeof(hdr));
	uint32_t size = f->size;
	uint8_t *prebuf = (uint8_t*)malloc(size);
	if (prebuf)
	{
		FileSeekLBA(f, 0);
		if (FileReadAdv(f, prebuf, size))
		{
			uint8_t *buf = prebuf;

			if (size & 512)
			{
				buf += 512;
				size -= 512;
			}

			*(uint32_t*)(&hdr[8]) = size;

			bool is_bsx_bios = false;
			if (!memcmp(buf+0x7FC0, "Satellaview BS-X     ", 21)) {
				is_bsx_bios = true;
			}

			uint32_t addr = find_header(buf, size);
			if (addr)
			{
				uint8_t ramsz = buf[addr + RamSize];
				if (ramsz >= 0x09) ramsz = 0;

				//re-calc rom size
				uint8_t romsz = 15;
				size--;
				if (!(size & 0xFF000000))
				{
					while (!(size & 0x1000000))
					{
						romsz--;
						size <<= 1;
					}
				}

				bool has_bsx_slot = false;
				if (buf[addr - 14] == 'Z' && buf[addr - 11] == 'J' &&
					((buf[addr - 13] >= 'A' && buf[addr - 13] <= 'Z') || (buf[addr - 13] >= '0' && buf[addr - 13] <= '9')) &&
					(buf[addr + Company] == 0x33 || (buf[addr - 10] == 0x00 && buf[addr - 4] == 0x00)) ) {
					has_bsx_slot = true;
				}

				//Rom type: 0-Low, 1-High, 2-ExHigh, 3-SpecialLoRom
				hdr[1] = (addr == 0x00ffc0) ? 1 :
						 (addr == 0x40ffc0) ? 2 :
						 has_bsx_slot ? 3 :
						 0;

				//BSX 3
				if (is_bsx_bios) {
					hdr[1] = 0x30;
				}
				else {

					//DSPn types 8..B
					if (buf[addr + Mapper] == 0x20 && buf[addr + RomType] == 0x03)
					{	//DSP1
						hdr[1] |= 0x84;
					}
					else if (buf[addr + Mapper] == 0x21 && buf[addr + RomType] == 0x03)
					{	//DSP1B
						hdr[1] |= 0x80;
					}
					else if (buf[addr + Mapper] == 0x30 && buf[addr + RomType] == 0x05 && buf[addr + Company] != 0xb2)
					{	//DSP1B
						hdr[1] |= 0x80;
					}
					else if (buf[addr + Mapper] == 0x31 && (buf[addr + RomType] == 0x03 || buf[addr + RomType] == 0x05))
					{	//DSP1B
						hdr[1] |= 0x80;
					}
					else if (buf[addr + Mapper] == 0x20 && buf[addr + RomType] == 0x05)
					{	//DSP2
						hdr[1] |= 0x90;
					}
					else if (buf[addr + Mapper] == 0x30 && buf[addr + RomType] == 0x05 && buf[addr + Company] == 0xb2)
					{	//DSP3
						hdr[1] |= 0xA0;
					}
					else if (buf[addr + Mapper] == 0x30 && buf[addr + RomType] == 0x03)
					{	//DSP4
						hdr[1] |= 0xB0;
					}
					else if (buf[addr + Mapper] == 0x30 && buf[addr + RomType] == 0xf6)
					{	//ST010
						hdr[1] |= 0x88;
						ramsz = 1;
						if (buf[addr + RomSize] < 10) hdr[1] |= 0x20; // ST011
					}
					else if (buf[addr + Mapper] == 0x30 && buf[addr + RomType] == 0x25)
					{	//OBC1
						hdr[1] |= 0xC0;
					}

					if (buf[addr + Mapper] == 0x3a && (buf[addr + RomType] == 0xf5 || buf[addr + RomType] == 0xf9)) {
						//SPC7110
						hdr[1] |= 0xD0;
						if (buf[addr + RomType] == 0xf9) hdr[1] |= 0x08; // with RTC
					}

					if (buf[addr + Mapper] == 0x35 && buf[addr + RomType] == 0x55)
					{
						//S-RTC (+ExHigh)
						hdr[1] |= 0x08;
					}

					//CX4 4
					if (buf[addr + Mapper] == 0x20 && buf[addr + RomType] == 0xf3)
					{
						hdr[1] |= 0x40;
					}

					//SDD1 5
					if (buf[addr + Mapper] == 0x32 && (buf[addr + RomType] == 0x43 || buf[addr + RomType] == 0x45))
					{
						if (romsz < 14) hdr[1] |= 0x50; // except Star Ocean un-SDD1
					}

					//SA1 6
					if (buf[addr + Mapper] == 0x23 && (buf[addr + RomType] == 0x32 || buf[addr + RomType] == 0x34 || buf[addr + RomType] == 0x35))
					{
						hdr[1] |= 0x60;
					}

					//GSU 7
					if (buf[addr + Mapper] == 0x20 && (buf[addr + RomType] == 0x13 || buf[addr + RomType] == 0x14 || buf[addr + RomType] == 0x15 || buf[addr + RomType] == 0x1a))
					{
						ramsz = buf[addr - 3];
						if (ramsz == 0xFF) ramsz = 5; //StarFox
						if (ramsz > 6) ramsz = 6;
						hdr[1] |= 0x70;
					}

					//1..2,E..F - reserved for other mappers.
				}

				hdr[2] = 0;

				//PAL Regions
				if ((buf[addr + CartRegion] >= 0x02 && buf[addr + CartRegion] <= 0x0C) || buf[addr + CartRegion] == 0x11)
				{
					hdr[3] |= 1;
				}

				hdr[0] = (ramsz << 4) | romsz;
				printf("Size from header: 0x%X, calculated size: 0x%X\n", buf[addr + RomSize], romsz);
			}
			*(uint32_t*)(&hdr[4]) = addr;
		}
		FileSeekLBA(f, 0);
		free(prebuf);
	}
	return hdr;
}

void snes_patch_bs_header(fileTYPE *f, uint8_t *buf)
{
	if ((f->offset == 0x008000 && (buf[0xFD8] == 0x20 || buf[0xFD8] == 0x30)) ||
		(f->offset == 0x010000 && (buf[0xFD8] == 0x21 || buf[0xFD8] == 0x31)))
	{
		if (buf[0xFD0] == 0xF0 || (buf[0xFD1] == 0xFF && buf[0xFD2] == 0xFF && buf[0xFD3] == 0xFF))
		{
			printf("SNES: Patch bad BS header: offset %04X, bad value %02X %02X %02X %02X\n", 0x7FD0 | (f->offset == 0x008000 ? 0x0000 : 0x8000), buf[0xFD0], buf[0xFD1], buf[0xFD2], buf[0xFD3]);
			buf[0xFD3] = 0x00;
			buf[0xFD2] = 0x00;
			buf[0xFD1] = 0x00;
			buf[0xFD0] = f->size <= 256 * 1024 ? 0x03 :
						 f->size <= 512 * 1024 ? 0x0F :
						 0xFF;
		}

		if (buf[0xFD5] >= 0x80)
		{
			printf("SNES: Patch bad BS header: offset %04X, bad value %02X %02X\n", 0x7FD4 | (f->offset == 0x008000 ? 0x0000 : 0x8000), buf[0xFD4], buf[0xFD5]);
			buf[0xFD5] = 0xFF;
			buf[0xFD4] = 0xFF;
		}

		if (buf[0xFDA] != 0x33)
		{
			printf("SNES: Patch bad BS header: offset %04X, bad value %02X\n", 0x7FDA | (f->offset == 0x008000 ? 0x0000 : 0x8000), buf[0xFDA]);
			buf[0xFDA] = 0x33;
		}
	}
}

////////////// MSU /////////////

#define MSU_CD_SET               1
#define MSU_AUDIO_TRACK_MOUNTED  2
#define MSU_DATA_BASE            3

static char snes_romFileName[1024] = {};
static char SelectedPath[1024] = {};
static uint8_t buf[1024];
static char has_cd = 0;
static fileTYPE f_audio = {};

static void msu_send_command(uint64_t cmd)
{
	spi_uio_cmd_cont(UIO_CD_SET);
	spi_w((cmd >> 00) & 0xFFFF);
	spi_w((cmd >> 16) & 0xFFFF);
	spi_w((cmd >> 32) & 0xFFFF);
	DisableIO();
}

static int msu_send_data(fileTYPE *f, int idx)
{
	int chunk = sizeof(buf);

	memset(buf, 0, chunk);
	if (f->size) FileReadAdv(f, buf, chunk);

	user_io_set_index(idx);
	user_io_set_download(1);
	user_io_file_tx_data(buf, chunk);
	user_io_set_download(0);

	return 1;
}

void snes_msu_init(const char* name)
{
	static fileTYPE f = {};
	FileClose(&f_audio);

	memset(snes_romFileName, 0, 1024);
	int extSize = strlen(strrchr(name, '.'));
	strncpy(snes_romFileName, name, strlen(name) - extSize);
	printf("MSU: Rom named '%s' initialised\n", name);

	snprintf(SelectedPath, sizeof(SelectedPath), "%s.msu", snes_romFileName);
	has_cd = FileOpen(&f, SelectedPath) ? 1 : 0;
	uint32_t size = f.size;
	FileClose(&f);

	printf("MSU: enable cd: %d\n", has_cd);

	if (size && size < 0x1F200000)
	{
		msu_send_command((0x20600000ULL << 16) | MSU_DATA_BASE);
		user_io_file_tx(SelectedPath, 3, 0, 0, 0, 0x20600000);
	}

	msu_send_command((has_cd << 15) | MSU_CD_SET);
}

void snes_poll(void)
{
	static uint8_t last_req = 255;
	if (!has_cd) return;

	// Detect incoming command via CD_GET (which we are repurposing for MSU1)
	uint8_t req = spi_uio_cmd_cont(UIO_CD_GET);
	if (req != last_req)
	{
		last_req = req;

		uint16_t command = spi_w(0);
		uint32_t data = spi_w(0);
		data = (spi_w(0) << 16) | data;
		DisableIO();

		switch(command)
		{
		case 0xFF:
			printf("MSU: request to reset\n");
			break;

		case 0x35:
			snprintf(SelectedPath, sizeof(SelectedPath), "%s-%d.pcm", snes_romFileName, data);
			printf("MSU: New track selected: %s\n", SelectedPath);
			FileOpen(&f_audio, SelectedPath);
			printf(f_audio.size ? "MSU: Track mounted\n" : "MSU: Track not found!\n");
			msu_send_command((f_audio.size << 16) | MSU_AUDIO_TRACK_MOUNTED);
			break;

		case 0x36:
			printf("MSU: Jump to offset: 0x%X\n", data * 1024);
			FileSeek(&f_audio, data * 1024, SEEK_SET);
			// fallthrough

		case 0x34:
			// Next sector requested
			msu_send_data(&f_audio, 2);
			break;
		}
	}
	else
	{
		DisableIO();
	}
}
