// Mac SCSI family hard disks: RAM write buffer in front of the sync-mounted card.

#ifndef MAC_DISK_H
#define MAC_DISK_H

#include <stdint.h>
#include "../../file_io.h"

// SD write hook. Returns 1 when the data was taken into the buffer (caller skips its write).
int mac_disk_write(int disk, fileTYPE *f, int cangrow, uint64_t off, const uint8_t *data, uint32_t sz);

// SD read hook: push out anything buffered that overlaps [off, off+len).
void mac_disk_before_read(int disk, uint64_t off, uint64_t len);

void mac_disk_flush(int disk);   // remount
void mac_disk_flush_all();       // before app_restart
void mac_disk_poll();            // idle flush, once per user_io_poll

#endif
