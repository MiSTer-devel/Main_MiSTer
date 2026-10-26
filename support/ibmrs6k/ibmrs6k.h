// IBM RS/6000 43P 7248: user_io hooks.

#ifndef IBMRS6K_H
#define IBMRS6K_H

#include <stdint.h>
#include "../../file_io.h"

char is_ibmrs6k();

int ibmrs6k_cdrom_slot();

#define IBMRS6K_DISK_SLOTS 4
int ibmrs6k_buffered_disks();

int ibmrs6k_mount_hook(int index, const char *name, fileTYPE *f, int *writable);
void ibmrs6k_unmount(int index);

void ibmrs6k_poll();

int ibmrs6k_sd_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack);

#endif
