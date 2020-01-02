#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>

#include "../../sxmlc.h"
#include "../../user_io.h"
#include "../../file_io.h"
#include "../../menu.h"
#include "../../fpga_io.h"
#include "../../lib/md5/md5.h"

#include "buffer.h"
#include "romutils.h"

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
	int validrom0;
	int insidesw;
	buffer_data *data;
	struct MD5Context context;
};

static char arcade_error_msg[kBigTextSize] = {};
static char arcade_root[kBigTextSize];
static char mame_root[kBigTextSize];

static sw_struct switches = {};

sw_struct *arcade_sw()
{
	return &switches;
}

void arcade_sw_send()
{
	if (switches.dip_num)
	{
		user_io_set_index(254);
		user_io_set_download(1);
		user_io_file_tx_write((uint8_t*)&switches.dip_cur, sizeof(switches.dip_cur));
		user_io_set_download(0);
	}
}

void arcade_sw_save()
{
	if (switches.dip_saved != switches.dip_cur)
	{
		char path[256] = CONFIG_DIR"/dips/";
		FileCreatePath(path);
		strcat(path, switches.name);
		if (FileSave(path, &switches.dip_cur, sizeof(switches.dip_cur)))
		{
			switches.dip_saved = switches.dip_cur;
		}
	}
}

void arcade_sw_load()
{
	char path[256] = "dips/";
	strcat(path, switches.name);
	FileLoadConfig(path, &switches.dip_cur, sizeof(switches.dip_cur));
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

static int      romlen = 0;
static int      romblkl = 0;
static uint8_t* romdata = 0;
static uint8_t  romindex = 0;

static void file_start(unsigned char index)
{
	romindex = index;
	if (romdata) free(romdata);
	romdata = 0;
	romlen = 0;
	romblkl = 0;
}

#define BLKL (1024*1024)
static int file_checksz(int chunk)
{
	if ((romlen + chunk) > romblkl)
	{
		romblkl += BLKL;
		romdata = (uint8_t*)realloc(romdata, romblkl);
		if (!romdata)
		{
			romblkl = 0;
			romlen = 0;
			return 0;
		}
	}
	return 1;
}

static int file_data(const uint8_t *buf, uint16_t chunk, struct MD5Context *md5context)
{
	if (!file_checksz(chunk)) return 0;

	memcpy(romdata + romlen, buf, chunk);
	romlen += chunk;

	if (md5context) MD5Update(md5context, buf, chunk);
	return 1;
}

static int file_file(const char *name, int start, int len, struct MD5Context *md5context)
{
	char mute = 0;
	fileTYPE f = {};
	static uint8_t buf[4096];
	if (!FileOpen(&f, name, mute)) return 0;
	if (start) FileSeek(&f, start, SEEK_SET);
	unsigned long bytes2send = f.size;
	if (len > 0 && len < (int)bytes2send) bytes2send = len;

	while (bytes2send)
	{
		uint16_t chunk = (bytes2send > sizeof(buf)) ? sizeof(buf) : bytes2send;

		FileReadAdv(&f, buf, chunk);
		if (!file_data(buf, chunk, md5context))
		{
			FileClose(&f);
			return 0;
		};

		bytes2send -= chunk;
	}

	FileClose(&f);
	return 1;
}

static int file_patch(const uint8_t *buf, int offset, uint16_t len)
{
	if ((offset + len) > romlen) return 0;
	memcpy(romdata + offset, buf, len);
	return 1;
}

static void file_finish(int send)
{
	if (romlen && romdata)
	{
		if (send)
		{
			// set index byte (0=bios rom, 1-n=OSD entry index)
			user_io_set_index(romindex);

			// prepare transmission of new file
			user_io_set_download(1);

			uint8_t *data = romdata;
			int len = romlen;
			while (romlen > 0)
			{
				uint16_t chunk = (romlen > 4096) ? 4096 : romlen;
				user_io_file_tx_write(data, chunk);

				romlen -= chunk;
				data += chunk;
			}

			// signal end of transmission
			user_io_set_download(0);
			printf("file_finish: %d bytes sent to FPGA\n", len);
		}
		else
		{
			printf("file_finish: discard the ROM\n");
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
	printf("file_finish: no data, discarded\n");
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
	struct arc_struct *arc_info = (struct arc_struct *)sd->user;
	(void)(sd);

	switch (evt)
	{
	case XML_EVENT_START_DOC:
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

		/* on the beginning of a rom tag, we need to reset the state*/
		if (!strcasecmp(node->tag, "rom"))
		{
			arc_info->insiderom = 1;
			arc_info->romname[0] = 0;
			arc_info->romindex = 0;
			arc_info->md5[0] = 0;
			MD5Init(&arc_info->context);
		}

		if (!strcasecmp(node->tag, "switches"))
		{
			arc_info->insidesw = 1;
			switches.dip_cur = 0;
			switches.dip_def = 0;
			switches.dip_num = 0;
			memset(&switches.dip, 0, sizeof(switches.dip));
		}

		// for each part tag, we clear the partzipname since it is optional and may not appear in the part tag
		if (!strcasecmp(node->tag, "part"))
			arc_info->partzipname[0] = 0;

		if (!strcasecmp(node->tag, "patch"))
			arc_info->patchaddr = 0;

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
			}
			/* these only exist if we are inside the rom tag, and in a part tag*/
			if (arc_info->insiderom)
			{
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
					arc_info->offset = atoi(node->attributes[i].value);
				}
				if (!strcasecmp(node->attributes[i].name, "length") && !strcasecmp(node->tag, "part"))
				{
					arc_info->length = atoi(node->attributes[i].value);
				}
				if (!strcasecmp(node->attributes[i].name, "repeat") && !strcasecmp(node->tag, "part"))
				{
					arc_info->repeat = atoi(node->attributes[i].value);
				}
				if (!strcasecmp(node->attributes[i].name, "offset") && !strcasecmp(node->tag, "patch"))
				{
					arc_info->patchaddr = strtoul(node->attributes[i].value, NULL, 0);
				}
			}

			if (arc_info->insidesw)
			{
				if (!strcasecmp(node->tag, "switches"))
				{
					if (!strcasecmp(node->attributes[i].name, "default"))
					{
						size_t len = 0;
						unsigned char* binary = hexstr_to_char(node->attributes[i].value, &len);
						for (size_t i = 0; i < len; i++) switches.dip_def |= binary[i] << (i * 8);
						free(binary);
					}
				}

				if (!strcasecmp(node->tag, "dip"))
				{
					if (!strcasecmp(node->attributes[i].name, "name"))
					{
						snprintf(switches.dip[switches.dip_num].name, sizeof(switches.dip[switches.dip_num].name), node->attributes[i].value);
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
							switches.dip[switches.dip_num].start = b;
							for (int i = 0; i < (e - b); i++) mask = (mask << 1) | 1;
							switches.dip[switches.dip_num].mask = mask << b;
							switches.dip[switches.dip_num].size = e - b + 1;
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
							if (sz > sizeof(switches.dip[0].id[0])) sz = sizeof(switches.dip[0].id[0]);
							snprintf(switches.dip[switches.dip_num].id[n], sz, val);
							val += len;
							if (*val == ',') val++;
							n++;
						}
						switches.dip[switches.dip_num].num = n;
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

							switches.dip[switches.dip_num].val[n] = v;
							val = endp;
							while (*val && (*val == ' ' || *val == ',')) val++;
							n++;
						}
						switches.dip[switches.dip_num].has_val = 1;
					}
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

			file_start(arc_info->romindex);
		}
		break;

	case XML_EVENT_TEXT:
		/* the text node is the data between tags, ie:  <part>this text</part>
		 *
		 * the buffer_append is part of a buffer library that will realloc automatically
		 */
		buffer_append(arc_info->data, text);
		//printf("XML_EVENT_TEXT: text [%s]\n",text);
		break;

	case XML_EVENT_END_NODE:
		//printf("XML_EVENT_END_NODE: tag [%s]\n",node->tag );

		// At the end of a rom node (when it is closed) we need to calculate hash values and clean up
		if (!strcasecmp(node->tag, "rom")) {
			if (arc_info->insiderom)
			{
				unsigned char checksum[16];
				int checksumsame = 1;
				char *md5 = arc_info->md5;
				MD5Final(checksum, &arc_info->context);
				if (*md5)
				{
					printf("md5[%s]\n", arc_info->md5);
					printf("md5-calc[");
					for (int i = 0; i < 16; i++)
					{
						char hex[10];
						snprintf(hex, 10, "%02x", (unsigned int)checksum[i]);
						printf("%02x", (unsigned int)checksum[i]);
						if (tolower(md5[0]) != tolower(hex[0]) || tolower(md5[1]) != tolower(hex[1])) {
							checksumsame = 0;
						}
						md5 += 2;
					}
					printf("]\n");
				}
				if (checksumsame == 0)
				{
					printf("mismatch\n");
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

				file_finish(checksumsame);
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
			repeat = arc_info->repeat;
			start = arc_info->offset;
			length = 0;
			if (arc_info->length > 0) length = arc_info->length;
			//printf("partname[%s]\n",arc_info->partname);
			//printf("zipname [%s]\n",arc_info->zipname);
			//printf("offset[%d]\n",arc_info->offset);
			//printf("length[%d]\n",arc_info->length);
			//printf("repeat[%d]\n",arc_info->repeat);
			//

			//user_io_file_tx_body_filepart(getFullPath(fname),0,0);
			if (strlen(arc_info->partname))
			{
				char *zipname = (strlen(arc_info->partzipname)) ? arc_info->partzipname : arc_info->zipname;
				const char *root = get_arcade_root(0);
				sprintf(fname, (zipname[0] == '/') ? "%s%s/%s" : "%s/mame/%s/%s", root, zipname, arc_info->partname);

				printf("file: %s, start=%d, len=%d\n", fname, start, length);
				for (int i = 0; i < repeat; i++)
				{
					int result = file_file(fname, start, length, &arc_info->context);

					// we should check file not found error for the zip
					if (result == 0)
					{
						printf("%s does not exist\n", fname);
						snprintf(arc_info->error_msg, kBigTextSize, "%s\nFile Not Found", fname + strlen(root));
					}
				}
			}
			else // we have binary data?
			{
				//printf("we have bin.hex data [%s]\n",arc_info->data->content);
				size_t len = 0;
				unsigned char* binary = hexstr_to_char(arc_info->data->content, &len);
				//printf("len %d:\n",len);
				//for (size_t i=0;i<len;i++) {
				//	printf(" %d ",binary[i]);
				//}
				//printf("\n");
				if (binary)
				{
					for (int i = 0; i < repeat; i++) file_data(binary, len, &arc_info->context);
					free(binary);
				}
			}
		}

		if (!strcasecmp(node->tag, "patch") && arc_info->insiderom)
		{
			size_t len = 0;
			unsigned char* binary = hexstr_to_char(arc_info->data->content, &len);
			if (binary)
			{
				file_patch(binary, arc_info->patchaddr, len);
				free(binary);
			}
		}

		if (!strcasecmp(node->tag, "dip"))
		{
			int n = switches.dip_num;
			for (int i = 0; i < switches.dip[n].num; i++)
			{
				switches.dip[n].val[i] = ((switches.dip[n].has_val) ? switches.dip[n].val[i] : i) << switches.dip[n].start;
			}

			if (switches.dip_num < 63) switches.dip_num++;
		}

		if (!strcasecmp(node->tag, "switches"))
		{
			arc_info->insidesw = 0;
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

static int arcade_type = 0;
int is_arcade()
{
	return arcade_type;
}

int arcade_send_rom(const char *xml)
{
	arcade_type = 1;

	const char *p = strrchr(xml, '/');
	p = p ? p + 1 : xml;
	snprintf(switches.name, sizeof(switches.name), p);
	char *ext = strcasestr(switches.name, ".mra");
	if (ext) strcpy(ext, ".dip");

	SAX_Callbacks sax;
	SAX_Callbacks_init(&sax);

	sax.all_event = xml_send_rom;

	set_arcade_root(xml);

	// create the structure we use for the XML parser
	struct arc_struct arc_info;
	arc_info.data = buffer_init(kBigTextSize);
	arc_info.error_msg[0] = 0;
	arc_info.validrom0 = 0;

	// parse
	XMLDoc_parse_file_SAX(xml, &sax, &arc_info);
	if (arc_info.validrom0 == 0 && strlen(arc_info.error_msg))
	{
		strcpy(arcade_error_msg, arc_info.error_msg);
		printf("arcade_send_rom: pretty error: [%s]\n", arcade_error_msg);
	}
	buffer_destroy(arc_info.data);

	switches.dip_cur = switches.dip_def;
	arcade_sw_load();
	switches.dip_saved = switches.dip_cur;
	arcade_sw_send();
	return 0;
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

	int found = 0;
	int len;
	while ((entry = readdir(dir)) != NULL)
	{
		if (entry->d_type != DT_DIR)
		{
			char newstring[kBigTextSize];
			//printf("entry name: %s\n",entry->d_name);

			snprintf(newstring, kBigTextSize, "Arcade-%s", rbfname);
			len = strlen(newstring);
			if (!strncasecmp(newstring, entry->d_name, len) && (entry->d_name[len] == '.' || entry->d_name[len] == '_'))
			{
				found = 1;
				break;
			}

			snprintf(newstring, kBigTextSize, "%s", rbfname);
			len = strlen(newstring);
			if (!strncasecmp(newstring, entry->d_name, len) && (entry->d_name[len] == '.' || entry->d_name[len] == '_'))
			{
				found = 1;
				break;
			}
		}
	}

	if (found) sprintf(rbfname, "%s/%s", dirname, entry->d_name);
	closedir(dir);

	return found ? rbfname : NULL;
}

int arcade_load(const char *xml)
{
	MenuHide();
	static char path[kBigTextSize];

	strcpy(path, xml);

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
