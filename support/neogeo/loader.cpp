// Part of Neogeo_MiSTer
// (C) 2019 Sean 'furrtek' Gonsalves

#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>   // clock_gettime, CLOCK_REALTIME
#include "graphics.h"
#include "loader.h"
#include "../../sxmlc.h"
#include "../../user_io.h"
#include "../../osd.h"
#include "../../menu.h"

static char pchar[] = { 0x8C, 0x8E, 0x8F, 0x90, 0x91, 0x7F };
static void neogeo_osd_progress(const char* name, unsigned int progress)
{
	static char progress_buf[64];
	memset(progress_buf, ' ', sizeof(progress_buf));

	// OSD width - width of white bar on the left - max width of file name = 32 - 2 - 11 - 1 = 18
	progress = (progress * 108) >> 8;
	if (progress > 108) progress = 108;

	char c = pchar[progress % 6];
	progress /= 6;

	strcpy(progress_buf, name);
	char *buf = progress_buf + strlen(progress_buf);
	*buf++ = ' ';

	for (unsigned int i = 0; i <= progress; i++) buf[1 + i] = (i < progress) ? 0x7F : c;
	buf[19] = 0;

	Info(progress_buf);
}

static int neogeo_file_tx(const char* path, const char* name, unsigned char neo_file_type, unsigned char index, unsigned long offset, unsigned long size)
{
	fileTYPE f = {};
	uint8_t buf[4096];	// Same in user_io_file_tx
	uint8_t buf_out[4096];
	static char name_buf[1024];
	struct timespec ts1, ts2;	// DEBUG PROFILING
	long us_acc = 0;	// DEBUG PROFILING

	sprintf(name_buf, "%s/%s", path, name);
	if (!FileOpen(&f, name_buf, 0)) return 0;
	if (!size && offset < f.size) size = f.size - offset;
	if (!size) return 0;

	unsigned long bytes2send = size;

	FileSeek(&f, offset, SEEK_SET);
	printf("Loading %s (offset %lu, size %lu, type %u) with index %u\n", name, offset, bytes2send, neo_file_type, index);

	// Put pairs of bitplanes in the correct order for the core
	if (neo_file_type == NEO_FILE_SPR && index != 15) index ^= 1;
	// set index byte
	user_io_set_index(index);

	// prepare transmission of new file
	EnableFpga();
	spi8(UIO_FILE_TX);
	spi8(0xff);
	DisableFpga();

	int progress = -1;

	while (bytes2send)
	{
		uint16_t chunk = (bytes2send > sizeof(buf)) ? sizeof(buf) : bytes2send;

		FileReadAdv(&f, buf, chunk);

		EnableFpga();
		spi8(UIO_FILE_TX_DAT);

		if (neo_file_type == NEO_FILE_RAW) {
			spi_write(buf, chunk, 1);
		} else if (neo_file_type == NEO_FILE_8BIT) {
			spi_write(buf, chunk, 0);
		} else {

			if (neo_file_type == NEO_FILE_FIX)
				fix_convert(buf, buf_out, sizeof(buf_out));
			else if (neo_file_type == NEO_FILE_SPR)
			{
				if(index == 15) spr_convert_dbl(buf, buf_out, sizeof(buf_out));
					else spr_convert(buf, buf_out, sizeof(buf_out));
			}

			clock_gettime(CLOCK_REALTIME, &ts1);	// DEBUG PROFILING
				spi_write(buf_out, chunk, 1);
			clock_gettime(CLOCK_REALTIME, &ts2);	// DEBUG PROFILING

			if (ts2.tv_nsec < ts1.tv_nsec) {	// DEBUG PROFILING
				ts2.tv_nsec += 1000000000;
				ts2.tv_sec--;
			}

			us_acc += ((ts2.tv_nsec - ts1.tv_nsec) / 1000);	// DEBUG PROFILING
		}

		DisableFpga();
		int new_progress = 256 - ((((uint64_t)bytes2send) << 8)/size);
		if (progress != new_progress)
		{
			progress = new_progress;
			neogeo_osd_progress(name, progress);
		}
		bytes2send -= chunk;
	}

	// DEBUG PROFILING
	printf("Gfx spi_write us total: %09ld\n", us_acc);
	// mslug all C ROMs:
	// spr_convert: 37680*4 = 150720us = 0.150s
	// spi_write: 2300766*4 = 9203064us = 9.2s !

	FileClose(&f);

	// signal end of transmission
	EnableFpga();
	spi8(UIO_FILE_TX);
	spi8(0x00);
	DisableFpga();

	return 1;
}

struct rom_info
{
	char name[16];
	char altname[256];
};

static rom_info roms[1000];
static uint32_t rom_cnt = 0;

static int xml_scan(XMLEvent evt, const XMLNode* node, SXML_CHAR* text, const int n, SAX_Data* sd)
{
	(void)(sd);

	switch (evt)
	{
	case XML_EVENT_START_NODE:
		if (!strcasecmp(node->tag, "romset") && (rom_cnt < (sizeof(roms) / sizeof(roms[0]))))
		{
			memset(&roms[rom_cnt], 0, sizeof(rom_info));
			for (int i = 0; i < node->n_attributes; i++)
			{
				if (!strcasecmp(node->attributes[i].name, "name"))
				{
					strncpy(roms[rom_cnt].name, node->attributes[i].value, sizeof(roms[rom_cnt].name) - 1);
					strncpy(roms[rom_cnt].altname, node->attributes[i].value, sizeof(roms[rom_cnt].name) - 1);
				}
			}

			for (int i = 0; i < node->n_attributes; i++)
			{
				if (!strcasecmp(node->attributes[i].name, "altname"))
				{
					memset(roms[rom_cnt].altname, 0, sizeof(roms[rom_cnt].altname));
					strncpy(roms[rom_cnt].altname, node->attributes[i].value, sizeof(roms[rom_cnt].altname) - 1);
				}
			}
			rom_cnt++;
		}
		break;

	case XML_EVENT_END_NODE:
		break;

	case XML_EVENT_ERROR:
		printf("XML parse: %s: ERROR %d\n", text, n);
		break;
	default:
		break;
	}

	return true;
}

static int xml_get_altname(XMLEvent evt, const XMLNode* node, SXML_CHAR* text, const int n, SAX_Data* sd)
{
	char* altname = (char*)sd->user;

	switch (evt)
	{
	case XML_EVENT_START_NODE:
		if (!strcasecmp(node->tag, "romset") && (rom_cnt < (sizeof(roms) / sizeof(roms[0]))))
		{
			memset(&roms[rom_cnt], 0, sizeof(rom_info));
			for (int i = 0; i < node->n_attributes; i++)
			{
				if (!strcasecmp(node->attributes[i].name, "name")) strncpy(altname, node->attributes[i].value, 255);
			}

			for (int i = 0; i < node->n_attributes; i++)
			{
				if (!strcasecmp(node->attributes[i].name, "altname")) strncpy(altname, node->attributes[i].value, 255);
			}
			rom_cnt++;
		}
		break;

	case XML_EVENT_END_NODE:
		break;

	case XML_EVENT_ERROR:
		printf("XML parse: %s: ERROR %d\n", text, n);
		break;
	default:
		break;
	}

	return true;
}

int neogeo_scan_xml()
{
	static char full_path[1024];
	sprintf(full_path, "%s/%s/romsets.xml", getRootDir(), HomeDir);
	SAX_Callbacks sax;
	SAX_Callbacks_init(&sax);

	rom_cnt = 0;
	sax.all_event = xml_scan;
	XMLDoc_parse_file_SAX(full_path, &sax, 0);
	return rom_cnt;
}

char *neogeo_get_altname(char *path, char *name)
{
	static char full_path[1024];
	strcpy(full_path, path);
	strcat(full_path, "/");
	strcat(full_path, name);
	strcat(full_path, "/romset.xml");

	if (!access(full_path, F_OK))
	{
		static char altname[256];
		altname[0] = 0;

		SAX_Callbacks sax;
		SAX_Callbacks_init(&sax);

		sax.all_event = xml_get_altname;
		XMLDoc_parse_file_SAX(full_path, &sax, &altname);
		if (*altname) return altname;
	}

	for (uint32_t i = 0; i < rom_cnt; i++)
	{
		if (!strcasecmp(name, roms[i].name)) return roms[i].altname;
	}
	return NULL;
}

static int checked_ok;
static int romsets = 0;
static int xml_check_files(XMLEvent evt, const XMLNode* node, SXML_CHAR* text, const int n, SAX_Data* sd)
{
	static int in_correct_romset = 0;
	static char full_path[1024];

	const char* path = (const char*)sd->user;
	if (!path) return 0;
	const char* romset = strrchr(path, '/');
	if (!romset) return 0;
	romset++;

	switch (evt)
	{
	case XML_EVENT_START_NODE:
		if (!strcasecmp(node->tag, "romsets")) romsets = 1;

		if (!strcasecmp(node->tag, "romset"))
		{
			if (!romsets)
			{
				in_correct_romset = 1;
			}
			else if (!strcasecmp(node->attributes[0].value, romset))
			{
				printf("Romset %s found !\n", romset);
				in_correct_romset = 1;
			}
			else
			{
				in_correct_romset = 0;
			}
		}
		if (in_correct_romset)
		{
			if (!strcasecmp(node->tag, "file"))
			{
				for (int i = 0; i < node->n_attributes; i++)
				{
					if (!strcasecmp(node->attributes[i].name, "name"))
					{
						sprintf(full_path, "%s/%s", path, node->attributes[i].value);
						if (FileExists(full_path))
						{
							printf("Found %s\n", full_path);
							break;
						}
						else
						{
							printf("Missing %s\n", full_path);
							sprintf(full_path, "Missing %s !", node->attributes[i].value);
							Info(full_path);
							return false;
						}
					}
				}
			}
		}
		break;

	case XML_EVENT_END_NODE:
		if (in_correct_romset)
		{
			if (!strcasecmp(node->tag, "romset"))
			{
				checked_ok = true;
				return false;
			}
		}
		if (!strcasecmp(node->tag, "romsets"))
		{
			printf("Couldn't find romset %s\n", romset);
			return false;
		}
		break;

	case XML_EVENT_ERROR:
		printf("XML parse: %s: ERROR %d\n", text, n);
		break;
	default:
		break;
	}

	return true;
}

static int xml_load_files(XMLEvent evt, const XMLNode* node, SXML_CHAR* text, const int n, SAX_Data* sd)
{
	static char file_name[16 + 1] { "" };
	static int in_correct_romset = 0;
	static int in_file = 0;
	static unsigned char file_index = 0;
	static char file_type = 0;
	static unsigned long int file_offset = 0, file_size = 0, vromb_offset = 0;
	static unsigned char hw_type = 0, use_pcm = 0;
	static int file_cnt = 0;

	const char* path = (const char*)sd->user;
	if (!path) return 0;
	const char* romset = strrchr(path, '/');
	if (!romset) return 0;
	romset++;

	switch (evt)
	{
	case XML_EVENT_START_NODE:
		if (!strcasecmp(node->tag, "romset")) {
			file_cnt = 0;
			vromb_offset = 0;
			use_pcm = 1;
			hw_type = 0;
			if (!romsets) in_correct_romset = 1;
			for (int i = 0; i < node->n_attributes; i++) {
				if (!strcasecmp(node->attributes[i].name, "name")) {
					if (!romsets || !strcasecmp(node->attributes[i].value, romset)) {
						printf("Romset %s found !\n", romset);
						in_correct_romset = 1;
					} else {
						in_correct_romset = 0;
					}
				} else if (!strcasecmp(node->attributes[i].name, "hw")) {
					hw_type = atoi(node->attributes[i].value);
				} else if (!strcasecmp(node->attributes[i].name, "pcm")) {
					use_pcm = atoi(node->attributes[i].value);
				} else if (!strcasecmp(node->attributes[i].name, "vromb_offset")) {
					vromb_offset = strtoul(node->attributes[i].value, NULL, 0);
					use_pcm = 0;
				}
			}
		}
		if (in_correct_romset) {
			if (!strcasecmp(node->tag, "file")) {
				file_offset = 0;
				file_size = 0;
				file_type = NEO_FILE_RAW;
				file_index = 0;

				int use_index = 0;
				for (int i = 0; i < node->n_attributes; i++) {
					if (!strcasecmp(node->attributes[i].name, "index")) use_index = 1;
				}

				printf("using index = %d\n", use_index);

				for (int i = 0; i < node->n_attributes; i++) {
					if (!strcasecmp(node->attributes[i].name, "name"))
						strncpy(file_name, node->attributes[i].value, 16);

					if (use_index) {
						if (!strcasecmp(node->attributes[i].name, "index"))
						{
							file_index = atoi(node->attributes[i].value);
							if (file_index >= 64 || file_index == 15) file_type = NEO_FILE_SPR;
							else if (file_index == 2 || file_index == 8) file_type = NEO_FILE_FIX;
						}
					}
					else
					{
						if (!strcasecmp(node->attributes[i].name, "type")) {
							switch (*node->attributes[i].value)
							{
							case 'C':
								file_index = 15;
								file_type = NEO_FILE_SPR;
								break;

							case 'M':
								file_index = 9;
								break;

							case 'P':
								file_index = 4;
								break;

							case 'S':
								file_index = 8;
								file_type = NEO_FILE_FIX;
								break;

							case 'V':
								file_index = 16;
								break;
							}
						}
					}

					if (!strcasecmp(node->attributes[i].name, "offset"))
						file_offset = strtol(node->attributes[i].value, NULL, 0);

					if (!strcasecmp(node->attributes[i].name, "size"))
						file_size = strtol(node->attributes[i].value, NULL, 0);
				}
				in_file = 1;
				file_cnt++;
			}
		}
		break;

	case XML_EVENT_END_NODE:
		if (in_correct_romset) {
			if (!strcasecmp(node->tag, "romset"))
			{
				if (!file_cnt)
				{
					printf("No parts specified. Trying to load known files:\n");
					neogeo_file_tx(path, "prom", NEO_FILE_RAW, 4, 0, 0);
					neogeo_file_tx(path, "p1rom", NEO_FILE_RAW, 4, 0, 0);
					neogeo_file_tx(path, "p2rom", NEO_FILE_RAW, 6, 0, 0);
					neogeo_file_tx(path, "srom", NEO_FILE_FIX, 8, 0, 0);
					neogeo_file_tx(path, "crom0", NEO_FILE_SPR, 15, 0, 0);
					neogeo_file_tx(path, "m1rom", NEO_FILE_RAW, 9, 0, 0);
					if (vromb_offset)
					{
						neogeo_file_tx(path, "vroma0", NEO_FILE_RAW, 16, 0, vromb_offset);
						neogeo_file_tx(path, "vroma0", NEO_FILE_RAW, 48, vromb_offset, 0);
					}
					else
					{
						neogeo_file_tx(path, "vroma0", NEO_FILE_RAW, 16, 0, 0);
						if(!use_pcm) neogeo_file_tx(path, "vromb0", NEO_FILE_RAW, 48, 0, 0);
					}
				}
				printf("Setting cart hardware type to %u\n", hw_type);
				user_io_8bit_set_status(((uint32_t)hw_type & 3) << 24, 0x03000000);
				printf("Setting cart to%s use the PCM chip\n", use_pcm ? "" : " not");
				user_io_8bit_set_status(((uint32_t)use_pcm & 1) << 26, 0x04000000);
				return 0;
			} else if (!strcasecmp(node->tag, "file")) {
				if (in_file)
					neogeo_file_tx(path, file_name, file_type, file_index, file_offset, file_size);
				in_file = 0;
			}
		}
		break;

	case XML_EVENT_ERROR:
		printf("XML parse: %s: ERROR %d\n", text, n);
		break;
	default:
		break;
	}

	return true;
}

int neogeo_romset_tx(char* name)
{
	char *romset = strrchr(name, '/');
	if (!romset) return 0;
	romset++;

	int system_type;
	static char full_path[1024];

	system_type = (user_io_8bit_set_status(0, 0) >> 1) & 3;
	printf("System type: %u\n", system_type);

	user_io_8bit_set_status(1, 1);	// Maintain reset

	// Look for the romset's file list in romsets.xml
	if (!(system_type & 2))
	{
		sprintf(full_path, "%s/%s/romset.xml", getRootDir(), name);
		if (access(full_path, F_OK)) sprintf(full_path, "%s/%s/romsets.xml", getRootDir(), HomeDir);

		printf("xml for %s: %s\n", name, full_path);

		SAX_Callbacks sax;
		SAX_Callbacks_init(&sax);

		checked_ok = false;
		romsets = 0;
		sax.all_event = xml_check_files;
		XMLDoc_parse_file_SAX(full_path, &sax, name);
		if (!checked_ok) return 0;

		sax.all_event = xml_load_files;
		XMLDoc_parse_file_SAX(full_path, &sax, name);
	}

	// Load system ROMs
	if (strcmp(romset, "debug")) {
		// Not loading the special 'debug' romset
		if (!(system_type & 2)) {
			sprintf(full_path, "%s/uni-bios.rom", HomeDir);
			if (FileExists(full_path)) {
				// Autoload Unibios for cart systems if present
				neogeo_file_tx(HomeDir, "uni-bios.rom", NEO_FILE_RAW, 0, 0, 0x20000);
			} else {
				// Otherwise load normal system roms
				if (system_type == 0)
					neogeo_file_tx(HomeDir, "neo-epo.sp1", NEO_FILE_RAW, 0, 0, 0x20000);
				else
					neogeo_file_tx(HomeDir, "sp-s2.sp1", NEO_FILE_RAW, 0, 0, 0x20000);
			}
		} else if (system_type == 2) {
			// NeoGeo CD
			neogeo_file_tx(HomeDir, "top-sp1.bin", NEO_FILE_RAW, 0, 0, 0x80000);
		} else {
			// NeoGeo CDZ
			neogeo_file_tx(HomeDir, "neocd.bin", NEO_FILE_RAW, 0, 0, 0x80000);
		}
	}

	if (!(system_type & 2))	neogeo_file_tx(HomeDir, "sfix.sfix", NEO_FILE_FIX, 2, 0, 0x10000);
	neogeo_file_tx(HomeDir, "000-lo.lo", NEO_FILE_8BIT, 1, 0, 0x10000);

	if (!strcmp(romset, "kof95"))
	{
		printf("Enabled sprite gfx gap hack for kof95\n");
		user_io_8bit_set_status(0x10000000, 0x30000000);
	}
	else if (!strcmp(romset, "whp"))
	{
		printf("Enabled sprite gfx gap hack for whp\n");
		user_io_8bit_set_status(0x20000000, 0x30000000);
	}
	else if (!strcmp(romset, "kizuna"))
	{
		printf("Enabled sprite gfx gap hack for kizuna\n");
		user_io_8bit_set_status(0x30000000, 0x30000000);
	}
	else
	{
		user_io_8bit_set_status(0x00000000, 0x30000000);
	}

	FileGenerateSavePath((system_type & 2) ? "ngcd" : name, (char*)full_path);
	user_io_file_mount((char*)full_path, 2, 1);

	user_io_8bit_set_status(0, 1);	// Release reset

	return 1;
}
