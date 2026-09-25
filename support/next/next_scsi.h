#ifndef NEXT_SCSI_H
#define NEXT_SCSI_H

#include <stdint.h>
#include "../../file_io.h"

#define NEXT_SCSI_UNITS 4
#define NEXT_MO_SLOT    5
#define NEXT_RESP_LEN   510

int  next_mount_hook(int index, const char *name, fileTYPE *f, int *writable);
void next_unmount(int index);
int  next_sd_service(int disk, int op, uint32_t lba, int sz, int ack);
void next_scsi_window_fill(uint32_t lba, uint8_t *buf, int sz);

#endif
