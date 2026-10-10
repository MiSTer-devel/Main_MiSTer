// Sun SCSI family hard disks: write buffer.

#ifndef SUN_DISK_H
#define SUN_DISK_H

#include <stdint.h>
#include "../../file_io.h"

int  sun_disk_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack);

void sun_disk_poll();

void sun_disk_flush(int disk);

#endif
