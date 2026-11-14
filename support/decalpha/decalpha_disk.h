#ifndef DECALPHA_DISK_H
#define DECALPHA_DISK_H

#include <stdint.h>
#include "../../file_io.h"

int  decalpha_disk_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack);

void decalpha_disk_poll();

void decalpha_disk_flush(int disk);

#endif
