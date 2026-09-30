// Mac SCSI family hard disks: RAM write buffer in front of the sync-mounted card.

#ifndef MAC_DISK_H
#define MAC_DISK_H

#include <stdint.h>
#include "../../file_io.h"

int mac_disk_write(int disk, fileTYPE *f, int cangrow, uint64_t off, const uint8_t *data, uint32_t sz);

void mac_disk_before_read(int disk, uint64_t off, uint64_t len);

void mac_disk_flush(int disk);  
void mac_disk_flush_all();      
void mac_disk_poll();            

void mac_disk_init();            
int  mac_disk_served(int disk);
int  mac_disk_wait_next();       

#endif
