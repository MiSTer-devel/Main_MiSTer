// SGI Indy: user_io hooks.

#include <stdint.h>
#include <strings.h>

#include "../../user_io.h"
#include "../../file_io.h"
#include "sgi.h"
#include "sgi_disk.h"

#define SGI_DISK_WBUF 0x6

char is_sgi_family()
{
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
