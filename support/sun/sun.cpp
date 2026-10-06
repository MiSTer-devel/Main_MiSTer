// Sun family: the user_io hooks.

#include <stdint.h>
#include <string.h>
#include <strings.h>

#include "../../user_io.h"
#include "../../spi.h"
#include "../../file_io.h"
#include "sun.h"
#include "sun_disk.h"
#include "sun_cdrom.h"

static char is_core_named(const char *n)
{
	size_t len = strlen(n);
	return !strncasecmp(user_io_get_core_name(0), n, len)
	    || !strncasecmp(user_io_get_core_name(1), n, len);
}

// Prefix match: list a name before any shorter name it starts with.
sun_model_t sun_model()
{
	if (is_core_named("SunSparcStation")) return SUN_SPARCSTATION;
	if (is_core_named("Sun-2"))           return SUN_2;
	if (is_core_named("Sun-3"))           return SUN_3;
	return SUN_NONE;
}

char is_sun_family()
{
	return sun_model() != SUN_NONE;
}

const char *sun_net_status()
{
	switch (sun_model())
	{
	case SUN_2:
	case SUN_3:            return "[11:9]";
	case SUN_SPARCSTATION: return "[26:24]";
	case SUN_NONE:         break;
	}
	return 0;
}

uint8_t sun_idprom_machine()
{
	switch (sun_model())
	{
	case SUN_2:            return 0x02;
	case SUN_3:            return 0x17;
	case SUN_SPARCSTATION: return 0x72;
	case SUN_NONE:         break;
	}
	return 0;
}

int sun_buffered_disks()
{
	switch (sun_model())
	{
	case SUN_2:            return 0;
	case SUN_3:
	case SUN_SPARCSTATION: return 2;
	case SUN_NONE:         break;
	}
	return 0;
}

int sun_cdrom_slot()
{
	switch (sun_model())
	{
	case SUN_SPARCSTATION: return 2;
	case SUN_2:
	case SUN_3:            return -1;     // slot 2 is its tape
	case SUN_NONE:         break;
	}
	return -1;
}

int sun_mount_hook(int index, const char *name, fileTYPE *f, int *writable)
{
	if (!is_sun_family()) return 1;

	sun_disk_flush(index);
	if (index != sun_cdrom_slot()) return 1;

	int r = sun_cdrom_mount(index, name);
	if (r == SUN_CDROM_HANDLED)
	{
		*writable = 0;
		f->size = (int64_t)sun_cdrom_size(index);   // core sees the virtual disc
	}
	else if (r == SUN_CDROM_REJECT)
	{
		FileClose(f);
		return 0;
	}
	return 1;
}

void sun_unmount(int index)
{
	if (!is_sun_family()) return;
	sun_disk_flush(index);
	sun_cdrom_unmount(index);
}

void sun_poll()
{
	sun_disk_poll();
}

int sun_sd_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack)
{
	if (!op || !is_sun_family()) return 0;
	if (sun_disk_service(disk, f, op, lba, sz, ack)) return 1;

	// translated CD: read-only, the core ties sd_wr off. A flat image stays
	// on the generic path, but for the TOC and audio windows above the data
	// (the SunSparcStation's CD audio).
	if (sun_cdrom_active(disk) || (disk == sun_cdrom_slot() && lba >= SUN_CDROM_AUDIO_BLK))
	{
		static uint8_t buf[UIO_BUFFER_SIZE];
		if (!(op & 1) || sz > (int)sizeof(buf)) return -1;

		if (!sun_cdrom_window(disk, lba, buf, sz))
			sun_cdrom_fill(disk, lba, buf, sz);
		EnableIO();
		spi_w(UIO_SECTOR_RD | ack);
		spi_block_write(buf, user_io_get_width(), sz);
		DisableIO();
		return 1;
	}

	return 0;
}
