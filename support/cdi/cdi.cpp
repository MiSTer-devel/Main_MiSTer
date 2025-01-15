
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <array>
#include <inttypes.h>

#include "../../file_io.h"
#include "../../user_io.h"
#include "../../spi.h"
#include "../../hardware.h"
#include "../../menu.h"
#include "cdi.h"
#include "../../cd.h"
#include "../chd/mister_chd.h"
#include <libchdr/chd.h>
#include <arpa/inet.h>

struct subcode
{
	uint16_t control;
	uint16_t track;
	uint16_t index;
	uint16_t mode1_mins;
	uint16_t mode1_secs;
	uint16_t mode1_frac;
	uint16_t mode1_zero;
	uint16_t mode1_amins;
	uint16_t mode1_asecs;
	uint16_t mode1_afrac;
	uint16_t mode1_crc0;
	uint16_t mode1_crc1;
};
static_assert(sizeof(struct subcode) == 24);

struct toc_entry
{
	uint8_t control;
	uint8_t track;
	uint8_t m;
	uint8_t s;
	uint8_t f;
};

static char buf[1024];
#define CD_SECTOR_LEN 2352
#define CDIC_BUFFER_SIZE (CD_SECTOR_LEN + sizeof(subcode))
static std::array<struct toc_entry, 200> toc_buffer;
uint32_t toc_entry_count = 0;

static uint8_t *chd_hunkbuf = NULL;
static int chd_hunknum;

static int sgets(char *out, int sz, char **in)
{
	*out = 0;
	do
	{
		char *instr = *in;
		int cnt = 0;

		while (*instr && *instr != 10)
		{
			if (*instr == 13)
			{
				instr++;
				continue;
			}

			if (cnt < sz - 1)
			{
				out[cnt++] = *instr;
				out[cnt] = 0;
			}

			instr++;
		}

		if (*instr == 10)
			instr++;
		*in = instr;
	} while (!*out && **in);

	return *out;
}

static void unload_chd(toc_t *table)
{
	if (table->chd_f)
	{
		chd_close(table->chd_f);
	}
	if (chd_hunkbuf)
		free(chd_hunkbuf);
	memset(table, 0, sizeof(toc_t));
	chd_hunknum = -1;
}

static void unload_cue(toc_t *table)
{
	for (int i = 0; i < table->last; i++)
	{
		FileClose(&table->tracks[i].f);
	}
	memset(table, 0, sizeof(toc_t));
}

static int load_chd(const char *filename, toc_t *table)
{

	unload_chd(table);
	chd_error err = mister_load_chd(filename, table);
	if (err != CHDERR_NONE)
	{
		return 0;
	}

	for (int i = 0; i < table->last; i++)
	{
		table->tracks[i].pregap = table->tracks[i].indexes[1];
		table->tracks[i].start += 150;
		table->tracks[i].end += 150;

		printf("\x1b[32mCHD: Track = %u, start = %u, end = %u, offset = %d, sector_size=%d, type = %u, pregap = %u\n\x1b[0m", i, table->tracks[i].start, table->tracks[i].end, table->tracks[i].offset, table->tracks[i].sector_size, table->tracks[i].type, table->tracks[i].pregap);
		printf("\x1b[32mCHD: Track = %u, Index %u %u seconds\n\x1b[0m", i, table->tracks[i].indexes[0], table->tracks[i].indexes[1]);
	}

	table->end += 150;

	chd_hunkbuf = (uint8_t *)malloc(table->chd_hunksize);
	chd_hunknum = -1;

	return 1;
}

static int load_cue(const char *filename, toc_t *table)
{
	static char fname[1024 + 10];
	static char line[128];
	char *ptr, *lptr;
	static char toc[100 * 1024];

	unload_cue(table);
	printf("\x1b[32mCDI: Open CUE: %s\n\x1b[0m", fname);

	strcpy(fname, filename);

	memset(toc, 0, sizeof(toc));
	if (!FileLoad(fname, toc, sizeof(toc) - 1))
	{
		printf("\x1b[32mCDI: cannot load file: %s\n\x1b[0m", fname);
		return 0;
	}

	int mm, ss, bb;
	int index0 = 0;
	int index1 = 0;

	char *buf = toc;
	while (sgets(line, sizeof(line), &buf))
	{
		lptr = line;
		while (*lptr == 0x20)
			lptr++;

		/* decode FILE commands */
		if (!(memcmp(lptr, "FILE", 4)))
		{
			ptr = fname + strlen(fname) - 1;
			while ((ptr - fname) && (*ptr != '/') && (*ptr != '\\'))
				ptr--;
			if (ptr - fname)
				ptr++;

			lptr += 4;
			while (*lptr == 0x20)
				lptr++;

			if (*lptr == '\"')
			{
				lptr++;
				while ((*lptr != '\"') && (lptr <= (line + 128)) && (ptr < (fname + 1023)))
					*ptr++ = *lptr++;
			}
			else
			{
				while ((*lptr != 0x20) && (lptr <= (line + 128)) && (ptr < (fname + 1023)))
					*ptr++ = *lptr++;
			}
			*ptr = 0;

			if (!FileOpen(&table->tracks[table->last].f, fname))
				return 0;

			printf("\x1b[32mCDI: Open track file: %s\n\x1b[0m", fname);

			table->tracks[table->last].offset = 0;

			if (!strstr(lptr, "BINARY"))
			{
				FileClose(&table->tracks[table->last].f);
				printf("\x1b[32mCDI: unsupported file: %s\n\x1b[0m", fname);
				return 0;
			}
		}

		/* decode PREGAP commands */
		else if (sscanf(lptr, "PREGAP %02d:%02d:%02d", &mm, &ss, &bb) == 3)
		{
			// TODO Find an example image
		}
		/* decode TRACK commands */
		else if ((sscanf(lptr, "TRACK %02d %*s", &bb)) || (sscanf(lptr, "TRACK %d %*s", &bb)))
		{
			index0 = 0;
			if (bb != (table->last + 1))
			{
				FileClose(&table->tracks[table->last].f);
				printf("\x1b[32mCDI: missing tracks: %s\n\x1b[0m", fname);
				return 0;
			}

			if (strstr(lptr, "MODE1/2352") || strstr(lptr, "MODE2/2352") || strstr(lptr, "CDI/2352"))
			{
				table->tracks[table->last].sector_size = CD_SECTOR_LEN;
				table->tracks[table->last].type = 1;
				if (!table->last)
					table->end = 150; // implicit 2 seconds pregap for track 1
			}
			else if (strstr(lptr, "AUDIO"))
			{
				table->tracks[table->last].sector_size = CD_SECTOR_LEN;
				table->tracks[table->last].type = 0;
				if (!table->last)
					table->end = 150; // implicit 2 seconds pregap for track 1
			}
			else
			{
				FileClose(&table->tracks[table->last].f);
				printf("\x1b[32mCDI: unsupported track type: %s\n\x1b[0m", lptr);
				return 0;
			}
		}

		/* decode INDEX commands */
		else if ((sscanf(lptr, "INDEX 00 %02d:%02d:%02d", &mm, &ss, &bb) == 3) ||
				 (sscanf(lptr, "INDEX 0 %02d:%02d:%02d", &mm, &ss, &bb) == 3))
		{
			index0 = bb + ss * 75 + mm * 60 * 75;
		}
		else if ((sscanf(lptr, "INDEX 01 %02d:%02d:%02d", &mm, &ss, &bb) == 3) ||
				 (sscanf(lptr, "INDEX 1 %02d:%02d:%02d", &mm, &ss, &bb) == 3))
		{
			index1 = bb + ss * 75 + mm * 60 * 75;

			if (!table->tracks[table->last].f.opened())
			{
				table->tracks[table->last].start = index1 + 150;
				table->tracks[table->last].pregap = index1 - index0;
				// Subtract the fake 150 sector pregap used for the first data track
				table->tracks[table->last].offset = index0 * table->tracks[table->last].sector_size;
				table->tracks[table->last - 1].end = table->tracks[table->last].start - 1 - table->tracks[table->last].pregap;
			}
			else
			{
				table->tracks[table->last].start = table->end + index0 + index1;
				table->tracks[table->last].pregap = index1 - index0;
				table->end += (table->tracks[table->last].f.size / table->tracks[table->last].sector_size);
				table->tracks[table->last].offset = 0;
			}
			table->tracks[table->last].end = table->end - 1;
			table->last++;
			if (table->last >= 99)
				break;
		}
	}

	for (int i = 0; i < table->last; i++)
	{
		printf("\x1b[32mCUE: Track = %u, start = %u, end = %u, offset = %d, sector_size=%d, type = %u, pregap = %u\n\x1b[0m", i, table->tracks[i].start, table->tracks[i].end, table->tracks[i].offset, table->tracks[i].sector_size, table->tracks[i].type, table->tracks[i].pregap);
	}

	return 1;
}

static int load_cd_image(const char *filename, toc_t *table)
{

	const char *ext = strrchr(filename, '.');
	if (!ext)
		return 0;

	if (!strncasecmp(".chd", ext, 4))
	{
		return load_chd(filename, table);
	}
	else if (!strncasecmp(".cue", ext, 4))
	{
		return load_cue(filename, table);
	}

	return 0;
}

static void prepare_toc_buffer(toc_t *toc)
{
	struct toc_entry *toc_ptr = toc_buffer.data();
	toc_entry_count = 0;

	auto add_entry = [&](uint8_t control, uint8_t track, uint8_t m, uint8_t s, uint8_t f)
	{
		for (int i = 0; i < 3; i++)
		{
			toc_ptr->control = control;
			toc_ptr->track = track;
			toc_ptr->m = m;
			toc_ptr->s = s;
			toc_ptr->f = f;

			toc_ptr++;

			if (toc_entry_count < toc_buffer.size())
				toc_entry_count++;
		}
	};

	for (int i = 0; i < toc->last; i++)
	{
		int lba = toc->tracks[i].start;
		uint8_t m, s, f;
		m = lba / (60 * 75);
		lba -= m * (60 * 75);
		s = lba / 75;
		f = lba % 75;
		add_entry((toc->tracks[i].type ? 0x41 : 0x01), BCD(i + 1), BCD(m), BCD(s), BCD(f));
	}

	add_entry(1, 0xA0, 1, 0, 0);
	add_entry(1, 0xA1, BCD(toc->last), 0, 0);

	{
		int lba = toc->end;
		uint8_t m, s, f;
		m = lba / (60 * 75);
		lba -= m * (60 * 75);
		s = lba / 75;
		f = lba % 75;
		add_entry(1, 0xA2, BCD(m), BCD(s), BCD(f));
	}
}

#define TIMEKEEPER_SIZE (8 * 1024)

static void cdi_mount_save(const char *filename)
{
	user_io_set_index(1);
	if (strlen(filename))
	{
		FileGenerateSavePath(filename, buf, 0);
		user_io_file_mount(buf, 1, 1, TIMEKEEPER_SIZE);
		StoreIdx_S(1, buf);
	}
	else
	{
		user_io_file_mount("", 1);
		StoreIdx_S(1, "");
	}
}

static toc_t toc = {};

int cdi_chd_hunksize()
{
	if (toc.chd_f)
		return toc.chd_hunksize;

	return 0;
}

// CRC routine from https://github.com/mamedev/mame/blob/master/src/mame/philips/cdicdic.cpp
const uint16_t s_crc_ccitt_table[256] =
	{
		0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
		0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
		0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
		0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
		0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
		0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
		0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
		0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
		0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
		0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
		0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
		0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
		0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
		0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
		0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
		0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
		0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
		0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
		0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
		0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
		0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
		0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
		0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
		0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
		0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
		0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
		0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
		0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
		0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
		0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
		0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
		0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0};

#define CRC_CCITT_ROUND(accum, data) (((accum << 8) | data) ^ s_crc_ccitt_table[accum >> 8])

void subcode_data(int lba, struct subcode &out)
{
	if (lba < 0)
	{
		// TOC is expected by the core at lba -65536
		lba += 65536;

		uint8_t am, as, af;
		am = lba / (60 * 75);
		int rem_lba = lba - am * (60 * 75);
		as = rem_lba / 75;
		af = rem_lba % 75;

		auto &toc_entry = toc_buffer[lba % toc_entry_count];

		out.control = htons(toc_entry.control);
		out.track = 0; // Track 0 for TOC
		out.index = htons(toc_entry.track);
		out.mode1_mins = htons(BCD(am));
		out.mode1_secs = htons(BCD(as));
		out.mode1_frac = htons(BCD(af));
		out.mode1_zero = 0;
		out.mode1_amins = htons(toc_entry.m);
		out.mode1_asecs = htons(toc_entry.s);
		out.mode1_afrac = htons(toc_entry.f);
		out.mode1_crc0 = htons(0xff);
		out.mode1_crc1 = htons(0xff);
	}
	else
	{
		uint8_t am, as, af;
		am = lba / (60 * 75);
		int rem_lba = lba - am * (60 * 75);
		as = rem_lba / 75;
		af = rem_lba % 75;

		int track = toc.GetTrackByLBA(lba + 150);

		int track_lba = lba - toc.tracks[track].start;
		int index = 1;

		if (track_lba < 0)
		{
			// Fix index 0 tracks which are defined as pause
			// The timecode seems to go backwards on a real machine
			track_lba = -track_lba;
			index = 0;
		};

		uint8_t tm, ts, tf;
		tm = track_lba / (60 * 75);
		track_lba -= tm * (60 * 75);
		ts = track_lba / 75;
		tf = track_lba % 75;

		out.control = htons(toc.tracks[track].type ? 0x41 : 0x01);
		out.track = htons(BCD(track + 1));
		out.index = htons(BCD(index));
		out.mode1_mins = htons(BCD(tm));
		out.mode1_secs = htons(BCD(ts));
		out.mode1_frac = htons(BCD(tf));
		out.mode1_zero = 0;
		out.mode1_amins = htons(BCD(am));
		out.mode1_asecs = htons(BCD(as));
		out.mode1_afrac = htons(BCD(af));
		out.mode1_crc0 = htons(0xff);
		out.mode1_crc1 = htons(0xff);
	}

	uint16_t crc_accum = 0;
	uint8_t *crc = reinterpret_cast<uint8_t *>(&out);
	for (int i = 0; i < 12; i++)
		crc_accum = CRC_CCITT_ROUND(crc_accum, crc[1 + i * 2]);

	out.mode1_crc0 = htons((crc_accum >> 8) & 0xff);
	out.mode1_crc1 = htons(crc_accum & 0xff);
#if 0
	printf("subcode %d   %02x %02x %02x %02x %02x %02x     %02x %02x %02x %02x %02x %02x\n", lba,
		   ntohs(out.control), ntohs(out.track), ntohs(out.index),
		   ntohs(out.mode1_mins), ntohs(out.mode1_secs), ntohs(out.mode1_frac), ntohs(out.mode1_zero),
		   ntohs(out.mode1_amins), ntohs(out.mode1_asecs), ntohs(out.mode1_afrac), ntohs(out.mode1_crc0),
		   ntohs(out.mode1_crc1));
#endif
}

void cdi_read_cd(uint8_t *buffer, int lba, int cnt)
{
	int calc_lba = lba;
	uint8_t am, as, af;
	am = calc_lba / (60 * 75);
	calc_lba -= am * (60 * 75);
	as = calc_lba / 75;
	af = calc_lba % 75;

	printf("req lba=%d, cnt=%d   %02d:%02d:%02d   %d %d\n", lba, cnt, am, as, af,
		   toc.tracks[0].start, toc.tracks[0].pregap);

	while (cnt > 0)
	{
		if (lba < 0 || !toc.last)
		{
			memset(buffer, 0, CD_SECTOR_LEN);
		}
		else
		{
			memset(buffer, 0xAA, CD_SECTOR_LEN);

			for (int i = 0; i < toc.last; i++)
			{
				if (lba >= (toc.tracks[i].start - toc.tracks[i].pregap) && lba <= toc.tracks[i].end)
				{
					if (!toc.chd_f)
					{
						if (toc.tracks[i].offset)
						{
							FileSeek(&toc.tracks[0].f, toc.tracks[i].offset + ((lba - toc.tracks[i].start + toc.tracks[i].pregap) * CD_SECTOR_LEN), SEEK_SET);
						}
						else
						{
							FileSeek(&toc.tracks[i].f, (lba - toc.tracks[i].start + toc.tracks[i].pregap) * CD_SECTOR_LEN, SEEK_SET);
						}
					}

					while (cnt)
					{
						if (toc.chd_f)
						{
							// The "fake" 150 sector pregap moves all the LBAs up by 150, so adjust here to read where the core actually wants data from
							int read_lba = lba - 150;
							if (mister_chd_read_sector(toc.chd_f, (read_lba + toc.tracks[i].offset), 0, 0, CD_SECTOR_LEN, buffer, chd_hunkbuf, &chd_hunknum) == CHDERR_NONE)
							{
								if (!toc.tracks[i].type) // CHD requires byteswap of audio data
								{
									for (int swapidx = 0; swapidx < CD_SECTOR_LEN; swapidx += 2)
									{
										uint8_t temp = buffer[swapidx];
										buffer[swapidx] = buffer[swapidx + 1];
										buffer[swapidx + 1] = temp;
									}
								}
							}
							else
							{
								printf("\x1b[32mCDI: CHD read error: %d\n\x1b[0m", lba);
							}
						}
						else
						{
							if (toc.tracks[i].offset)
								FileReadAdv(&toc.tracks[0].f, buffer, CD_SECTOR_LEN);
							else
								FileReadAdv(&toc.tracks[i].f, buffer, CD_SECTOR_LEN);
						}
						if ((lba + 1) > toc.tracks[i].end)
							break;

						buffer += CD_SECTOR_LEN;
						subcode_data(lba, *reinterpret_cast<struct subcode *>(buffer));
						buffer += sizeof(struct subcode);
						cnt--;
						lba++;
					}
					break;
				}
			}
		}

		buffer += CD_SECTOR_LEN;
		subcode_data(lba, *reinterpret_cast<struct subcode *>(buffer));
		buffer += sizeof(struct subcode);
		cnt--;
		lba++;
	}
}

static void mount_cd(int size, int index)
{
	spi_uio_cmd_cont(UIO_SET_SDINFO);
	spi32_w(size);
	spi32_w(0);
	DisableIO();
	spi_uio_cmd8(UIO_SET_SDSTAT, (1 << index) | 0x80);
	user_io_bufferinvalidate(1);
}

void cdi_mount_cd(int s_index, const char *filename)
{
	int loaded = 0;

	if (strlen(filename))
	{
		if (load_cd_image(filename, &toc) && toc.last)
		{
			cdi_mount_save(filename);
			prepare_toc_buffer(&toc);
			user_io_set_index(0);
			mount_cd(toc.end * CD_SECTOR_LEN, s_index);
			loaded = 1;
		}
	}

	if (!loaded)
	{
		printf("Unmount CD\n");
		unload_cue(&toc);
		unload_chd(&toc);
		mount_cd(0, s_index);
	}
}

void cdi_poll()
{
	spi_uio_cmd(UIO_CD_GET);
}
