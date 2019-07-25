#include "../../file_io.h"

#define NEO_FILE_RAW 0
#define NEO_FILE_8BIT 1
#define NEO_FILE_FIX 2
#define NEO_FILE_SPR 3

extern bool checked_ok;
int neogeo_romset_tx(char* name);
int neogeo_scan_xml();
dirent *neogeo_set_altname(char *name);
const char *neogeo_get_name(uint32_t num);
