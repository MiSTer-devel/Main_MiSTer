#ifndef ST_FDD_H
#define ST_FDD_H

#include <stdint.h>
#include "../../file_io.h"

#define ST_FDD_SIZE_TYPE_SHIFT  56
#define ST_FDD_TYPE_MFMT        1ULL

int  st_fdd_mount_hook(int index, const char *name, fileTYPE *f, int *writable);
void st_fdd_unmount(int index);

uint64_t st_fdd_size(int index, uint64_t size);

int  st_fdd_sd_service(int disk, int op, uint64_t lba, int sz, int ack);

#endif
