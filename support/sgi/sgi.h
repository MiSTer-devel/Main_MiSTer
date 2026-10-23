// SGI Indy (SGIIndy): user_io hooks, each gated on the core.

#ifndef SGI_H
#define SGI_H

#include <stdint.h>
#include "../../file_io.h"

char is_sgi_family();

// bit n: slot n is behind the hard-disk write buffer
#define SGI_DISK_SLOTS 4
int sgi_buffered_disks();

// Returns 0 to fail the mount.
int sgi_mount_hook(int index, const char *name, fileTYPE *f, int *writable);
void sgi_unmount(int index);

void sgi_poll();

// 0 = not ours, 1 = serviced, -1 = ours but unsupported (caller breaks the loop).
int sgi_sd_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack);

#endif
