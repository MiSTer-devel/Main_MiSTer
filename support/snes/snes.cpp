
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "../../file_io.h"

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
	uint8_t *buf = (uint8_t*)malloc(f->size);
	if (buf)
	{
		FileSeekLBA(f, 0);
		if (FileReadAdv(f, buf, f->size))
		{
			uint32_t addr = find_header(buf, f->size);
			if (addr)
			{
				uint8_t romsz = buf[addr + RomSize];
				uint8_t ramsz = buf[addr + RamSize];
				if (romsz >= 0x10) romsz = 0;
				if (ramsz >= 0x08) ramsz = 0;
				hdr[0] = (ramsz << 4) | romsz;

				hdr[1] = (addr == 0x00ffc0) ? 1 : (addr == 0x40ffc0) ? 5 : 0; //LHRom type

				hdr[2] = buf[addr + RomType];

				if ((buf[addr + CartRegion] >= 0x02 && buf[addr + CartRegion] <= 0x0C) || buf[addr + CartRegion] == 0x11)
				{ //PAL Regions
					hdr[3] |= 1;
				}

				*(uint32_t*)(&hdr[4]) = addr;
			}
		}
		FileSeekLBA(f, 0);
		free(buf);
	}
	return hdr;
}
