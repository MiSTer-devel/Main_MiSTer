// PC-8801 floppy images (slots 0 and 1).

#include <string.h>
#include <strings.h>
#include "../../file_io.h"
#include "../../user_io.h"
#include "../../spi.h"
#include "pc88.h"

static uint8_t buf[UIO_BUFFER_SIZE];
static uint32_t grow_to[2];

static char is_pc88()
{
	return !strcasecmp(user_io_get_core_name(1), "PC8801");
}

void pc88_mount(int index)
{
	if (index >= 0 && index < 2) grow_to[index] = 0;
}

static void read_blocks(int disk, fileTYPE *f, uint64_t lba, int sz, int ack)
{
	uint64_t size = f->size;
	uint64_t off = lba * 512;

	if (!lba) grow_to[disk] = 0;
	memset(buf, 0, sz);
	if (off < size)
	{
		uint64_t n = size - off;
		if (n > (uint64_t)sz) n = sz;
		diskled_on();
		if (FileSeek(f, off, SEEK_SET)) FileReadAdv(f, buf, n);
	}
	EnableIO();
	spi_w(UIO_SECTOR_RD | ack);
	spi_block_write(buf, user_io_get_width(), sz);
	DisableIO();
}

static void write_blocks(int disk, fileTYPE *f, uint64_t lba, int sz, int ack)
{
	uint32_t new_size = 0;

	EnableIO();
	spi_w(UIO_SECTOR_WR | ack);
	spi_block_read(buf, user_io_get_width(), sz);
	DisableIO();

	if (!lba)
	{
		grow_to[disk] = 0;
		new_size = buf[0x1C] | (buf[0x1D] << 8) | (buf[0x1E] << 16) | ((uint32_t)buf[0x1F] << 24);
		if (new_size > 4 * 1024 * 1024) new_size = 0;
	}

	for (int i = 0; i < sz; i += 512)
	{
		uint64_t size = f->size;
		uint64_t off = lba * 512 + i;
		uint64_t n = 512;

		if (off >= size)
		{
			if (off + n > grow_to[disk]) continue;
		}
		else if (off + n > size)
		{
			n = size - off;
		}
		diskled_on();
		if (FileSeek(f, off, SEEK_SET) && FileWriteAdv(f, buf + i, n) && !off) grow_to[disk] = new_size;
	}
}

int pc88_sd_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack)
{
	if (disk < 0 || disk > 1 || !is_pc88() || f->size < 512) return 0;
	if (op == 2) write_blocks(disk, f, lba, sz, ack);
	else if (op & 1) read_blocks(disk, f, lba, sz, ack);
	else return -1;
	return 1;
}
