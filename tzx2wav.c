/////////////////////////////////////////////////////////////////////
// TZX to VAV Converter v0.2 for Bloodshed Dev-C++ compiler        //
// (C) 2005-2006 Francisco Javier Crespo <tzx2wav@ya.com>          //
//                                                                 //
// MiSTer adaptation                                               //
// (C) 2017 Francisco Javier Crespo <tzx2wav@ya.com>               //
//                                                                 //
// Originally based on source code from these works:               //
// PLAYTZX v0.60b for Watcom C compiler (C) 1997-2004 Tomaz Kac    //
// PLAYTZX Unix v0.12b (C) 2003 Tero Turtiainen / Fredrick Meunier //
/////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "tzx2wav.h"
#include "spi.h"

// Computer entries

const char hwid_01_01[] = "ZX Spectrum 16k";
const char hwid_01_02[] = "ZX Spectrum 48k, Plus";
const char hwid_01_03[] = "ZX Spectrum 48k Issue 1";
const char hwid_01_04[] = "ZX Spectrum 128k (Sinclair)";
const char hwid_01_05[] = "ZX Spectrum 128k +2 (Grey case)";
const char hwid_01_06[] = "ZX Spectrum 128k +2A, +3";
const char hwid_01_07[] = "Timex Sinclair TC-2048";
const char hwid_01_08[] = "Timex Sinclair TS-2068";
const char hwid_01_09[] = "Pentagon 128";
const char hwid_01_10[] = "Sam Coupe";
const char hwid_01_11[] = "Didaktik M";
const char hwid_01_12[] = "Didaktik Gama";
const char hwid_01_13[] = "ZX-81 with 1k RAM";
const char hwid_01_14[] = "ZX-81 with 16k RAM or more";
const char hwid_01_15[] = "ZX Spectrum 128k, Spanish version";
const char hwid_01_16[] = "ZX Spectrum, Arabic version";
const char hwid_01_17[] = "TK 90-X";
const char hwid_01_18[] = "TK 95";
const char hwid_01_19[] = "Byte";
const char hwid_01_20[] = "Elwro";
const char hwid_01_21[] = "ZS Scorpion";
const char hwid_01_22[] = "Amstrad CPC 464";
const char hwid_01_23[] = "Amstrad CPC 664";
const char hwid_01_24[] = "Amstrad CPC 6128";
const char hwid_01_25[] = "Amstrad CPC 464+";
const char hwid_01_26[] = "Amstrad CPC 6128+";
const char hwid_01_27[] = "Jupiter ACE";
const char hwid_01_28[] = "Enterprise";
const char hwid_01_29[] = "Commodore 64";
const char hwid_01_30[] = "Commodore 128";

const char *hwids_01[30] =
{ hwid_01_01, hwid_01_02, hwid_01_03, hwid_01_04, hwid_01_05, hwid_01_06,
hwid_01_07, hwid_01_08, hwid_01_09, hwid_01_10, hwid_01_11, hwid_01_12,
hwid_01_13, hwid_01_14, hwid_01_15, hwid_01_16, hwid_01_17, hwid_01_18,
hwid_01_19, hwid_01_20, hwid_01_21, hwid_01_22, hwid_01_23, hwid_01_24,
hwid_01_25, hwid_01_26, hwid_01_27, hwid_01_28, hwid_01_29, hwid_01_30 };


const char *build= "20060225";

#define MAJREV 1        // Major revision of the format this program supports
#define MINREV 13       // Minor revision of the format this program supports

// C64 Loader defines ...

#define ROM_S_HALF  616     // ROM Loader SHORT  Half Wave
#define ROM_M_HALF  896     // ROM Loader MEDIUM Half Wave
#define ROM_L_HALF 1176     // ROM Loader LONG   Half Wave

#define STT_0_HALF  426     // Standard Turbo Tape BIT 0 Half Wave
#define STT_1_HALF  596     // Standard Turbo Tape BIT 1 Half Wave

// Other defines ...

#define SGNLOW    0
#define SGNHIGH   1

unsigned int sgn;  // Sign of the wave being played
unsigned int freq = 44100;              // Default Sample Frequency

int prvi;
int n,m;
int num;
unsigned char *d;
unsigned char *mem = 0; // File in Memory
int pos;                // Position in File
int curr;               // Current block that is playing
int numblocks;          // Total Num. of blocks
unsigned long oflen;    // Length of output file
int block[2048];        // Array of Block starts
double cycle;           // Frequency / 3500000 (Z80 clock)

int cpc=0;              // Amstrad CPC tape ?
int sam=0;              // SAM Coupe tape ?

int id;                 // Current Block ID
int pilot;              // Len of Pilot signal (in hp's)
int sb_pilot;           // Pilot pulse
int sb_sync1;           // Sync first half-period (hp)
int sb_sync2;           // Sync second
int sb_bit0;            // Bit-0
int sb_bit1;            // Bit-1
int sb_pulse;           // Pulse in Sequence of pulses and direct recording block
int lastbyte;           // How many bits are in last byte of data ?
int pause_ms;              // Pause after current block (in milliseconds)
int skippause=0;        // Overrides pause value in last TZX block

int singlepulse;        // Flag to activate single pulse waves
int manchester;         // Flag to activate manchester encoded waves

unsigned char *data;    // Data to be played
int datalen;            // Len of ^^^
int datapos;            // Position in ^^^
int bitcount;           // How many bits to play in current byte ?
int sb_bit;             // should we play bit 0 or 1 ?
char databyte;          // Current Byte to be replayed of the data
signed short jump;      // Relative Jump 
int not_rec;            // Some blocks were not recognised ??
int files=0;            // Number of Files on the command line
int starting=1;         // starting block
int ending=0;           // ending block

int pages=0;            // Waiting after each page of the info ?
int expand=0;           // Expand Groups ?
int draw=1;             // Local flag for outputing a line when in a group
int mode128=0;          // Are we working in 128k mode ? (for Stop in 48k block)

int nfreq=0;            // Did we choose new frequency with /freq switch ?
char k;
int speed;
int x,last,lastlen;

int loop_start=0;       // Position of the last Loop Start block
int loop_count=0;       // Counter of the Loop
int call_pos=0;         // Position of the last Call Sequence block
int call_num=0;         // Number of Calls in the last Call Sequence block
int call_cur=0;         // Current Call to be made
int num_sel;            // Number of Selections in the Select block
int jumparray[256];     // Array of all possible jumps in Select block

int sb_bit0_f, sb_bit0_s, sb_bit1_f, sb_bit1_s, xortype, sb_finishbyte_f,
    sb_finishbyte_s, sb_finishdata_f, sb_finishdata_s, num_lead_in, xorvalue;
int trailing, sb_trailing;
char lead_in_byte;
int endian;
char add_bit;

int inv = 0;

char tstr[255];
char tstr2[255];
char tstr3[255];
char tstr4[255];
char spdstr[255];
char pstr[255];

int numt, nump, t2;

static void core_write(void *buf, int size)
{
	char *addr = (char*)buf;
	while (size--)
	{
		spi8(*addr++);
		oflen++;
	}
}

/////////////////////////////////////////////////
// Garbage collector and error handling routines
/////////////////////////////////////////////////

void GarbageCollector(void)
{
	if (mem != NULL)
	{
		free(mem);
		mem = 0;
	}
}

static void Error(char *errstr)
{
	GarbageCollector();
	printf("\n-- Error: %s\n", errstr);
}

///////////////////////////////
// CSW v1.01 handling routines
///////////////////////////////

void CSW1_Init(void)
{
	// Official CSW format documentation at:
	// http://www.ramsoft.bbk.org/csw.html

	unsigned short Revision = 0x0101;
	unsigned char CompType = 1;
	unsigned int Reserved = 0;

	core_write("Compressed Square Wave\032", 23);
	core_write(&Revision, 2);   // Major & Minor revision
	core_write(&freq, 2);       // Sample Rate
	core_write(&CompType, 1);   // Compression Type
	core_write(&inv, 1);        // Polarity
	core_write(&Reserved, 3);   // Reserved bytes
}

void CSW1_Write(unsigned int samples)
{
	if (samples < 256)
	{
		core_write(&samples, 1);
	}
	else
	{
		int zero = 0;
		core_write(&zero, 1);
		core_write(&samples, 4);
	}
}

//////////////////////////////////
// Generic wave handling routines
//////////////////////////////////

unsigned int Samples(unsigned int n)
{
	// Convert a sampling value in Z80 T-States to samples for wave output 
	return ((unsigned int)(0.5 + (cycle*(double)n)));
}

void PlayWave(unsigned int len)
{
	CSW1_Write(len);
}

void PauseWave(unsigned int pause_ms)
{
	// Waits for "pause" milliseconds

	int p;
	if ((!skippause) || (curr != (numblocks - 1)))
	{
		p = (unsigned int)((((float)pause_ms)*freq) / 1000.0);
		PlayWave(p);
	}
}

/////////////////////////////
// TZX Commodore 64 routines
/////////////////////////////

void PlayC64(unsigned int len)
{
	PlayWave(len);
	PlayWave(len);
}

void PlayC64ROMByte(char byte, int finish)
{
	xorvalue = xortype;
	while (bitcount)
	{
		if (!endian) sb_bit = byte & 0x01;
		else        sb_bit = byte & 0x80;
		if (sb_bit)
		{
			if (sb_bit1_f) PlayC64(sb_bit1_f);
			if (sb_bit1_s) PlayC64(sb_bit1_s);
			xorvalue ^= sb_bit;
		}
		else
		{
			if (sb_bit0_f) PlayC64(sb_bit0_f);
			if (sb_bit0_s) PlayC64(sb_bit0_s);
			xorvalue ^= sb_bit;
		}
		if (!endian) byte >>= 1;
		else        byte <<= 1;
		bitcount--;
	}
	if (xortype != 0xFF)
	{
		if (xorvalue)
		{
			if (sb_bit1_f) PlayC64(sb_bit1_f);
			if (sb_bit1_s) PlayC64(sb_bit1_s);
		}
		else
		{
			if (sb_bit0_f) PlayC64(sb_bit0_f);
			if (sb_bit0_s) PlayC64(sb_bit0_s);
		}
	}
	if (!finish)
	{
		if (sb_finishbyte_f) PlayC64(sb_finishbyte_f);
		if (sb_finishbyte_s) PlayC64(sb_finishbyte_s);
	}
	else
	{
		if (sb_finishdata_f) PlayC64(sb_finishdata_f);
		if (sb_finishdata_s) PlayC64(sb_finishdata_s);
	}
}

void PlayC64TurboByte(char byte)
{
	int add_num;

	add_num = add_bit & 3;

	if (add_num && !(add_bit & 4))
	{
		while (add_num)
		{
			if (add_bit & 8) PlayC64(sb_bit1);
			else PlayC64(sb_bit0);
			add_num--;
		}
	}

	while (bitcount)
	{
		if (!endian) sb_bit = byte & 0x01;
		else        sb_bit = byte & 0x80;
		if (sb_bit)  PlayC64(sb_bit1);
		else        PlayC64(sb_bit0);
		if (!endian) byte >>= 1;
		else        byte <<= 1;
		bitcount--;
	}

	if (add_num && (add_bit & 4))
	{
		while (add_num)
		{
			if (add_bit & 8) PlayC64(sb_bit1);
			else           PlayC64(sb_bit0);
			add_num--;
		}
	}
}

////////////////////////////////
// Game identification routines
////////////////////////////////

void GetC64ROMName(char *name, unsigned char *data)
{
	char d;

	for (n = 0; n < 16; n++)
	{
		d = data[14 + n];
		if (d < 32 || d>125)
			name[n] = ' ';
		else
			name[n] = d;
	}
	name[n] = 0;
}

void GetC64StandardTurboTapeName(char *name, unsigned char *data)
{
	char d;

	for (n = 0; n < 16; n++)
	{
		d = data[15 + n];
		if (d < 32 || d>125)
			name[n] = ' ';
		else
			name[n] = d;
	}
	name[n] = 0;
}

void IdentifyC64ROM(int pos, unsigned char *data, int type)
{
	char name[255];

	// Determine Loader type
	if ((sb_pilot == ROM_S_HALF) && (sb_sync1 == ROM_L_HALF) && (sb_sync2 == ROM_M_HALF) &&
		(sb_bit0_f == ROM_S_HALF) && (sb_bit0_s == ROM_M_HALF) && (sb_bit1_f == ROM_M_HALF) &&
		(sb_bit1_s == ROM_S_HALF) && (xortype == 0x01))
	{
		// ROM Loader
		if ((data[0] == 0x89) && (data[1] == 0x88) && (data[2] == 0x87) && (data[3] == 0x86) &&
			(data[4] == 0x85) && (data[5] == 0x84) && (data[6] == 0x83) && (data[7] == 0x82) &&
			(data[8] == 0x81))
		{
			if (pos == 202)
			{
				if (!type)
				{
					strcpy(name, "Header: ");
					GetC64ROMName(name + 8, data);
				}
				else
				{
					strcpy(name, "ROM Header: ");
					GetC64ROMName(name + 12, data);
				}
			}
			else
			{
				if (!type)
				{
					strcpy(name, "Data Block              ");
				}
				else
				{
					strcpy(name, "ROM: Data Block");
				}
			}
		}
		else
		{
			if (!type) strcpy(name, "------------------------");
			else       strcpy(name, "ROM: Last Block Repeated");
		}
		strcpy(tstr, name);
		strcpy(spdstr, "C64 ROM Data ");
		return;
	}

	if (!type) strcpy(tstr, "------------------------");
	else       strcpy(tstr, "Unknown");
	strcpy(spdstr, "C64 Data     ");
}

void IdentifyC64Turbo(int pos, unsigned char *data, int type)
{
	char name[255];

	// Determine Loader type
	if (sb_bit0 == STT_0_HALF && sb_bit1 == STT_1_HALF && lead_in_byte == 0x02)
	{
		// Standard Turbo Tape Loader
		if (data[0] == 0x09 && data[1] == 0x08 && data[2] == 0x07 && data[3] == 0x06 &&
			data[4] == 0x05 && data[5] == 0x04 && data[6] == 0x03 && data[7] == 0x02 &&
			data[8] == 0x01)
		{
			if (pos == 32 && data[9] != 0x00)
			{
				if (!type)
				{
					strcpy(name, "Header: ");
					GetC64StandardTurboTapeName(name + 8, data);
				}
				else
				{
					strcpy(name, "TurboTape Header: ");
					GetC64StandardTurboTapeName(name + 18, data);
				}
			}
			else
			{
				if (!type) strcpy(name, "------------------------");
				else       strcpy(name, "TurboTape Data Block");
			}
		}
		else
		{
			if (!type) strcpy(name, "------------------------");
			else       strcpy(name, "TurboTape Unknown");
		}
		strcpy(tstr, name);
		strcpy(spdstr, "C64 Turbo    ");
		return;
	}
	if (!type) strcpy(tstr, "------------------------");
	else       strcpy(tstr, "Unknown");
	strcpy(spdstr, "C64 Data     ");
}

void Identify(int len, unsigned char *temp, int type)
{
	int n;
	int s;

	if (cpc)
	{
		if (temp[0] == 44)
		{
			if (!type) s = 4;
			else s = 0;
			strcpy(tstr, "    ");
			for (n = 0; n < 16; n++)
			{
				if (temp[n + 1]) tstr[n + s] = temp[n + 1];
				else tstr[n + s] = ' ';
			}
			for (n = 0; n < 4; n++) tstr[n + s + 16] = ' ';
			tstr[n + s + 16] = 0;
		}
		else
		{
			if (!type)
				strcpy(tstr, "    ------------------  ");
			else
				strcpy(tstr, "Headerless");
		}
		return;
	}

	if (sam)
	{
		if (temp[0] == 1 && (len>80 && len < 84) && (temp[1] >= 0x10 && temp[1] <= 0x13))
		{
			if (!type)
			{
				s = 14;
				switch (temp[1])
				{
				case 0x10: strcpy(tstr, "    Program : "); break;
				case 0x11: strcpy(tstr, " Num. Array : "); break;
				case 0x12: strcpy(tstr, "Char. Array : "); break;
				case 0x13: strcpy(tstr, "      Bytes : "); break;
				}
			}
			else
			{
				switch (temp[1])
				{
				case 0x10: strcpy(tstr, "Program : "); s = 10; break;
				case 0x11: strcpy(tstr, "Num. Array : "); s = 13; break;
				case 0x12: strcpy(tstr, "Char. Array : "); s = 14; break;
				case 0x13: strcpy(tstr, "Bytes : "); s = 8; break;
				}
			}
			for (n = 0; n < 10; n++)
			{
				if (temp[n + 2]>31 && temp[n + 2] < 127)
					tstr[n + s] = temp[n + 2];
				else
					tstr[n + s] = 32;
			}
			tstr[n + s] = 0;
		}
		else
		{
			if (!type)
				strcpy(tstr, "    --------------------");  // Not Header
			else
				strcpy(tstr, "Headerless");
		}
		return;
	}

	if (temp[0] == 0 && (len == 19 || len == 20) && temp[1] < 4)
	{
		if (!type)
		{
			s = 14;
			switch (temp[1])
			{
			case 0x00: strcpy(tstr, "    Program : "); break;
			case 0x01: strcpy(tstr, " Num. Array : "); break;
			case 0x02: strcpy(tstr, "Char. Array : "); break;
			case 0x03: strcpy(tstr, "      Bytes : "); break;
			}
		}
		else
		{
			switch (temp[1])
			{
			case 0x00: strcpy(tstr, "Program : "); s = 10; break;
			case 0x01: strcpy(tstr, "Num. Array : "); s = 13; break;
			case 0x02: strcpy(tstr, "Char. Array : "); s = 14; break;
			case 0x03: strcpy(tstr, "Bytes : "); s = 8; break;
			}
		}
		for (n = 0; n < 10; n++)
		{
			if (temp[n + 2]>31 && temp[n + 2] < 127)
				tstr[n + s] = temp[n + 2];
			else
				tstr[n + s] = 32;
		}
		tstr[n + s] = 0;
	}
	else
	{
		if (!type)
			strcpy(tstr, "    --------------------");  // Not Header
		else
			strcpy(tstr, "Headerless");
	}
}

//////////////////////////////////////////////////////////
// Conversion routines to fetch bytes in Big Endian order
//////////////////////////////////////////////////////////

unsigned int Get2(unsigned char *pointer)
{
	return (pointer[0] | (pointer[1] << 8));
}

unsigned int Get3(unsigned char *pointer)
{
	return (pointer[0] | (pointer[1] << 8) | (pointer[2] << 16));
}

unsigned int Get4(unsigned char *pointer)
{
	return (pointer[0] | (pointer[1] << 8) | (pointer[2] << 16) | (pointer[3] << 24));
}

/////////////////////////
// Miscelaneous routines
/////////////////////////

void CopyString(char *destination, unsigned char *source, unsigned int len)
{
	// Could just use strcpy ... 

	unsigned int n;
	for (n = 0; n < len; n++)
		destination[n] = source[n];
	destination[n] = 0;
}

void MakeFixedString(char *s, int i)
{
	// This will create a fixed length string from null-terminated one...

	int n = 0;
	int k = 0;

	while (i)
	{
		if (!s[n]) k = 1;
		if (k) s[n] = ' ';
		n++;
		i--;
	}
	s[n] = 0;
}

///////////////////////////////
// TZX Blocks Parsing routines
///////////////////////////////

void Analyse_ID10(void)  // Standard Loading Data block
{
	pause_ms = Get2(&data[0]);
	datalen = Get2(&data[2]);
	data += 4;
	if (data[0] == 0x00) pilot = 8064;
	else pilot = 3220;
	sb_pilot = Samples(2168);
	sb_sync1 = Samples(667);
	sb_sync2 = Samples(735);
	sb_bit0 = Samples(885);
	sb_bit1 = Samples(1710);
	lastbyte = 8;
}

void Analyse_ID11(void)  // Custom Loading Data block
{
	sb_pilot = Samples(Get2(&data[0]));
	sb_sync1 = Samples(Get2(&data[2]));
	sb_sync2 = Samples(Get2(&data[4]));
	sb_bit0 = Samples(Get2(&data[6]));
	sb_bit1 = Samples(Get2(&data[8]));
	speed = (int)((1710.0 / (double)Get2(&data[8]))*100.0);
	pilot = Get2(&data[10]);
	lastbyte = (int)data[12];
	pause_ms = Get2(&data[13]);
	datalen = Get3(&data[15]);
	data += 18;
}

void Analyse_ID12(void)  // Pure Tone
{
	sb_pilot = Samples(Get2(&data[0]));
	pilot = Get2(&data[2]);
	if (draw) printf("    Pure Tone             Length: %5d\n", pilot);
	while (pilot)
	{
		PlayWave(sb_pilot);
		pilot--;
	}
}

void Analyse_ID13(void)  // Sequence of Pulses
{
	pilot = (int)data[0]; data++;
	if (draw) printf("    Sequence of Pulses    Length: %5d\n", pilot);
	while (pilot)
	{
		sb_pulse = Samples(Get2(&data[0]));
		PlayWave(sb_pulse);
		pilot--;
		data += 2;
	}
}

void Analyse_ID14(void)  // Pure Data
{
	sb_pilot = pilot = sb_sync1 = sb_sync2 = 0;
	sb_bit0 = Samples(Get2(&data[0]));
	sb_bit1 = Samples(Get2(&data[2]));
	speed = (int)((1710.0 / (double)Get2(&data[2]))*100.0);
	lastbyte = (int)data[4];
	pause_ms = Get2(&data[5]);
	datalen = Get3(&data[7]);
	data += 10;
}

void Analyse_ID15(void)  // Direct Recording
{
	// For now the BEST way is to use the sample frequency for replay that is
	// exactly the SAME as the Original Freq. used when sampling this block !
	// i.e. NO downsampling is handled YET ... use TAPER when you need it ! ;-)

	sb_pulse = Samples(Get2(&data[0]));
	if (!sb_pulse) sb_pulse = 1;       // In case sample frequency > 44100
	pause_ms = Get2(&data[2]);            // (Should work for frequencies upto 48000)
	lastbyte = (int)data[4];
	datalen = Get3(&data[5]);
	if (draw) printf("    Direct Recording      Length:%6d  Original Freq.: %5d Hz\n",
		datalen, (int)(0.5 + (3500000.0 / (double)Get2(&data[0]))));
	data = &data[8];
	datapos = 0;
	// Replay Direct Recording block ... 
	while (datalen)
	{
		if (datalen != 1) bitcount = 8;
		else bitcount = lastbyte;
		databyte = data[datapos];
		while (bitcount)
		{
			PlayWave(sb_pulse);
			databyte <<= 1;
			bitcount--;
		}
		datalen--;
		datapos++;
	}
	if (pause_ms) PauseWave(pause_ms);
}

void Analyse_ID16(void)  // C64 ROM Type Data Block
{
	data += 4;
	sb_pilot = Get2(&data[0]);
	pilot = Get2(&data[2]);
	sb_sync1 = Get2(&data[4]);
	sb_sync2 = Get2(&data[6]);
	sb_bit0_f = Get2(&data[8]);
	sb_bit0_s = Get2(&data[10]);
	sb_bit1_f = Get2(&data[12]);
	sb_bit1_s = Get2(&data[14]);
	xortype = (int)(data[16]);
	sb_finishbyte_f = Get2(&data[17]);
	sb_finishbyte_s = Get2(&data[19]);
	sb_finishdata_f = Get2(&data[21]);
	sb_finishdata_s = Get2(&data[23]);
	sb_trailing = Get2(&data[25]);
	trailing = Get2(&data[27]);
	lastbyte = (int)(data[29]);
	endian = data[30];
	pause_ms = Get2(&data[31]);
	datalen = Get3(&data[33]);
	data += 36;
	IdentifyC64ROM(datalen, data, 1);
}

void Analyse_ID17(void)  // C64 Turbo Tape Data Block
{
	data += 4;
	sb_bit0 = Get2(&data[0]);
	sb_bit1 = Get2(&data[2]);
	add_bit = data[4];
	num_lead_in = Get2(&data[5]);
	lead_in_byte = data[7];
	lastbyte = (int)data[8];
	endian = data[9];
	trailing = Get2(&data[10]);
	sb_trailing = data[12];
	pause_ms = Get2(&data[13]);
	datalen = Get3(&data[15]);
	data += 18;
	IdentifyC64Turbo(datalen, data, 1);
}

void Analyse_ID20(void)  // Pause or Stop the Tape command
{
	pause_ms = Get2(&data[0]);
	if (pause_ms)
	{
		if (draw) printf("    Pause                 Length: %2.3fs\n", ((float)pause_ms) / 1000.0);
		PauseWave(pause_ms);
	}
	else
	{
		if (draw) printf("    Stop the tape command!\n");
		PauseWave(2000); // 2 seconds of pause in "Stop Tape" wave output
	}
}

void Analyse_ID21(void) // Group Start
{
	CopyString(pstr, &data[1], data[0]);
	if (draw) printf("    Group: %s\n", pstr);
	if (!expand) draw = 0;
}

void Analyse_ID22(void)  // Group End
{
	if (draw) printf("    Group End\n");
	draw = 1;
}

void Analyse_ID23(void)  // Jump To Relative
{
	jump = (signed short)(data[0] + data[1] * 256);
	if (draw) printf("    Jump Relative: %d (To Block %d)\n", jump, curr + jump + 1);
	curr += jump;
	curr--;
}

void Analyse_ID24(void)  // Loop Start
{
	loop_start = curr;
	loop_count = Get2(&data[0]);
	if (draw) printf("    Loop Start, Counter: %d\n", loop_count);
}

void Analyse_ID25(void)  // Loop End
{
	loop_count--;
	if (loop_count > 0)
	{
		if (draw) printf("    Loop End, Still To Go %d Time(s)!\n", loop_count);
		curr = loop_start;
	}
	else
	{
		if (draw) printf("    Loop End, Finished\n");
	}
}

void Analyse_ID26(void)  // Call Sequence
{
	call_pos = curr;
	call_num = Get2(&data[0]);
	call_cur = 0;
	jump = (signed short)(data[2] + data[3] * 256);
	if (draw) printf("    Call Sequence, Number of Calls : %d, First: %d (To Block %d)\n", call_num, jump, curr + jump + 1);
	curr += jump;
	curr--;
}

void Analyse_ID27(void)  // Return from Sequence
{
	call_cur++;
	if (call_cur == call_num)
	{
		if (draw) printf("    Return from Call, Last Call Finished\n");
		curr = call_pos;
	}
	else
	{
		curr = call_pos;
		data = &mem[block[curr] + 1];
		jump = (signed short)(data[call_cur * 2 + 2] + data[call_cur * 2 + 3] * 256);
		if (draw) printf("    Return from Call, Calls Left: %d, Next: %d (To Block %d)\n",
			call_num - call_cur, jump, curr + jump + 1);
		curr += jump;
		curr--;
	}
}

void Analyse_ID28(void)  // Select Block
{
	num_sel = data[2];
	printf("    Select :\n");
	data += 3;
	for (n = 0; n < num_sel; n++)
	{
		jump = (signed short)(data[0] + data[1] * 256);
		jumparray[n] = jump;
		CopyString(spdstr, &data[3], data[2]);
		printf("%5d : %s\n", n + 1, spdstr);
		data += 3 + data[2];
	}

	//no interactive shell. choose 1.
	PauseWave(200);
	k = 1;

	/*
	printf(">> Press the number!\n");
	PauseWave(5000);   // Why?!?!?!?!
	k = getchar();
	if (k == 27) Error("ESCAPE key pressed!");
	k -= 48;
	if (k<1 || k>num_sel) printf("Illegal Selection... Continuing...\n");
	else
	*/
	{
		curr += jumparray[k - 1];
		curr--;
	}
}

void Analyse_ID2A(void)  // Stop the tape if in 48k mode
{
	if (mode128)
	{
		if (draw) printf("    Stop the tape only in 48k mode!\n");
	}
	else
	{
		if (draw) printf("    Stop the tape in 48k mode!\n");
		PauseWave(3000);
	}
}

void Analyse_ID30(void)  // Description
{
	CopyString(pstr, &data[1], data[0]);
	if (draw) printf("    Description: %s\n", pstr);
}

void Analyse_ID31(void)  // Message
{
	CopyString(pstr, &data[2], data[1]);
	// Pause in Message block is ignored ...
	if (draw) printf("    Message: %s\n", pstr);
}

void Analyse_ID32(void)  // Archive Info
{
	if (draw)
	{
		if (data[3] == 0)
		{
			CopyString(spdstr, &data[5], data[4]);
			sprintf(tstr, "    Title: %s", spdstr);
			MakeFixedString(tstr, 69);
			strcpy(tstr + 52, " (-v for more)");
			printf("%s\n", tstr);
		}
		else
		{
			sprintf(tstr, "    Archive Info");
			MakeFixedString(tstr, 69);
			strcpy(tstr + 52, " (-v for more)");
			printf("%s\n", tstr);
		}
	}
}

void Analyse_ID33(void)  // Hardware Info
{
	if (data[1] == 0 && data[2] > 0x14 && data[2] < 0x1a && data[3] == 1) cpc = 1;
	if (data[1] == 0 && data[2] == 0x09 && data[3] == 1) sam = 1;
	if (draw)
	{
		if (data[1] != 0 || data[3] != 1)
		{
			sprintf(tstr, "    Hardware Type");
			MakeFixedString(tstr, 69);
			strcpy(tstr + 52, " (-v for more)");
			printf("%s\n", tstr);
		}
		else
		{
			printf("    This tape is made for %s !\n", hwids_01[data[2]]);
		}
	}
}

void Analyse_ID34(void)  // Emulation info
{
	if (draw) printf("    Information for emulators.\n");
}

void Analyse_ID35(void)  // Custom Info
{
	CopyString(pstr, data, 16);
	if (draw)
	{
		if (strcmp(pstr, "POKEs           "))
			printf("    Custom Info: %s\n", pstr);
		// Only Name of Custom info except POKEs is used ...
		else
		{
			sprintf(tstr, "    Custom Info: %s", pstr);
			MakeFixedString(tstr, 69);
			strcpy(tstr + 52, " (-v for more)");
			printf("%s\n", tstr);
		}
	}
}

void Analyse_ID40(void)  // Snapshot
{
	if (draw) printf("    Snapshot               (Not Supported yet)\n");
}

void Analyse_ID5A(void)  // ZXTape!
{
	if (draw) printf("    Start of the new tape  (Merged Tapes)\n");
}

void Analyse_Unknown(void)  // Unknown blocks
{
	if (draw) printf("    Unknown block %02X !\n", id);
}

////////////////////////
// Main TZX2WAV program
////////////////////////

int tzx2csw(fileTYPE *f)
{
	nfreq = 44100;
	starting = 1;
	ending = 0;
	expand = 0;
	mode128 = 0;
	skippause = 0;
	inv = 0;
	oflen = 0;

	if (nfreq) freq = nfreq;

	mem = (unsigned char *)malloc(f->size);
	if (mem == NULL)
	{
		Error("Not enough memory to load the file!");
		GarbageCollector();
		return 0;
	}

	// Start reading file...
	FileReadAdv(f, mem, 10);
	mem[7] = 0;

	if (strcmp((const char*)mem, "ZXTape!"))
	{
		Error("File is not in ZXTape format!");
		GarbageCollector();
		return 0;
	}

	printf("\nZXTape file revision %d.%02d\n", mem[8], mem[9]);
	if (!mem[8])
	{
		Error("Development versions of ZXTape format are not supported!");
		GarbageCollector();
		return 0;
	}

	if (mem[8] > MAJREV) printf("\n-- Warning: Some blocks may not be recognised and used!\n");
	if (mem[8] == MAJREV && mem[9] > MINREV) printf("\n-- Warning: Some of the data might not be properly recognised!\n");
	FileReadAdv(f, mem, f->size - 10);
	numblocks = 0; pos = 0;
	not_rec = 0;

	// Go through the file and record block starts ...
	// (not necessary, could just go right through it)

	while (pos < f->size - 10)
	{
		block[numblocks] = pos;
		pos++;
		switch (mem[pos - 1])
		{
		case 0x10: pos += Get2(&mem[pos + 0x02]) + 0x04; break;
		case 0x11: pos += Get3(&mem[pos + 0x0F]) + 0x12; break;
		case 0x12: pos += 0x04; break;
		case 0x13: pos += (mem[pos + 0x00] * 0x02) + 0x01; break;
		case 0x14: pos += Get3(&mem[pos + 0x07]) + 0x0A; break;
		case 0x15: pos += Get3(&mem[pos + 0x05]) + 0x08; break;
		case 0x16: pos += Get4(&mem[pos + 0x00]) + 0x04; break;
		case 0x17: pos += Get4(&mem[pos + 0x00]) + 0x04; break;

		case 0x20: pos += 0x02; break;
		case 0x21: pos += mem[pos + 0x00] + 0x01; break;
		case 0x22: break;
		case 0x23: pos += 0x02; break;
		case 0x24: pos += 0x02; break;
		case 0x25: break;
		case 0x26: pos += Get2(&mem[pos + 0x00]) * 0x02 + 0x02; break;
		case 0x27: break;
		case 0x28: pos += Get2(&mem[pos + 0x00]) + 0x02; break;

		case 0x2A: pos += 0x04; break;

		case 0x30: pos += mem[pos + 0x00] + 0x01; break;
		case 0x31: pos += mem[pos + 0x01] + 0x02; break;
		case 0x32: pos += Get2(&mem[pos + 0x00]) + 0x02; break;
		case 0x33: pos += (mem[pos + 0x00] * 0x03) + 0x01; break;
		case 0x34: pos += 0x08; break;
		case 0x35: pos += Get4(&mem[pos + 0x10]) + 0x14; break;

		case 0x40: pos += Get3(&mem[pos + 0x01]) + 0x04; break;

		case 0x5A: pos += 0x09; break;

		default:  pos += Get4(&mem[pos + 0x00]) + 0x04;
			not_rec = 1;
		}
		numblocks++;
	}

	printf("Number of Blocks: %d\n", numblocks);

	if (not_rec)
	{
		printf("\n-- Warning: Some blocks were *NOT* recognised!\n");
	}

	curr = 0;

	if (starting > 1)
	{
		if (starting > numblocks)
		{
			Error("Invalid Starting Block");
			GarbageCollector();
			return 0;
		}
		curr = starting - 1;
	}

	if (ending > 0)
	{
		if (ending > numblocks || ending < starting)
		{
			Error("Invalid Ending Block");
			GarbageCollector();
			return 0;
		}
		numblocks = ending;
	}

	printf("\nCreating CSW v1");
	printf(" file using %d Hz frequency ...\n\n", freq);

	CSW1_Init();

	singlepulse = 0;
	manchester = 0;
	cycle = (double)freq / 3500000.0;   // This is for the conversion later ...

	///////////////////////////////////////////////////// 
	// Start replay of blocks (Main loop of the program)
	/////////////////////////////////////////////////////

	while (curr < numblocks)
	{
		if (draw) printf("Block %03d:", curr + 1);

		id = mem[block[curr]];
		data = &mem[block[curr] + 1];
		switch (id)
		{
		case 0x10: Analyse_ID10();  // Standard Loading Data block
			break;
		case 0x11: Analyse_ID11();  // Custom Loading Data block
			break;
		case 0x12: Analyse_ID12();  // Pure Tone
			break;
		case 0x13: Analyse_ID13();  // Sequence of Pulses
			break;
		case 0x14: Analyse_ID14();  // Pure Data
			break;
		case 0x15: Analyse_ID15();  // Direct Recording
			break;
		case 0x16: Analyse_ID16();  // C64 ROM Type Data Block
			break;
		case 0x17: Analyse_ID17();  // C64 Turbo Tape Data Block
			break;
		case 0x20: Analyse_ID20();  // Pause or Stop the Tape command
			break;
		case 0x21: Analyse_ID21();  // Group Start
			break;
		case 0x22: Analyse_ID22();  // Group End
			break;
		case 0x23: Analyse_ID23();  // Jump To Relative
			break;
		case 0x24: Analyse_ID24();  // Loop Start
			break;
		case 0x25: Analyse_ID25();  // Loop End
			break;
		case 0x26: Analyse_ID26();  // Call Sequence
			break;
		case 0x27: Analyse_ID27();  // Return from Sequence
			break;
		case 0x28: Analyse_ID28();  // Select Block
			break;
		case 0x2A: Analyse_ID2A();  // Stop the tape if in 48k mode
			break;
		case 0x30: Analyse_ID30();  // Description
			break;
		case 0x31: Analyse_ID31();  // Message
			break;
		case 0x32: Analyse_ID32();  // Archive Info
			break;
		case 0x33: Analyse_ID33();  // Hardware Info
			break;
		case 0x34: Analyse_ID34();  // Emulation info
			break;
		case 0x35: Analyse_ID35();  // Custom Info
			break;
		case 0x40: Analyse_ID40();  // Snapshot
			break;
		case 0x5A: Analyse_ID5A();  // ZXTape!
			break;
		default:  Analyse_Unknown(); // Unknown blocks
		}

		// TZX file blocks analysis finished
		// Now we start generating the sound waves

		if ((id == 0x10 || id == 0x11 || id == 0x14)) // One of the data blocks ...
		{
			if (id != 0x14)   Identify(datalen, data, 0);
			else            strcpy(tstr, "    Pure Data           ");
			if (id == 0x10)   sprintf(spdstr, "Normal Speed");
			else            sprintf(spdstr, " Speed: %3d%%", speed);
			sprintf(pstr, "Pause: %5d ms", pause_ms);
			if (draw) printf("%s  Length:%6d %s %s\n", tstr, datalen, spdstr, pstr);

			{
				while (pilot)  // Play PILOT TONE
				{
					PlayWave(sb_pilot);
					pilot--;
				}
				if (sb_sync1)  // Play first SYNC pulse
				{
					PlayWave(sb_sync1);
				}
				if (sb_sync2)  // Play second SYNC pulse
				{
					PlayWave(sb_sync2);
				}
				datapos = 0;
				while (datalen)  // Play actual DATA
				{
					if (datalen != 1) bitcount = 8;
					else bitcount = lastbyte;
					databyte = data[datapos];
					while (bitcount)
					{
						if (databyte & 0x80) sb_bit = sb_bit1;
						else sb_bit = sb_bit0;
						PlayWave(sb_bit);   // Play first pulse of the bit
						if (!singlepulse)
						{
							PlayWave(sb_bit); // Play second pulse of the bit
						}
						databyte <<= 1;
						bitcount--;
					}
					datalen--; datapos++;
				}
				singlepulse = 0;   // Reset flag for next TZX blocks

				// If there is pause after block present then make first millisecond the oposite
				// pulse of last pulse played and the rest in LOAMP ... otherwise don't do ANY pause
				if (pause_ms)
				{
					PauseWave(1);
					if (pause_ms > 1) PauseWave(pause_ms - 1);
				}
			}
		}

		if (id == 0x16)  // C64 ROM data block ...
		{
			IdentifyC64ROM(datalen, data, 0);
			sprintf(pstr, "Pause: %5d ms", pause_ms);
			if (draw) printf(" %s Length:%6d %s %s\n", tstr, datalen, spdstr, pstr);

			{
				sb_pilot = Samples(sb_pilot);
				sb_sync1 = Samples(sb_sync1); sb_sync2 = Samples(sb_sync2);
				sb_bit1_f = Samples(sb_bit1_f); sb_bit1_s = Samples(sb_bit1_s);
				sb_bit0_f = Samples(sb_bit0_f); sb_bit0_s = Samples(sb_bit0_s);
				sb_finishbyte_f = Samples(sb_finishbyte_f);
				sb_finishbyte_s = Samples(sb_finishbyte_s);
				sb_finishdata_f = Samples(sb_finishdata_f);
				sb_finishdata_s = Samples(sb_finishdata_s);
				sb_trailing = Samples(sb_trailing);
				num_lead_in = 0;
				while (pilot)     // Play PILOT TONE
				{
					PlayC64(sb_pilot);
					pilot--;
				}
				if (sb_sync1) PlayC64(sb_sync1);  // Play SYNC PULSES
				if (sb_sync2) PlayC64(sb_sync2);
				datapos = 0;
				while (datalen)   // Play actual DATA
				{
					if (datalen != 1)
					{
						bitcount = 8;
						PlayC64ROMByte(data[datapos], 0);
					}
					else
					{
						bitcount = lastbyte;
						PlayC64ROMByte(data[datapos], 1);
					}
					databyte = data[datapos];
					datalen--; datapos++;
				}
				while (trailing)  // Play TRAILING TONE
				{
					PlayC64(sb_trailing);
					trailing--;
				}

				// If there is pause after block present then make first millisecond the oposite
				// pulse of last pulse played and the rest in LOAMP ... otherwise don't do ANY pause

				if (pause_ms)
				{
					PauseWave(pause_ms / 2);
					PauseWave((pause_ms / 2) + (pause_ms % 2));
				}
			}
		}

		if (id == 0x17)    // C64 Turbo Tape data block ...
		{
			IdentifyC64Turbo(datalen, data, 0);
			sprintf(pstr, "Pause: %5d ms", pause_ms);
			if (draw) printf(" %s Length:%6d %s %s\n", tstr, datalen, spdstr, pstr);

			{
				sb_bit1 = Samples(sb_bit1);
				sb_bit0 = Samples(sb_bit0);
				while (num_lead_in)  // Play Lead In bytes
				{
					bitcount = 8;
					PlayC64TurboByte(lead_in_byte);
					num_lead_in--;
				}
				datapos = 0;
				while (datalen)      // Play actual DATA
				{
					if (datalen != 1) bitcount = 8;
					else bitcount = lastbyte;
					PlayC64TurboByte(data[datapos]);
					databyte = data[datapos];
					datalen--; datapos++;
				}
				while (trailing)     // Play Trailing bytes
				{
					bitcount = 8;
					PlayC64TurboByte((unsigned char)sb_trailing);
					trailing--;
				}

				// If there is pause after block present then make first millisecond the oposite
				// pulse of last pulse played and the rest in LOAMP ... otherwise don't do ANY pause

				if (pause_ms)
				{
					PauseWave(pause_ms / 2);
					PauseWave((pause_ms / 2) + (pause_ms % 2));
				}
			}
		}

		curr++; // We continue to replay the next TZX block
	} // This is the main loop end

	PauseWave(200);  // Finish always with 200 ms of pause after the last block
	printf("\n%d bytes sent to the core.\n", oflen);
	GarbageCollector();
	return 1;
}
