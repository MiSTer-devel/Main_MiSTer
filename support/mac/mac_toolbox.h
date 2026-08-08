// BlueSCSI Toolbox — HPS-side handlers for the Mac SCSI family's two Toolbox
// slots: file sharing (0xD0-D5) and the CD changer (0xD7/D8/DA).

#ifndef MAC_TOOLBOX_H
#define MAC_TOOLBOX_H

#include <stdint.h>

// Shared folder served to the Mac: `shared_folder=` in the .ini (absolute or
// SD-root relative), else games/<core>/shared. _ready() = the directory exists.
const char *toolbox_shared_dir();
bool        toolbox_shared_ready();

// CD-image folder exposed to the changer: games/<core>/CD3 under the SD root
// (BlueSCSI "CD<id>" convention, CD is SCSI ID 3). _ready() = it exists.
const char *cdchanger_dir();
bool        cdchanger_ready();

// Write op: lba 0 carries the 10-byte CDB (+ any DataOut payload at buf[16..]);
// run the op and stage the reply.
void toolbox_request(uint32_t lba, const uint8_t *buf, int sz);
void cdchanger_request(uint32_t lba, const uint8_t *buf, int sz);

// Read op: lba 0 = status block [0]=SCSI status [1]=0xB5 sig [2..3]=len BE;
// lba 1+k = 512-byte DataIn block k of the staged reply.
void toolbox_fill(uint32_t lba, uint8_t *buf, int sz);
void cdchanger_fill(uint32_t lba, uint8_t *buf, int sz);

// Once per user_io_poll: performs any SET NEXT CD remount staged by
// cdchanger_request (deferred off the sector-service loop).
void cdchanger_poll();

#endif
