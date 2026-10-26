// IBM RS/6000 43P 7248: user_io hooks.

#include <stdint.h>
#include <string.h>
#include <strings.h>

#include "../../user_io.h"
#include "../../spi.h"
#include "../../file_io.h"
#include "ibmrs6k.h"
#include "ibmrs6k_cdrom.h"
#include "ibmrs6k_disk.h"

char is_ibmrs6k()
{
	return !strcasecmp(user_io_get_core_name(1), "IBMRS6K7248");
}

int ibmrs6k_cdrom_slot()
{
	return is_ibmrs6k() ? 2 : -1;
}

int ibmrs6k_buffered_disks()
{
	return is_ibmrs6k() ? 0x3 : 0;
}

int ibmrs6k_mount_hook(int index, const char *name, fileTYPE *f, int *writable)
{
	if (!is_ibmrs6k()) return 1;

	ibmrs6k_disk_flush(index);
	if (index != ibmrs6k_cdrom_slot()) return 1;

	int r = ibmrs6k_cdrom_mount(index, name);
	if (r == IBMRS6K_CDROM_HANDLED)
	{
		*writable = 0;
		f->size = (int64_t)ibmrs6k_cdrom_size(index);
	}
	else if (r == IBMRS6K_CDROM_REJECT)
	{
		FileClose(f);
		return 0;
	}
	return 1;
}

void ibmrs6k_unmount(int index)
{
	if (!is_ibmrs6k()) return;
	ibmrs6k_disk_flush(index);
	ibmrs6k_cdrom_unmount(index);
}

void ibmrs6k_poll()
{
	ibmrs6k_disk_poll();
}

int ibmrs6k_sd_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack)
{
	if (!op || !is_ibmrs6k()) return 0;
	if (ibmrs6k_disk_service(disk, f, op, lba, sz, ack)) return 1;

	if (ibmrs6k_cdrom_active(disk) || (disk == ibmrs6k_cdrom_slot() && lba >= IBMRS6K_CDROM_AUDIO_BLK))
	{
		static uint8_t buf[UIO_BUFFER_SIZE];
		if (!(op & 1) || sz > (int)sizeof(buf)) return -1;

		if (!ibmrs6k_cdrom_window(disk, lba, buf, sz))
			ibmrs6k_cdrom_fill(disk, lba, buf, sz);
		EnableIO();
		spi_w(UIO_SECTOR_RD | ack);
		spi_block_write(buf, user_io_get_width(), sz);
		DisableIO();
		return 1;
	}

	return 0;
}
