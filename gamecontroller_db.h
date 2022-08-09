#include <linux/input.h>
#include <sys/types.h>
#include <stdint.h>

#define MAX_GCDB_ENTRIES 16384

void load_gcdb_file(char *fname);
void load_gcdb_maps();
bool gcdb_map_for_controller(uint16_t bustype, uint16_t vid, uint16_t pid, uint16_t version, uint32_t *fill_map);

