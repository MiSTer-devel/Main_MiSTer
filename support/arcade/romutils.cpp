#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>


#include "sxmlc.h"

#include "../../user_io.h"
#include "../../file_io.h"
#include "../../menu.h"

#include "buffer.h"


#include "md5.h"
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
    unsigned char* chrs = (unsigned char*)malloc((len+1) * sizeof(*chrs));
    int dest=0;
    // point to the beginning of the array
    const char *ptr = hexstr;
    while (*ptr) {
            // check to see if we have a space
	    while (*ptr=='\n' || *ptr=='\r' || *ptr==' ' || *ptr=='\t' || *ptr==9 /*horiz tab*/) ptr++;
	    if (*ptr==0) break;

            // pull two characters off
	    int val1= (*ptr % 32 + 9) % 25 * 16;
	    ptr++;
	    /* check to odd numbers of characters*/
	    if (*ptr==0) {
               int val= (ptr[-1] % 32 + 9) % 25;
               chrs[dest++] = val;
                break;
            }
	    int val2= (*ptr % 32 + 9) % 25;
	    ptr++;
            chrs[dest++] = val1+val2;
    }
    chrs[dest]=0;
    *out_len = dest; /* dest is 0 based, so we don't need to subtract 1*/
    return chrs;
}


/*
user_io.h:int user_io_file_tx_start(const char *name,unsigned char index=0);
user_io.h:int user_io_file_tx_body(const uint8_t *buf,uint16_t chunk);
user_io.h:int user_io_file_tx_body_filepart(const char *name,int start=0, int len=0);
user_io.h:int user_io_file_tx_finish();
*/


#define kBigTextSize 4096
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

/*
 * AJS - TODO:
 *  DONE -- create  a structure we can pass into send_rom
 *  DONE -- use dynamic allocation (realloc?) to allocate the bigtext
 *  -- make sure we push / pop the stack for file names, etc
 *  -- make sure that merged / non merged roms work
 *
 *  ???? -- SORG -- make one big array, and send it to the _tx code as one shot, we can check the MD5 and put up warning as well easily
 *  DONE -- SORG -- make the HEX look like a hex dump so it is easy to edit
 *  DONE -- SORG -- get rid off uudecode stuff
 *  SORG -- part - offset / length get rid of action
 *  AJS = Here are some of the things: Mame Roms, Mame Merged Roms, Binary Sound Files, repeated chunks. Partial Rom Chunks, Flipped Rom Chunks, games that use two mame zips (maybe?), patches, extra data sitting in the releases directory
 */


static int xml_send_rom(XMLEvent evt, const XMLNode* node, SXML_CHAR* text, const int n, SAX_Data* sd)
{
	struct arc_struct *arc_info = (struct arc_struct *)sd->user;
        (void)(sd);

        switch (evt)
        {
        case XML_EVENT_START_NODE:
		buffer_destroy(arc_info->data);
		arc_info->data=buffer_init(kBigTextSize);
		arc_info->partname[0]=0;
		arc_info->offset=0;
		arc_info->length=-1;
		arc_info->repeat=1;
	   	if (!strcasecmp(node->tag,"rom"))
		{
			arc_info->insiderom=1;
			arc_info->romname[0]=0;
			arc_info->romindex=0;
			arc_info->md5[0]=0;
                	MD5Init (&arc_info->context);
		}
	       if (!strcasecmp(node->tag,"part"))
		  arc_info->partzipname[0]=0;
		//printf("XML_EVENT_START_NODE: tag [%s]\n",node->tag);
                for (int i = 0; i < node->n_attributes; i++)
                        {
			   //printf("attribute %d name [%s] value [%s]\n",i,node->attributes[i].name,node->attributes[i].value);
			   if (!strcasecmp(node->attributes[i].name,"zip") && !strcasecmp(node->tag,"rom"))
			   {
				   strcpy(arc_info->zipname,node->attributes[i].value);
			   }
			   if (!strcasecmp(node->attributes[i].name,"name") && !strcasecmp(node->tag,"rom"))
			   {
				   strcpy(arc_info->romname,node->attributes[i].value);
			   }
			   if (!strcasecmp(node->attributes[i].name,"md5") && !strcasecmp(node->tag,"rom"))
			   {
				   strcpy(arc_info->md5,node->attributes[i].value);
			   }
			   if (!strcasecmp(node->attributes[i].name,"index") && !strcasecmp(node->tag,"rom"))
			   {
				   arc_info->romindex=atoi(node->attributes[i].value);
			   }
			   if (arc_info->insiderom) {
			   	if (!strcasecmp(node->attributes[i].name,"zip") && !strcasecmp(node->tag,"part"))
			   	{
				   strcpy(arc_info->partzipname,node->attributes[i].value);
				   //printf("found partzipname %s\n",arc_info->partzipname);
			   	}
			   	if (!strcasecmp(node->attributes[i].name,"name") && !strcasecmp(node->tag,"part"))
			   	{
				   strcpy(arc_info->partname,node->attributes[i].value);
			   	}
			   	if (!strcasecmp(node->attributes[i].name,"offset") && !strcasecmp(node->tag,"part"))
			   	{
				   arc_info->offset=atoi(node->attributes[i].value);
			   	}
			   	if (!strcasecmp(node->attributes[i].name,"length") && !strcasecmp(node->tag,"part"))
			   	{
				   arc_info->length=atoi(node->attributes[i].value);
			   	}
			   	if (!strcasecmp(node->attributes[i].name,"repeat") && !strcasecmp(node->tag,"part"))
			   	{
				   arc_info->repeat=atoi(node->attributes[i].value);
			   	}
			   }
                        }

	   	if (!strcasecmp(node->tag,"rom")) 
		{
		   user_io_file_tx_start(arc_info->romname,arc_info->romindex);
		}
                break;
        case XML_EVENT_TEXT:
		buffer_append(arc_info->data, text);
		//printf("XML_EVENT_TEXT: text [%s]\n",text);
		break;
        case XML_EVENT_END_NODE:
		//printf("XML_EVENT_END_NODE: tag [%s]\n",node->tag );
		if (!strcasecmp(node->tag,"rom")) {
			if (arc_info->insiderom) 
			{
			        unsigned char checksum[16];
				int checksumsame=1;
				char *md5=arc_info->md5;
				user_io_file_tx_finish();
                		MD5Final (checksum, &arc_info->context);
				printf("md5[%s]\n",arc_info->md5);
				printf("md5-calc[");
                		for (int i = 0; i < 16; i++)
                		{
					char hex[10];
					snprintf(hex,10,"%02x", (unsigned int) checksum[i]);
                        		printf ("%02x", (unsigned int) checksum[i]);
					if (md5[0]!=hex[0] || md5[1]!=hex[1]) {
						checksumsame=0;
					}
					md5+=2;
                		}
                		printf ("]\n");
				if (checksumsame==0) 
				{
					printf("mismatch\n");
					if (!strlen(arc_info->error_msg))
						snprintf(arc_info->error_msg,kBigTextSize,"md5 mismatch for rom %d",arc_info->romindex);
				}
				else 
				{
					if (arc_info->romindex==0)
					{
						arc_info->validrom0=1;
						arc_info->error_msg[0]=0;
					}
				}
			}
			arc_info->insiderom=0;
		}

		//int user_io_file_tx_body_filepart(const char *name,int start, int len)
		if (!strcasecmp(node->tag,"part") && arc_info->insiderom) 
		{
			char fname[kBigTextSize*2+16];
			int start,length,repeat;
			repeat=arc_info->repeat;
			start=arc_info->offset;
			length=0;
			if (arc_info->length>0) length = arc_info->length;
			//printf("partname[%s]\n",arc_info->partname);
			//printf("zipname [%s]\n",arc_info->zipname);
			//printf("offset[%d]\n",arc_info->offset);
			//printf("length[%d]\n",arc_info->length);
			//printf("repeat[%d]\n",arc_info->repeat);
			if (strlen(arc_info->partzipname))
				sprintf(fname,"arcade/mame/%s/%s",arc_info->partzipname,arc_info->partname);
			else
				sprintf(fname,"arcade/mame/%s/%s",arc_info->zipname,arc_info->partname);



			//user_io_file_tx_body_filepart(getFullPath(fname),0,0);
			if (strlen(arc_info->partname)) {
			        printf("user_io_file_tx_body_filepart(const char *name[%s],int start[%d], int len[%d])\n",fname,start,length);
				for (int i=0;i<repeat;i++) {
					int result=user_io_file_tx_body_filepart(fname,start,length,&arc_info->context);
					// we should check file not found error for the zip
					if (result==0)
					{
						printf("%s does not exist\n",fname);
						snprintf(arc_info->error_msg,kBigTextSize,"%s\n"
						"\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\n"
							       	"File Not Found",fname);
					}
				}
			}
			else // we have binary data?
			{
					//printf("we have bin.hex data [%s]\n",arc_info->data->content);
					size_t len=0;
					unsigned char* binary=hexstr_to_char(arc_info->data->content,&len);
					//printf("len %d:\n",len);
					//for (size_t i=0;i<len;i++) {
					//	printf(" %d ",binary[i]);
					//}
					//printf("\n");
					for (int i=0;i<repeat;i++) {
				       		user_io_file_tx_body(binary,len,&arc_info->context);
					}
					if (binary) free(binary);
			}


		}
                break;

        case XML_EVENT_ERROR:
                printf("XML parse: %s: ERROR %d\n", text, n);
		snprintf(arc_info->error_msg,kBigTextSize,"XML parse: %s: ERROR %d\n", text, n);
                break;
        default:
                break;
        }

        return true;
}
static int xml_scan_rbf(XMLEvent evt, const XMLNode* node, SXML_CHAR* text, const int n, SAX_Data* sd)
{
	static int insiderbf=0;
	char bigtext[kBigTextSize];
	char *rbf = (char *)sd->user;

        switch (evt)
        {
        case XML_EVENT_START_NODE:
		bigtext[0]=0;
		if (!strcasecmp(node->tag,"rbf")) {
			insiderbf=1;
		}
		printf("XML_EVENT_START_NODE: tag [%s]\n",node->tag);
                for (int i = 0; i < node->n_attributes; i++)
                        {
			   printf("attribute %d name [%s] value [%s]\n",i,node->attributes[i].name,node->attributes[i].value);
                        }

                break;
        case XML_EVENT_TEXT:
		if (insiderbf) {
		strncat(bigtext,text,kBigTextSize-strlen(bigtext)-1);
		printf("XML_EVENT_TEXT: text [%s]\n",text);
		}
		break;
        case XML_EVENT_END_NODE:
		if (!strcasecmp(node->tag,"rbf"))
		{
			insiderbf=0;
			//printf("bigtext [%s]\n",bigtext);
			strncpy(rbf,bigtext,kBigTextSize);
			//printf("got rbf tag [%s]\n",rbf);
		}
		printf("XML_EVENT_END_NODE: tag [%s]\n",node->tag );
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
	arc_info.data=buffer_init(kBigTextSize);
	arc_info.error_msg[0]=0;
	arc_info.validrom0=0;

	// parse
        XMLDoc_parse_file_SAX(xml, &sax, &arc_info);
	if (arc_info.validrom0==0 && strlen(arc_info.error_msg))
	{
		strcpy(global_error_msg,arc_info.error_msg);
		printf("arcade_send_rom: pretty error: [%s]\n",global_error_msg);
	}
	buffer_destroy(arc_info.data);
        return 0;
}

int CheckArcadeError(void)
{
	if (global_error_msg[0]!=0) {
		printf("ERROR: [%s]\n",global_error_msg);
		Info(global_error_msg,1000*30);
		global_error_msg[0]=0;
	}

	return 0;
}

int arcade_scan_xml_for_rbf(const char *xml,char *rbfname) 
{
	char rbfname_fragment[kBigTextSize];
	rbfname_fragment[0]=0;
	rbfname[0]=0;
        SAX_Callbacks sax;
        SAX_Callbacks_init(&sax);

        sax.all_event = xml_scan_rbf;
        XMLDoc_parse_file_SAX(xml, &sax, rbfname_fragment);
	strcpy(rbfname,rbfname_fragment);

	//printf("arcade_scan_xml_for_rbf [%s]\n",xml);
	/* once we have the rbfname fragment from the MRA xml file
	 * search the arcade folder for the match */
        struct dirent *entry;
        DIR *dir=NULL;
	//printf("opendir(%s)\n",getFullPath("arcade"));
        if (!(dir = opendir(getFullPath("arcade"))))
        {
		printf("arcade directory not found\n");
                return 0;
        }

        while ((entry = readdir(dir)) != NULL) {
                if (entry->d_type == DT_DIR) {
                }
                else {
			char newstring[kBigTextSize];
			//printf("entry name: %s\n",entry->d_name);
			snprintf(newstring,kBigTextSize,"Arcade-%s_",rbfname_fragment);
			if (!strncasecmp(newstring,entry->d_name,strlen(newstring))) {
        			closedir(dir);
				strcpy(rbfname,entry->d_name);
        			return 0;

			}
			snprintf(newstring,kBigTextSize,"%s_",rbfname_fragment);
			if (!strncasecmp(newstring,entry->d_name,strlen(newstring))) {
        			closedir(dir);
				strcpy(rbfname,entry->d_name);
        			return 0;
			}

                }
        }

       	if (dir) closedir(dir);
	strcpy(rbfname,rbfname_fragment);
        return 0;
}

