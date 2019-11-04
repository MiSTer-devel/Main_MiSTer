#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "sxmlc.h"
#include "base64simple.h"

#include "../../user_io.h"

#include "buffer.h"
/*
 * adapted from https://gist.github.com/xsleonard/7341172
 *
 * hexstr_to_char will take hex strings in two types:
 *
 * 00 01 02
 * 000102
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
            // check to see if we have a space
            if (*ptr == ' ') ptr++;
    }
    chrs[dest]=0;
    *out_len = dest; /* dest is 0 based, so we don't need to subtract 1*/
    return chrs;
}


static int xml_scan(XMLEvent evt, const XMLNode* node, SXML_CHAR* text, const int n, SAX_Data* sd)
{
        (void)(sd);

        switch (evt)
        {
        case XML_EVENT_START_NODE:
		printf("XML_EVENT_START_NODE: tag [%s]\n",node->tag);
                for (int i = 0; i < node->n_attributes; i++)
                        {
			   printf("attribute %d name [%s] value [%s]\n",i,node->attributes[i].name,node->attributes[i].value);
                        }

                break;
        case XML_EVENT_TEXT:
		printf("XML_EVENT_TEXT: text [%s]\n",text);
		break;
        case XML_EVENT_END_NODE:
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
/*
user_io.h:int user_io_file_tx_start(const char *name,unsigned char index=0);
user_io.h:int user_io_file_tx_body(const uint8_t *buf,uint16_t chunk);
user_io.h:int user_io_file_tx_body_filepart(const char *name,int start=0, int len=0);
user_io.h:int user_io_file_tx_finish();
*/


struct arc_struct {
	
};

/*
 * AJS - TODO:
 *  create  a structure we can pass into send_rom
 *  use dynamic allocation (realloc?) to allocate the bigtext
 *  make sure we push / pop the stack for file names, etc
 *  make sure that merged / non merged roms work
 *
 *  ???? -- SORG -- make one big array, and send it to the _tx code as one shot, we can check the MD5 and put up warning as well easily
 *  DONE -- SORG -- make the HEX look like a hex dump so it is easy to edit
 *  SORG -- get rid off uudecode stuff
 *  SORG -- part - offset / length get rid of action
 *  AJS = Here are some of the things: Mame Roms, Mame Merged Roms, Binary Sound Files, repeated chunks. Partial Rom Chunks, Flipped Rom Chunks, games that use two mame zips (maybe?), patches, extra data sitting in the releases directory
 */


static int xml_send_rom(XMLEvent evt, const XMLNode* node, SXML_CHAR* text, const int n, SAX_Data* sd)
{
#define kBigTextSize 4096
	static char zipname[kBigTextSize];
	static char partname[kBigTextSize];
	static char bigtext[kBigTextSize];
	static char action[kBigTextSize];
	static char dtdt[kBigTextSize];
	//zipname[0]=0;
        (void)(sd);

        switch (evt)
        {
        case XML_EVENT_START_NODE:
		bigtext[0]=0;
		action[0]=0;
		printf("XML_EVENT_START_NODE: tag [%s]\n",node->tag);
                for (int i = 0; i < node->n_attributes; i++)
                        {
			   printf("attribute %d name [%s] value [%s]\n",i,node->attributes[i].name,node->attributes[i].value);
			   if (!strcasecmp(node->attributes[i].name,"zip") && !strcasecmp(node->tag,"rom"))
			   {
				   strcpy(zipname,node->attributes[i].value);
				   printf("found zip: [%s]\n",zipname);
			   }
			   if (!strcasecmp(node->attributes[i].name,"name") && !strcasecmp(node->tag,"rom"))
			   {
				   user_io_file_tx_start(node->attributes[i].value);
			   }
			   if (!strcasecmp(node->attributes[i].name,"action") && !strcasecmp(node->tag,"part"))
			   {
				   strcpy(action,node->attributes[i].value);
			   }
			   if (!strcasecmp(node->attributes[i].name,"zip") && !strcasecmp(node->tag,"part"))
			   {
				   strcpy(zipname,node->attributes[i].value);
			   }
			   if (!strcasecmp(node->attributes[i].name,"name") && !strcasecmp(node->tag,"part"))
			   {
				   strcpy(partname,node->attributes[i].value);
			   }
			   if (!strcasecmp(node->attributes[i].name,"dt:dt") && !strcasecmp(node->tag,"part"))
			   {
				   strcpy(dtdt,node->attributes[i].value);
			   }
                        }

                break;
        case XML_EVENT_TEXT:
		strncat(bigtext,text,kBigTextSize-strlen(bigtext)-1);
		printf("XML_EVENT_TEXT: text [%s]\n",text);
		break;
        case XML_EVENT_END_NODE:
		printf("XML_EVENT_END_NODE: tag [%s]\n",node->tag );
		if (!strcasecmp(node->tag,"rom")) {
			user_io_file_tx_finish();
		}

		//int user_io_file_tx_body_filepart(const char *name,int start, int len)
		if (!strcasecmp(node->tag,"part")) 
		{
			char fname[4096];
			printf("bigtext [%s]\n",bigtext);
			printf("partname[%s]\n",partname);
			printf("zipname [%s]\n",zipname);
			sprintf(fname,"arcade/mame/%s/%s",zipname,partname);
			//user_io_file_tx_body_filepart(getFullPath(fname),0,0);
			if (strlen(partname)) {
			        printf("user_io_file_tx_body_filepart(const char *name[%s],int start[], int len[])\n",fname);
				user_io_file_tx_body_filepart(fname,0,0);
			}
			else // we have binary data?
			{
				if (!strcasecmp(dtdt,"bin.base64"))
				{
					size_t i,size,r_size;
					printf("we have bin.base64 data [%s]\n",bigtext);
					size = strlen(bigtext);
	                                unsigned char *decoded = base64simple_decode(bigtext, size, &r_size);
	                                if (decoded == NULL) {
		                            printf("Improperly Encoded String or Insufficient Memory!\n");
	                                } else {
		                             for (i = 0; i < r_size; ++i) {
				               printf(" %d (%c)",decoded[i],decoded[i]);
			                      // Do something with decoded[i] here
		                             }
				             user_io_file_tx_body((const uint8_t*)decoded,size);
	                                }
	                                if (decoded) free(decoded);
				}
				else if (!strcasecmp(dtdt,"bin.hex")) 
				{
					printf("we have bin.hex data [%s]\n",bigtext);
					size_t len=0;
					unsigned char* binary=hexstr_to_char(bigtext,&len);
					printf("len %d:\n",len);
					for (size_t i=0;i<len;i++) {
						printf(" %d ",binary[i]);
					}
					printf("\n");
				        user_io_file_tx_body(binary,len);
					if (binary) free(binary);
				}
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
static int xml_scan_rbf(XMLEvent evt, const XMLNode* node, SXML_CHAR* text, const int n, SAX_Data* sd)
{
#define kBigTextSize 4096
	char bigtext[kBigTextSize];
	char *rbf = (char *)sd->user;

        switch (evt)
        {
        case XML_EVENT_START_NODE:
		bigtext[0]=0;
		printf("XML_EVENT_START_NODE: tag [%s]\n",node->tag);
                for (int i = 0; i < node->n_attributes; i++)
                        {
			   printf("attribute %d name [%s] value [%s]\n",i,node->attributes[i].name,node->attributes[i].value);
                        }

                break;
        case XML_EVENT_TEXT:
		strncat(bigtext,text,kBigTextSize-strlen(bigtext)-1);
		printf("XML_EVENT_TEXT: text [%s]\n",text);
		break;
        case XML_EVENT_END_NODE:
		if (!strcasecmp(node->tag,"rbf"))
		{
			printf("bigtext [%s]\n",bigtext);
			strncpy(rbf,bigtext,kBigTextSize);
			printf("got rbf tag [%s]\n",rbf);
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

int arcade_scan_xml(char *xml)
{
        SAX_Callbacks sax;
        SAX_Callbacks_init(&sax);

        sax.all_event = xml_scan;
        XMLDoc_parse_file_SAX(xml, &sax, 0);
        return 0;
}


int arcade_send_rom(const char *xml)
{
        SAX_Callbacks sax;
        SAX_Callbacks_init(&sax);

        sax.all_event = xml_send_rom;

	// grab the file size so we can pre-allocate buffers for inline data
        struct stat st;
        stat(xml, &st);
        off_t size = st.st_size;

	// create the structure we use for the XML parser

	// parse
        XMLDoc_parse_file_SAX(xml, &sax, 0);
        return 0;
}

int arcade_scan_xml_for_rbf(const char *xml,char *rbfname) 
{
	rbfname[0]=0;
        SAX_Callbacks sax;
        SAX_Callbacks_init(&sax);


        sax.all_event = xml_scan_rbf;
        XMLDoc_parse_file_SAX(xml, &sax, rbfname);
        return 0;
}

#if 0
int main (int argc, char *argv[])
{
	//char rbfname[4096];
	//arcade_scan_xml("test.xml");
	//arcade_scan_xml_for_rbf("test.xml",rbfname);
	//printf("rbfname=[%s]\n",rbfname);
	arcade_send_rom("test.xml");
}
#endif
