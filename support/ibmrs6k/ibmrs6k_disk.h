// IBM RS/6000 43P 7248 hard disks: write buffer.

#ifndef IBMRS6K_DISK_H
#define IBMRS6K_DISK_H

#include <stdint.h>
#include "../../file_io.h"

int  ibmrs6k_disk_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack);

void ibmrs6k_disk_poll();

void ibmrs6k_disk_flush(int disk);

#endif
