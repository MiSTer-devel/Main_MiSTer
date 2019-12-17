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
	int validrom0;
	buffer_data *data;
	struct MD5Context context;
};

char global_error_msg[kBigTextSize];

#define DEBUG_ROM_BINARY 0
#if DEBUG_ROM_BINARY
FILE *rombinary;
#endif

static const char * get_arcade_root()
{
	static char path[kBigTextSize] = {};
	if (!path[0])
	{
		sprintf(path, "%s/arcade", getRootDir());
	}
	return path;
}

static int file_tx_start(unsigned char index)
{
	// set index byte (0=bios rom, 1-n=OSD entry index)
	user_io_set_index(index);

	// prepare transmission of new file
	user_io_set_download(1);

#if DEBUG_ROM_BINARY
	rombinary = fopen("/media/fat/this.rom", "wb");
#endif
	return 1;
}

static int file_tx_data(const uint8_t *buf, uint16_t chunk, struct MD5Context *md5context)
{
	user_io_file_tx_write(buf, chunk);

	if (md5context) MD5Update(md5context, buf, chunk);
#if DEBUG_ROM_BINARY
	fwrite(buf, 1, chunk, rombinary);
#endif

	return 1;
}

static int file_tx_file(const char *name, int start, int len, struct MD5Context *md5context)
{
	char mute = 0;
	fileTYPE f = {};
	static uint8_t buf[4096];
	if (!FileOpen(&f, name, mute)) return 0;
	if (start) FileSeek(&f, start, SEEK_SET);
	unsigned long bytes2send = f.size;
	if (len > 0 && len < (int)bytes2send) bytes2send = len;
	/* transmit the entire file using one transfer */
	//printf("Selected file %s with %lu bytes to send  \n", name, bytes2send);
	while (bytes2send)
	{
		uint16_t chunk = (bytes2send > sizeof(buf)) ? sizeof(buf) : bytes2send;

		FileReadAdv(&f, buf, chunk);
		file_tx_data(buf, chunk, md5context);

		bytes2send -= chunk;
	}

	return 1;
}

static int file_tx_finish()
{
	printf("\n");
#if DEBUG_ROM_BINARY
	fclose(rombinary);
#endif

	// signal end of transmission
	user_io_set_download(0);
	return 1;
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
	unsigned char* chrs = (unsigned char*)malloc((len + 1) * sizeof(*chrs));
	int dest = 0;
	// point to the beginning of the array
	const char *ptr = hexstr;
	while (*ptr) {
		// check to see if we have a space
		while (*ptr == '\n' || *ptr == '\r' || *ptr == ' ' || *ptr == '\t' || *ptr == 9 /*horiz tab*/) ptr++;
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

		// for each part tag, we clear the partzipname since it is optional and may not appear in the part tag
		if (!strcasecmp(node->tag, "part"))
			arc_info->partzipname[0] = 0;


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
			if (arc_info->insiderom) {
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

			file_tx_start(arc_info->romindex);
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
				file_tx_finish();
				MD5Final(checksum, &arc_info->context);
				printf("md5[%s]\n", arc_info->md5);
				printf("md5-calc[");
				for (int i = 0; i < 16; i++)
				{
					char hex[10];
					snprintf(hex, 10, "%02x", (unsigned int)checksum[i]);
					printf("%02x", (unsigned int)checksum[i]);
					if (md5[0] != hex[0] || md5[1] != hex[1]) {
						checksumsame = 0;
					}
					md5 += 2;
				}
				printf("]\n");
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
				sprintf(fname, (zipname[0] == '/') ? "%s%s/%s" : "%s/mame/%s/%s", get_arcade_root(), zipname, arc_info->partname);

				printf("file: %s, start=%d, len=%d\n", fname, start, length);
				for (int i = 0; i < repeat; i++)
				{
					int result = file_tx_file(fname, start, length, &arc_info->context);

					// we should check file not found error for the zip
					if (result == 0)
					{
						printf("%s does not exist\n", fname);
						snprintf(arc_info->error_msg, kBigTextSize, "%s\nFile Not Found", fname + strlen(get_arcade_root()));
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
					for (int i = 0; i < repeat; i++) file_tx_data(binary, len, &arc_info->context);
					free(binary);
				}
			}
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

int arcade_send_rom(const char *xml)
{
	SAX_Callbacks sax;
	SAX_Callbacks_init(&sax);

	sax.all_event = xml_send_rom;

	// create the structure we use for the XML parser
	struct arc_struct arc_info;
	arc_info.data = buffer_init(kBigTextSize);
	arc_info.error_msg[0] = 0;
	arc_info.validrom0 = 0;

	// parse
	XMLDoc_parse_file_SAX(xml, &sax, &arc_info);
	if (arc_info.validrom0 == 0 && strlen(arc_info.error_msg))
	{
		strcpy(global_error_msg, arc_info.error_msg);
		printf("arcade_send_rom: pretty error: [%s]\n", global_error_msg);
	}
	buffer_destroy(arc_info.data);
	return 0;
}

int arcade_check_error(void)
{
	if (global_error_msg[0] != 0) {
		printf("ERROR: [%s]\n", global_error_msg);
		Info(global_error_msg, 1000 * 30);
		global_error_msg[0] = 0;
	}

	return 0;
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

	if (!(dir = opendir(get_arcade_root())))
	{
		printf("%s directory not found\n", get_arcade_root());
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

	if (found) sprintf(rbfname, "%s/%s", get_arcade_root(), entry->d_name);
	closedir(dir);

	return found ? rbfname : NULL;
}

int arcade_load(const char *xml)
{
	MenuHide();

	printf("arcade_scan_xml_for_rbf [%s]\n", xml);
	const char *rbf = get_rbf(xml);

	if (rbf)
	{
		printf("MRA: %s, RBF: %s\n", xml, rbf);
		fpga_load_rbf(rbf, NULL, xml);
	}
	else
	{
		Info("No rbf found!");
	}

	return 0;
}
