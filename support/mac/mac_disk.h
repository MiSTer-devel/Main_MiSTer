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

// Quadra 800 tight service loop: after serving a hard-disk request keep polling
// the core for the next one instead of waiting for the next user_io_poll pass.
void mac_disk_init();            // env MAC_SD_SPIN_US (0 = off) / MAC_SD_BUDGET_US
int  mac_disk_served(int disk);  // call after a request; 1 = poll again right away
int  mac_disk_wait_next();       // 1 = a request is pending, 0 = give the pass back

#endif
