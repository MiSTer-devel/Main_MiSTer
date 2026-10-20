#include "chd_hdd_overlay.h"
#include "../../lib/miniz/miniz.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <new>
#include <string>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>

static const unsigned HEADER_SIZE = 64;
static const unsigned MAX_SECTORS = 32;
static const unsigned RECORD_HEADER_SIZE = 20;
static const unsigned RECORD_SIZE = RECORD_HEADER_SIZE + MAX_SECTORS * 512 + 4;

struct overlay_sector
{
	__off64_t offset;
	uint32_t crc;
};

struct chd_hdd_overlay
{
	int fd = -1;
	bool writable = false;
	bool failed = false;
	uint64_t size = 0;
	__off64_t end = HEADER_SIZE;
	std::unordered_map<uint64_t, overlay_sector> sectors;
	std::string missing;
};

static std::unordered_map<std::string, unsigned> missing_save_readers;

static uint64_t get_le(const uint8_t *p, unsigned bytes)
{
	uint64_t value = 0;
	for (unsigned i = 0; i < bytes; ++i) value |= (uint64_t)p[i] << (i * 8);
	return value;
}

static void put_le(uint8_t *p, uint64_t value, unsigned bytes)
{
	for (unsigned i = 0; i < bytes; ++i) p[i] = value >> (i * 8);
}

static int error(int code)
{
	errno = code;
	return -1;
}

static int transfer(int fd, void *data, size_t length, __off64_t offset, bool write)
{
	uint8_t *p = (uint8_t *)data;
	while (length)
	{
		ssize_t n = write ? pwrite64(fd, p, length, offset) : pread64(fd, p, length, offset);
		if (n < 0)
		{
			if (errno == EINTR) continue;
			return -1;
		}
		if (!n) return error(EIO);
		p += n;
		offset += n;
		length -= n;
	}
	return 0;
}

static int sync_dir(const std::string &dir)
{
	int fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0) return -1;
	int ret = fsync(fd), saved = errno;
	close(fd);
	errno = saved;
	return ret;
}

static int sync_ancestors(const char *path, const char *root)
{
	std::string dir(path), top(root ? root : "");
	while (top.size() > 1 && top.back() == '/') top.pop_back();
	do
	{
		size_t slash = dir.rfind('/');
		dir = slash == std::string::npos ? "." : dir.substr(0, slash ? slash : 1);
		if (sync_dir(dir)) return -1;
	} while (!top.empty() && dir.size() > top.size() && !dir.compare(0, top.size() + 1, top + "/"));
	return 0;
}

static int replay(chd_hdd_overlay *o, __off64_t size)
{
	uint8_t record[RECORD_SIZE];
	while (o->end < size)
	{
		if (size - o->end < RECORD_HEADER_SIZE) break;
		if (transfer(o->fd, record, RECORD_HEADER_SIZE, o->end, false)) return -1;
		if (get_le(record + 16, 4) != mz_crc32(0, record, 16)) return error(EILSEQ);
		unsigned count = get_le(record + 4, 4);
		uint64_t sector = get_le(record + 8, 8);
		if (memcmp(record, "SECT", 4) || !count || count > MAX_SECTORS ||
			sector >= o->size / 512 || count > o->size / 512 - sector) return error(EINVAL);
		unsigned bytes = RECORD_HEADER_SIZE + count * 512 + 4;
		if (size - o->end < bytes) break;
		if (transfer(o->fd, record + RECORD_HEADER_SIZE, bytes - RECORD_HEADER_SIZE,
			o->end + RECORD_HEADER_SIZE, false)) return -1;
		if (get_le(record + bytes - 4, 4) != mz_crc32(0, record, bytes - 4)) return error(EILSEQ);
		for (unsigned i = 0; i < count; ++i)
			o->sectors[sector + i] = {o->end + RECORD_HEADER_SIZE + i * 512,
				(uint32_t)mz_crc32(0, record + RECORD_HEADER_SIZE + i * 512, 512)};
		o->end += bytes;
	}
	if (o->end != size && o->writable)
	{
		if (ftruncate64(o->fd, o->end) || fdatasync(o->fd)) return -1;
	}
	return 0;
}

void chd_hdd_overlay_close(chd_hdd_overlay *o)
{
	if (!o) return;
	if (o->fd >= 0) close(o->fd);
	auto reader = missing_save_readers.find(o->missing);
	if (!o->missing.empty() && reader != missing_save_readers.end() && !--reader->second)
		missing_save_readers.erase(reader);
	delete o;
}

int chd_hdd_overlay_open(const char *path, const char *root, uint64_t size, const uint8_t identity[20],
	uint8_t identity_type, bool writable, chd_hdd_overlay **overlay)
{
	*overlay = nullptr;
	chd_hdd_overlay *o = new (std::nothrow) chd_hdd_overlay;
	if (!o) return error(ENOMEM);
	o->writable = writable;
	o->size = size;
	bool created = false;
	int ret = -1;
	o->fd = open(path, (writable ? O_RDWR : O_RDONLY) | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (o->fd < 0 && errno == ENOENT)
	{
		if (!writable)
		{
			try
			{
				o->missing = path;
				++missing_save_readers[o->missing];
			}
			catch (const std::bad_alloc &)
			{
				delete o;
				return error(ENOMEM);
			}
			*overlay = o;
			return 0;
		}
		if (missing_save_readers.count(path))
		{
			delete o;
			return error(EBUSY);
		}
		o->fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0666);
		created = o->fd >= 0;
		if (o->fd < 0 && errno == EEXIST)
			o->fd = open(path, O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	}
	if (o->fd >= 0)
	{
		try
		{
			struct stat64 st;
			uint8_t header[HEADER_SIZE] = {}, actual[HEADER_SIZE];
			memcpy(header, "MiSTCOW1", 8);
			put_le(header + 8, size, 8);
			memcpy(header + 16, identity, 20);
			header[36] = identity_type;
			put_le(header + 60, mz_crc32(0, header, 60), 4);
			if (flock(o->fd, (writable ? LOCK_EX : LOCK_SH) | LOCK_NB) || fstat64(o->fd, &st)) {}
			else if (!S_ISREG(st.st_mode)) error(EINVAL);
			else if (created)
			{
				if (!transfer(o->fd, header, sizeof(header), 0, true) && !fdatasync(o->fd) && !sync_ancestors(path, root)) ret = 0;
			}
			else if (st.st_size < HEADER_SIZE) error(EINVAL);
			else if (!transfer(o->fd, actual, sizeof(actual), 0, false))
			{
				if (memcmp(actual, header, sizeof(header))) error(EINVAL);
				else ret = replay(o, st.st_size);
			}
		}
		catch (const std::bad_alloc &) { error(ENOMEM); }
	}
	if (ret)
	{
		int saved = errno;
		if (created) unlink(path);
		chd_hdd_overlay_close(o);
		errno = saved;
		return -1;
	}
	*overlay = o;
	return 0;
}

int chd_hdd_overlay_read(chd_hdd_overlay *o, uint64_t sector, uint8_t data[512])
{
	if (!o) return 0;
	auto found = o->sectors.find(sector);
	if (found == o->sectors.end()) return 0;
	if (transfer(o->fd, data, 512, found->second.offset, false)) return -1;
	if (mz_crc32(0, data, 512) != found->second.crc) return error(EILSEQ);
	return 1;
}

int chd_hdd_overlay_write(chd_hdd_overlay *o, uint64_t offset, const void *data, uint32_t length)
{
	if (!o || !o->writable) return error(EROFS);
	if (o->failed) return error(EIO);
	if (!length || offset % 512 || length % 512 || length / 512 > MAX_SECTORS ||
		offset > o->size || length > o->size - offset) return error(EINVAL);
	if (o->end > LLONG_MAX - RECORD_HEADER_SIZE - 4 - length) return error(EFBIG);
	unsigned count = length / 512;
	uint64_t sector = offset / 512;
	overlay_sector *entries[MAX_SECTORS];
	bool inserted[MAX_SECTORS] = {};
	unsigned prepared = 0;
	try
	{
		for (; prepared < count; ++prepared)
		{
			auto entry = o->sectors.emplace(sector + prepared, overlay_sector{});
			entries[prepared] = &entry.first->second;
			inserted[prepared] = entry.second;
		}
	}
	catch (const std::bad_alloc &)
	{
		for (unsigned i = 0; i < prepared; ++i) if (inserted[i]) o->sectors.erase(sector + i);
		return error(ENOMEM);
	}
	uint8_t record[RECORD_SIZE];
	memcpy(record, "SECT", 4);
	put_le(record + 4, count, 4);
	put_le(record + 8, sector, 8);
	put_le(record + 16, mz_crc32(0, record, 16), 4);
	memcpy(record + RECORD_HEADER_SIZE, data, length);
	put_le(record + RECORD_HEADER_SIZE + length, mz_crc32(0, record, RECORD_HEADER_SIZE + length), 4);
	if (transfer(o->fd, record, RECORD_HEADER_SIZE + 4 + length, o->end, true) || fdatasync(o->fd))
	{
		int saved = errno;
		for (unsigned i = 0; i < count; ++i) if (inserted[i]) o->sectors.erase(sector + i);
		o->failed = true;
		return error(saved);
	}
	for (unsigned i = 0; i < count; ++i)
		*entries[i] = {o->end + RECORD_HEADER_SIZE + i * 512,
			(uint32_t)mz_crc32(0, record + RECORD_HEADER_SIZE + i * 512, 512)};
	o->end += RECORD_HEADER_SIZE + 4 + length;
	return 0;
}
