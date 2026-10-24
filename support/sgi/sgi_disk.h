// SGI Indy hard disk write buffer.

#ifndef SGI_DISK_H
#define SGI_DISK_H

#include <stdint.h>
#include "../../file_io.h"

int  sgi_disk_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack);

void sgi_disk_poll();

void sgi_disk_flush(int disk);

#endif
