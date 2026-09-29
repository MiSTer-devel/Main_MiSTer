#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../../file_io.h"
#include "../../user_io.h"
#include "../../spi.h"
#include "../../menu.h"

#include "../a2/iigs_fmt.h"
#include "a3_woz.h"
#include "a3_disk.h"

#define SLOTS        8
#define FLOPPY_SLOTS 4

enum { MODE_NONE, MODE_BLOCK, MODE_FLOPPY };

static struct
{
	int mode;
	int readonly;
	int native;
	int nib;
	int prodos;
	int pending;
	int64_t off;
	uint8_t *woz;
	size_t woz_sz;
} g[SLOTS];

static uint8_t buf[UIO_BUFFER_SIZE];

static int eqi(const char *a, const char *b)
{
	return a && b && !strcasecmp(a, b);
}

static char is_apple3()
{
	return !strcasecmp(user_io_get_core_name(0), "Apple-III") ||
		!strcasecmp(user_io_get_core_name(1), "Apple-III");
}

void a3_unmount(int index)
{
	if (index < 0 || index >= SLOTS) return;
	free(g[index].woz);
	memset(&g[index], 0, sizeof(g[index]));
	g[index].pending = -1;
}

static uint8_t *read_all(fileTYPE *f, size_t *out_len)
{
	size_t n = (size_t)f->size;
	uint8_t *b = (uint8_t *)malloc(n ? n : 1);
	if (!b) return NULL;
	FileSeek(f, 0, SEEK_SET);
	size_t got = 0;
	while (got < n)
	{
		int r = FileReadAdv(f, b + got, n - got);
		if (r <= 0) break;
		got += r;
	}
	FileSeek(f, 0, SEEK_SET);
	if (got != n)
	{
		free(b);
		return NULL;
	}
	*out_len = n;
	return b;
}

static int sos_directory(const uint8_t *block)
{
	return (block[4] >> 4) == 0xf && block[4 + 0x1f] == 39 &&
		(block[4 + 0x20] == 12 || block[4 + 0x20] == 13);
}

static int prodos_order(const uint8_t *pay, const char *ext)
{
	A2SectorOrder order = a2_detect_525_order(pay, A2_525_IMAGE_SIZE);
	if (order != A2_ORDER_UNKNOWN) return order == A2_ORDER_PRODOS;
	if (sos_directory(pay + 2 * 512)) return 1;
	uint8_t block[512];
	memcpy(block, pay + 11 * 256, 256);
	memcpy(block + 256, pay + 10 * 256, 256);
	if (sos_directory(block)) return 0;
	return eqi(ext, "po");
}

static const char *mount_block(int index, fileTYPE *f, const uint8_t *head, const char *ext,
	const TwoMG *m, int dc42, int wt, int *writable)
{
	if (wt > 0 || f->size == A2_NIB_IMAGE_SIZE) return "Block device needs a ProDOS block image, not a floppy.";
	if (m && m->format != 1) return "This 2MG is not a ProDOS-order block image.";
	if (!m && !dc42 && eqi(ext, "do")) return "Use a ProDOS-order PO/HDV image in a block-device slot.";
	int64_t off = 0, payload = f->size;
	if (m)
	{
		off = m->data_offset;
		payload = m->data_len;
	}
	else if (dc42)
	{
		DC42 d;
		dc42_parse(head, f->size, &d);
		off = 84;
		payload = d.data_size;
	}
	if (!off) return NULL;  // raw po/hdv: generic path serves it
	g[index].mode = MODE_BLOCK;
	g[index].off = off;
	g[index].readonly = !*writable;
	f->size = payload;
	return NULL;
}

static const char *mount_floppy(int index, fileTYPE *f, const char *ext,
	const TwoMG *m, int dc42, int wt, int *writable)
{
	if (wt > 0)
	{
		if (wt != 1) return "Disk III needs a 5.25\" WOZ.";
		g[index].native = 1;
		g[index].readonly = !*writable;
		if (f->zip)
		{
			// zip is slow to seek back, serve it from RAM
			size_t n = 0;
			uint8_t *woz = read_all(f, &n);
			if (!woz) return "Could not read the disk image.";
			g[index].mode = MODE_FLOPPY;
			g[index].woz = woz;
			g[index].woz_sz = n;
			return NULL;
		}
		return NULL;
	}

	size_t raw_len = 0;
	uint8_t *raw = read_all(f, &raw_len);
	if (!raw) return "Could not read the disk image.";
	const uint8_t *pay = raw;
	size_t pay_len = raw_len;
	int prodos = 0, nib = 0, volume = 0;
	if (m)
	{
		pay = raw + m->data_offset;
		pay_len = m->data_len;
		prodos = m->format == 1;
		nib = m->format == 2;
		if (m->flags & 0x100) volume = m->flags & 255;
	}
	else if (dc42)
	{
		DC42 d;
		dc42_parse(raw, raw_len, &d);
		pay = raw + 84;
		pay_len = d.data_size;
		prodos = 1;
	}
	else if (raw_len == A2_NIB_IMAGE_SIZE)
	{
		nib = 1;
	}
	else if (raw_len == A2_525_IMAGE_SIZE)
	{
		prodos = prodos_order(raw, ext);
	}

	size_t cap = 512 * 1024, n = 0;
	uint8_t *woz = (uint8_t *)malloc(cap);
	if (woz && nib && pay_len == A2_NIB_IMAGE_SIZE)
	{
		n = a3_nib_to_woz(woz, cap, pay);
	}
	else if (woz && !nib && pay_len == A2_525_IMAGE_SIZE)
	{
		static uint8_t dsk[A2_525_IMAGE_SIZE];
		if (prodos) a2_prodos_to_dos(dsk, pay);
		else memcpy(dsk, pay, sizeof(dsk));
		if (!volume) volume = a3_dos33_volume(dsk);
		// key only for encrypted SOS.INTERP, plain one would fail with SYSTEM FAILURE $06
		n = a3_dsk_to_woz(woz, cap, dsk, a3_sos_interp_encrypted(dsk), volume ? volume : 254);
	}
	int64_t off = pay - raw;
	free(raw);
	if (!n)
	{
		free(woz);
		return "Disk III needs a 140K DSK/DO/PO, a NIB or a WOZ image.";
	}
	g[index].mode = MODE_FLOPPY;
	g[index].nib = nib;
	g[index].prodos = prodos;
	g[index].off = off;
	g[index].woz = woz;
	g[index].woz_sz = n;
	g[index].readonly = !*writable || dc42;
	f->size = n;
	*writable = !g[index].readonly;
	return NULL;
}

int a3_mount_hook(int index, const char *name, fileTYPE *f, int *writable)
{
	if (!is_apple3() || index < 0 || index >= SLOTS) return 1;
	a3_unmount(index);

	uint8_t head[128] = {};
	size_t hn = f->size < (int64_t)sizeof(head) ? (size_t)f->size : sizeof(head);
	FileSeek(f, 0, SEEK_SET);
	FileReadAdv(f, head, hn);
	FileSeek(f, 0, SEEK_SET);
	const char *dot = strrchr(name, '.');
	const char *ext = dot ? dot + 1 : "";
	TwoMG m;
	const int is_2mg = twomg_parse(head, f->size, &m);
	const int dc42 = dc42_probe(head, f->size);
	const int wt = woz_disk_type(head, hn);

	const char *msg;
	if (index >= FLOPPY_SLOTS)
		msg = mount_block(index, f, head, ext, is_2mg ? &m : NULL, dc42, wt, writable);
	else
		msg = mount_floppy(index, f, ext, is_2mg ? &m : NULL, dc42, wt, writable);

	if (!msg)
	{
		printf("Apple ///: slot %d %s, %lld bytes, %s\n", index,
			index >= FLOPPY_SLOTS ? "block image" : g[index].native ? "WOZ" : "converted to WOZ",
			(long long)f->size, *writable ? "read-write" : "read-only");
		return 1;
	}
	a3_unmount(index);
	printf("Apple ///: slot %d rejected: %s\n", index, msg);
	InfoMessage(msg, 5000, "Apple ///");
	FileClose(f);
	return 0;
}

static void store_pending(int disk, fileTYPE *f)
{
	int t = g[disk].pending;
	g[disk].pending = -1;
	if (t < 0) return;
	uint8_t trk[A2_TRACK_SIZE], out[A2_TRACK_SIZE], volumes[16];
	int found = a3_decode_track(g[disk].woz, g[disk].woz_sz, t, trk, volumes);
	if (g[disk].nib)
	{
		if (found != 16) return;
		static uint8_t nibtrk[A2_NIB_TRACK_SIZE];
		a3_nib_track(nibtrk, trk, t, volumes);
		if (FileSeek(f, g[disk].off + (int64_t)t * A2_NIB_TRACK_SIZE, SEEK_SET)) FileWriteAdv(f, nibtrk, A2_NIB_TRACK_SIZE);
		return;
	}
	if (!found) return;
	if (g[disk].prodos) a2_dos_track_to_prodos(out, trk);
	else memcpy(out, trk, A2_TRACK_SIZE);
	// one write per track, 16 sector writes with O_SYNC are too slow for SOS formatter
	if (FileSeek(f, g[disk].off + (int64_t)t * A2_TRACK_SIZE, SEEK_SET)) FileWriteAdv(f, out, A2_TRACK_SIZE);
}

// core saves the track block by block, so decode only after its last block
static void converted_write(int disk, fileTYPE *f, uint64_t lba, const uint8_t *block)
{
	if (lba * 512 + 512 > g[disk].woz_sz) return;
	memcpy(g[disk].woz + lba * 512, block, 512);
	int t = a2_woz_track_for_lba(g[disk].woz, g[disk].woz_sz, (uint32_t)lba);
	if (t < 0) return;
	if (g[disk].pending >= 0 && g[disk].pending != t) store_pending(disk, f);
	g[disk].pending = t;
	if (a2_woz_track_for_lba(g[disk].woz, g[disk].woz_sz, (uint32_t)lba + 1) != t) store_pending(disk, f);
}

static void read_blocks(int disk, fileTYPE *f, uint64_t lba, int sz, int ack)
{
	if (g[disk].pending >= 0) store_pending(disk, f);
	memset(buf, 0, sz);
	uint64_t off = lba * 512;
	if (off < (uint64_t)f->size)
	{
		size_t n = (uint64_t)f->size - off < (unsigned)sz ? (size_t)((uint64_t)f->size - off) : (size_t)sz;
		diskled_on();
		if (g[disk].mode == MODE_FLOPPY)
		{
			if (g[disk].woz && off + n <= g[disk].woz_sz) memcpy(buf, g[disk].woz + off, n);
		}
		else if (FileSeek(f, off + g[disk].off, SEEK_SET))
		{
			FileReadAdv(f, buf, n);
		}
	}
	EnableIO();
	spi_w(UIO_SECTOR_RD | ack);
	spi_block_write(buf, user_io_get_width(), sz);
	DisableIO();
}

static void write_blocks(int disk, fileTYPE *f, uint64_t lba, int sz, int ack)
{
	EnableIO();
	spi_w(UIO_SECTOR_WR | ack);
	spi_block_read(buf, user_io_get_width(), sz);
	DisableIO();

	if (g[disk].readonly) return;
	diskled_on();
	if (g[disk].mode == MODE_BLOCK)
	{
		if (FileSeek(f, lba * 512 + g[disk].off, SEEK_SET)) FileWriteAdv(f, buf, sz);
	}
	else
	{
		for (int i = 0; i < sz; i += 512) converted_write(disk, f, lba + i / 512, buf + i);
	}
}

int a3_sd_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack)
{
	if (disk < 0 || disk >= SLOTS || !g[disk].mode || !is_apple3()) return 0;
	if (op == 2) write_blocks(disk, f, lba, sz, ack);
	else if (op & 1) read_blocks(disk, f, lba, sz, ack);
	else return -1;
	return 1;
}
