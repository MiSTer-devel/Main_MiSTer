// Moves bytes around so that the fix graphics are stored in a way that
// takes advantage of the SDRAM 16-bit bus
// Part of Neogeo_MiSTer
// (C) 2019 Sean 'furrtek' Gonsalves

#include "graphics.h"

void spr_convert(uint8_t* buf_in, uint8_t* buf_out, unsigned int size)
{
	/*
	In C ROMs, a word provides two bitplanes for an 8-pixel wide line
	They're used in pairs to provide 32 bits at once (all four bitplanes)
	For one sprite tile, bytes are used like this: ([...] represents one 8-pixel wide line)
	Even ROM					Odd ROM
	[  40 41  ][  00 01  ]		[  42 43  ][  02 03  ]
	[  44 45  ][  04 05  ]  	[  46 47  ][  06 07  ]
	[  48 49  ][  08 09  ]  	[  4A 4B  ][  0A 0B  ]
	[  4C 4D  ][  0C 0D  ]  	[  4E 4F  ][  0E 0F  ]
	[  50 51  ][  10 11  ]  	[  52 53  ][  12 13  ]
	...							...
	The data read for a given tile line (16 pixels) is always the same, only the rendering order of the pixels can change
	To take advantage of the SDRAM burst read feature, the data can be loaded so that all 16 pixels of a tile
	line can be read sequentially: () are 16-bit words, [] is the 4-word burst read
	[(40 41) (00 01) (42 43) (02 03)]
	[(44 45) (04 05) (46 47) (06 07)]...
	Word interleaving is done on the FPGA side to mix the two C ROMs data (even/odd)

	In:  FEDCBA9876 54321 0
	Out: FEDCBA9876 15432 0
	*/
	for (unsigned int i = 0; i < size; i++)
		buf_out[i] = buf_in[(i & 0xFFC0) | (i & 1) | ((i >> 1) & 0x1E) | (((i & 2) ^ 2) << 4)];

	/*
	0 <- 20
	1 <- 21
	2 <- 00
	3 <- 01
	4 <- 22
	5 <- 23
	6 <- 02
	7 <- 03
	...

	00 -> 02
	01 -> 03
	02 -> 06
	03 -> 07
	...
	*/
}

void spr_convert_dbl(uint8_t* buf_in, uint8_t* buf_out, unsigned int size)
{
	/*
	In C ROMs, a word provides two bitplanes for an 8-pixel wide line
	They're used in pairs to provide 32 bits at once (all four bitplanes)
	For one sprite tile, bytes are used like this: ([...] represents one 8-pixel wide line)
	ROM
	[  42 43  ][  02 03  ][  40 41  ][  00 01  ]
	[  46 47  ][  06 07  ][  44 45  ][  04 05  ]
	[  4A 4B  ][  0A 0B  ][  48 49  ][  08 09  ]
	[  4E 4F  ][  0E 0F  ][  4C 4D  ][  0C 0D  ]
	[  52 53  ][  12 13  ][  50 51  ][  10 11  ]
	...					  ...
	The data read for a given tile line (16 pixels) is always the same, only the rendering order of the pixels can change
	To take advantage of the SDRAM burst read feature, the data can be loaded so that all 16 pixels of a tile
	line can be read sequentially: () are 16-bit words, [] is the 4-word burst read
	[(40 41) (00 01) (42 43) (02 03)]
	[(44 45) (04 05) (46 47) (06 07)]...
	This is consolidated version of C ROM with both parts in one.

	*/
	for (unsigned int i = 0; i < size; i++)
		buf_out[i] = buf_in[(i & 0xFF80) | ((i ^ 2) & 3) | ((i >> 1) & 0x3C) | (((i & 4) ^ 4) << 4)];
}

void fix_convert(uint8_t* buf_in, uint8_t* buf_out, unsigned int size)
{
	/*
	In S ROMs, a byte provides two pixels
	For one fix tile, bytes are used like this: ([...] represents a pair of pixels)
	[10][18][00][08]
	[11][19][01][09]
	[12][1A][02][0A]
	[13][1B][03][0B]
	[14][1C][04][0C]
	[15][1D][05][0D]
	[16][1E][06][0E]
	[17][1F][07][0F]
	The data read for a given tile line (8 pixels) is always the same
	To take advantage of the SDRAM burst read feature, the data can be loaded so that all 8 pixels of a tile
	line can be read sequentially: () are 16-bit words, [] is the 2-word burst read
	[(10 18) (00 08)]
	[(11 19) (01 09)]...

	In:  FEDCBA9876543210
	Out: FEDCBA9876510432
	*/
	for (unsigned int i = 0; i < size; i++)
		buf_out[i] = buf_in[(i & 0xFFE0) | ((i >> 2) & 7) | ((i & 1) << 3) | (((i & 2) << 3) ^ 0x10)];
}

