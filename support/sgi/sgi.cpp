// SGI Indy: the user_io hooks.

#include <stdint.h>
#include <string.h>
#include <strings.h>

#include "../../user_io.h"
#include "../../file_io.h"
#include "sgi.h"
#include "sgi_disk.h"

// Slots 1 and 2 are the disks at SCSI IDs 1 and 2; slot 3 is the CD-ROM at ID 6
// and slot 0 is unused (the PROM arrives over ioctl). See the core's CONF_STR.
#define SGI_DISK_WBUF 0x6

char is_sgi_family()
{
	// the core's own name, not the CONF_STR override; it only changes with a new core
	return !strcasecmp(user_io_get_core_name(1), "SGIIndy");
}

int sgi_buffered_disks()
{
	return is_sgi_family() ? SGI_DISK_WBUF : 0;
}

int sgi_mount_hook(int index, const char *, fileTYPE *, int *)
{
	if (is_sgi_family()) sgi_disk_flush(index);
	return 1;
}

void sgi_unmount(int index)
{
	if (is_sgi_family()) sgi_disk_flush(index);
}

void sgi_poll()
{
	sgi_disk_poll();
}

int sgi_sd_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack)
{
	if (!op || !is_sgi_family()) return 0;
	return sgi_disk_service(disk, f, op, lba, sz, ack);
}
