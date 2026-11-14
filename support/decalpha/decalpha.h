#ifndef DECALPHA_H
#define DECALPHA_H

#include <stdint.h>
#include "../../file_io.h"

char is_decalpha();

int decalpha_cdrom_slot();

#define DECALPHA_DISK_SLOTS 2
int decalpha_buffered_disks();

int decalpha_mount_hook(int index, const char *name, fileTYPE *f, int *writable);
void decalpha_unmount(int index);

void decalpha_poll();

int decalpha_sd_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack);

#endif
