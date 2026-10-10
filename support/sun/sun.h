// Sun family (Sun-2, Sun-3, SPARCstation): user_io hooks, each gated on the family.

#ifndef SUN_H
#define SUN_H

#include <stdint.h>
#include "../../file_io.h"

// To add a model: a value here, its core name in sun_model() and a case in each sun.cpp switch.
enum sun_model_t
{
	SUN_NONE,
	SUN_2,
	SUN_3,
	SUN_SPARCSTATION,
};

sun_model_t sun_model();
char is_sun_family();

// OSD "Network" status bits, or 0 if the core is not a Sun.
const char *sun_net_status();
uint8_t sun_idprom_machine();

// Disk slots 0..n-1 whose writes Main buffers.
int sun_buffered_disks();
#define SUN_DISK_SLOTS 2

// CD-ROM slot with image translation, or -1.
int sun_cdrom_slot();

// Returns 0 to fail the mount; a translated CD clears *writable and sets f->size.
int sun_mount_hook(int index, const char *name, fileTYPE *f, int *writable);
void sun_unmount(int index);
void sun_poll();

// 0 = not ours, 1 = serviced, -1 = ours but unsupported (caller breaks the loop).
int sun_sd_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack);

#endif
