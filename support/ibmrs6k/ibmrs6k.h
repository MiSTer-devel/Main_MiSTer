// IBM RS/6000 43P 7248 (IBMRS6K7248): the user_io hooks, each gated on the core.

#ifndef IBMRS6K_H
#define IBMRS6K_H

#include <stdint.h>
#include "../../file_io.h"

char is_ibmrs6k();

// the CD-ROM's slot (SC2, SCSI ID 3)
int ibmrs6k_cdrom_slot();

// bit n: slot n is behind the hard-disk write buffer (SC0 and SC1, SCSI IDs 0 and 1)
#define IBMRS6K_DISK_SLOTS 4
int ibmrs6k_buffered_disks();

// Returns 0 to fail the mount; a translated CD clears *writable and sets f->size.
int ibmrs6k_mount_hook(int index, const char *name, fileTYPE *f, int *writable);
void ibmrs6k_unmount(int index);

void ibmrs6k_poll();

// 0 = not ours, 1 = serviced, -1 = ours but unsupported (caller breaks the loop).
int ibmrs6k_sd_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack);

#endif
