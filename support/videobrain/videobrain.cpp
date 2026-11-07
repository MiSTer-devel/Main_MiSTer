#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "../../file_io.h"
#include "../../user_io.h"
#include "../../menu.h"
#include "../../lib/miniz/miniz.h"
#include "videobrain.h"

static char notice[1100];

void videobrain_init()
{
	notice[0] = 0;
}

static void put32(uint8_t *p, uint32_t value)
{
	for (int i = 0; i < 4; i++) p[i] = value >> (8 * i);
}

void videobrain_new_image(int bit)
{
	if (!is_videobrain()) return;
	if (bit != VIDEOBRAIN_BIT_NEW_TAPE && bit != VIDEOBRAIN_BIT_NEW_PRINTER) return;

	const bool tape = bit == VIDEOBRAIN_BIT_NEW_TAPE;
	const int slot = tape ? 1 : 2;
	const char *kind = tape ? "Tape" : "Printer";
	const char *ext = tape ? "vbt" : "vbp";
	const size_t size = tape ? 8 * 1024 * 1024 : 1024 * 1024;
	char dir[1024], path[1024], full[2048];
	const char *home = HomeDir();
	snprintf(notice, sizeof(notice), "%s image not created", kind);
	if (!FileCreatePath(home)) return;
	if (snprintf(dir, sizeof(dir), "%s/saves", home) >= (int)sizeof(dir)) return;
	if (!FileCreatePath(dir)) return;

	int fd = -1;
	for (int n = 1; n <= 99999; n++)
	{
		if (snprintf(path, sizeof(path), "%s/%s_%d.%s", dir, kind, n, ext) >= (int)sizeof(path)) return;
		if (path[0] == '/') snprintf(full, sizeof(full), "%s", path);
		else if (snprintf(full, sizeof(full), "%s/%s", getRootDir(), path) >= (int)sizeof(full)) return;
		fd = open(full, O_WRONLY | O_CREAT | O_EXCL, 0666);
		if (fd >= 0) break;
		if (errno != EEXIST) return;
	}
	if (fd < 0) return;

	uint8_t buf[65536] = {};
	memcpy(buf, tape ? "VBT1" : "VBP1", 4);
	if (tape) put32(buf + 4, 14318180);
	else
	{
		put32(buf + 4, 1);
		put32(buf + 8, 512);
		put32(buf + 12, 32);
		put32(buf + 16, size / 512);
		put32(buf + 28, mz_crc32(0, buf, 512));
	}

	bool ok = true;
	for (size_t offset = 0; ok && offset < size; offset += sizeof(buf))
	{
		size_t written = 0;
		while (written < sizeof(buf))
		{
			ssize_t count = write(fd, buf + written, sizeof(buf) - written);
			if (count < 0 && errno == EINTR) continue;
			if (count <= 0) { ok = false; break; }
			written += count;
		}
		if (!offset) memset(buf, 0, sizeof(buf));
	}
	if (ok && fsync(fd)) ok = false;
	if (close(fd)) ok = false;
	if (!ok)
	{
		unlink(full);
		return;
	}

	if (!user_io_file_mount(path, slot))
	{
		snprintf(notice, sizeof(notice), "Image created but not mounted:\n%s", path);
		return;
	}
	StoreIdx_S(slot, path);
	char cfg[128], saved[1024] = {};
	snprintf(cfg, sizeof(cfg), "%s.s%d", user_io_get_core_name(), slot);
	snprintf(saved, sizeof(saved), "%s", path);
	FileSaveConfig(cfg, saved, sizeof(saved));
	snprintf(notice, sizeof(notice), "New %s image mounted:\n%s", kind, path);
}

void videobrain_poll()
{
	if (notice[0] && !user_io_osd_is_visible())
	{
		Info(notice, 4000);
		notice[0] = 0;
	}
}
