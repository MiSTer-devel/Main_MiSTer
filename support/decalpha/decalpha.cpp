#include <stdint.h>
#include <string.h>
#include <strings.h>

#include "../../user_io.h"
#include "../../spi.h"
#include "../../file_io.h"
#include "decalpha.h"
#include "decalpha_cdrom.h"
#include "decalpha_disk.h"

char is_decalpha()
{
	return !strcasecmp(user_io_get_core_name(1), "DECAlphaStation200");
}

int decalpha_cdrom_slot()
{
	return is_decalpha() ? 2 : -1;
}

int decalpha_buffered_disks()
{
	return is_decalpha() ? 0x3 : 0;
}

int decalpha_mount_hook(int index, const char *name, fileTYPE *f, int *writable)
{
	if (!is_decalpha()) return 1;

	decalpha_disk_flush(index);
	if (index != decalpha_cdrom_slot()) return 1;

	int r = decalpha_cdrom_mount(index, name);
	if (r == DECALPHA_CDROM_HANDLED)
	{
		*writable = 0;
		f->size = (int64_t)decalpha_cdrom_size(index);
	}
	else if (r == DECALPHA_CDROM_REJECT)
	{
		FileClose(f);
		return 0;
	}
	else *writable = 0;
	return 1;
}

void decalpha_unmount(int index)
{
	if (!is_decalpha()) return;
	decalpha_disk_flush(index);
	decalpha_cdrom_unmount(index);
}

void decalpha_poll()
{
	if (!is_decalpha()) return;
	decalpha_disk_poll();
}

int decalpha_sd_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack)
{
	if (!op || !is_decalpha()) return 0;
	if (decalpha_disk_service(disk, f, op, lba, sz, ack)) return 1;

	if (decalpha_cdrom_active(disk) || (disk == decalpha_cdrom_slot() && lba >= DECALPHA_CDROM_AUDIO_BLK))
	{
		static uint8_t buf[UIO_BUFFER_SIZE];
		if (!(op & 1) || sz > (int)sizeof(buf)) return -1;

		if (!decalpha_cdrom_window(disk, lba, buf, sz))
			decalpha_cdrom_fill(disk, lba, buf, sz);
		EnableIO();
		spi_w(UIO_SECTOR_RD | ack);
		spi_block_write(buf, user_io_get_width(), sz);
		DisableIO();
		return 1;
	}

	return 0;
}
