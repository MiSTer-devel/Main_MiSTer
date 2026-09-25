#include <stdio.h>
#include <string.h>

#include "../../user_io.h"
#include "../../spi.h"
#include "../../file_io.h"
#include "next_scsi.h"
#include "next_cdrom.h"
#include "next_mo.h"

static uint64_t unit_size[NEXT_SCSI_UNITS];
static uint8_t  unit_ro[NEXT_SCSI_UNITS];

static int unit_is_cd(unsigned unit)
{
	return unit == NEXT_CDROM_SLOT;
}

static uint32_t unit_blocks(unsigned unit)
{
	return (unit < NEXT_SCSI_UNITS) ? (uint32_t)(unit_size[unit] >> 9) : 0;
}

int next_mount_hook(int index, const char *name, fileTYPE *f, int *writable)
{
	if (!is_next()) return 1;
	if (index == NEXT_CDROM_SLOT)
	{
		if (next_cdrom_mount(index, name) != NEXT_CDROM_HANDLED)
		{
			FileClose(f);
			return 0;
		}
		*writable = 0;
		f->size = (int64_t)next_cdrom_size(index);
	}
	if (index < NEXT_SCSI_UNITS)
	{
		unit_size[index] = (uint64_t)f->size;
		unit_ro[index] = !*writable;
	}
	return 1;
}

void next_unmount(int index)
{
	if (!is_next()) return;
	if (index == NEXT_CDROM_SLOT) next_cdrom_unmount(index);
	if (index >= 0 && index < NEXT_SCSI_UNITS) unit_size[index] = 0;
}

static int resp_inquiry(unsigned unit, int lun_nz, uint8_t *out)
{
	int cd = unit_is_cd(unit);
	out[0] = lun_nz ? 0x7F : cd ? 0x05 : 0x00;
	out[1] = cd ? 0x80 : 0x00;
	out[2] = 0x01;
	out[3] = 0x01;
	out[4] = 0x31;
	out[7] = 0x1C;
	memcpy(out + 8, "Previous", 8);
	memcpy(out + 16, cd ? "CD-ROM          " : "HDD             ", 16);
	out[32] = cd ? '1' : 'B';
	return 54;
}

static int resp_capacity(unsigned unit, uint8_t *out)
{
	uint32_t last = unit_blocks(unit) - 1;
	out[0] = (uint8_t)(last >> 24); out[1] = (uint8_t)(last >> 16);
	out[2] = (uint8_t)(last >> 8);  out[3] = (uint8_t)last;
	out[6] = 0x02;
	return 8;
}

static int page_bytes(unsigned unit, uint8_t page, uint8_t *o)
{
	uint32_t blocks = unit_blocks(unit);
	uint32_t cyl = (blocks >> 7) + ((blocks & 127) ? 1 : 0);
	switch (page)
	{
	case 0x00:
		o[1] = 0x02; o[2] = 0x80;
		return 4;
	case 0x01:
		o[0] = 0x01; o[1] = 0x02; o[3] = 0x1B;
		return 4;
	case 0x03:
		o[0] = 0x03; o[1] = 0x16; o[11] = 32; o[12] = 0x02; o[15] = 0x01; o[20] = 0x80;
		return 24;
	case 0x04:
		o[0] = 0x04; o[1] = 0x12;
		o[2] = (uint8_t)(cyl >> 16); o[3] = (uint8_t)(cyl >> 8); o[4] = (uint8_t)cyl;
		o[5] = 4;
		return 20;
	case 0x0E:
		if (!unit_is_cd(unit)) return 0;
		o[0] = 0x0E; o[1] = 0x0E; o[2] = 0x04; o[6] = 75; o[7] = 75;
		memcpy(o + 8, next_cdrom_ports(), 4);
		return 16;
	case 0x2A:
		if (!unit_is_cd(unit)) return 0;
		o[0] = 0x2A; o[1] = 0x18; o[4] = 0x71; o[6] = 0x28; o[7] = 0x03; o[10] = 0x01;
		return 26;
	default:
		return 0;
	}
}

static int resp_mode_sense(unsigned unit, int dbd, uint8_t cdb2, uint8_t *out)
{
	uint8_t page = cdb2 & 0x3F;
	unsigned pc = cdb2 >> 6;
	uint32_t blocks = unit_blocks(unit);
	int hdr = dbd ? 4 : 12;
	int len = hdr;
	if (pc == 1 || pc == 3) return 0;
	if (page == 0x3F)
	{
		len += page_bytes(unit, 0x01, out + len);
		len += page_bytes(unit, 0x03, out + len);
		len += page_bytes(unit, 0x04, out + len);
		len += page_bytes(unit, 0x00, out + len);
	}
	else
	{
		int n = page_bytes(unit, page, out + len);
		if (!n) return 0;
		len += n;
	}
	out[0] = (uint8_t)(len - 1);
	out[2] = (unit_ro[unit] || unit_is_cd(unit)) ? 0x80 : 0x00;
	out[3] = 0x08;
	if (!dbd)
	{
		out[5] = (uint8_t)(blocks >> 16); out[6] = (uint8_t)(blocks >> 8); out[7] = (uint8_t)blocks;
		out[10] = 0x02;
	}
	return len;
}

void next_scsi_window_fill(uint32_t lba, uint8_t *buf, int sz)
{
	memset(buf, 0, sz);
	if (lba == NEXT_FRAME_BLK) { next_cdrom_frame(buf, sz); return; }
	if (lba < NEXT_RESP_BLK || sz < 512) return;

	unsigned unit = (lba >> 20) & 0xF;
	unsigned fl   = (lba >> 16) & 0xF;
	unsigned op   = (lba >> 8) & 0xFF;
	uint8_t  a    = (uint8_t)lba;
	int len = 0;
	if (unit >= NEXT_SCSI_UNITS) return;

	switch (op)
	{
	case 0x12: len = resp_inquiry(unit, fl & 8, buf); break;
	case 0x25: len = resp_capacity(unit, buf); break;
	case 0x1A: len = resp_mode_sense(unit, fl & 1, a, buf); break;
	case 0x43:
		if (unit_is_cd(unit) && next_cdrom_toc())
			len = next_cd_resp_toc(next_cdrom_toc(), fl & 1, (fl >> 1) & 3, a, buf);
		break;
	case 0x42:
		if (unit_is_cd(unit))
		{
			next_cd_pos pos;
			next_cdrom_pos(&pos);
			len = next_cd_resp_subch(&pos, fl & 1, fl & 2, a, buf);
		}
		break;
	default: break;
	}
	buf[NEXT_RESP_LEN]     = (uint8_t)(len >> 8);
	buf[NEXT_RESP_LEN + 1] = (uint8_t)len;
}

int next_sd_service(int disk, int op, uint32_t lba, int sz, int ack)
{
	static uint8_t buf[4096];
	if (!is_next() || sz > (int)sizeof(buf)) return 0;

	if (disk == NEXT_CDROM_SLOT && lba >= NEXT_WIN_BASE)
	{
		if (op == 2)
		{
			EnableIO();
			spi_w(UIO_SECTOR_WR | ack);
			spi_block_read(buf, user_io_get_width(), sz);
			DisableIO();
			next_cdrom_command(lba, buf, sz);
		}
		else if (op & 1)
		{
			next_scsi_window_fill(lba, buf, sz);
			EnableIO();
			spi_w(UIO_SECTOR_RD | ack);
			spi_block_write(buf, user_io_get_width(), sz);
			DisableIO();
		}
		else return -1;
		return 1;
	}

	if (disk == NEXT_MO_SLOT && lba >= NEXT_WIN_BASE)
	{
		if (op == 2)
		{
			EnableIO();
			spi_w(UIO_SECTOR_WR | ack);
			spi_block_read(buf, user_io_get_width(), sz);
			DisableIO();
			next_mo_command(lba, buf, sz);
		}
		else if (op & 1)
		{
			next_mo_fill(lba, buf, sz);
			EnableIO();
			spi_w(UIO_SECTOR_RD | ack);
			spi_block_write(buf, user_io_get_width(), sz);
			DisableIO();
		}
		else return -1;
		return 1;
	}

	if (next_cdrom_active(disk))
	{
		if (op & 1)
		{
			next_cdrom_fill(disk, lba, buf, sz);
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
