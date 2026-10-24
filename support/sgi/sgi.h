// SGI Indy: user_io hooks.

#ifndef SGI_H
#define SGI_H

#include <stdint.h>
#include "../../file_io.h"

char is_sgi_family();

#define SGI_DISK_SLOTS 4
int sgi_buffered_disks();

int sgi_mount_hook(int index, const char *name, fileTYPE *f, int *writable);
void sgi_unmount(int index);

void sgi_poll();

int sgi_sd_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack);

#endif
