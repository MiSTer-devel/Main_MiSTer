// Mac SCSI family hard disks: write buffer.

#ifndef MAC_DISK_H
#define MAC_DISK_H

#include <stdint.h>
#include "../../file_io.h"

int  mac_disk_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack);

void mac_disk_poll();

void mac_disk_flush(int disk);

#endif
