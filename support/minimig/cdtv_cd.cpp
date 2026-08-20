#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include <byteswap.h>

#include "../../spi.h"
#include "../../fpga_io.h"
#include "../../user_io.h"
#include "../../menu.h"
#include "../../hardware.h"
#include "../../ide.h"
#include "../../ide_cdrom.h"
#include "../chd/mister_chd.h"
#include "cdtv_cd.h"
#include "minimig_config.h"

static void cdtv_diag(const char *fmt, ...)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	printf("[%6lu.%06lu] ", (unsigned long)ts.tv_sec, (unsigned long)(ts.tv_nsec / 1000));
	va_list ap; va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
	fflush(stdout);
}

#define CDTV_DEBUG 1
#if CDTV_DEBUG
	#define cdtv_dbg(fmt, ...) cdtv_diag("[cdtv] " fmt, ##__VA_ARGS__)
#else
	#define cdtv_dbg(...) do {} while (0)
#endif

#define CDTV_BRIDGE_ADDR   0xF800
#define CDTV_SEC_ADDR      0xF820
#define CDTV_STCH_ADDR     0xF840
#define CDTV_CDDA_ADDR     0xF200
#define CDTV_CDDA_BYTES    2352
#define CDTV_STATUS_CDDA_REQ (1u << 8)
#define CDTV_STATUS_CMD    0x63
#define CDTV_STATUS_REQ    (1u << 6)
#define CDTV_STATUS_NVR_DIRTY (1u << 12)

#define CDTV_NVRAM_ADDR       0xF880
#define CDTV_NVRAM_BYTES      16384
#define CDTV_NVRAM_FILE_BYTES 32768
#define CDTV_NVRAM_DIR        "/media/fat/saves/Minimig"

#define CDTV_CARD_ADDR        0xF8C0
#define CDTV_CARD_BYTES       8192
#define CDTV_STATUS_CARD_DIRTY (1u << 13)

static int cr511_command_length(uint8_t op)
{
	switch (op) {
		case 0x00: case 0x80:                      return 2;
		case 0x81: case 0x85: case 0x86:
		case 0x88: case 0xA2:                      return 1;
		case 0x01: case 0x02: case 0x04: case 0x05:
		case 0x09: case 0x0a: case 0x0b: case 0x82:
		case 0x83: case 0x84: case 0x87: case 0x89:
		case 0x8a: case 0x8b: case 0xa3:           return 7;
		default:                                   return -1;
	}
}

#define CDTV_CMD_MAX  16
static uint8_t  cmd_buf[CDTV_CMD_MAX];
static int      cmd_idx = 0;
static int      cmd_need = 0;

static uint8_t  cd_motor       = 0;
static uint8_t  cd_isready     = 0;
static uint8_t  cd_media       = 0;
static uint8_t  cd_playing     = 0;
static uint8_t  cd_finished    = 0;
static uint8_t  cd_error       = 0;
static uint16_t cd_sectorsize  = 2048;

static char     cd_path_active[1024] = {0};

static int32_t  cdtv_play_lba_next = -1;
static int32_t  cdtv_play_lba_end  = -1;
static drive_t *cdtv_play_drv      = NULL;
static uint8_t  cd_paused          = 0;

static uint8_t  cd_audio_status    = 0x15;

static int32_t  cdtv_last_lba      = 0;

static int            stch_retries  = 0;
static unsigned long  stch_next_ms  = 0;
#define STCH_RETRY_BUDGET    40
#define STCH_RETRY_PERIOD_MS 250

static bool           cd_swap_pending   = false;
static unsigned long  cd_swap_ready_ms  = 0;
#define CDTV_SWAP_SETTLE_MS 700

static bool           stch_wait_ack = false;
#define STCH_PLAYEND_BUDGET    400
#define STCH_PLAYEND_PERIOD_MS 60

static bool           stch_playstart_pending = false;

static inline bool cdtv_active(void)
{
	return (minimig_config.cdtv_drive.cfg != 0);
}

#define cdtv_find_drive() minimig_cd_drive_get(1)

static uint16_t cdtv_read_status(void)
{
	uint16_t res;
	EnableIO();
	res = spi_w(CDTV_STATUS_CMD);
	if (!res) res = spi_w(0);
	DisableIO();
	return res;
}

static uint8_t cdtv_drain_byte(void)
{
	uint8_t b;
	EnableIO();
	spi8(UIO_DMA_READ);
	spi32_w(CDTV_BRIDGE_ADDR);
	b = (uint8_t)spi_w(0);
	DisableIO();
	return b;
}

static void cdtv_inject_stch(void)
{
	EnableIO();
	spi8(UIO_DMA_WRITE);
	spi32_w(CDTV_STCH_ADDR);
	spi_w(0);
	DisableIO();
	cdtv_dbg("STCH inject");
}

static bool cdtv_stch_acked(void)
{
	uint16_t v;
	EnableIO();
	spi8(UIO_DMA_READ);
	spi32_w(CDTV_STCH_ADDR);
	v = (uint16_t)spi_w(0);
	DisableIO();
	return (v & 1) != 0;
}

static void cdtv_push_reply(const uint8_t *buf, int len)
{
	if (len <= 0) return;
	EnableIO();
	spi8(UIO_DMA_WRITE);
	spi32_w(CDTV_BRIDGE_ADDR);
	for (int i = 0; i < len; i++) {
		spi_w(buf[i]);
	}
	DisableIO();

#if CDTV_DEBUG
	char dump[3 * CDTV_CMD_MAX + 4];
	int n = 0;
	int dump_n = (len > CDTV_CMD_MAX) ? CDTV_CMD_MAX : len;
	for (int i = 0; i < dump_n; i++) n += snprintf(dump + n, sizeof(dump) - n, "%02x ", buf[i]);
	cdtv_dbg("TX[%d]: %s%s", len, dump, (len > dump_n) ? "..." : "");
#endif
}

static void cdtv_push_sector(const uint8_t *buf, int len)
{
	if (len <= 0) return;
	EnableIO();
	spi8(UIO_DMA_WRITE);
	spi32_w(CDTV_SEC_ADDR);
	for (int i = 0; i < len; i++) {
		spi_w(buf[i]);
	}
	DisableIO();
}

static bool cdtv_read_audio_sector(drive_t *drv, uint32_t lba, uint8_t *buf2352)
{
	if (!drv || !drv->chd_f || !buf2352) return false;

	track_t *track = NULL;
	int real_tracks = drv->track_cnt > 0 ? drv->track_cnt - 1 : 0;
	for (int i = 0; i < real_tracks; i++) {
		uint32_t end = drv->track[i].start + drv->track[i].length;
		if (lba >= drv->track[i].start && lba < end) {
			track = &drv->track[i];
			break;
		}
	}
	if (!track) return false;
	if (track->attr & 0x40) return false;
	if (track->sectorSize != CDTV_CDDA_BYTES) return false;

	uint32_t chd_lba = lba + track->chd_offset;
	if (mister_chd_read_sector(drv->chd_f, chd_lba, 0, 0,
	                           CDTV_CDDA_BYTES, buf2352,
	                           drv->chd_hunkbuf, &drv->chd_hunknum)
	    != CHDERR_NONE) {
		return false;
	}
	return true;
}

static bool cdtv_audio_fifo_ready(void)
{
	return (cdtv_read_status() & CDTV_STATUS_CDDA_REQ) != 0;
}

static void cdtv_push_audio_sector(const uint8_t *buf2352)
{
	const uint16_t *src = (const uint16_t *)buf2352;
	const int nframes = CDTV_CDDA_BYTES / 4;

	EnableIO();
	spi8(UIO_DMA_WRITE);
	spi32_w(CDTV_CDDA_ADDR);
	for (int i = 0; i < nframes; i++) {
		uint16_t l = bswap_16(src[2 * i + 0]);
		uint16_t r = bswap_16(src[2 * i + 1]);
		spi_w(l);
		spi_w(r);
	}
	DisableIO();
}

static bool cdtv_cdda_pump(void)
{
	if (cdtv_play_lba_next < 0) return false;
	if (cd_paused) return false;
	if (!cdtv_play_drv) {
		cdtv_play_lba_next = -1;
		cdtv_play_lba_end  = -1;
		return false;
	}
	if (!cdtv_audio_fifo_ready()) return false;

	uint8_t buf[CDTV_CDDA_BYTES];
	uint32_t lba = (uint32_t)cdtv_play_lba_next;
	if (!cdtv_read_audio_sector(cdtv_play_drv, lba, buf)) {
		cdtv_dbg("CDDA pump read FAIL at lba=%u — aborting", lba);
		memset(buf, 0, sizeof(buf));
		cdtv_push_audio_sector(buf);
		cdtv_last_lba      = (int32_t)lba;
		cdtv_play_lba_next = -1;
		cdtv_play_lba_end  = -1;
		cdtv_play_drv      = NULL;
		cd_playing         = 0;
		cd_paused          = 0;
		cd_finished        = 1;
		cd_audio_status    = 0x14;
		cdtv_inject_stch();
		return true;
	}

	cdtv_push_audio_sector(buf);
	cdtv_last_lba = (int32_t)lba;
	cdtv_play_lba_next++;

	if ((lba & 0xff) == 0) {
		cdtv_dbg("CDDA pump lba=%u (rem=%d)", lba,
		         cdtv_play_lba_end - cdtv_play_lba_next);
	}

	if (cdtv_play_lba_next >= cdtv_play_lba_end) {
		cdtv_dbg("CDDA natural end at lba=%u", lba);
		cdtv_play_lba_next = -1;
		cdtv_play_lba_end  = -1;
		cdtv_play_drv      = NULL;
		cd_playing         = 0;
		cd_paused          = 0;
		cd_finished        = 1;
		cd_audio_status    = 0x13;
		cdtv_inject_stch();
		stch_wait_ack = true;
		stch_retries  = STCH_PLAYEND_BUDGET;
		stch_next_ms  = GetTimer(STCH_PLAYEND_PERIOD_MS);
	}
	return true;
}

static uint32_t cdtv_msf_to_lba(uint32_t msf)
{
	uint32_t m = (msf >> 16) & 0xff;
	uint32_t s = (msf >>  8) & 0xff;
	uint32_t f =  msf        & 0xff;
	uint32_t lsn = (m * 60u + s) * 75u + f;
	return (lsn >= 150u) ? (lsn - 150u) : 0u;
}

static uint32_t cdtv_lba2msf(uint32_t lba)
{
	uint32_t m = lba / (60u * 75u);
	uint32_t s = (lba / 75u) % 60u;
	uint32_t f =  lba % 75u;
	return (m << 16) | (s << 8) | f;
}

static int cmd_subq(const uint8_t *cmd, uint8_t *out)
{
	bool msf = (cmd[1] & 0x02) != 0;

	memset(out, 0, 13);
	out[0] = cd_audio_status;
	out[1] = 0x10;

	int32_t pump_lba = (cdtv_play_lba_next >= 0) ? cdtv_play_lba_next : cdtv_last_lba;
	uint32_t disk_lba = (pump_lba > 0) ? (uint32_t)pump_lba : 0;

	drive_t *drv = cdtv_play_drv ? cdtv_play_drv : cdtv_find_drive();
	int track_idx = 0;
	uint32_t track_start = 0;
	if (drv && drv->track_cnt > 1) {
		int real_tracks = drv->track_cnt - 1;
		for (int i = 0; i < real_tracks; i++) {
			uint32_t s_lba = drv->track[i].start;
			uint32_t e_lba = s_lba + drv->track[i].length;
			if (disk_lba >= s_lba && disk_lba < e_lba) {
				track_idx   = i;
				track_start = s_lba;
				break;
			}
			track_idx   = i;
			track_start = s_lba;
		}
	}

	out[2] = (uint8_t)(track_idx + 1);
	out[3] = 0x01;

	uint32_t track_lba = (disk_lba > track_start) ? (disk_lba - track_start) : 0;
	uint32_t disk_pos  = msf ? cdtv_lba2msf(disk_lba + 150) : disk_lba;
	uint32_t track_pos = msf ? cdtv_lba2msf(track_lba)      : track_lba;

	out[5]  = (disk_pos  >> 16) & 0xff;
	out[6]  = (disk_pos  >>  8) & 0xff;
	out[7]  = (disk_pos       ) & 0xff;
	out[9]  = (track_pos >> 16) & 0xff;
	out[10] = (track_pos >>  8) & 0xff;
	out[11] = (track_pos       ) & 0xff;

	return 13;
}

static int cmd_read_toc(const uint8_t *cmd, uint8_t *out)
{
	drive_t *drv = cdtv_find_drive();
	if (!drv || drv->track_cnt < 1) {
		cd_error = 1;
		return -1;
	}

	uint8_t want = cmd[2];
	bool msf = (cmd[1] & 0x02) != 0;
	int real_tracks = drv->track_cnt - 1;
	if (real_tracks < 1) real_tracks = 1;

	uint32_t lba;
	uint8_t  control = 0x04;
	uint8_t  point   = want;

	if (want == 0xa0) {
		lba = drv->track[0].number;
		point = 0xa0;
	} else if (want == 0xa1) {
		lba = (uint32_t)real_tracks;
		point = 0xa1;
	} else if (want == 0xa2) {
		uint32_t end = 0;
		for (int i = 0; i < drv->track_cnt; i++) {
			uint32_t e = drv->track[i].start + drv->track[i].length;
			if (e > end) end = e;
		}
		lba = end;
		point = 0xa2;
	} else if (want >= 1 && want <= 99 && want <= real_tracks) {
		int idx = want - 1;
		lba = drv->track[idx].start;
		if (drv->track[idx].attr & 0x01) control = 0x00;
	} else {
		return -1;
	}

	uint32_t addr;
	if (msf) {
		uint32_t lsn = lba + 150u;
		uint32_t mins = (lsn / 75u) / 60u;
		uint32_t secs = (lsn / 75u) % 60u;
		uint32_t fr   = lsn % 75u;
		addr = (mins << 16) | (secs << 8) | fr;
	} else {
		addr = lba;
	}

	out[0] = 0;
	out[1] = (1u << 4) | control;
	out[2] = point;
	out[3] = (uint8_t)real_tracks;
	out[4] = 0;
	out[5] = (uint8_t)(addr >> 16);
	out[6] = (uint8_t)(addr >>  8);
	out[7] = (uint8_t)(addr      );
	cd_finished = 1;
	return 8;
}

static int cmd_info(uint8_t *out)
{
	drive_t *drv = cdtv_find_drive();
	if (!drv || drv->track_cnt < 1) {
		cd_error = 1;
		return -1;
	}

	int real_tracks = drv->track_cnt - 1;
	if (real_tracks < 1) real_tracks = 1;

	uint32_t end = 0;
	for (int i = 0; i < drv->track_cnt; i++) {
		uint32_t e = drv->track[i].start + drv->track[i].length;
		if (e > end) end = e;
	}
	uint32_t lsn = end + 150u;
	uint32_t mins = (lsn / 75u) / 60u;
	uint32_t secs = (lsn / 75u) % 60u;
	uint32_t fr   = lsn % 75u;

	out[0] = 1;
	out[1] = (uint8_t)real_tracks;
	out[2] = (uint8_t)mins;
	out[3] = (uint8_t)secs;
	out[4] = (uint8_t)fr;
	cd_finished = 1;
	return 5;
}

static void cmd_mode_set(const uint8_t *cmd)
{
	uint16_t sz = (uint16_t)((cmd[2] << 8) | cmd[3]);
	switch (sz) {
		case 512: case 1024: case 2048:
		case 2052: case 2336: case 2340:
			cd_sectorsize = sz;
			cd_finished = 1;
			break;
		default:
			cd_error = 1;
			break;
	}
}

static int cmd_play(const uint8_t *cmd, uint8_t *out)
{
	drive_t *drv = cdtv_find_drive();
	if (!drv || drv->track_cnt < 1) {
		cd_error = 1;
		out[0] = 0x00;
		return -1;
	}

	uint8_t op = cmd[0];
	uint32_t s_lba = 0, e_lba = 0;
	int real_tracks = drv->track_cnt - 1;
	if (real_tracks < 1) real_tracks = 1;

	if (op == 0x0b) {
		int track_start = cmd[1];
		int track_end   = cmd[3];
		if (track_start == 0 && track_end == 0) {
			cdtv_dbg("PLAY TRACK 0,0 — stop");
			cdtv_play_lba_next = -1;
			cdtv_play_lba_end  = -1;
			cdtv_play_drv      = NULL;
			cd_playing = 0;
			cd_paused  = 0;
			cd_motor   = 0;
			cd_error   = 1;
			cd_audio_status = 0x15;
			out[0] = 0x00;
			return 1;
		}
		bool got_start = false;
		uint32_t lead_out = drv->track[real_tracks].start;
		uint32_t s = 0, e = lead_out;
		for (int i = 0; i < real_tracks; i++) {
			int trk = i + 1;
			if (trk == track_start) { s = drv->track[i].start; got_start = true; }
			if (trk == track_end)   { e = drv->track[i].start; }
		}
		if (!got_start) {
			cdtv_dbg("PLAY TRACK %d-%d: illegal start", track_start, track_end);
			cd_error = 1;
			out[0] = 0x00;
			return 1;
		}
		s_lba = s;
		e_lba = e;
	} else {
		uint32_t a = ((uint32_t)cmd[1] << 16) | ((uint32_t)cmd[2] << 8) | cmd[3];
		uint32_t b = ((uint32_t)cmd[4] << 16) | ((uint32_t)cmd[5] << 8) | cmd[6];
		if (op == 0x09) {
			s_lba = a;
			e_lba = a + b;
		} else {
			s_lba = cdtv_msf_to_lba(a);
			e_lba = (b < 0x00ffffff) ? cdtv_msf_to_lba(b) : 0xffffffffu;
		}
		uint32_t lead_out = drv->track[real_tracks].start;
		if (e_lba > lead_out) e_lba = lead_out;
	}

	if (s_lba == 0 && e_lba == 0) {
		cdtv_dbg("PLAY stop (0,0)");
		cdtv_play_lba_next = -1;
		cdtv_play_lba_end  = -1;
		cdtv_play_drv      = NULL;
		cd_playing = 0;
		cd_paused  = 0;
		cd_motor   = 0;
		cd_error   = 1;
		cd_audio_status = 0x15;
		out[0] = 0x00;
		return 1;
	}

	bool start_is_audio = false;
	for (int i = 0; i < real_tracks; i++) {
		uint32_t end = drv->track[i].start + drv->track[i].length;
		if (s_lba >= drv->track[i].start && s_lba < end) {
			start_is_audio = !(drv->track[i].attr & 0x40);
			break;
		}
	}

	if (start_is_audio && e_lba > s_lba) {
		cdtv_play_drv      = drv;
		cdtv_play_lba_next = (int32_t)s_lba;
		cdtv_play_lba_end  = (int32_t)e_lba;
		cdtv_last_lba      = (int32_t)s_lba;
		cd_paused = 0;
		cd_audio_status    = 0x11;
		cdtv_dbg("PLAY arm op=%02x s_lba=%u e_lba=%u (n=%u)",
		         op, s_lba, e_lba, e_lba - s_lba);
	} else {
		cdtv_play_drv      = NULL;
		cdtv_play_lba_next = -1;
		cdtv_play_lba_end  = -1;
		cd_audio_status    = 0x14;
		cdtv_dbg("PLAY refuse op=%02x s_lba=%u e_lba=%u (audio=%d)",
		         op, s_lba, e_lba, start_is_audio);
	}

	cd_playing = 1;
	cd_motor   = 1;
	stch_playstart_pending = true;
	out[0] = 0x42;
	return 1;
}

static void cdtv_dispatch(void)
{
	uint8_t op = cmd_buf[0];
	uint8_t reply[CDTV_CMD_MAX];
	int rlen = 0;

	if (op != 0x81 && stch_retries > 0) {
		cdtv_dbg("STCH retry budget cleared (op=%02x)", op);
		stch_retries = 0;
		stch_wait_ack = false;
	}

#if CDTV_DEBUG
	{
		char dump[3 * CDTV_CMD_MAX + 4];
		int n = 0;
		int dump_n = (cmd_idx > CDTV_CMD_MAX) ? CDTV_CMD_MAX : cmd_idx;
		for (int i = 0; i < dump_n; i++) n += snprintf(dump + n, sizeof(dump) - n, "%02x ", cmd_buf[i]);
		cdtv_dbg("RX[%d]: %s", cmd_idx, dump);
	}
#endif

	switch (op) {
		case 0x00:
		case 0x80:
			reply[0] = 0xaa;
			reply[1] = 0x55;
			rlen = 2;
			break;

		case 0x01:
			cd_finished = 1;
			rlen = 0;
			break;

		case 0x02: {
			uint32_t lba   = ((uint32_t)cmd_buf[1] << 16)
			               | ((uint32_t)cmd_buf[2] <<  8)
			               | ((uint32_t)cmd_buf[3]      );
			uint16_t nsec  = ((uint16_t)cmd_buf[4] <<  8) | cmd_buf[5];

			drive_t *drv = cdtv_find_drive();
			if (!drv || !drv->chd_f) {
				cdtv_dbg("READ DATA lba=%u nsec=%u — no drive/chd", lba, nsec);
				cd_error    = 1;
				cd_finished = 1;
				rlen        = 0;
				break;
			}

			cdtv_dbg("READ DATA lba=%u nsec=%u sec_sz=%u", lba, nsec, cd_sectorsize);

			uint8_t raw[2352];
			bool    failed = false;
			for (uint16_t s = 0; s < nsec; s++) {
				if (cdrom_read_raw_sector(drv, lba + s, raw) != 0) {
					cdtv_dbg("READ DATA chd_read failed at lba=%u", lba + s);
					failed = true;
					break;
				}

				const uint8_t *src;
				uint16_t       len;
				switch (cd_sectorsize) {
					case 2048: src = raw + 16; len = 2048; break;
					case 2052: src = raw + 16; len = 2052; break;
					case 2336: src = raw + 16; len = 2336; break;
					case 2340: src = raw + 12; len = 2340; break;
					case 2352: src = raw;      len = 2352; break;
					default:   src = raw + 16; len = cd_sectorsize; break;
				}

				cdtv_push_sector(src, len);

				if (s + 1 < nsec) usleep(1000);
			}

			if (failed) cd_error = 1;
			cd_finished = 1;
			rlen = 0;
			break;
		}

		case 0x04: cd_motor = 1; cd_finished = 1; rlen = 0; break;
		case 0x05:
			cdtv_play_lba_next = -1;
			cdtv_play_lba_end  = -1;
			cdtv_play_drv      = NULL;
			cd_playing = 0;
			cd_paused  = 0;
			cd_motor   = 0;
			cd_finished = 1;
			cd_audio_status = 0x15;
			rlen = 0;
			break;

		case 0x09:
		case 0x0a:
		case 0x0b:
			rlen = cmd_play(cmd_buf, reply);
			if (rlen < 0) rlen = 0;
			break;

		case 0x81: {
			uint8_t flag = 0;
			if (!cd_isready) flag |= 1 << 0;
			if (cd_playing)  flag |= 1 << 2;
			if (cd_finished) flag |= 1 << 3;
			if (cd_error)    flag |= 1 << 4;
			if (cd_motor)    flag |= 1 << 5;
			if (cd_media)    flag |= 1 << 6;
			reply[0] = flag;
			rlen = 1;
			cd_finished = 0;
			break;
		}

		case 0x82: {
			memset(reply, 0, 6);
			if (cd_error) reply[2] |= 1 << 4;
			cd_error   = 0;
			cd_isready = 0;
			rlen = 6;
			cd_finished = 1;
			break;
		}

		case 0x83: {
			static const char *MODEL = "MATSHITA0.96";
			int n = (int)strlen(MODEL);
			memcpy(reply, MODEL, n);
			rlen = n;
			cd_finished = 1;
			break;
		}

		case 0x84:
			cmd_mode_set(cmd_buf);
			rlen = 0;
			break;

		case 0x85:
			reply[0] = (uint8_t)(cd_sectorsize >> 8);
			reply[1] = (uint8_t)(cd_sectorsize     );
			rlen = 2;
			break;

		case 0x86: {
			drive_t *drv = cdtv_find_drive();
			if (!drv || drv->track_cnt < 1) {
				cd_error = 1;
				rlen = 0;
				break;
			}
			uint32_t end = 0;
			for (int i = 0; i < drv->track_cnt; i++) {
				uint32_t e = drv->track[i].start + drv->track[i].length;
				if (e > end) end = e;
			}
			uint32_t size = end - 1;
			reply[0] = (uint8_t)(size >> 16);
			reply[1] = (uint8_t)(size >>  8);
			reply[2] = (uint8_t)(size      );
			reply[3] = (uint8_t)(cd_sectorsize >> 8);
			reply[4] = (uint8_t)(cd_sectorsize     );
			rlen = 5;
			break;
		}

		case 0x87: rlen = cmd_subq(cmd_buf, reply); break;

		case 0x88:
			memset(reply, 0, 14);
			rlen = 14;
			break;

		case 0x89: {
			int n = cmd_info(reply);
			rlen = (n > 0) ? n : 0;
			break;
		}

		case 0x8a: {
			int n = cmd_read_toc(cmd_buf, reply);
			rlen = (n > 0) ? n : 0;
			break;
		}

		case 0x8b:
			cd_paused = (cmd_buf[1] == 0x00) ? 1 : 0;
			if (cdtv_play_lba_next >= 0)
				cd_audio_status = cd_paused ? 0x12 : 0x11;
			cdtv_dbg("PAUSE/RESUME cmd1=%02x → paused=%d", cmd_buf[1], cd_paused);
			cd_finished = 1;
			rlen = 0;
			break;

		case 0xa2:
			memset(reply, 0, 4);
			rlen = 4;
			break;

		case 0xa3:
			cd_finished = 1;
			rlen = 0;
			break;

		default:
			cdtv_dbg("unknown opcode %02x", op);
			cd_error = 1;
			rlen = 0;
			break;
	}

	if (rlen > 0) cdtv_push_reply(reply, rlen);

	cmd_idx  = 0;
	cmd_need = 0;
}

static char cd_save_path_active[256] = {0};
static bool cd_save_load_pending     = false;
static bool cd_save_dirty_observed   = false;
static bool cd_save_load_failed      = false;

static char card_save_path_active[256] = {0};
static bool card_save_dirty_observed   = false;

static uint32_t nvr_verify_due_at_ms = 0;
static char     nvr_verify_path[256] = {0};

static uint64_t fnv1a_64(const char *s)
{
	uint64_t h = 1469598103934665603ull;
	while (*s) {
		h ^= (uint8_t)*s++;
		h *= 1099511628211ull;
	}
	return h;
}

static void cdtv_compute_save_path(const char *cd_path, char *out, size_t outsz)
{
	const char *base = strrchr(cd_path, '/');
	base = base ? base + 1 : cd_path;
	snprintf(out, outsz, "%s/cdtv-%016" PRIx64 ".nvr", CDTV_NVRAM_DIR, fnv1a_64(base));
}

static void cdtv_compute_card_path(const char *cd_path, char *out, size_t outsz)
{
	const char *base = strrchr(cd_path, '/');
	base = base ? base + 1 : cd_path;
	snprintf(out, outsz, "%s/cdtv-%016" PRIx64 ".card", CDTV_NVRAM_DIR, fnv1a_64(base));
}

static void cdtv_nvram_dump(uint8_t *out)
{
	EnableIO();
	spi8(UIO_DMA_READ);
	spi32_w(CDTV_NVRAM_ADDR);
	for (int i = 0; i < CDTV_NVRAM_BYTES; i++) out[i] = (uint8_t)spi_w(0);
	DisableIO();
}

static void cdtv_nvram_upload(const uint8_t *buf)
{
	static uint16_t words[CDTV_NVRAM_BYTES / 2];
	memcpy(words, buf, CDTV_NVRAM_BYTES);

	EnableIO();
	fpga_spi_fast(UIO_DMA_WRITE);
	fpga_spi_fast(CDTV_NVRAM_ADDR);
	fpga_spi_fast(0);
	fpga_spi_fast_block_write(words, CDTV_NVRAM_BYTES / 2);
	DisableIO();
}

static bool cdtv_nvram_load_from_path(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0) {
		cdtv_dbg("NVR load skip: no file at %s (errno=%d)", path, errno);
		return false;
	}
	if (st.st_size < CDTV_NVRAM_BYTES) {
		cdtv_dbg("NVR load skip: %s too small %lld (want >= %d)",
		         path, (long long)st.st_size, CDTV_NVRAM_BYTES);
		return false;
	}

	static uint8_t nvr[CDTV_NVRAM_BYTES];
	FILE *f = fopen(path, "rb");
	if (!f) {
		cdtv_dbg("NVR load FAIL: fopen(%s) errno=%d", path, errno);
		cd_save_load_failed = true;
		return false;
	}
	size_t got = fread(nvr, 1, CDTV_NVRAM_BYTES, f);
	fclose(f);
	if (got != CDTV_NVRAM_BYTES) {
		cdtv_dbg("NVR load FAIL: read %u of %d bytes from %s",
		         (unsigned)got, CDTV_NVRAM_BYTES, path);
		cd_save_load_failed = true;
		return false;
	}

	cdtv_nvram_upload(nvr);

	cd_save_load_failed = false;
	strncpy(nvr_verify_path, path, sizeof(nvr_verify_path) - 1);
	nvr_verify_path[sizeof(nvr_verify_path) - 1] = 0;
	nvr_verify_due_at_ms = (uint32_t)GetTimer(100);
	cdtv_dbg("NVR loaded (%d bytes from %s) - verify in 100 ms", CDTV_NVRAM_BYTES, path);
	return true;
}

static void cdtv_nvram_verify_load_pending(void)
{
	if (!nvr_verify_due_at_ms) return;
	if (!CheckTimer(nvr_verify_due_at_ms)) return;
	nvr_verify_due_at_ms = 0;

	static uint8_t verify[CDTV_NVRAM_BYTES];
	static uint8_t expected[CDTV_NVRAM_BYTES];

	cdtv_nvram_dump(verify);

	FILE *f = fopen(nvr_verify_path, "rb");
	if (!f) {
		cdtv_dbg("NVR verify: fopen(%s) failed errno=%d", nvr_verify_path, errno);
		cd_save_load_failed = true;
		return;
	}
	size_t got = fread(expected, 1, CDTV_NVRAM_BYTES, f);
	fclose(f);
	if (got != CDTV_NVRAM_BYTES) {
		cdtv_dbg("NVR verify: short read %u/%d", (unsigned)got, CDTV_NVRAM_BYTES);
		cd_save_load_failed = true;
		return;
	}
	if (!memcmp(expected, verify, CDTV_NVRAM_BYTES)) {
		cdtv_dbg("NVR loaded %d bytes from %s (verify OK)",
		         CDTV_NVRAM_BYTES, nvr_verify_path);
		cd_save_load_failed = false;
		return;
	}

	int first_bad = -1, mismatches = 0;
	for (int i = 0; i < CDTV_NVRAM_BYTES; i++) {
		if (verify[i] != expected[i]) {
			if (first_bad < 0) first_bad = i;
			mismatches++;
		}
	}
	cdtv_dbg("NVR LOAD VERIFY FAILED: %d/%d mismatched (first @ 0x%04x)",
	         mismatches, CDTV_NVRAM_BYTES, first_bad);
	cd_save_load_failed = true;
}

static bool cdtv_nvram_save_to_path(const char *path)
{
	static uint8_t buf[CDTV_NVRAM_BYTES];
	cdtv_nvram_dump(buf);

	FILE *cur = fopen(path, "rb");
	if (cur) {
		static uint8_t disk_buf[CDTV_NVRAM_BYTES];
		size_t n = fread(disk_buf, 1, CDTV_NVRAM_BYTES, cur);
		fclose(cur);
		if (n == CDTV_NVRAM_BYTES && !memcmp(disk_buf, buf, CDTV_NVRAM_BYTES)) {
			cdtv_dbg("NVR save skipped: identical to %s", path);
			return true;
		}
	}

	if (mkdir(CDTV_NVRAM_DIR, 0755) != 0 && errno != EEXIST) {
		cdtv_dbg("NVR mkdir(%s) failed: errno=%d", CDTV_NVRAM_DIR, errno);
	}

	char tmp_path[300];
	snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

	FILE *f = fopen(tmp_path, "wb");
	if (!f) {
		cdtv_dbg("NVR fopen(%s) failed: errno=%d", tmp_path, errno);
		return false;
	}

	uint8_t zero[512] = {0};
	size_t wrote = fwrite(buf, 1, CDTV_NVRAM_BYTES, f);
	for (int i = 0; i < (CDTV_NVRAM_FILE_BYTES - CDTV_NVRAM_BYTES) / (int)sizeof(zero); i++) {
		wrote += fwrite(zero, 1, sizeof(zero), f);
	}
	int flush = fflush(f);
	int fsy   = fsync(fileno(f));
	int cls   = fclose(f);
	if (wrote != CDTV_NVRAM_FILE_BYTES || flush != 0 || fsy != 0 || cls != 0) {
		cdtv_dbg("NVR write incomplete: wrote=%u flush=%d fsync=%d close=%d",
		         (unsigned)wrote, flush, fsy, cls);
		unlink(tmp_path);
		return false;
	}

	if (rename(tmp_path, path) != 0) {
		cdtv_dbg("NVR rename(%s -> %s) failed: errno=%d", tmp_path, path, errno);
		unlink(tmp_path);
		return false;
	}

	cdtv_dbg("NVR saved %d bytes to %s", CDTV_NVRAM_BYTES, path);
	return true;
}

static void cdtv_card_dump(uint8_t *out)
{
	EnableIO();
	spi8(UIO_DMA_READ);
	spi32_w(CDTV_CARD_ADDR);
	for (int i = 0; i < CDTV_CARD_BYTES; i++) out[i] = (uint8_t)spi_w(0);
	DisableIO();
}

static void cdtv_card_upload(const uint8_t *buf)
{
	static uint16_t words[CDTV_CARD_BYTES / 2];
	memcpy(words, buf, CDTV_CARD_BYTES);

	EnableIO();
	fpga_spi_fast(UIO_DMA_WRITE);
	fpga_spi_fast(CDTV_CARD_ADDR);
	fpga_spi_fast(0);
	fpga_spi_fast_block_write(words, CDTV_CARD_BYTES / 2);
	DisableIO();
}

static void cdtv_card_load_from_path(const char *path)
{
	static uint8_t card[CDTV_CARD_BYTES];
	memset(card, 0, sizeof(card));

	FILE *f = path[0] ? fopen(path, "rb") : NULL;
	if (f) {
		size_t got = fread(card, 1, CDTV_CARD_BYTES, f);
		fclose(f);
		cdtv_dbg("CARD loaded %u/%d bytes from %s", (unsigned)got, CDTV_CARD_BYTES, path);
	}
	else {
		cdtv_dbg("CARD blank (no file at %s)", path[0] ? path : "(none)");
	}

	cdtv_card_upload(card);
}

static bool cdtv_card_save_to_path(const char *path)
{
	static uint8_t buf[CDTV_CARD_BYTES];
	cdtv_card_dump(buf);

	if (FILE *cur = fopen(path, "rb")) {
		static uint8_t disk_buf[CDTV_CARD_BYTES];
		size_t n = fread(disk_buf, 1, CDTV_CARD_BYTES, cur);
		fclose(cur);
		if (n == CDTV_CARD_BYTES && !memcmp(disk_buf, buf, CDTV_CARD_BYTES)) {
			card_save_dirty_observed = false;
			return true;
		}
	}

	if (mkdir(CDTV_NVRAM_DIR, 0755) != 0 && errno != EEXIST) {
		cdtv_dbg("CARD mkdir(%s) failed: errno=%d", CDTV_NVRAM_DIR, errno);
		return false;
	}

	FILE *f = fopen(path, "wb");
	if (!f) {
		cdtv_dbg("CARD fopen(%s) failed: errno=%d", path, errno);
		return false;
	}

	size_t wrote = fwrite(buf, 1, CDTV_CARD_BYTES, f);
	int flush = fflush(f);
	int fsy   = fsync(fileno(f));
	int cls   = fclose(f);

	if (wrote != CDTV_CARD_BYTES || flush != 0 || fsy != 0 || cls != 0) {
		cdtv_dbg("CARD save FAILED %s wrote=%u flush=%d fsync=%d close=%d errno=%d",
		         path, (unsigned)wrote, flush, fsy, cls, errno);
		return false;
	}

	card_save_dirty_observed = false;
	cdtv_dbg("CARD saved %d bytes to %s", CDTV_CARD_BYTES, path);
	return true;
}

static bool cdtv_nvram_save_to_disk(void)
{
	if (!cd_save_path_active[0]) return false;
	if (cd_save_load_failed) {
		static bool warned_once = false;
		if (!warned_once) {
			cdtv_dbg("NVR save BLOCKED: load failed for %s - refusing to overwrite "
			         "a real save with a blank NVRAM. Future blocks silent.",
			         cd_save_path_active);
			warned_once = true;
		}
		return false;
	}
	return cdtv_nvram_save_to_path(cd_save_path_active);
}

void cdtv_cd_set_cd_path(const char *path)
{
	if (!path) path = "";

	char new_save[sizeof(cd_save_path_active)] = {0};
	if (path[0]) cdtv_compute_save_path(path, new_save, sizeof(new_save));

	char new_card[sizeof(card_save_path_active)] = {0};
	if (path[0]) cdtv_compute_card_path(path, new_card, sizeof(new_card));

	if (strcmp(new_card, card_save_path_active) != 0) {
		if (card_save_path_active[0] && card_save_dirty_observed) {
			cdtv_dbg("set_cd_path: flushing old card %s before swap", card_save_path_active);
			cdtv_card_save_to_path(card_save_path_active);
		}
		strncpy(card_save_path_active, new_card, sizeof(card_save_path_active) - 1);
		card_save_path_active[sizeof(card_save_path_active) - 1] = 0;
		card_save_dirty_observed = false;
	}

	if (strcmp(new_save, cd_save_path_active) != 0) {
		if (cd_save_path_active[0] && cd_save_dirty_observed) {
			cdtv_dbg("set_cd_path: flushing old save %s before swap", cd_save_path_active);
			cdtv_nvram_save_to_path(cd_save_path_active);
		}
		strncpy(cd_save_path_active, new_save, sizeof(cd_save_path_active) - 1);
		cd_save_path_active[sizeof(cd_save_path_active) - 1] = 0;
		cd_save_dirty_observed = false;
		cd_save_load_failed    = false;
		cd_save_load_pending   = (cd_save_path_active[0] != 0);
	}
	bool has_path    = (path[0] != 0);
	bool was_present = (cd_media != 0);
	bool swap        = was_present && has_path && strcmp(cd_path_active, path) != 0;

	strncpy(cd_path_active, path, sizeof(cd_path_active) - 1);
	cd_path_active[sizeof(cd_path_active) - 1] = 0;

	cd_media   = swap ? 0 : (has_path ? 1 : 0);
	cd_isready = 0;
	cd_motor   = 0;
	cd_playing = 0;
	cd_paused  = 0;
	cd_audio_status = 0x15;
	cdtv_last_lba   = 0;
	cdtv_play_lba_next = -1;
	cdtv_play_lba_end  = -1;
	cdtv_play_drv      = NULL;
	cd_finished = has_path ? 1 : 0;

	stch_retries = STCH_RETRY_BUDGET;
	stch_next_ms = GetTimer(0);

	if (swap) {
		cd_swap_pending  = true;
		cd_swap_ready_ms = GetTimer(CDTV_SWAP_SETTLE_MS);
	} else {
		cd_swap_pending  = false;
		cd_swap_ready_ms = 0;
	}

	cdtv_dbg("set_cd_path: %s%s", has_path ? path : "(empty)",
	         swap ? " (swap, removal edge synthesized)" : "");
}

void cdtv_cd_init(void)
{
	cmd_idx       = 0;
	cmd_need      = 0;
	cd_motor      = 0;
	cd_media      = (cd_path_active[0] != 0) ? 1 : 0;
	cd_isready    = 0;
	cd_playing    = 0;
	cd_paused     = 0;
	cd_audio_status = 0x15;
	cdtv_last_lba   = 0;
	cdtv_play_lba_next = -1;
	cdtv_play_lba_end  = -1;
	cdtv_play_drv      = NULL;
	cd_finished   = cd_media ? 1 : 0;
	cd_error      = 0;
	cd_sectorsize = 2048;
	if (cd_media) {
		stch_retries = STCH_RETRY_BUDGET;
		stch_next_ms = GetTimer(0);
	}

	cd_swap_pending  = false;
	cd_swap_ready_ms = 0;

	cd_save_dirty_observed   = false;
	cd_save_load_failed      = false;
	card_save_dirty_observed = false;
	if (cd_save_path_active[0]) cd_save_load_pending = true;

	cdtv_dbg("init media=%d isready=%d stch_retries=%d", cd_media, cd_isready, stch_retries);
}

void cdtv_cd_poll(void)
{
	if (!cdtv_active()) return;

	if (cd_swap_pending && CheckTimer(cd_swap_ready_ms)) {
		cd_swap_pending = false;
		cd_media    = 1;
		cd_finished = 1;
		stch_retries = STCH_RETRY_BUDGET;
		stch_next_ms = GetTimer(0);
		cdtv_dbg("swap: settle elapsed, new disc now present");
	}

	if (cd_save_load_pending) {
		cd_save_load_pending = false;
		if (cd_save_path_active[0]) cdtv_nvram_load_from_path(cd_save_path_active);
		cdtv_card_load_from_path(card_save_path_active);
	}

	cdtv_nvram_verify_load_pending();

	{
		static int nvr_menu_was_present = 0;
		const bool nvr_dirty_now = (cdtv_read_status() & CDTV_STATUS_NVR_DIRTY) != 0;
		const int  nvr_menu_now  = menu_present();
		const bool osd_open_edge = nvr_menu_now && !nvr_menu_was_present;
		nvr_menu_was_present     = nvr_menu_now;

		if (nvr_dirty_now) cd_save_dirty_observed = true;

		const bool card_dirty_now = (cdtv_read_status() & CDTV_STATUS_CARD_DIRTY) != 0;
		if (card_dirty_now) card_save_dirty_observed = true;

		if (osd_open_edge && cd_save_dirty_observed) {
			cdtv_dbg("NVR flush: OSD opened with dirty pending");
			cdtv_nvram_save_to_disk();
		}

		if (osd_open_edge && card_save_dirty_observed && card_save_path_active[0]) {
			cdtv_dbg("CARD flush: OSD opened with dirty pending");
			cdtv_card_save_to_path(card_save_path_active);
		}
	}

	if (stch_playstart_pending) {
		stch_playstart_pending = false;
		cdtv_dbg("STCH play-start (playing=%d motor=%d)", cd_playing, cd_motor);
		cdtv_inject_stch();
	}

	if (stch_retries > 0 && CheckTimer(stch_next_ms)) {
		cdtv_inject_stch();
		stch_retries--;
		stch_next_ms = GetTimer(stch_wait_ack ? STCH_PLAYEND_PERIOD_MS
		                                      : STCH_RETRY_PERIOD_MS);
	}

	cdtv_cdda_pump();

	if (stch_wait_ack && cdtv_stch_acked()) {
		stch_wait_ack = false;
		stch_retries  = 0;
		cdtv_dbg("STCH play-end ACKed — retry stopped");
	}

	for (int guard = 0; guard < 32; guard++) {
		uint16_t status = cdtv_read_status();
		if (!(status & CDTV_STATUS_REQ)) break;

		uint8_t b = cdtv_drain_byte();

		if (cmd_idx == 0) {
			cmd_need = cr511_command_length(b);
			if (cmd_need < 0) {
				cdtv_dbg("drop unknown opcode %02x", b);
				cmd_idx = 0;
				cmd_need = 0;
				continue;
			}
		}

		if (cmd_idx < CDTV_CMD_MAX) {
			cmd_buf[cmd_idx++] = b;
		}

		if (cmd_idx >= cmd_need) {
			cdtv_dispatch();
		}
	}
}
