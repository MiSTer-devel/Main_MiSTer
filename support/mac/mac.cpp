// Mac SCSI family glue — the user_io hooks (see mac.h).

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "../../user_io.h"
#include "../../spi.h"
#include "../../file_io.h"
#include "mac.h"

static char is_core_named(const char *n)
{
	size_t len = strlen(n);
	return !strncasecmp(user_io_get_core_name(0), n, len)
	    || !strncasecmp(user_io_get_core_name(1), n, len);
}

char is_mac_scsi_family()
{
	return is_core_named("maclc") || is_core_named("lbmactwo") || is_core_named("maciivi");
}

#define MAC_TOOLBOX_SLOT    3   // MacLC.sv VD_TOOLBOX
#define MAC_CD_TOOLBOX_SLOT 5   // MacLC.sv VD_CD_TOOLBOX

static int mac_slots(void)
{
	return is_mac_scsi_family() && !is_core_named("lbmactwo");
}

int mac_toolbox_slot()    { return mac_slots() ? MAC_TOOLBOX_SLOT    : -1; }
int mac_cdrom_slot()      { return mac_slots() ? MAC_CDROM_SLOT      : -1; }
int mac_cd_toolbox_slot() { return mac_slots() ? MAC_CD_TOOLBOX_SLOT : -1; }

// CD image translation on the CD-ROM slot: CUE/CHD/raw-2352 become a flat
// 2048-byte-sector virtual disc; flat ISO/TOAST stays on the generic path.
int mac_mount_hook(int index, const char *name, fileTYPE *f, int *writable)
{
	if (index != mac_cdrom_slot()) return 1;

	int r = mac_cdrom_mount(index, name);
	if (r == MAC_CDROM_HANDLED)
	{
		*writable = 0;
		f->size = (int64_t)mac_cdrom_size(index);   // core sees the virtual disc
	}
	else if (r == MAC_CDROM_REJECT)
	{
		FileClose(f);
		return 0;
	}
	return 1;
}

// Announce a Toolbox slot once per core session: the UIO_SET_SDSTAT pulse
// sets img_mounted[slot], which latches the core-side ready flag.
static void announce_slot(int slot)
{
	spi_uio_cmd8(UIO_SET_SDSTAT, (uint8_t)((1 << slot) | 0x80));
}

void mac_poll()
{
	static int tb_inited, cdc_inited;

	int tb_slot = mac_toolbox_slot();
	if (tb_slot >= 0 && (tb_inited || toolbox_shared_ready()))
	{
		if (!tb_inited)
		{
			announce_slot(tb_slot);
			printf("BlueSCSI Toolbox: shared folder \"%s\" on slot %d\n", toolbox_shared_dir(), tb_slot);
			tb_inited = 1;
		}
	}
	else tb_inited = 0;

	// The CD3 folder may appear later: LIST just returns empty until then.
	int cdc_slot = mac_cd_toolbox_slot();
	if (cdc_slot >= 0)
	{
		if (!cdc_inited)
		{
			announce_slot(cdc_slot);
			printf("BlueSCSI CD Changer: control slot %d (folder \"%s\")\n", cdc_slot, cdchanger_dir());
			cdc_inited = 1;
		}
		cdchanger_poll();   // perform any staged SET NEXT CD image remount
		mac_cdrom_poll();   // one-shot boot repulse of the CD mount
	}
	else cdc_inited = 0;
}

int mac_cdda_window(int disk, uint32_t lba)
{
	return disk == MAC_CDROM_SLOT && is_mac_scsi_family() &&
	       lba >= MAC_CDROM_AUDIO_BLK && lba < MAC_CDROM_TOC_BLK;
}

int mac_sd_service(int disk, int op, uint32_t lba, int sz, int ack)
{
	static uint8_t buf[4096];
	if (sz > (int)sizeof(buf)) return 0;

	// Toolbox round-trip (file or CD-changer slot): op 2 = the CDB -> run the
	// op and stage the reply; op 1 = status (lba 0) or DataIn (lba 1+k).
	if (disk == mac_toolbox_slot() || disk == mac_cd_toolbox_slot())
	{
		int cdc = (disk == mac_cd_toolbox_slot());
		if (op == 2)
		{
			EnableIO();
			spi_w(UIO_SECTOR_WR | ack);
			spi_block_read(buf, user_io_get_width(), sz);
			DisableIO();
			cdc ? cdchanger_request(lba, buf, sz) : toolbox_request(lba, buf, sz);
		}
		else if (op & 1)
		{
			cdc ? cdchanger_fill(lba, buf, sz) : toolbox_fill(lba, buf, sz);
			EnableIO();
			spi_w(UIO_SECTOR_RD | ack);
			spi_block_write(buf, user_io_get_width(), sz);
			DisableIO();
		}
		else return -1;
		return 1;
	}

	// CD slot with translation active: read-only views of the virtual disc
	// (the core ties sd_wr off). Flat images stay on the generic path.
	if (mac_cdrom_active(disk))
	{
		if (op & 1)
		{
			mac_cdrom_fill(disk, lba, buf, sz);
			EnableIO();
			spi_w(UIO_SECTOR_RD | ack);
			spi_block_write(buf, user_io_get_width(), sz);
			DisableIO();
		}
		else return -1;
		return 1;
	}

	return 0;
}
