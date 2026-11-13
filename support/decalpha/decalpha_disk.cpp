#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio_ext.h>
#include <sys/stat.h>

#include "../../hardware.h"
#include "../../file_io.h"
#include "../../user_io.h"
#include "../../spi.h"
#include "decalpha.h"
#include "decalpha_disk.h"

#define BLKSZ    512
#define SLOTS    DECALPHA_DISK_SLOTS
#define RUN_MAX  (64 * 1024)
#define RUNS     64
#define IDLE_MS  20
#define AGE_MS   500

struct run { uint64_t off; uint32_t len; unsigned long born, last; int queued; uint8_t data[RUN_MAX]; };

struct slot
{
	run           *runs;
	int            pending;
	unsigned long  last;
	fileTYPE      *f;
	dev_t          dev;
	ino_t          ino;
	int            fd;
	int            dirty;
	char           path[1024];
};

static slot slots[SLOTS];
static volatile int pending;

static pthread_mutex_t qm = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  qc = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  dc = PTHREAD_COND_INITIALIZER;
static struct { int disk; run *r; } queue[SLOTS * RUNS];
static int q_head, q_tail, q_n;
static int writer_up;

static void write_run(int disk, run *r)
{
	slot *s = &slots[disk];
	if (s->fd < 0) { printf("decalpha_disk: no descriptor for %s, write %u @ %llu lost\n", s->path, r->len, (unsigned long long)r->off); return; }
	if (pwrite64(s->fd, r->data, r->len, r->off) != (ssize_t)r->len)
		printf("decalpha_disk: write %u @ %llu to %s failed\n", r->len, (unsigned long long)r->off, s->path);
}

static void *writer(void *)
{
	pthread_mutex_lock(&qm);
	for (;;)
	{
		while (!q_n) pthread_cond_wait(&qc, &qm);
		int disk = queue[q_head].disk;
		run *r = queue[q_head].r;
		q_head = (q_head + 1) % (SLOTS * RUNS); q_n--;
		pthread_mutex_unlock(&qm);

		write_run(disk, r);

		pthread_mutex_lock(&qm);
		r->len = 0; r->queued = 0;
		slots[disk].pending--;
		pending--;
		pthread_cond_broadcast(&dc);
	}
	return 0;
}

static void writer_start()
{
	if (writer_up) return;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(0, &set);
	pthread_attr_setaffinity_np(&attr, sizeof(set), &set);
	pthread_t th;
	if (!pthread_create(&th, &attr, writer, 0)) { pthread_detach(th); writer_up = 1; }
	pthread_attr_destroy(&attr);
}

static void enqueue_locked(int disk, run *r)
{
	if (!r->len || r->queued) return;
	r->queued = 1;
	queue[q_tail].disk = disk; queue[q_tail].r = r;
	q_tail = (q_tail + 1) % (SLOTS * RUNS); q_n++;
	pthread_cond_signal(&qc);
}

static void flush_run(int disk, run *r)
{
	if (!r->len || r->queued) return;
	writer_start();
	pthread_mutex_lock(&qm);
	enqueue_locked(disk, r);
	pthread_mutex_unlock(&qm);
}

static void flush_overlap(int disk, uint64_t off, uint64_t len, run *keep)
{
	slot *s = &slots[disk];
	if (!s->pending) return;
	writer_start();
	pthread_mutex_lock(&qm);
	for (int i = 0; i < RUNS; i++)
	{
		run *r = &s->runs[i];
		if (r != keep && r->len && off < r->off + r->len && r->off < off + len) enqueue_locked(disk, r);
	}
	for (int i = 0; i < RUNS; i++)
	{
		run *r = &s->runs[i];
		while (r != keep && r->queued && off < r->off + r->len && r->off < off + len) pthread_cond_wait(&dc, &qm);
	}
	pthread_mutex_unlock(&qm);
}

void decalpha_disk_flush(int disk)
{
	if (disk < 0 || disk >= SLOTS) return;
	slot *s = &slots[disk];
	if (!s->runs) return;
	writer_start();
	pthread_mutex_lock(&qm);
	for (;;)
	{
		run *lo = 0;
		for (int i = 0; i < RUNS; i++)
		{
			run *r = &s->runs[i];
			if (r->len && !r->queued && (!lo || r->off < lo->off)) lo = r;
		}
		if (!lo) break;
		enqueue_locked(disk, lo);
	}
	while (s->pending) pthread_cond_wait(&dc, &qm);
	if (s->fd >= 0) { close(s->fd); s->fd = -1; }
	pthread_mutex_unlock(&qm);
}

void decalpha_disk_poll()
{
	if (!pending) return;

	unsigned long now = GetTimer(0);
	for (int d = 0; d < SLOTS; d++)
	{
		slot *s = &slots[d];
		if (!s->pending) continue;
		int all = now - s->last >= IDLE_MS;
		for (int i = 0; i < RUNS; i++)
			if (s->runs[i].len && !s->runs[i].queued && (all || now - s->runs[i].born >= AGE_MS)) flush_run(d, &s->runs[i]);
	}
}

static int can_buffer(int disk, fileTYPE *f, uint64_t off, int sz)
{
	slot *s = &slots[disk];
	if (!f->filp || f->type == 2 || (f->mode & O_ACCMODE) == O_RDONLY) return 0;
	if (sz <= 0 || sz > RUN_MAX || off + sz > (uint64_t)f->size) return 0;

	if (!s->runs)
	{
		if (!(s->runs = (run*)calloc(RUNS, sizeof(run)))) return 0;
		s->fd = -1;
	}

	if (!s->pending)
	{
		struct stat64 st;
		if (fstat64(fileno(f->filp), &st)) return 0;
		if (s->fd >= 0 && (st.st_dev != s->dev || st.st_ino != s->ino)) { close(s->fd); s->fd = -1; }
		if (s->fd < 0)
		{
			char procpath[64];
			snprintf(procpath, sizeof(procpath), "/proc/self/fd/%d", fileno(f->filp));
			s->fd = open(procpath, O_WRONLY | O_SYNC | O_CLOEXEC);
			if (s->fd < 0) s->fd = dup(fileno(f->filp));
			if (s->fd < 0) return 0;
		}
		s->f = f;
		s->dev = st.st_dev;
		s->ino = st.st_ino;
		snprintf(s->path, sizeof(s->path), "%s", f->name);
	}
	return 1;
}

static run *free_run(int disk)
{
	slot *s = &slots[disk];
	writer_start();
	pthread_mutex_lock(&qm);
	for (;;)
	{
		run *lru = 0;
		for (int i = 0; i < RUNS; i++)
		{
			run *r = &s->runs[i];
			if (!r->len) { pthread_mutex_unlock(&qm); return r; }
			if (!r->queued && (!lru || r->last < lru->last)) lru = r;
		}
		if (lru) enqueue_locked(disk, lru);
		pthread_cond_wait(&dc, &qm);
	}
}

static void stage(int disk, uint64_t off, const uint8_t *data, uint32_t sz)
{
	slot *s = &slots[disk];
	unsigned long now = GetTimer(0);

	s->last = now;
	s->dirty = 1;
	for (int again = 1; again;)
	{
		again = 0;
		for (int i = 0; i < RUNS; i++)
		{
			run *r = &s->runs[i];
			if (r->queued && r->len && off < r->off + r->len && r->off < off + sz)
			{
				pthread_mutex_lock(&qm);
				while (r->queued) pthread_cond_wait(&dc, &qm);
				pthread_mutex_unlock(&qm);
				again = 1;
				break;
			}
		}
	}
	for (int i = 0; i < RUNS; i++)
	{
		run *r = &s->runs[i];
		if (!r->len || r->queued) continue;
		if (off >= r->off && off + sz <= r->off + r->len)
		{
			memcpy(r->data + (off - r->off), data, sz);
			r->last = now;
			return;
		}
		if (off == r->off + r->len && r->len + sz <= RUN_MAX)
		{
			flush_overlap(disk, off, sz, r);
			memcpy(r->data + r->len, data, sz);
			r->len += sz;
			r->last = now;
			if (r->len == RUN_MAX) flush_run(disk, r);
			return;
		}
	}

	flush_overlap(disk, off, sz, 0);
	run *r = free_run(disk);
	r->off = off;
	r->len = sz;
	r->queued = 0;
	r->born = r->last = now;
	memcpy(r->data, data, sz);
	pthread_mutex_lock(&qm);
	s->pending++;
	pending++;
	pthread_mutex_unlock(&qm);
	if (r->len == RUN_MAX) flush_run(disk, r);
}

int decalpha_disk_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack)
{
	static uint8_t buf[UIO_BUFFER_SIZE];

	if (disk < 0 || disk >= SLOTS || !(decalpha_buffered_disks() & (1 << disk)) || !op) return 0;

	uint64_t off = lba * BLKSZ;
	if (op != 2)
	{
		slot *s = &slots[disk];
		if (s->runs)
		{
			flush_overlap(disk, off, sz + 2ULL * UIO_BUFFER_SIZE, 0);
			if (s->dirty && f->filp) { __fpurge(f->filp); s->dirty = 0; }
		}
		return 0;
	}

	if (sz > (int)sizeof(buf) || !can_buffer(disk, f, off, sz))
	{
		decalpha_disk_flush(disk);
		return 0;
	}

	EnableIO();
	spi_w(UIO_SECTOR_WR | ack);
	spi_block_read(buf, user_io_get_width(), sz);
	DisableIO();

	diskled_on();
	user_io_bufferinvalidate(disk);
	stage(disk, off, buf, sz);
	return 1;
}
