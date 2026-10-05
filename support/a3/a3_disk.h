#ifndef A3_DISK_H
#define A3_DISK_H

#include <stdint.h>

struct fileTYPE;

int  a3_mount_hook(int index, const char *name, fileTYPE *f, int *writable);
void a3_unmount(int index);
int  a3_sd_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack);

#endif
