#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>

#include "../../sxmlc.h"
#include "../../user_io.h"
#include "../../input.h"
#include "../../file_io.h"
#include "../../menu.h"
#include "../../fpga_io.h"
#include "../../lib/md5/md5.h"
#include "../../shmem.h"

#include "buffer.h"
#include "mra_loader.h"

#define kBigTextSize 1024
struct arc_struct {
	char md5[kBigTextSize];
	char zipname[kBigTextSize];
	char partzipname[kBigTextSize];
	char partname[kBigTextSize];
	char romname[kBigTextSize];
	char error_msg[kBigTextSize];
	int romindex;
	int offset;
	int length;
	int repeat;
	int insiderom;
	int patchaddr;
	int dataop;
	int validrom0;
	int insidesw;
	int insideinterleave;
	int ifrom;
	int ito;
	int imap;
	int file_size;
	uint32_t address;
	uint32_t crc;
	buffer_data *data;
	struct MD5Context context;
};

static char arcade_error_msg[kBigTextSize] = {};
static char arcade_root[kBigTextSize];
static char mame_root[kBigTextSize];

static sw_struct switches[2] = {};

static int  nvram_idx  = 0;
static int  nvram_size = 0;
static char nvram_name[200] = {};

void arcade_nvm_save()
{
	if(nvram_idx && nvram_size)
	{
		char path[256] = CONFIG_DIR"/nvram/";
		FileCreatePath(path);
		strcat(path, nvram_name);

		uint8_t *buf = new uint8_t[nvram_size];
		if (buf)
		{
			printf("Request for nvram (idx=%d, size=%d) data\n", nvram_idx, nvram_size);

			user_io_set_index(nvram_idx);
			user_io_set_upload(1);
			user_io_file_rx_data(buf, nvram_size);
			user_io_set_upload(0);

			FileSave(path, buf, nvram_size);
			delete(buf);
		}
	}
}

static void arcade_nvm_load()
{
	if (nvram_idx && nvram_size)
	{
		char path[256] = "nvram/";
		uint8_t *buf = new uint8_t[nvram_size];
		if (buf)
		{
			memset(buf, 0, nvram_size);

			strcat(path, nvram_name);
			if (FileLoadConfig(path, buf, nvram_size))
			{
				printf("Sending nvram (idx=%d, size=%d) to core\n", nvram_idx, nvram_size);
				user_io_set_index(nvram_idx);
				user_io_set_download(1);
				user_io_file_tx_data(buf, nvram_size);
				user_io_set_download(0);
			}

			delete(buf);
		}
	}
}

sw_struct *arcade_sw(int n)
{
	if (n > 1) n = 1;
	if (n < 0) n = 0;
	return &switches[n];
}

void arcade_sw_send(int n)
{
	sw_struct *sw = arcade_sw(n);
	if (sw->dip_num)
	{
		user_io_set_index(254 + n);
		user_io_set_download(1);
		user_io_file_tx_data((uint8_t*)&sw->dip_cur, sizeof(sw->dip_cur));
		user_io_set_download(0);
	}
}

void arcade_sw_save(int n)
{
	sw_struct *sw = arcade_sw(n);
	if (sw->dip_num && sw->dip_saved != sw->dip_cur)
	{
		static char path[1024];
		strcpy(path, (n) ? CONFIG_DIR"/cheats/" : CONFIG_DIR"/dips/");
		FileCreatePath(path);
		strcat(path, sw->name);
		if (FileSave(path, &sw->dip_cur, sizeof(sw->dip_cur)))
		{
			sw->dip_saved = sw->dip_cur;
		}
	}
}

void arcade_sw_load(int n)
{
	sw_struct *sw = arcade_sw(n);
	static char path[1024];
	strcpy(path, (n) ? "cheats/" : "dips/");
	strcat(path, sw->name);
	FileLoadConfig(path, &sw->dip_cur, sizeof(sw->dip_cur));
}

static void set_arcade_root(const char *path)
{
	strcpy(arcade_root, path);
	char *p = strstr(arcade_root, "/_");
	if (p) p = strchr(p + 1, '/');

	if (p) *p = 0;
	else strcpy(arcade_root, getRootDir());

	printf("arcade_root %s\n", arcade_root);

	strcpy(mame_root, "mame");
	if (findPrefixDir(mame_root, sizeof(mame_root)))
	{
		char *p = strrchr(mame_root, '/');
		if (p) *p = 0;
		else mame_root[0] = 0;
	}
	else
	{
		strcpy(mame_root, arcade_root);
	}

	printf("mame_root %s\n", mame_root);
}

static const char *get_arcade_root(int rbf)
{
	static char path[kBigTextSize];

	if (!rbf) strcpy(path, mame_root);
	else sprintf(path, "%s/cores", arcade_root);

	return path;
}

static int      unitlen = 0;
static int      romlen[8] = {};
static int      romblkl = 0;
static uint8_t* romdata = 0;
static uint8_t  romindex = 0;

static void rom_start(unsigned char index)
{
	romindex = index;
	if (romdata) free(romdata);
	romdata = 0;
	memset(romlen, 0, sizeof(romlen));
	romblkl = 0;
	unitlen = 1;
}

#define BLKL (1024*1024)
static int rom_checksz(int idx, int chunk)
{
	if ((romlen[idx] + chunk) > romblkl)
	{
	        if (romlen[idx] + chunk > romblkl + BLKL)
		  romblkl += (chunk + BLKL);
		else
		  romblkl += BLKL;
		romdata = (uint8_t*)realloc(romdata, romblkl);
		if (!romdata)
		{
			printf("realloc failed - romblkl %d \n",romblkl);
			romblkl = 0;
			memset(romlen, 0, sizeof(romlen));
			return 0;
		}
	}
	return 1;
}

static int rom_data(const uint8_t *buf, int chunk, int map, struct MD5Context *md5context)
{
	uint8_t offsets[8]; // assert (unitlen <= 8)
	int bytes_in_iter = 0;

	if (md5context) MD5Update(md5context, buf, chunk);

	int idx = 0;
	if (!map) map = 1;

	int map_reg = map;
	for (int i = 0; i < unitlen; i++)
	{
		if (map_reg & 0xf)
			break;
		map_reg >>= 4;
		idx++;
	}

	if (idx >= unitlen)
		return 0; // illegal map
	if (!rom_checksz(idx, chunk*unitlen))
		return 0;

	map_reg = map;
	for (int i = 0; i < unitlen; i++)
	{
		if (map_reg & 0xf)
		{
			offsets[bytes_in_iter] = idx + (map_reg & 0xf) - 1;
			bytes_in_iter++;
		}
		map_reg >>= 4;
	}

	while (chunk)
	{
		for (int i = 0; i < bytes_in_iter; i++)
		{
			*(romdata + romlen[idx] + offsets[i]) = *buf++;
			chunk--;
		}
		romlen[idx] += unitlen;
	}

	return 1;
}

static int rom_file(const char *name, uint32_t crc32, int start, int len, int map, struct MD5Context *md5context)
{
	fileTYPE f = {};
	static uint8_t buf[8192];
	if (!FileOpenZip(&f, name, crc32)) return 0;
	if (start) FileSeek(&f, start, SEEK_SET);
	unsigned long bytes2send = f.size - f.offset;
	if (len > 0 && len < (int)bytes2send) bytes2send = len;

	while (bytes2send)
	{
		uint16_t chunk = (bytes2send > sizeof(buf)) ? sizeof(buf) : bytes2send;

		FileReadAdv(&f, buf, chunk);
		if (!rom_data(buf, chunk, map, md5context))
		{
			FileClose(&f);
			return 0;
		};

		bytes2send -= chunk;
	}

	FileClose(&f);
	return 1;
}

static int rom_patch(const uint8_t *buf, int offset, uint16_t len, int dataop)
{
	if ((offset + len) > romlen[0]) return 0;
	if (!dataop)
	{
		memcpy(romdata + offset, buf, len);
	}
	else
	{
		for (int i = 0; i < len; i++) romdata[offset + i] ^= buf[i];
	}

	return 1;
}

static void rom_finish(int send, uint32_t address, int index)
{
	if (romlen[0] && romdata)
	{
		if (send)
		{
			uint8_t *data = romdata;
			int len = romlen[0];

			// set index byte (0=bios rom, 1-n=OSD entry index)
			user_io_set_index(romindex);

			// prepare transmission of new file
			user_io_set_download(1, address ? len : 0);

			if (address)
			{
				shmem_put(fpga_mem(address), len, data);
			}
			else
			{
				char str[32];
				sprintf(str, "ROM #%d", index);

				ProgressMessage(0, 0, 0, 0);
				while (romlen[0] > 0)
				{
					ProgressMessage("Sending", str, len - romlen[0], len);

					uint32_t chunk = (romlen[0] > 4096) ? 4096 : romlen[0];
					user_io_file_tx_data(data, chunk);

					romlen[0] -= chunk;
					data += chunk;
				}
				ProgressMessage(0, 0, 0, 0);
			}

			// signal end of transmission
			user_io_set_download(0);
			printf("file_finish: 0x%X bytes sent to FPGA\n\n", len);
		}
		else
		{
			printf("file_finish: discard the ROM\n\n");
		}
		free(romdata);
		romdata = 0;
		return;
	}

	if (romdata)
	{
		free(romdata);
		romdata = 0;
	}
	printf("file_finish: no data, discarded\n\n");
}


/*
 * adapted from https://gist.github.com/xsleonard/7341172
 *
 * hexstr_to_char will take hex strings in two types:
 *
 * 00 01 02
 * 000102
 * 00 01 2 03
 * 0001 0203
 *
 * and return an array and a length of binary values
 *
 * caller must free string that is returned
 *
 * */
unsigned char* hexstr_to_char(const char* hexstr, size_t *out_len)
{
	size_t len = strlen(hexstr);
	unsigned char* chrs = (unsigned char*)malloc(len + 1);
	if (!chrs)
		printf("hexstr_to_char: malloc failed len+1=%d\n",len+1);
	int dest = 0;
	// point to the beginning of the array
	const char *ptr = hexstr;
	while (*ptr) {
		// check to see if we have a space
		while (*ptr == '\n' || *ptr == '\r' || *ptr == ' ' || *ptr == ',' || *ptr == '\t' || *ptr == 9 /*horiz tab*/) ptr++;
		if (*ptr == 0) break;

		// pull two characters off
		int val1 = (*ptr % 32 + 9) % 25 * 16;
		ptr++;
		/* check to odd numbers of characters*/
		if (*ptr == 0) {
			int val = (ptr[-1] % 32 + 9) % 25;
			chrs[dest++] = val;
			break;
		}
		int val2 = (*ptr % 32 + 9) % 25;
		ptr++;
		chrs[dest++] = val1 + val2;
	}
	chrs[dest] = 0;
	*out_len = dest; /* dest is 0 based, so we don't need to subtract 1*/
	return chrs;
}

/*
 *  xml_send_rom
 *
 *  This is a callback from the XML parser of the MRA file
 *
 *  It parses the MRA, and sends commands to send rom parts to the fpga
 * */
static int xml_send_rom(XMLEvent evt, const XMLNode* node, SXML_CHAR* text, const int n, SAX_Data* sd)
{
	static char message[32];
	struct arc_struct *arc_info = (struct arc_struct *)sd->user;

	switch (evt)
	{
	case XML_EVENT_START_DOC:
		message[0] = 0;
		arc_info->insiderom = 0;
		arc_info->insidesw = 0;
		break;

	case XML_EVENT_START_NODE:

		/* initialization */
		// initialize things for each tag (node):
		buffer_destroy(arc_info->data);
		arc_info->data = buffer_init(kBigTextSize);
		arc_info->partname[0] = 0;
		arc_info->offset = 0;
		arc_info->length = -1;
		arc_info->repeat = 1;
		arc_info->crc = 0;

		/* on the beginning of a rom tag, we need to reset the state*/
		if (!strcasecmp(node->tag, "rom"))
		{
			arc_info->insiderom = 1;
			arc_info->romname[0] = 0;
			arc_info->romindex = 0;
			arc_info->md5[0] = 0;
			arc_info->ifrom = 0;
			arc_info->ito = 0;
			arc_info->imap = 0;
			arc_info->zipname[0] = 0;
			arc_info->address = 0;
			arc_info->insideinterleave = 0;
			MD5Init(&arc_info->context);
			ProgressMessage(0, 0, 0, 0);
		}

		if (!strcasecmp(node->tag, "switches"))
		{
			arc_info->insidesw = 1;
			switches[0].dip_cur = 0;
			switches[0].dip_def = 0;
			switches[0].dip_num = 0;
			memset(&switches[0].dip, 0, sizeof(switches[0].dip));
		}

		if (!strcasecmp(node->tag, "cheats"))
		{
			arc_info->insidesw = 2;
			switches[1].dip_cur = 0;
			switches[1].dip_def = 0;
			switches[1].dip_num = 0;
			memset(&switches[1].dip, 0, sizeof(switches[1].dip));
		}

		if (!strcasecmp(node->tag, "interleave"))
		{
			arc_info->insideinterleave = 1;
			arc_info->ifrom = 8; // default 8.
			arc_info->ito = 0;
			arc_info->imap = 0;
		}

		// for each part tag, we clear the partzipname since it is optional and may not appear in the part tag
		if (!strcasecmp(node->tag, "part"))
		{
			arc_info->partzipname[0] = 0;
			arc_info->imap = 0;
		}

		if (!strcasecmp(node->tag, "patch"))
		{
			arc_info->patchaddr = 0;
			arc_info->dataop = 0;
		}

		//printf("XML_EVENT_START_NODE: tag [%s]\n",node->tag);
		// walk the attributes and save them in the data structure as appropriate
		for (int i = 0; i < node->n_attributes; i++)
		{
			//printf("attribute %d name [%s] value [%s]\n",i,node->attributes[i].name,node->attributes[i].value);
			if (!strcasecmp(node->attributes[i].name, "zip") && !strcasecmp(node->tag, "rom"))
			{
				strcpy(arc_info->zipname, node->attributes[i].value);
			}
			if (!strcasecmp(node->attributes[i].name, "name") && !strcasecmp(node->tag, "rom"))
			{
				strcpy(arc_info->romname, node->attributes[i].value);
			}
			if (!strcasecmp(node->attributes[i].name, "md5") && !strcasecmp(node->tag, "rom"))
			{
				strcpy(arc_info->md5, node->attributes[i].value);
			}
			if (!strcasecmp(node->attributes[i].name, "index") && !strcasecmp(node->tag, "rom"))
			{
				arc_info->romindex = atoi(node->attributes[i].value);
				sprintf(message, "Assembling ROM #%d", arc_info->romindex);
			}
			if (!strcasecmp(node->attributes[i].name, "address") && !strcasecmp(node->tag, "rom"))
			{
				arc_info->address = strtoul(node->attributes[i].value, NULL, 0);
			}

			if (!strcasecmp(node->attributes[i].name, "names") && !strcasecmp(node->tag, "buttons"))
			{
				set_ovr_buttons(node->attributes[i].value, 0);
			}

			if (!strcasecmp(node->attributes[i].name, "default") && !strcasecmp(node->tag, "buttons"))
			{
				set_ovr_buttons(node->attributes[i].value, 1);
			}

			/* these only exist if we are inside the rom tag, and in a part tag*/
			if (arc_info->insiderom)
			{
				if (!strcasecmp(node->attributes[i].name, "input") && !strcasecmp(node->tag, "interleave"))
				{
					arc_info->ifrom = strtol(node->attributes[i].value, NULL, 0);
				}
				if (!strcasecmp(node->attributes[i].name, "output") && !strcasecmp(node->tag, "interleave"))
				{
					arc_info->ito = strtol(node->attributes[i].value, NULL, 0);
				}

				if (!strcasecmp(node->attributes[i].name, "zip") && !strcasecmp(node->tag, "part"))
				{
					strcpy(arc_info->partzipname, node->attributes[i].value);
				}
				if (!strcasecmp(node->attributes[i].name, "name") && !strcasecmp(node->tag, "part"))
				{
					strcpy(arc_info->partname, node->attributes[i].value);
				}
				if (!strcasecmp(node->attributes[i].name, "offset") && !strcasecmp(node->tag, "part"))
				{
					arc_info->offset = strtoul(node->attributes[i].value, NULL, 0);
				}
				if (!strcasecmp(node->attributes[i].name, "length") && !strcasecmp(node->tag, "part"))
				{
					arc_info->length = strtoul(node->attributes[i].value, NULL, 0);
				}
				if (!strcasecmp(node->attributes[i].name, "repeat") && !strcasecmp(node->tag, "part"))
				{
					arc_info->repeat = strtoul(node->attributes[i].value, NULL, 0);
				}
				if (!strcasecmp(node->attributes[i].name, "crc") && !strcasecmp(node->tag, "part"))
				{
					arc_info->crc = strtoul(node->attributes[i].value, NULL, 16);
				}
				if (!strcasecmp(node->attributes[i].name, "offset") && !strcasecmp(node->tag, "patch"))
				{
					arc_info->patchaddr = strtoul(node->attributes[i].value, NULL, 0);
				}
				if (!strcasecmp(node->attributes[i].name, "operation") && !strcasecmp(node->tag, "patch"))
				{
					if (!strcasecmp(node->attributes[i].value, "xor")) arc_info->dataop = 1;
				}
				if (!strcasecmp(node->attributes[i].name, "map") && !strcasecmp(node->tag, "part"))
				{
					arc_info->imap = strtoul(node->attributes[i].value, NULL, 16);
					if (!arc_info->insideinterleave && arc_info->imap)
					{
						unitlen = strlen(node->attributes[i].value);
						if (unitlen > 8) unitlen = 8;
						for (int i = 1; i < 8; i++) romlen[i] = romlen[0];
					}
				}
			}
			else if (arc_info->insidesw)
			{
				sw_struct* sw = &switches[arc_info->insidesw - 1];
				if (!strcasecmp(node->tag, "switches") || !strcasecmp(node->tag, "cheats"))
				{
					if (!strcasecmp(node->attributes[i].name, "default"))
					{
						size_t len = 0;
						unsigned char* binary = hexstr_to_char(node->attributes[i].value, &len);
						for (size_t i = 0; i < len; i++) sw->dip_def |= binary[i] << (i * 8);

						free(binary);
					}
				}

				if (!strcasecmp(node->tag, "dip"))
				{
					if (!strcasecmp(node->attributes[i].name, "name"))
					{
						snprintf(sw->dip[sw->dip_num].name, sizeof(sw->dip[sw->dip_num].name), node->attributes[i].value);
					}

					if (!strcasecmp(node->attributes[i].name, "bits"))
					{
						int b = 0, e = 0;
						int num = sscanf(node->attributes[i].value, "%d,%d", &b, &e);
						if (num <= 0 || b < 0 || b > 63 || e < 0 || e > 63 || (num == 2 && e < b))
						{
							printf("Invalid bits field: ""%s"" (%d, %d, %d)\n", node->attributes[i].value, num, b, e);
						}
						else
						{
							uint64_t mask = 1;
							if (num == 1) e = b;
							sw->dip[sw->dip_num].start = b;
							for (int i = 0; i < (e - b); i++) mask = (mask << 1) | 1;
							sw->dip[sw->dip_num].mask = mask << b;
							sw->dip[sw->dip_num].size = e - b + 1;
						}
					}

					if (!strcasecmp(node->attributes[i].name, "ids"))
					{
						int n = 0;
						char *val = node->attributes[i].value;
						while (*val && n < 32)
						{
							char *p = strchr(val, ',');
							size_t len = p ? p - val : strlen(val);
							size_t sz = len + 1;
							if (sz > sizeof(sw->dip[0].id[0])) sz = sizeof(sw->dip[0].id[0]);
							snprintf(sw->dip[sw->dip_num].id[n], sz, val);
							val += len;
							if (*val == ',') val++;
							n++;
						}
						sw->dip[sw->dip_num].num = n;
					}

					if (!strcasecmp(node->attributes[i].name, "values"))
					{
						int n = 0;
						char *val = node->attributes[i].value;
						while (*val && n < 32)
						{
							char *endp = 0;
							uint64_t v = strtoul(val, &endp, 0);
							if (endp <= val)
							{
								printf("Invalid values field: ""%s""\n", node->attributes[i].value);
								break;
							}

							sw->dip[sw->dip_num].val[n] = v;
							val = endp;
							while (*val && (*val == ' ' || *val == ',')) val++;
							n++;
						}
						sw->dip[sw->dip_num].has_val = 1;
					}
				}
			}
			else
			{
				if (!strcasecmp(node->attributes[i].name, "index") && !strcasecmp(node->tag, "nvram"))
				{
					nvram_idx = strtoul(node->attributes[i].value, NULL, 0);
				}

				if (!strcasecmp(node->attributes[i].name, "size") && !strcasecmp(node->tag, "nvram"))
				{
					nvram_size = strtoul(node->attributes[i].value, NULL, 0);
				}
			}
		}

		/* at the beginning of each rom - tell the user_io to start a new message */
		if (!strcasecmp(node->tag, "rom"))
		{

			// clear an error message if we have a second rom0
			// this is kind of a problem - you will never see the
			// error from the first rom0?
			//
			if (arc_info->romindex == 0 && strlen(arc_info->zipname))
				arc_info->error_msg[0] = 0;

			rom_start(arc_info->romindex);
		}

		if (arc_info->insiderom && !strcasecmp(node->tag, "interleave"))
		{
			int valid = 1;
			if (arc_info->ifrom != 8) valid = 0;
			if (arc_info->ito < 8 || arc_info->ito>64 || (arc_info->ito & 7)) valid = 0;
			if (arc_info->ito < arc_info->ifrom) valid = 0;

			unitlen = arc_info->ifrom ? arc_info->ito / arc_info->ifrom : 1;
			if (unitlen < 0 && unitlen>8) valid = 0;

			if (!valid)
			{
				printf("Invalid interleave format (from=%d to %d)!\n", arc_info->ifrom, arc_info->ito);

				arc_info->ifrom = 0;
				arc_info->ito = 0;
				arc_info->imap = 0;
				unitlen = 1;
			}
			else
			{
				printf("Using interleave: input %d, output %d\n", arc_info->ifrom, arc_info->ito);
			}

			for (int i = 1; i < 8; i++) romlen[i] = romlen[0];
		}

		ProgressMessage("Loading", message, ftell(sd->file), arc_info->file_size);
		break;

	case XML_EVENT_TEXT:
		/* the text node is the data between tags, ie:  <part>this text</part>
		 *
		 * the buffer_append is part of a buffer library that will realloc automatically
		 */
		{
			int result = buffer_append(arc_info->data, text);
			if (result<0)
				printf("buffer_append failed %d\n",result);
			if (result==-1)
				printf("-1 no data given\n");
			if (result==-2)
				printf("-2 could not allocate\n");
		}
		//printf("XML_EVENT_TEXT: text [%s]\n",text);
		break;

	case XML_EVENT_END_NODE:
		//printf("XML_EVENT_END_NODE: tag [%s]\n",node->tag );

		// At the end of a rom node (when it is closed) we need to calculate hash values and clean up
		if (!strcasecmp(node->tag, "rom"))
		{
			message[0] = 0;

			if (arc_info->insiderom)
			{
				unsigned char checksum[16];
				MD5Final(checksum, &arc_info->context);

				char hex[40];
				char *p = hex;
				for (int i = 0; i < 16; i++)
				{
					sprintf(p, "%02x", (unsigned int)checksum[i]);
					p += 2;
				}

				int checksumsame = !strlen(arc_info->zipname) || !strcasecmp(arc_info->md5, hex);
				if (checksumsame == 0)
				{
					printf("\n*** Checksum mismatch\n");
					printf("    md5-orig = %s\n", arc_info->md5);
					printf("    md5-calc = %s\n\n", hex);
				}

				checksumsame |= !strcasecmp(arc_info->md5, "none");
				if (checksumsame == 0)
				{
					if (!strlen(arc_info->error_msg))
						snprintf(arc_info->error_msg, kBigTextSize, "md5 mismatch for rom %d", arc_info->romindex);
				}
				else
				{
					// this code sets the validerom0 and clears the message
					// if a rom with index 0 has a correct md5. It supresses
					// sending any further rom0 messages
					if (arc_info->romindex == 0)
					{
						arc_info->validrom0 = 1;
						arc_info->error_msg[0] = 0;
					}
				}

				rom_finish(checksumsame, arc_info->address, arc_info->romindex);
			}
			arc_info->insiderom = 0;
		}

		// At the end of a part node, send the rom part if we are inside a rom tag
		//int user_io_file_tx_body_filepart(const char *name,int start, int len)
		if (!strcasecmp(node->tag, "part") && arc_info->insiderom)
		{
			// suppress rom0 if we already sent a valid one
			// this is useful for merged rom sets - if the first one was valid, use it
			// the second might not be
			if (arc_info->romindex == 0 && arc_info->validrom0 == 1) break;
			char fname[kBigTextSize * 2 + 16];
			int start, length, repeat;
			uint32_t crc32;
			repeat = arc_info->repeat;
			start = arc_info->offset;
			crc32 = arc_info->crc;
			length = 0;
			if (arc_info->length > 0) length = arc_info->length;
			//printf("partname[%s]\n",arc_info->partname);
			//printf("zipname [%s]\n",arc_info->zipname);
			//printf("offset[%d]\n",arc_info->offset);
			//printf("length[%d]\n",arc_info->length);
			//printf("repeat[%d]\n",arc_info->repeat);
			//

			if (unitlen == 1 || (arc_info->imap & 0xF)) printf("%6X: ", romlen[0]);
			else printf("        ");

			//user_io_file_tx_body_filepart(getFullPath(fname),0,0);
			if (strlen(arc_info->partname))
			{
				char zipnames_list[kBigTextSize];

				if (strlen(arc_info->partzipname))
				{
					strcpy(zipnames_list, arc_info->partzipname);
				} else {
					strcpy(zipnames_list, arc_info->zipname);
				}

				char *zipname = NULL;
				char *zipptr = zipnames_list;
				const char *root = get_arcade_root(0);
				int result = 0;
				while ((zipname = strsep(&zipptr, "|")) != NULL)
				{
					sprintf(fname, (zipname[0] == '/') ? "%s%s/%s" : "%s/mame/%s/%s", root, zipname, arc_info->partname);

					if(unitlen>1) printf("file: %s, start=%d, len=%d, map(%d)=%X\n", fname, start, length, unitlen, arc_info->imap);
					else printf("file: %s, start=%d, len=%d\n", fname, start, length);

					for (int i = 0; i < repeat; i++)
					{
						result = rom_file(fname, crc32, start, length, arc_info->imap, &arc_info->context);

						// we should check file not found error for the zip
						if (result == 0)
						{
							break;
						}
					}

					if (result)
					{
						break;
					}
				}
				if (result == 0)
				{
					printf("%s does not exist\n", arc_info->partname);
					snprintf(arc_info->error_msg, kBigTextSize, "%s\nFile Not Found", arc_info->partname);
				}
			}
			else // we have binary data?
			{
				size_t len = 0;
				unsigned char* binary = hexstr_to_char(arc_info->data->content, &len);
				int prev_len = romlen[0];
				printf("data: ");
				if (binary)
				{
					for (int i = 0; i < repeat; i++) rom_data(binary, len, arc_info->imap, &arc_info->context);
					free(binary);
				}
				printf("%d(0x%X) bytes from xml\n", romlen[0] - prev_len, romlen[0] - prev_len);
			}

			if (!arc_info->insideinterleave) unitlen = 1;
		}

		if (!strcasecmp(node->tag, "patch") && arc_info->insiderom)
		{
			size_t len = 0;
			unsigned char* binary = hexstr_to_char(arc_info->data->content, &len);
			if (binary)
			{
				rom_patch(binary, arc_info->patchaddr, len, arc_info->dataop);
				free(binary);
			}
		}

		if (arc_info->insidesw && !strcasecmp(node->tag, "dip"))
		{
			sw_struct* sw = &switches[arc_info->insidesw - 1];

			int n = sw->dip_num;
			for (int i = 0; i < sw->dip[n].num; i++)
			{
				sw->dip[n].val[i] = ((sw->dip[n].has_val) ? sw->dip[n].val[i] : i) << sw->dip[n].start;
			}

			if (sw->dip_num < 63) sw->dip_num++;
		}

		if (!strcasecmp(node->tag, "nvram")) arcade_nvm_load();

		if (!strcasecmp(node->tag, "switches"))
		{
			arc_info->insidesw = 0;
		}

		if (!strcasecmp(node->tag, "interleave"))
		{
			arc_info->ifrom = 0;
			arc_info->ito = 0;
			arc_info->imap = 0;
			unitlen = 1;
			arc_info->insideinterleave = 0;
			printf("Disable interleave\n");
		}
		break;

	case XML_EVENT_ERROR:
		printf("XML parse: %s: ERROR %d\n", text, n);
		snprintf(arc_info->error_msg, kBigTextSize, "XML parse: %s: ERROR %d\n", text, n);
		break;
	default:
		break;
	}

	return true;
}

static int xml_scan_rbf(XMLEvent evt, const XMLNode* node, SXML_CHAR* text, const int n, SAX_Data* sd)
{
	static int insiderbf = 0;
	char *rbf = (char *)sd->user;

	switch (evt)
	{
	case XML_EVENT_START_DOC:
		insiderbf = 0;
		break;

	case XML_EVENT_START_NODE:
		if (!strcasecmp(node->tag, "rbf")) insiderbf = 1;
		break;

	case XML_EVENT_TEXT:
		if (insiderbf)
		{
			insiderbf = 0;
			strncpy(rbf, text, kBigTextSize);
		}
		break;

	case XML_EVENT_END_NODE:
		insiderbf = 0;
		break;

	case XML_EVENT_ERROR:
		printf("XML parse: %s: ERROR %d\n", text, n);
		break;
	default:
		break;
	}

	return true;
}

static int xml_read_setname(XMLEvent evt, const XMLNode* node, SXML_CHAR* text, const int n, SAX_Data* sd)
{
	(void)(sd);
	static int insetname = 0;

	switch (evt)
	{
	case XML_EVENT_START_DOC:
		insetname = 0;
		break;

	case XML_EVENT_START_NODE:

		/* on the beginning of a rom tag, we need to reset the state*/
		if (!strcasecmp(node->tag, "setname"))
		{
			insetname = 1;
		}
		break;

	case XML_EVENT_TEXT:
		if(insetname) user_io_name_override(text);
		break;

	case XML_EVENT_END_NODE:
		if (!strcasecmp(node->tag, "setname"))
		{
			insetname = 0;
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

int arcade_send_rom(const char *xml)
{
	const char *p = strrchr(xml, '/');
	p = p ? p + 1 : xml;
	snprintf(switches[0].name, sizeof(switches[0].name), "%s", p);
	char *ext = strcasestr(switches[0].name, ".mra");
	if (ext) strcpy(ext, ".dip");
	memcpy(switches[1].name, switches[0].name, sizeof(switches[1].name));

	snprintf(nvram_name, sizeof(nvram_name), p);
	ext = strcasestr(nvram_name, ".mra");
	if (ext) strcpy(ext, ".nvm");

	SAX_Callbacks sax;
	SAX_Callbacks_init(&sax);

	sax.all_event = xml_send_rom;

	set_arcade_root(xml);

	// create the structure we use for the XML parser
	struct arc_struct arc_info;
	arc_info.data = buffer_init(kBigTextSize);
	arc_info.error_msg[0] = 0;
	arc_info.validrom0 = 0;
	struct stat64 *st = getPathStat(xml);
	if (st) arc_info.file_size = (int)st->st_size;
	ProgressMessage(0, 0, 0, 0);

	// parse
	XMLDoc_parse_file_SAX(xml, &sax, &arc_info);
	if (arc_info.validrom0 == 0 && strlen(arc_info.error_msg))
	{
		strcpy(arcade_error_msg, arc_info.error_msg);
		printf("arcade_send_rom: pretty error: [%s]\n", arcade_error_msg);
	}
	buffer_destroy(arc_info.data);

	for (int n = 0; n < 2; n++)
	{
		switches[n].dip_cur = switches[n].dip_def;
		arcade_sw_load(n);
		switches[n].dip_saved = switches[n].dip_cur;
		arcade_sw_send(n);
	}
	return 0;
}

void arcade_override_name(const char *xml)
{
	SAX_Callbacks sax;
	SAX_Callbacks_init(&sax);

	sax.all_event = xml_read_setname;
	XMLDoc_parse_file_SAX(xml, &sax, NULL);
}

void arcade_check_error()
{
	if (arcade_error_msg[0] != 0) {
		printf("ERROR: [%s]\n", arcade_error_msg);
		Info(arcade_error_msg, 1000 * 30);
		arcade_error_msg[0] = 0;
		sleep(3);
	}
}

static const char *get_rbf(const char *xml)
{
	static char rbfname[kBigTextSize];

	rbfname[0] = 0;
	SAX_Callbacks sax;
	SAX_Callbacks_init(&sax);

	sax.all_event = xml_scan_rbf;
	XMLDoc_parse_file_SAX(xml, &sax, rbfname);

	/* once we have the rbfname fragment from the MRA xml file
	 * search the arcade folder for the match */
	struct dirent *entry;
	DIR *dir;

	const char *dirname = get_arcade_root(1);
	if (!(dir = opendir(dirname)))
	{
		printf("%s directory not found\n", dirname);
		return NULL;
	}

	int len;
	static char lastfound[256] = {};
	while ((entry = readdir(dir)) != NULL)
	{
		len = strlen(entry->d_name);
		if (entry->d_type != DT_DIR && len > 4 && !strcasecmp(entry->d_name+len-4,".rbf"))
		{
			static char newstring[kBigTextSize];
			//printf("entry name: %s\n",entry->d_name);

			snprintf(newstring, kBigTextSize, "Arcade-%s", rbfname);
			len = strlen(newstring);
			if (!strncasecmp(newstring, entry->d_name, len) && (entry->d_name[len] == '.' || entry->d_name[len] == '_'))
			{
				if (!lastfound[0] || strcmp(lastfound, entry->d_name) < 0)
				{
					strcpy(lastfound, entry->d_name);
				}
			}

			snprintf(newstring, kBigTextSize, "%s", rbfname);
			len = strlen(newstring);
			if (!strncasecmp(newstring, entry->d_name, len) && (entry->d_name[len] == '.' || entry->d_name[len] == '_'))
			{
				if (!lastfound[0] || strcmp(lastfound, entry->d_name) < 0)
				{
					strcpy(lastfound, entry->d_name);
				}
			}
		}
	}

	if (lastfound[0]) sprintf(rbfname, "%s/%s", dirname, lastfound);
	closedir(dir);

	return lastfound[0] ? rbfname : NULL;
}

int arcade_load(const char *xml)
{
	MenuHide();
	static char path[kBigTextSize];

	if(xml[0] == '/') strcpy(path, xml);
	else sprintf(path, "%s/%s", getRootDir(), xml);

	set_arcade_root(path);
	printf("arcade_load [%s]\n", path);
	const char *rbf = get_rbf(path);

	if (rbf)
	{
		printf("MRA: %s, RBF: %s\n", path, rbf);
		fpga_load_rbf(rbf, NULL, path);
	}
	else
	{
		Info("No rbf found!");
	}

	return 0;
}
