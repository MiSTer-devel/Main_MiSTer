#include "../../file_io.h"

#define NEO_FILE_RAW 0
#define NEO_FILE_8BIT 1
#define NEO_FILE_FIX 2
#define NEO_FILE_SPR 3

int neogeo_romset_tx(char* name);
int neogeo_scan_xml(char *path);
char *neogeo_get_altname(char *path, direntext_t *de);
