// HP 9000 family hard disks: write buffer.

#ifndef HP9K_DISK_H
#define HP9K_DISK_H

#include <stdint.h>
#include "../../file_io.h"

int  hp9k_disk_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack);

void hp9k_disk_poll();

void hp9k_disk_flush(int disk);

#endif
