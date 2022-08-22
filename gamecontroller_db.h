#ifndef GAMECONTROLLER_DB_H
#define GAMECONTROLLER_DB_H
#include <linux/input.h>
#include <sys/types.h>
#include <stdint.h>

#define MAX_GCDB_ENTRIES 6 

//Including terminating nul
#define GUID_LEN 33 

bool gcdb_map_for_controller(uint16_t bustype, uint16_t vid, uint16_t pid, uint16_t version, int dev_fd, uint32_t *fill_map);
#endif


