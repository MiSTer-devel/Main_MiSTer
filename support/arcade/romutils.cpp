#include <string.h>
#include "sxmlc.h"



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

static int xml_send_rom(XMLEvent evt, const XMLNode* node, SXML_CHAR* text, const int n, SAX_Data* sd)
{
#define kBigTextSize 4096
	char bigtext[kBigTextSize];
	char zipname[kBigTextSize];
        (void)(sd);

        switch (evt)
        {
        case XML_EVENT_START_NODE:
		bigtext[0]=0;
		printf("XML_EVENT_START_NODE: tag [%s]\n",node->tag);
                for (int i = 0; i < node->n_attributes; i++)
                        {
			   printf("attribute %d name [%s] value [%s]\n",i,node->attributes[i].name,node->attributes[i].value);
			   if (!strcasecmp(node->attributes[i].name,"zip"))
			   {
				   strcpy(zipname,node->attributes[i].value);
			   }
                        }

                break;
        case XML_EVENT_TEXT:
		strncat(bigtext,text,kBigTextSize-strlen(bigtext)-1);
		printf("XML_EVENT_TEXT: text [%s]\n",text);
		break;
        case XML_EVENT_END_NODE:
		printf("XML_EVENT_END_NODE: tag [%s]\n",node->tag );
		//int user_io_file_tx_body_filepart(const char *name,int start, int len)
		if (!strcasecmp(node->tag,"part")) 
		{
			char fname[4096];
			printf("bigtext [%s]\n",bigtext);
			sprintf(fname,"%s/%s",zipname,bigtext);
			printf("user_io_file_tx_body_filepart(const char *name[%s],int start[], int len[])\n",fname);
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
