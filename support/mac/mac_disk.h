// Mac SCSI family hard disks (slots 0/1): RAM write buffer in front of the
// O_SYNC image on the sync-mounted card. Reached only through the mac.cpp hooks.

#ifndef MAC_DISK_H
#define MAC_DISK_H

#include <stdint.h>
#include "../../file_io.h"

// mac_sd_service() for the hard-disk slots: takes a write (SPI included) into
// the buffer and returns 1, or returns 0 and leaves the request to the generic
// path (reads, after writing out any buffered data they could see).
int  mac_disk_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack);

// mac_poll(): write out idle or old data.
void mac_disk_poll();

// mac_mount_hook(): write out what the slot holds for the previous image.
void mac_disk_flush(int disk);

#endif
