// PC-8801 floppy images (slots 0 and 1).
// A D88 file can hold several disks. Then only one of them is mounted: the core sees that disk alone,
// nothing outside it is written and the file never grows.

#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "../../file_io.h"
#include "../../user_io.h"
#include "../../spi.h"
#include "pc88.h"

#define D88_HDR   0x2B0
#define D88_MAX   (4 * 1024 * 1024)
#define D88_DISKS 64

struct d88_slot
{
	int image;
	uint64_t start, size;
	int tracked;
	dev_t dev;
	ino_t ino;
	int rw;
	int ro;
	char label[32];
};

static uint8_t buf[UIO_BUFFER_SIZE];
static uint32_t grow_to[2];
static d88_slot slots[2];

static char is_pc88()
{
	return !strcasecmp(user_io_get_core_name(1), "PC8801");
}

static uint32_t le32(const uint8_t *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int is_d88(const char *name)
{
	const char *ext = strrchr(name, '.');
	return ext && (!strcasecmp(ext, ".d88") || !strcasecmp(ext, ".d77"));
}

static int file_read(fileTYPE *f, uint64_t off, void *p, int len)
{
	if (f->filp) return pread(fileno(f->filp), p, len, off) == len;
	return f->zip && FileSeek(f, off, SEEK_SET) && FileReadAdv(f, p, len) == len;
}

// Disk names drop Shift-JIS and keep printable ASCII; a disk without one is named by its number.
static void make_name(const uint8_t *raw, int image, char *out, int len)
{
	char name[18];
	int n = 0;

	for (int i = 0; i < 17 && raw[i]; i++)
	{
		uint8_t c = raw[i];
		if ((c >= 0x81 && c <= 0x9F) || c >= 0xE0) i++;  // Shift-JIS pair
		else if (c >= 0x20 && c < 0x7F && (n || c != ' ')) name[n++] = c;
	}
	while (n && name[n - 1] == ' ') n--;
	name[n] = 0;

	if (n) snprintf(out, len, "%d %s", image, name);
	else snprintf(out, len, "Disk %d", image);
}

// Number of disks when their size fields fill the file exactly, otherwise 0.
// One pass forward, so a zip is read through only once.
static int scan(fileTYPE *f, int image, uint64_t *start, uint64_t *size, char (*names)[32] = NULL)
{
	uint64_t off = 0;
	int n = 0;

	while (off < (uint64_t)f->size)
	{
		uint8_t h[0x20];
		if (n == D88_DISKS || off + D88_HDR > (uint64_t)f->size || !file_read(f, off, h, sizeof(h))) return 0;

		uint32_t sz = le32(h + 0x1C);
		if (sz < D88_HDR || sz > D88_MAX || off + sz > (uint64_t)f->size) return 0;

		if (++n == image)
		{
			*start = off;
			*size = sz;
		}
		if (names) make_name(h, n, names[n - 1], sizeof(names[0]));
		off += sz;
	}

	return (n > 1) ? n : 0;
}

static char names[D88_DISKS][32];
static int names_n;

// Counts the disks and keeps their names for pc88_disk_name().
int pc88_disk_count(int index, const char *path)
{
	names_n = 0;
	if (index < 0 || index > 1 || !is_pc88() || !is_d88(path)) return 0;

	fileTYPE f;
	if (!FileOpen(&f, path, 1)) return 0;

	uint64_t start, size;
	names_n = scan(&f, 0, &start, &size, names);
	FileClose(&f);
	return names_n;
}

// Name of a disk of the file last given to pc88_disk_count().
const char *pc88_disk_name(int image)
{
	return (image > 0 && image <= names_n) ? names[image - 1] : "";
}

// Image 0 (MGL and other mounts without a choice) mounts the whole file as before.
// Returns 0 when the chosen disk is no longer in the file, so nothing is mounted.
int pc88_mount(int index, fileTYPE *f, const char *name, int image, int *writable)
{
	if (index < 0 || index > 1) return 1;
	grow_to[index] = 0;

	d88_slot *s = &slots[index];
	memset(s, 0, sizeof(*s));
	if (!f || !is_pc88()) return 1;

	if (image > 0)
	{
		if (!name || !is_d88(name) || scan(f, image, &s->start, &s->size) < image)
		{
			printf("pc88: disk %d not found\n", image);
			return 0;
		}

		uint8_t raw[17];
		s->image = image;
		if (!file_read(f, s->start, raw, sizeof(raw))) raw[0] = 0;
		make_name(raw, image, s->label, sizeof(s->label));
	}

	// Any plain file can be the same file under another name; a zip is never written.
	struct stat st;
	if (f->filp && !fstat(fileno(f->filp), &st))
	{
		s->tracked = 1;
		s->dev = st.st_dev;
		s->ino = st.st_ino;

		// Another slot already writes this disk: read only.
		d88_slot *o = &slots[index ^ 1];
		if (o->tracked && o->rw && o->dev == s->dev && o->ino == s->ino &&
			(s->image || o->image) && (!s->image || !o->image || s->image == o->image))
		{
			s->ro = 1;
			*writable = 0;
		}
	}

	if (s->image && !*writable) s->ro = 1;
	s->rw = *writable;
	return 1;
}

// The mounted disk goes first so that it stays visible when the line is cut.
const char *pc88_slot_label(int index, const char *file)
{
	static char str[300];
	if (index < 0 || index > 1 || !slots[index].image) return file;

	snprintf(str, sizeof(str), "%s  %s", slots[index].label, file);
	return str;
}

uint64_t pc88_size(int index, uint64_t size)
{
	return (index >= 0 && index < 2 && slots[index].image) ? slots[index].size : size;
}

// Keep the size field and the track table pointing inside the disk.
static int keep_header(fileTYPE *f, d88_slot *s, uint64_t off, uint64_t n)
{
	uint8_t old[D88_HDR], hdr[D88_HDR];
	if (!file_read(f, s->start, old, D88_HDR)) return 0;

	uint64_t end = (off + n > D88_HDR) ? D88_HDR : off + n;
	memcpy(hdr, old, D88_HDR);
	memcpy(hdr + off, buf, end - off);

	for (int i = 0x1C; i < D88_HDR; i += 4)
	{
		uint32_t v = le32(hdr + i);
		int ok = (i == 0x1C) ? (v == s->size) : (!v || (v >= D88_HDR && v < s->size));
		if (!ok) memcpy(hdr + i, old + i, 4);
	}

	memcpy(buf, hdr + off, end - off);
	return 1;
}

// One disk of the file, or a slot that must not write.
static int disk_service(d88_slot *s, fileTYPE *f, int op, uint64_t lba, int sz, int ack)
{
	if ((!f->filp && !f->zip) || sz > (int)sizeof(buf)) return 0;

	uint64_t off = lba * 512;
	uint64_t n = (s->image && off < s->size) ? s->size - off : 0;
	if (n > (uint64_t)sz) n = sz;

	if (op == 2)
	{
		EnableIO();
		spi_w(UIO_SECTOR_WR | ack);
		spi_block_read(buf, user_io_get_width(), sz);
		DisableIO();

		if (!n || s->ro || !f->filp || (off < D88_HDR && !keep_header(f, s, off, n))) return 1;
		diskled_on();
		if (pwrite(fileno(f->filp), buf, n, s->start + off) != (ssize_t)n) printf("pc88: write failed at %llu\n", (unsigned long long)(s->start + off));
		return 1;
	}

	if (op & 1)
	{
		memset(buf, 0, sz);
		if (n)
		{
			diskled_on();
			file_read(f, s->start + off, buf, n);
		}
		EnableIO();
		spi_w(UIO_SECTOR_RD | ack);
		spi_block_write(buf, user_io_get_width(), sz);
		DisableIO();
		return 1;
	}

	return -1;
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
	if (disk < 0 || disk > 1 || !is_pc88()) return 0;
	if (slots[disk].image || (slots[disk].ro && op == 2)) return disk_service(&slots[disk], f, op, lba, sz, ack);
	if (f->size < 512) return 0;
	if (op == 2) write_blocks(disk, f, lba, sz, ack);
	else if (op & 1) read_blocks(disk, f, lba, sz, ack);
	else return -1;
	return 1;
}
