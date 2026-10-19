// HP 9000 family: the user_io hooks.

#include <stdint.h>
#include <string.h>
#include <strings.h>

#include "../../user_io.h"
#include "../../spi.h"
#include "../../file_io.h"
#include "hp9k.h"
#include "hp9k_cdrom.h"
#include "hp9k_disk.h"

struct hp9k_model_info
{
	const char  *core;
	hp9k_model_t model;
	int          cdrom_slot;
	const char  *net_status;
	int          disk_wbuf;
};

static const hp9k_model_info models[] =
{
	{ "HP9K715", HP9K_715, 3, "[14:12]", 0x0 },
	{ "HP9K385", HP9K_385, 2, "[14:12]", 0x3 },
};

static const hp9k_model_info *model_info()
{
	// the RBF name, not the CONF_STR override; it only changes with a new core
	const char *name = user_io_get_core_name(1);
	for (const hp9k_model_info &m : models)
		if (!strcasecmp(name, m.core)) return &m;
	return 0;
}

hp9k_model_t hp9k_model()
{
	const hp9k_model_info *m = model_info();
	return m ? m->model : HP9K_NONE;
}

char is_hp9k_family()
{
	return model_info() != 0;
}

const char *hp9k_net_status()
{
	const hp9k_model_info *m = model_info();
	return m ? m->net_status : 0;
}

int hp9k_cdrom_slot()
{
	const hp9k_model_info *m = model_info();
	return m ? m->cdrom_slot : -1;
}

int hp9k_buffered_disks()
{
	const hp9k_model_info *m = model_info();
	return m ? m->disk_wbuf : 0;
}

int hp9k_mount_hook(int index, const char *name, fileTYPE *f, int *writable)
{
	if (!is_hp9k_family()) return 1;

	hp9k_disk_flush(index);
	if (index != hp9k_cdrom_slot()) return 1;

	int r = hp9k_cdrom_mount(index, name);
	if (r == HP9K_CDROM_HANDLED)
	{
		*writable = 0;
		f->size = (int64_t)hp9k_cdrom_size(index);
	}
	else if (r == HP9K_CDROM_REJECT)
	{
		FileClose(f);
		return 0;
	}
	return 1;
}

void hp9k_unmount(int index)
{
	if (!is_hp9k_family()) return;
	hp9k_disk_flush(index);
	hp9k_cdrom_unmount(index);
}

void hp9k_poll()
{
	hp9k_disk_poll();
}

int hp9k_sd_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack)
{
	if (!op || !is_hp9k_family()) return 0;
	if (hp9k_disk_service(disk, f, op, lba, sz, ack)) return 1;

	// translated CD, or the CD-audio windows of any CD (read-only)
	if (hp9k_cdrom_active(disk) || (disk == hp9k_cdrom_slot() && lba >= HP9K_CDROM_AUDIO_BLK))
	{
		static uint8_t buf[UIO_BUFFER_SIZE];
		if (!(op & 1) || sz > (int)sizeof(buf)) return -1;

		if (!hp9k_cdrom_window(disk, lba, buf, sz))
			hp9k_cdrom_fill(disk, lba, buf, sz);
		EnableIO();
		spi_w(UIO_SECTOR_RD | ack);
		spi_block_write(buf, user_io_get_width(), sz);
		DisableIO();
		return 1;
	}

	return 0;
}
