// Mac SCSI family hard disks: write buffer.
//
// /media/fat is sync mounted, so every write() costs ~4 ms on the card no matter
// the size. The Mac cores flush single sectors in LBA order and a Finder copy spent
// 11 of 12 s in write(). Gather writes into runs, ack at once, write a run out when
// it fills, when all runs are busy, before a read that overlaps it, after 20 ms idle,
// on remount and before a restart. Runs never overlap each other.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../file_io.h"
#include "../../user_io.h"
#include "../../spi.h"
#include "mac.h"
#include "mac_disk.h"

#define RUN_MAX  (64 * 1024)
#define RUNS     8      // a copy interleaves data with catalog/bitmap writes; one run per stream
#define IDLE_US  20000
#define SLOTS    4

struct run { uint64_t off; uint32_t len; uint64_t last; uint8_t data[RUN_MAX]; };

static run       runs[SLOTS][RUNS];
static uint64_t  last_write[SLOTS];
static fileTYPE *slot_file[SLOTS];

static uint64_t now_us()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
}

static int eligible(int disk, fileTYPE *f, int cangrow)
{
	return disk >= 0 && disk < SLOTS && is_mac_scsi_family() &&
		disk != mac_cdrom_slot() && disk != mac_toolbox_slot() && disk != mac_cd_toolbox_slot() &&
		f->type != 2 && !cangrow && f->size;
}

static void flush_run(int disk, run *r)
{
	uint32_t len = r->len;
	if (!len) return;
	r->len = 0;
	diskled_on();
	if (!FileSeek(slot_file[disk], r->off, SEEK_SET) || !FileWriteAdv(slot_file[disk], r->data, len))
		printf("mac_disk: write %u @ %llu slot %d failed\n", len, (unsigned long long)r->off, disk);
}

// all runs of a slot, lowest offset first
void mac_disk_flush(int disk)
{
	if (disk < 0 || disk >= SLOTS) return;
	for (;;)
	{
		run *lo = 0;
		for (int i = 0; i < RUNS; i++)
			if (runs[disk][i].len && (!lo || runs[disk][i].off < lo->off)) lo = &runs[disk][i];
		if (!lo) return;
		flush_run(disk, lo);
	}
}

static void flush_overlap(int disk, uint64_t off, uint64_t len, run *keep)
{
	for (int i = 0; i < RUNS; i++)
	{
		run *r = &runs[disk][i];
		if (r != keep && r->len && off < r->off + r->len && r->off < off + len) flush_run(disk, r);
	}
}

int mac_disk_write(int disk, fileTYPE *f, int cangrow, uint64_t off, const uint8_t *data, uint32_t sz)
{
	if (!eligible(disk, f, cangrow) || !sz || sz > RUN_MAX || off + sz > (uint64_t)f->size)
	{
		mac_disk_flush(disk);
		return 0;
	}

	slot_file[disk] = f;
	uint64_t now = now_us();
	run *free_r = 0, *lru = 0;
	for (int i = 0; i < RUNS; i++)
	{
		run *r = &runs[disk][i];
		if (!r->len) { if (!free_r) free_r = r; continue; }
		if (off >= r->off && off + sz <= r->off + r->len)      // rewrite inside a run
		{
			memcpy(r->data + (off - r->off), data, sz);
			r->last = last_write[disk] = now;
			return 1;
		}
		if (off == r->off + r->len && r->len + sz <= RUN_MAX)  // extends a run
		{
			flush_overlap(disk, off, sz, r);
			memcpy(r->data + r->len, data, sz);
			r->len += sz;
			r->last = last_write[disk] = now;
			if (r->len == RUN_MAX) flush_run(disk, r);
			return 1;
		}
		if (!lru || r->last < lru->last) lru = r;
	}

	// new run
	flush_overlap(disk, off, sz, 0);
	if (!free_r)
	{
		for (int i = 0; i < RUNS; i++) if (!runs[disk][i].len) { free_r = &runs[disk][i]; break; }
		if (!free_r) { flush_run(disk, lru); free_r = lru; }
	}
	free_r->off = off;
	free_r->len = sz;
	memcpy(free_r->data, data, sz);
	free_r->last = last_write[disk] = now;
	if (free_r->len == RUN_MAX) flush_run(disk, free_r);
	return 1;
}

void mac_disk_before_read(int disk, uint64_t off, uint64_t len)
{
	if (disk < 0 || disk >= SLOTS) return;
	flush_overlap(disk, off, len, 0);
}

void mac_disk_flush_all()
{
	for (int d = 0; d < SLOTS; d++) mac_disk_flush(d);
}

// Tight service loop. With the core's SCSI cache off every 512-byte sector is
// its own request, raised ~110 us after the guest drained the previous one, so
// each sector used to wait a whole Main pass. Spin on SDSTAT for up to spin_us
// after each request, for at most budget_us per pass.

static uint32_t spin_us = 250;
static uint32_t budget_us = 2000;
static uint64_t pass_start;

void mac_disk_init()
{
	if (const char *e = getenv("MAC_SD_SPIN_US")) spin_us = strtoul(e, 0, 0);
	if (const char *e = getenv("MAC_SD_BUDGET_US")) budget_us = strtoul(e, 0, 0);
	if (getenv("MAC_SD_SPIN_US") || getenv("MAC_SD_BUDGET_US"))
		printf("mac_disk: spin %u us, budget %u us\n", spin_us, budget_us);
}

void mac_disk_poll()
{
	uint64_t now = 0;
	for (int d = 0; d < SLOTS; d++)
	{
		int any = 0;
		for (int i = 0; i < RUNS; i++) any |= (runs[d][i].len != 0);
		if (!any) continue;
		if (!now) now = now_us();
		if (now - last_write[d] >= IDLE_US) mac_disk_flush(d);
	}
	pass_start = 0;
}

int mac_disk_served(int disk)
{
	if (!spin_us || (disk != 0 && disk != 1) || !is_mac_scsi_optimized()) return 0;
	if (disk == mac_cdrom_slot() || disk == mac_toolbox_slot() || disk == mac_cd_toolbox_slot()) return 0;
	if (!pass_start) pass_start = now_us();
	return 1;
}

// one-word SDSTAT read has no side effect (round-robin advances on the 2nd word)
int mac_disk_wait_next()
{
	uint64_t t0 = now_us();
	if (t0 - pass_start >= budget_us) return 0;
	for (;;)
	{
		uint16_t c = spi_uio_cmd(UIO_GET_SDSTAT);
		if ((c & 0x8000) && (c & 3)) return 1;
		if (now_us() - t0 >= spin_us) return 0;
	}
}
