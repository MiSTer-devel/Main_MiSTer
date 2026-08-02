#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <byteswap.h>

#include "../../spi.h"
#include "../../fpga_io.h"
#include "../../user_io.h"
#include "../../ide.h"
#include "../../ide_cdrom.h"
#include "../../hardware.h"
#include "../../menu.h"
#include "../chd/mister_chd.h"
#include "minimig_config.h"
#include "akiko_cd32.h"

static void akiko_diag(const char *fmt, ...);

#define AKIKO_CD32_DEBUG 0
#if AKIKO_CD32_DEBUG
	#define akiko_dbg(fmt, ...) akiko_diag("[akiko] " fmt, ##__VA_ARGS__)
#else
	#define akiko_dbg(...) do { } while (0)
#endif

#define AKIKO_BRIDGE_ADDR  0xF400

#define AKIKO_CDDA_ADDR        0xF200
#define AKIKO_CDDA_BYTES       2352
#define AKIKO_STATUS_CDDA_REQ  (1u << 8)

#define AKIKO_STATUS_CMD             0x63
#define AKIKO_STATUS_REQ             (1u << 11)
#define AKIKO_STATUS_SEC_REQ         (1u << 10)
#define AKIKO_STATUS_RX_BUSY (1u << 9)

#define AKIKO_SECTOR_ADDR  0xF500

#define AKIKO_SUBCODE_ADDR 0xF410
#define AKIKO_SUB_ENTRY    12
#define AKIKO_SUB_BYTES    96

#define AKIKO_NVRAM_ADDR              0xF440
#define AKIKO_NVRAM_BYTES             1024
#define AKIKO_NVRAM_DIR               "/media/fat/saves/Minimig"
#define AKIKO_NVRAM_FILE_LEGACY       "/media/fat/saves/Minimig/cd32.nvr"
#define AKIKO_STATUS_NVR_DIRTY        (1u << 7)
#define AKIKO_NVRAM_POLL_PERIOD_MS    1000
#define AKIKO_NVRAM_DIRTY_DEBOUNCE_MS 30000
#define AKIKO_NVRAM_MIN_SAVE_INTERVAL_MS 60000

#define AKIKO_SECTOR_BYTES 2352

static const int command_lengths[16] = {
	1, 2, 1, 1, 12, 2, 1, 1, 4, 1, 2, -1, -1, -1, -1, -1
};

#define CH_ERR_OK          0x00
#define CH_ERR_BADCOMMAND  0x80
#define CH_ERR_NODISK      0xf8
#define CH_ERR_CHECKSUM    0x88
#define CDS_ERROR          0x80
#define CDS_PLAYING        0x08
#define CDS_PLAYEND        0x00

#define AKIKO_FIRMWARE     "CHINON  O-658-2 24"

#define AKIKO_CMD_MAX      32

static uint8_t  cd_initialized      = 0;
static uint8_t  cd_paused           = 0;
static uint8_t  cd_playing          = 0;
static uint8_t  cd_led_state        = 0;
static uint8_t  cd_door             = 1;
static uint32_t cd_play_start_lba   = 0;
static uint32_t cd_play_end_lba     = 0;

static int32_t  cd_data_lba_base    = -1;

#define AKIKO_PREFETCH_SECTORS 128
#define AKIKO_PREFETCH_WINDOWS 4

typedef struct {
	int32_t  base_lba;
	uint32_t lru_tick;
	uint8_t  valid[AKIKO_PREFETCH_SECTORS];
	uint8_t  buf[AKIKO_PREFETCH_SECTORS * AKIKO_SECTOR_BYTES];
} prefetch_window_t;

static prefetch_window_t cd_prefetch_windows[AKIKO_PREFETCH_WINDOWS];
static uint32_t cd_prefetch_lru_counter = 0;
static uint32_t cd_prefetch_hits = 0;
static uint32_t cd_prefetch_misses = 0;
static uint32_t cd_prefetch_failed_sectors = 0;

static inline void akiko_prefetch_invalidate(void)
{
	for (int i = 0; i < AKIKO_PREFETCH_WINDOWS; i++) {
		cd_prefetch_windows[i].base_lba = -1;
	}
}

static int akiko_prefetch_find(uint32_t lba)
{
	for (int i = 0; i < AKIKO_PREFETCH_WINDOWS; i++) {
		const prefetch_window_t *w = &cd_prefetch_windows[i];
		if (w->base_lba >= 0
		    && lba >= (uint32_t)w->base_lba
		    && lba <  (uint32_t)w->base_lba + AKIKO_PREFETCH_SECTORS) {
			return i;
		}
	}
	return -1;
}

static int akiko_prefetch_pick_evict(void)
{
	int pick = 0;
	for (int i = 0; i < AKIKO_PREFETCH_WINDOWS; i++) {
		if (cd_prefetch_windows[i].base_lba < 0) return i;
		if (cd_prefetch_windows[i].lru_tick < cd_prefetch_windows[pick].lru_tick) {
			pick = i;
		}
	}
	return pick;
}

static int8_t   cd_audio_timeout    = 0;

static uint32_t cd_audio_play_until_ms = 0;

static int32_t cd_cdda_q_pos    = -1;
static int32_t cd_cdda_q_end    = -1;
static int32_t cd_cdda_lba_next = -1;
static int32_t cd_cdda_lba_end  = -1;
static drive_t *cd_cdda_drv     = NULL;

static bool     cd_last_mounted     = false;

static bool     g_akiko_pending_minimig_reset = false;

static uint8_t  cd_post_info_media_push_pending = 0;

#define AKIKO_TOC_REPEAT       3
#define AKIKO_TOC_PUSH_PERIOD_MS 20
#define AKIKO_TOC_MAX_POINTS   32
static uint8_t  toc_buffer[AKIKO_TOC_MAX_POINTS * 13];
static uint8_t  toc_point_count     = 0;
static int16_t  toc_push_idx        = -1;
static uint32_t toc_push_last_ms    = 0;

static char cd_save_path_active[256] = {0};
static bool cd_save_load_pending     = false;
static bool cd_save_dirty_observed   = false;

static bool cd_save_load_failed      = false;

static uint64_t fnv1a_64(const char *s)
{
	uint64_t h = 0xcbf29ce484222325ULL;
	while (*s) {
		h ^= (uint64_t)(uint8_t)*s++;
		h *= 0x100000001b3ULL;
	}
	return h;
}

static void compute_save_path(const char *cd_path, char *out, size_t outsz)
{
	const char *base = strrchr(cd_path, '/');
	base = base ? base + 1 : cd_path;
	uint64_t h = fnv1a_64(base);
	snprintf(out, outsz, "%s/cd32-%016" PRIx64 ".nvr", AKIKO_NVRAM_DIR, h);
}

static inline uint32_t msf_to_lba(uint8_t m, uint8_t s, uint8_t f)
{
	return (((uint32_t)m * 60u + s) * 75u + f) - 150u;
}

static inline uint8_t bcd_to_bin(uint8_t b)
{
	return (uint8_t)(((b >> 4) * 10u) + (b & 0x0fu));
}

static inline uint8_t bin_to_bcd(uint8_t v)
{
	return (uint8_t)(((v / 10u) << 4) | (v % 10u));
}

static inline bool cd32_active(void)
{
	return (minimig_config.cpu & 0x03) == 3
	    && ((minimig_config.chipset >> 2) & 7) == 6
	    && minimig_config.hardfile[0].cfg == 2;
}

static bool cd_is_mounted(void)
{
	for (int p = 0; p < 2; p++) {
		for (int d = 0; d < 2; d++) {
			drive_t *drv = &ide_inst[p].drive[d];
			if (drv->cd && (drv->chd_f || drv->f)) {
				return true;
			}
		}
	}
	return false;
}

static drive_t *cd_find_drive(void)
{
	for (int p = 0; p < 2; p++) {
		for (int d = 0; d < 2; d++) {
			drive_t *drv = &ide_inst[p].drive[d];
			if (drv->cd && (drv->chd_f || drv->f)) {
				return drv;
			}
		}
	}
	return NULL;
}

static uint16_t akiko_read_status(void)
{
	uint16_t res;
	EnableIO();
	res = spi_w(AKIKO_STATUS_CMD);
	if (!res) res = (uint8_t)spi_w(0);
	DisableIO();
	return res;
}

static uint8_t akiko_read_sec_counter(void);

static bool akiko_wait_status_bit(uint16_t mask, bool want_set, int max_iters)
{
	for (int i = 0; i < max_iters; i++) {
		uint16_t s = akiko_read_status();
		if (((s & mask) != 0) == want_set) return true;
	}
	return false;
}

static int akiko_drain_command(uint8_t *buf)
{
	EnableIO();
	spi8(UIO_DMA_READ);
	spi32_w(AKIKO_BRIDGE_ADDR);

	buf[0] = (uint8_t)spi_w(0);

	int op = buf[0] & 0x0f;
	int payload_len = command_lengths[op];
	int total;
	if (payload_len < 0) {
		total = AKIKO_CMD_MAX;
	} else {
		total = payload_len + 1;
	}
	if (total > AKIKO_CMD_MAX) total = AKIKO_CMD_MAX;

	for (int i = 1; i < total; i++) {
		buf[i] = (uint8_t)spi_w(0);
	}

	DisableIO();

	akiko_wait_status_bit(AKIKO_STATUS_REQ, false, 200);

	return total;
}

static void akiko_send_response(const uint8_t *payload, int len)
{
	if (len < 1 || len > AKIKO_CMD_MAX) {
		akiko_dbg("send_response: bad len %d\n", len);
		return;
	}

	uint8_t out[AKIKO_CMD_MAX + 1];
	memcpy(out, payload, (size_t)len);

	uint32_t sum = 0;
	for (int i = 0; i < len; i++) sum += out[i];
	out[len] = (uint8_t)(0xff - (sum & 0xff));

	int total = len + 1;

	EnableIO();
	spi8(UIO_DMA_WRITE);
	spi32_w(AKIKO_BRIDGE_ADDR);
	for (int i = 0; i < total; i++) {
		spi_w(out[i]);
	}
	DisableIO();

	akiko_wait_status_bit(AKIKO_STATUS_RX_BUSY, true, 200);

#if AKIKO_CD32_DEBUG
	akiko_dbg("TX %d bytes:", total);
	for (int i = 0; i < total; i++) printf(" %02x", out[i]);
	printf("\n");
#endif
}

static void toc_pack_entry(int point, uint8_t control, uint32_t msf_or_track)
{
	if (toc_point_count >= AKIKO_TOC_MAX_POINTS) return;
	uint8_t *d = &toc_buffer[toc_point_count * 13];
	memset(d, 0, 13);
	d[1] = 1u | ((uint8_t)control << 4);
	d[3] = (point < 100) ? bin_to_bcd((uint8_t)point) : (uint8_t)point;
	if (point == 0xA0 || point == 0xA1) {
		d[8] = bin_to_bcd((uint8_t)msf_or_track);
	} else {
		uint32_t lsn = msf_or_track + 150u;
		uint32_t mins = (lsn / 75u) / 60u;
		uint32_t secs = (lsn / 75u) % 60u;
		uint32_t fr   = lsn % 75u;
		d[8]  = bin_to_bcd((uint8_t)mins);
		d[9]  = bin_to_bcd((uint8_t)secs);
		d[10] = bin_to_bcd((uint8_t)fr);
	}
	toc_point_count++;
}

static void akiko_build_toc(void)
{
	toc_point_count = 0;
	toc_push_idx    = -1;

	drive_t *drv = cd_find_drive();
	if (!drv || drv->track_cnt < 2) {
		akiko_diag("[akiko] TOC build SKIP (no drive or no tracks)");
		return;
	}

	int real_tracks = drv->track_cnt - 1;
	if (real_tracks < 1 || real_tracks > 99) {
		akiko_diag("[akiko] TOC build SKIP (real_tracks=%d)", real_tracks);
		return;
	}

	for (int i = 0; i <= real_tracks; i++) {
		akiko_diag("[akiko] TRACK[%d] num=%u attr=0x%02x start=%u length=%u chd_off=%u",
		           i, drv->track[i].number, drv->track[i].attr,
		           drv->track[i].start, drv->track[i].length,
		           drv->track[i].chd_offset);
	}

	uint8_t first_ctrl = (drv->track[0].attr & 0x40) ? 0x04 : 0x00;
	toc_pack_entry(0xA0, first_ctrl, 1);
	toc_pack_entry(0xA1, first_ctrl, real_tracks);
	toc_pack_entry(0xA2, first_ctrl, drv->track[real_tracks].start);

	for (int i = 0; i < real_tracks; i++) {
		uint8_t ctrl = (drv->track[i].attr & 0x40) ? 0x04 : 0x00;
		toc_pack_entry(drv->track[i].number, ctrl, drv->track[i].start);
	}

	toc_push_last_ms = 0;
	akiko_diag("[akiko] TOC built: %u points (%d real tracks, lead-out lba=%u)",
	           toc_point_count, real_tracks, drv->track[real_tracks].start);
}

static bool akiko_push_toc_entry(void)
{
	if (toc_push_idx < 0) return false;
	int point_idx = toc_push_idx / AKIKO_TOC_REPEAT;
	if (point_idx >= toc_point_count) {
		toc_push_idx = -1;
		akiko_diag("[akiko] TOC drip complete (%u points * %d repeat = %d frames)",
		           toc_point_count, AKIKO_TOC_REPEAT,
		           toc_point_count * AKIKO_TOC_REPEAT);
		return false;
	}
	uint8_t r[15];
	memset(r, 0, sizeof(r));
	r[0] = 0x06;
	r[1] = 0x0a;
	memcpy(r + 2, &toc_buffer[point_idx * 13], 13);
	int counter = toc_push_idx;
	r[6] = bin_to_bcd(99);
	r[7] = bin_to_bcd((uint8_t)(24u + (uint32_t)counter / 75u));
	r[8] = bin_to_bcd((uint8_t)((uint32_t)counter % 75u));
	akiko_send_response(r, 15);
	akiko_diag("[akiko] TOC push idx=%d point_idx=%d point=0x%02x ctrl=0x%02x msf=%02x:%02x:%02x",
	           toc_push_idx, point_idx, r[5], (r[3] >> 4) & 0x0f, r[10], r[11], r[12]);
	toc_push_idx++;
	return true;
}

static void cmd_info(const uint8_t *cmd)
{
	uint8_t r[20];
	r[0] = cmd[0];
	r[1] = cd_door;
	memcpy(&r[2], AKIKO_FIRMWARE, 18);
	akiko_send_response(r, 20);
	cd_initialized = 2;
	akiko_dbg("INFO -> initialized=2\n");
	akiko_build_toc();
}

static void emit_playend_notify(int status)
{
	uint8_t r[2];
	r[0] = 0x04;
	if (status < 0)        r[1] = CDS_ERROR;
	else if (status == 0)  r[1] = CDS_PLAYING | 0x02;
	else                   r[1] = CDS_PLAYEND;
	r[1] |= cd_door;
	akiko_send_response(r, 2);
	akiko_dbg("PLAYEND_NOTIFY status=%d -> %02x\n", status, r[1]);
}

static void cmd_stop(const uint8_t *cmd)
{
	uint8_t r[2];
	r[0] = cmd[0];
	r[1] = cd_is_mounted() ? 0x00 : (CH_ERR_NODISK | cd_door);
	cd_playing = 0;
	cd_paused = 0;
	cd_data_lba_base = -1;
	cd_cdda_lba_next = -1;
	cd_cdda_lba_end  = -1;
	cd_cdda_drv      = NULL;
	if (cd_audio_timeout > 0) cd_audio_timeout = 0;
	cd_audio_play_until_ms = 0;
	akiko_send_response(r, 2);
	akiko_dbg("STOP\n");
}

static void cmd_pause(const uint8_t *cmd)
{
	uint8_t r[2];
	r[0] = cmd[0];
	if (!cd_is_mounted()) {
		r[1] = CH_ERR_NODISK | cd_door;
	} else {
		r[1] = (cd_playing ? CDS_PLAYING : 0) | cd_door;
	}
	toc_push_idx     = -1;
	toc_push_last_ms = 0;
	cd_paused = 1;
	akiko_send_response(r, 2);
	akiko_dbg("PAUSE (playing=%d, mounted=%d)\n", cd_playing, cd_is_mounted());
}

static void cmd_unpause(const uint8_t *cmd)
{
	uint8_t r[2];
	r[0] = cmd[0];
	if (!cd_is_mounted()) {
		r[1] = CH_ERR_NODISK | cd_door;
	} else {
		r[1] = (cd_playing ? CDS_PLAYING : 0) | cd_door;
	}
	cd_paused = 0;
	akiko_send_response(r, 2);
	akiko_dbg("UNPAUSE (playing=%d, mounted=%d)\n", cd_playing, cd_is_mounted());
}

static int cdrom_speed = 1;

static void cmd_multi(const uint8_t *cmd)
{
	uint8_t r[2];
	r[0] = cmd[0];

	int new_speed = (cmd[8] & 0x40) ? 2 : 1;
	if (new_speed != cdrom_speed) {
		akiko_diag("[akiko] cdrom_speed change %dx -> %dx (cmd8=0x%02x)",
		           cdrom_speed, new_speed, cmd[8]);
		cdrom_speed = new_speed;
	}

	if (!cd_is_mounted()) {
		r[1] = 0x01;
		akiko_send_response(r, 2);
		akiko_dbg("PLAY: no disk\n");
		return;
	}

	uint32_t s_msf_total = (((uint32_t)bcd_to_bin(cmd[1]) * 60u
	                       + bcd_to_bin(cmd[2])) * 75u + bcd_to_bin(cmd[3]));
	bool seek_negative = (s_msf_total < 150u);
	uint32_t s_lba = s_msf_total - 150u;
	uint32_t e_lba = msf_to_lba(bcd_to_bin(cmd[4]), bcd_to_bin(cmd[5]), bcd_to_bin(cmd[6]));

	cd_play_start_lba = s_lba;
	cd_play_end_lba   = e_lba;

	bool data_read = (cmd[7] & 0x80) != 0;
	if (data_read) {
		drive_t *drv2 = cd_find_drive();
		bool start_is_audio = false;
		bool start_is_oob   = false;
		if (!seek_negative && drv2) {
			int real_tracks2 = drv2->track_cnt > 0 ? drv2->track_cnt - 1 : 0;
			uint32_t lead_out2 = (real_tracks2 > 0) ? drv2->track[real_tracks2].start : 0;
			if (lead_out2 && s_lba >= lead_out2) {
				start_is_oob = true;
			}
			for (int i = 0; i < real_tracks2 && !start_is_oob; i++) {
				uint32_t body_end = drv2->track[i].start + drv2->track[i].length;
				if (s_lba >= drv2->track[i].start && s_lba < body_end) {
					start_is_audio = !(drv2->track[i].attr & 0x40);
					break;
				}
				if (i + 1 < real_tracks2 &&
				    s_lba >= body_end && s_lba < drv2->track[i + 1].start) {
					start_is_audio = !(drv2->track[i + 1].attr & 0x40);
					break;
				}
			}
		}
		if (start_is_audio) {
			cd_data_lba_base = -1;
			r[1] = CDS_PLAYEND | cd_door;
			akiko_diag("[akiko] PLAY DATA REFUSED audio start_lba=%u (cmd7=0x%02x)",
			           s_lba, cmd[7]);
		} else if (start_is_oob) {
			cd_data_lba_base = -1;
			r[1] = 0x02;
			akiko_diag("[akiko] PLAY DATA REFUSED oob start_lba=%u (cmd7=0x%02x) — silent (no PBX, no playend)",
			           s_lba, cmd[7]);
		} else {
			cd_data_lba_base = (int32_t)s_lba;
			cd_paused = 0;
			r[1] = 0x02;
			akiko_diag("[akiko] PLAY DATA arm: start_lba=%d (cmd7=0x%02x)", (int32_t)s_lba, cmd[7]);
		}
	} else if (seek_negative) {
		cd_data_lba_base = -1;
		cd_playing = 0;
		cd_paused = 0;
		toc_push_idx = (toc_point_count > 0) ? 0 : -1;
		toc_push_last_ms = 0;
		r[1] = 0x00;
		akiko_diag("[akiko] MULTI scan-TOC trigger (points=%u)", toc_point_count);
	} else {
		cd_data_lba_base = -1;
		cd_playing = 1;
		cd_paused = 0;
		r[1] = 0x42;
		cd_audio_timeout = 2;

		drive_t *drv = cd_find_drive();
		bool valid = false;
		if (drv && e_lba > s_lba) {
			int real_tracks = drv->track_cnt > 0 ? drv->track_cnt - 1 : 0;
			for (int i = 0; i < real_tracks; i++) {
				uint32_t end = drv->track[i].start + drv->track[i].length;
				if (s_lba >= drv->track[i].start && s_lba < end) {
					if (!(drv->track[i].attr & 0x40)) valid = true;
					break;
				}
			}
		}

		if (valid) {
			bool same_range_inflight =
				(cd_cdda_drv == drv) &&
				(cd_cdda_lba_end == (int32_t)e_lba) &&
				(cd_cdda_lba_next >= (int32_t)s_lba) &&
				(cd_cdda_lba_next <  (int32_t)e_lba);
			cd_cdda_drv      = drv;
			if (!same_range_inflight) {
				cd_cdda_lba_next = (int32_t)s_lba;
				cd_cdda_lba_end  = (int32_t)e_lba;
				cd_cdda_q_pos    = (int32_t)s_lba;
			}
			cd_cdda_q_end = (int32_t)e_lba;
			cd_audio_play_until_ms = 0;
			if (same_range_inflight) {
				akiko_dbg("CDDA re-PLAY same range s=%u e=%u next=%d kept\n",
				          s_lba, e_lba, cd_cdda_lba_next);
				akiko_diag("[akiko] CDDA re-PLAY same range s_lba=%u e_lba=%u next=%d (position kept)",
				           s_lba, e_lba, cd_cdda_lba_next);
			} else {
				akiko_dbg("CDDA arm s=%u e=%u\n", s_lba, e_lba);
				akiko_diag("[akiko] CDDA arm: s_lba=%u e_lba=%u (drv=%p)",
				           s_lba, e_lba, (void*)drv);
			}
		} else {
			cd_cdda_drv      = NULL;
			cd_cdda_lba_next = -1;
			cd_cdda_lba_end  = -1;
			cd_audio_timeout = -3;
			cd_audio_play_until_ms = 0;
			cd_playing       = 0;
			akiko_diag("[akiko] CDDA arm INVALID: drv=%p s_lba=%u e_lba=%u",
			           (void*)drv, s_lba, e_lba);
		}
	}

	akiko_send_response(r, 2);
	akiko_dbg("PLAY %s start=%u(%s) end=%u\n",
		data_read ? "DATA" : "AUDIO", s_lba, seek_negative ? "neg" : "pos", e_lba);
}

static void cmd_led(const uint8_t *cmd)
{
	uint8_t r[2];
	r[0] = cmd[0];

	if (cmd[1] & 0x80) {
		cd_led_state = cmd[1] & 0x01;
		r[1] = cd_led_state;
		akiko_send_response(r, 2);
	} else {
	}
	akiko_dbg("LED cmd1=%02x state=%d\n", cmd[1], cd_led_state);
}

static void cmd_subq(const uint8_t *cmd)
{
	uint8_t r[15];
	memset(r, 0, sizeof(r));
	r[0] = cmd[0];

	drive_t *drv = cd_find_drive();
	uint32_t cur_lba = 0;
	bool valid = false;

	if (drv) {
		if (cd_data_lba_base >= 0) {
			cur_lba = (uint32_t)cd_data_lba_base + akiko_read_sec_counter();
			valid = true;
		} else if (cd_playing && cd_cdda_q_pos >= 0) {
			cur_lba = (uint32_t)cd_cdda_q_pos;
			if (cd_cdda_q_end > 0 && (cd_cdda_q_end - (int32_t)cur_lba) < 10)
				cur_lba = (uint32_t)cd_cdda_q_end;
			valid = true;
		} else if (cd_playing && cd_play_start_lba > 0) {
			cur_lba = cd_play_start_lba;
			valid = true;
		}
	}

	if (!valid) {
		r[2 + 2] = 0x80;
		akiko_send_response(r, 15);
		return;
	}

	int trk_idx = 0;
	int real_tracks = drv->track_cnt > 0 ? drv->track_cnt - 1 : 0;
	for (int i = 0; i < real_tracks; i++) {
		uint32_t end = (i + 1 < drv->track_cnt) ? drv->track[i + 1].start
		                                        : drv->track[i].start + drv->track[i].length;
		if (cur_lba >= drv->track[i].start && cur_lba < end) {
			trk_idx = i;
			break;
		}
	}

	const track_t &t = drv->track[trk_idx];
	uint8_t ctl_adr = (uint8_t)((t.attr & 0xf0) | 0x01);

	uint32_t trk_lsn  = (cur_lba >= t.start) ? (cur_lba - t.start) + 150u : 150u;
	uint32_t disk_lsn = cur_lba + 150u;

	uint32_t tm = (trk_lsn / 75u) / 60u;
	uint32_t ts = (trk_lsn / 75u) % 60u;
	uint32_t tf =  trk_lsn % 75u;
	uint32_t dm = (disk_lsn / 75u) / 60u;
	uint32_t ds = (disk_lsn / 75u) % 60u;
	uint32_t df =  disk_lsn % 75u;

	r[2 + 0] = 0;
	r[2 + 1] = ctl_adr;
	r[2 + 2] = bin_to_bcd((uint8_t)t.number);
	r[2 + 3] = bin_to_bcd(1);
	r[2 + 4] = bin_to_bcd((uint8_t)tm);
	r[2 + 5] = bin_to_bcd((uint8_t)ts);
	r[2 + 6] = bin_to_bcd((uint8_t)tf);
	r[2 + 7] = 0;
	r[2 + 8] = bin_to_bcd((uint8_t)dm);
	r[2 + 9] = bin_to_bcd((uint8_t)ds);
	r[2 + 10] = bin_to_bcd((uint8_t)df);

	akiko_send_response(r, 15);
	akiko_dbg("SUBQ trk=%u lba=%u tpos=%u:%u:%u dpos=%u:%u:%u\n",
	          t.number, cur_lba, tm, ts, tf, dm, ds, df);
}

static void cmd_bad(const uint8_t *cmd, uint8_t err_code)
{
	uint8_t r[2];
	r[0] = (uint8_t)((cmd[0] & 0xf0) | 5);
	r[1] = err_code | cd_door;
	akiko_send_response(r, 2);
	akiko_dbg("BAD cmd=%02x err=%02x\n", cmd[0], err_code);
}

static uint8_t akiko_read_sec_counter(void)
{
	EnableIO();
	spi8(UIO_DMA_READ);
	spi32_w(AKIKO_SECTOR_ADDR);
	uint16_t w = spi_w(0);
	DisableIO();
	return (uint8_t)(w & 0xff);
}

static void akiko_nvram_dump(uint8_t *out_1024)
{
	EnableIO();
	spi8(UIO_DMA_READ);
	spi32_w(AKIKO_NVRAM_ADDR);
	for (int i = 0; i < AKIKO_NVRAM_BYTES; i++) {
		out_1024[i] = (uint8_t)spi_w(0);
	}
	DisableIO();
}

static uint32_t  nvr_verify_due_at_ms = 0;
static char      nvr_verify_path[1024];

static bool akiko_nvram_load_from_path(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0) {
		akiko_diag("[akiko] NVR load skip: no file at %s (errno=%d)",
		           path, errno);
		return false;
	}
	if (st.st_size != AKIKO_NVRAM_BYTES) {
		akiko_diag("[akiko] NVR load skip: %s wrong size %lld (want %d)",
		           path, (long long)st.st_size, AKIKO_NVRAM_BYTES);
		return false;
	}

	int rc = user_io_file_mount(path, 0, 0, 0);
	if (rc != 1) {
		akiko_diag("[akiko] NVR mount FAIL: user_io_file_mount(%s, slot=0) rc=%d",
		           path, rc);
		cd_save_load_failed = true;
		return false;
	}

	akiko_diag("[akiko] NVR mounted slot 0 (%d bytes from %s) — verify in 100 ms",
	           AKIKO_NVRAM_BYTES, path);
	cd_save_load_failed = false;
	strncpy(nvr_verify_path, path, sizeof(nvr_verify_path) - 1);
	nvr_verify_path[sizeof(nvr_verify_path) - 1] = '\0';
	nvr_verify_due_at_ms = (uint32_t)GetTimer(100);
	return true;
}

static void akiko_nvram_verify_load_pending(void)
{
	if (!nvr_verify_due_at_ms) return;
	if (!CheckTimer(nvr_verify_due_at_ms)) return;
	nvr_verify_due_at_ms = 0;

	uint8_t verify[AKIKO_NVRAM_BYTES];
	akiko_nvram_dump(verify);
	uint8_t expected[AKIKO_NVRAM_BYTES];
	FILE *f = fopen(nvr_verify_path, "rb");
	if (!f) {
		akiko_diag("[akiko] NVR verify: fopen(%s) failed errno=%d",
		           nvr_verify_path, errno);
		cd_save_load_failed = true;
		return;
	}
	size_t got = fread(expected, 1, AKIKO_NVRAM_BYTES, f);
	fclose(f);
	if (got != AKIKO_NVRAM_BYTES) {
		akiko_diag("[akiko] NVR verify: short read %zu/%d", got, AKIKO_NVRAM_BYTES);
		cd_save_load_failed = true;
		return;
	}
	if (memcmp(expected, verify, AKIKO_NVRAM_BYTES) == 0) {
		akiko_diag("[akiko] NVR loaded %d bytes from %s (verify OK)",
		           AKIKO_NVRAM_BYTES, nvr_verify_path);
		cd_save_load_failed = false;
		return;
	}
	int first_bad = -1, mismatches = 0;
	for (int i = 0; i < AKIKO_NVRAM_BYTES; i++) {
		if (verify[i] != expected[i]) {
			if (first_bad < 0) first_bad = i;
			mismatches++;
		}
	}
	akiko_diag("[akiko] NVR LOAD VERIFY FAILED: %d/%d mismatched (first @ 0x%03x)",
	           mismatches, AKIKO_NVRAM_BYTES, first_bad);
	int logged = 0;
	for (int i = 0; i < AKIKO_NVRAM_BYTES && logged < 32; i++) {
		if (verify[i] != expected[i]) {
			akiko_diag("[akiko]   @0x%03x: got 0x%02x want 0x%02x",
			           i, verify[i], expected[i]);
			logged++;
		}
	}
	cd_save_load_failed = true;
}

static bool akiko_nvram_save_to_path(const char *path)
{
	uint8_t buf[AKIKO_NVRAM_BYTES];
	akiko_nvram_dump(buf);

	{
		FILE *cur = fopen(path, "rb");
		if (cur) {
			uint8_t disk_buf[AKIKO_NVRAM_BYTES];
			size_t n = fread(disk_buf, 1, AKIKO_NVRAM_BYTES, cur);
			fclose(cur);
			if (n == AKIKO_NVRAM_BYTES) {
				if (memcmp(disk_buf, buf, AKIKO_NVRAM_BYTES) == 0) {
					akiko_diag("[akiko] NVR save skipped: identical to %s", path);
					return true;
				}
				int buf_entry_nz  = 0;
				int disk_entry_nz = 0;
				for (int i = 25; i < AKIKO_NVRAM_BYTES; i++) {
					if (buf[i])      buf_entry_nz++;
					if (disk_buf[i]) disk_entry_nz++;
				}
				if (buf_entry_nz == 0 && disk_entry_nz > 0) {
					akiko_diag("[akiko] NVR save BLOCKED: about to overwrite "
					           "%s (%d entry bytes) with empty FlashFile bootstrap "
					           "(0 entry bytes) — destructive cascade detected, "
					           "refusing.",
					           path, disk_entry_nz);
					return false;
				}
			}
		}
	}

	if (mkdir(AKIKO_NVRAM_DIR, 0755) != 0 && errno != EEXIST) {
		akiko_diag("[akiko] NVR mkdir(%s) failed: errno=%d", AKIKO_NVRAM_DIR, errno);
	}

	char tmp_path[300];
	snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

	FILE *f = fopen(tmp_path, "wb");
	if (!f) {
		akiko_diag("[akiko] NVR fopen(%s) failed: errno=%d", tmp_path, errno);
		return false;
	}
	size_t wrote = fwrite(buf, 1, AKIKO_NVRAM_BYTES, f);
	int    flush = fflush(f);
	int    fsy   = fsync(fileno(f));
	int    cls   = fclose(f);
	if (wrote != AKIKO_NVRAM_BYTES || flush != 0 || fsy != 0 || cls != 0) {
		akiko_diag("[akiko] NVR write incomplete: wrote=%zu flush=%d fsync=%d close=%d",
		           wrote, flush, fsy, cls);
		unlink(tmp_path);
		return false;
	}

	if (rename(tmp_path, path) != 0) {
		akiko_diag("[akiko] NVR rename(%s -> %s) failed: errno=%d",
		           tmp_path, path, errno);
		unlink(tmp_path);
		return false;
	}

	akiko_diag("[akiko] NVR saved %d bytes to %s", AKIKO_NVRAM_BYTES, path);
	return true;
}

static bool akiko_nvram_save_to_disk(void)
{
	if (cd_save_load_failed && cd_save_path_active[0]) {
		static bool warned_once = false;
		if (!warned_once) {
			akiko_diag("[akiko] NVR save BLOCKED: load failed for %s — refusing to "
			           "overwrite real save with BIOS bootstrap. Future blocks silent.",
			           cd_save_path_active);
			warned_once = true;
		}
		return false;
	}
	const char *target = (cd_save_path_active[0])
		? cd_save_path_active
		: AKIKO_NVRAM_FILE_LEGACY;
	return akiko_nvram_save_to_path(target);
}

#define AKIKO_SEC_SLOT 1
static const bool g_akiko_fast_push = (getenv("AKIKO_SLOW_PUSH") == nullptr);

static void akiko_push_sector(const uint8_t *buf)
{
	if (g_akiko_fast_push) {
		EnableIO();
		spi_w(UIO_SECTOR_RD | (AKIKO_SEC_SLOT << 8));
		fpga_spi_fast_block_write_8(buf, AKIKO_SECTOR_BYTES);
		DisableIO();
	} else {
		EnableIO();
		spi8(UIO_DMA_WRITE);
		spi32_w(AKIKO_SECTOR_ADDR);
		for (int i = 0; i < AKIKO_SECTOR_BYTES; i++) {
			spi_w(buf[i]);
		}
		DisableIO();
	}
}

static bool cd_read_audio_sector(drive_t *drv, uint32_t lba, uint8_t *buf2352)
{
	if (!drv || !drv->chd_f || !buf2352) return false;

	bool index0 = false;
	track_t *track = NULL;
	int real_tracks = drv->track_cnt > 0 ? drv->track_cnt - 1 : 0;
	for (int i = 0; i < real_tracks; i++) {
		uint32_t end = drv->track[i].start + drv->track[i].length;
		if (lba >= drv->track[i].start && lba < end) {
			track = &drv->track[i];
			break;
		}
		if (i + 1 < real_tracks && lba < drv->track[i + 1].start &&
		    lba >= end) {
			track = &drv->track[i];
			index0 = true;
			break;
		}
	}
	(void)index0;

	if (!track) return false;
	if (track->attr & 0x40) return false;
	if (track->sectorSize != AKIKO_CDDA_BYTES) return false;

	uint32_t chd_lba = lba + track->chd_offset;
	if (mister_chd_read_sector(drv->chd_f, chd_lba, 0, 0,
	                           AKIKO_CDDA_BYTES, buf2352,
	                           drv->chd_hunkbuf, &drv->chd_hunknum)
	    != CHDERR_NONE) {
		return false;
	}
	return true;
}

static bool akiko_audio_fifo_ready(void)
{
	return (akiko_read_status() & AKIKO_STATUS_CDDA_REQ) != 0;
}

static void akiko_push_audio_sector(const uint8_t *buf2352)
{
	const uint16_t *src = (const uint16_t *)buf2352;
	const int nframes = AKIKO_CDDA_BYTES / 4;

	EnableIO();
	spi8(UIO_DMA_WRITE);
	spi32_w(AKIKO_CDDA_ADDR);
	for (int i = 0; i < nframes; i++) {
		uint16_t l = bswap_16(src[2 * i + 0]);
		uint16_t r = bswap_16(src[2 * i + 1]);
		spi_w(l);
		spi_w(r);
	}
	DisableIO();
}

static void build_subcode_deint(drive_t *drv, uint32_t lba, uint8_t out[AKIKO_SUB_BYTES])
{
	memset(out, 0, AKIKO_SUB_BYTES);
	if (!drv) return;

	int trk_idx = 0;
	int real_tracks = drv->track_cnt > 0 ? drv->track_cnt - 1 : 0;
	for (int i = 0; i < real_tracks; i++) {
		uint32_t end = (i + 1 < drv->track_cnt) ? drv->track[i + 1].start
		                                        : drv->track[i].start + drv->track[i].length;
		if (lba >= drv->track[i].start && lba < end) { trk_idx = i; break; }
	}
	const track_t &t = drv->track[trk_idx];

	uint8_t *q = out + AKIKO_SUB_ENTRY;
	q[0] = (uint8_t)((t.attr & 0xf0) | 0x01);
	q[1] = bin_to_bcd((uint8_t)t.number);
	q[2] = bin_to_bcd(1);

	uint32_t rel = (lba >= t.start) ? (lba - t.start) : 0u;
	q[3] = bin_to_bcd((uint8_t)((rel / 75u) / 60u));
	q[4] = bin_to_bcd((uint8_t)((rel / 75u) % 60u));
	q[5] = bin_to_bcd((uint8_t)( rel % 75u));
	q[6] = 0;

	uint32_t ab = lba + 150u;
	q[7] = bin_to_bcd((uint8_t)((ab / 75u) / 60u));
	q[8] = bin_to_bcd((uint8_t)((ab / 75u) % 60u));
	q[9] = bin_to_bcd((uint8_t)( ab % 75u));
}

static void sub_to_interleaved(const uint8_t *s, uint8_t *d)
{
	for (int i = 0; i < AKIKO_SUB_BYTES; i++) {
		int dmask = 0x80;
		int smask = 1 << (7 - (i & 7));
		d[i] = 0;
		for (int j = 0; j < 8; j++) {
			if (s[(i / 8) + j * AKIKO_SUB_ENTRY] & smask) d[i] |= dmask;
			dmask >>= 1;
		}
	}
}

static void akiko_push_subcode(const uint8_t *d96)
{
	EnableIO();
	spi8(UIO_DMA_WRITE);
	spi32_w(AKIKO_SUBCODE_ADDR);
	for (int i = 0; i < AKIKO_SUB_BYTES; i++) spi_w(d96[i]);
	DisableIO();
}

static bool akiko_cdda_pump(void)
{
	if (cd_cdda_lba_next < 0) return false;
	if (cd_paused) return false;
	if (!cd_cdda_drv) {
		cd_cdda_lba_next = -1;
		cd_cdda_lba_end  = -1;
		cd_audio_timeout = -3;
		akiko_diag("[akiko] CDDA pump abort: drive gone");
		return false;
	}
	if (!akiko_audio_fifo_ready()) return false;

	uint8_t buf[AKIKO_CDDA_BYTES];
	uint32_t lba = (uint32_t)cd_cdda_lba_next;
	if (!cd_read_audio_sector(cd_cdda_drv, lba, buf)) {
		akiko_diag("[akiko] CDDA pump read FAIL at lba=%u, aborting", lba);
		memset(buf, 0, sizeof(buf));
		akiko_push_audio_sector(buf);
		cd_cdda_lba_next = -1;
		cd_cdda_lba_end  = -1;
		cd_cdda_drv      = NULL;
		cd_audio_timeout = -3;
		return true;
	}

	akiko_push_audio_sector(buf);
	cd_cdda_lba_next++;
	cd_cdda_q_pos = (int32_t)lba;

	{
		uint8_t sub_deint[AKIKO_SUB_BYTES];
		uint8_t sub_intlv[AKIKO_SUB_BYTES];
		build_subcode_deint(cd_cdda_drv, lba, sub_deint);
		sub_to_interleaved(sub_deint, sub_intlv);
		akiko_push_subcode(sub_intlv);
	}

	if ((lba & 0xff) == 0) {
		int32_t remaining = cd_cdda_lba_end - cd_cdda_lba_next;
		(void)remaining;
		akiko_dbg("CDDA pump pushed lba=%u (rem=%d)\n", lba, remaining);
	}

	if (cd_cdda_lba_next >= cd_cdda_lba_end) {
		akiko_dbg("CDDA done at lba=%u\n", lba);
		akiko_diag("[akiko] CDDA pump natural end at lba=%u", lba);
		cd_cdda_q_pos    = cd_cdda_q_end;
		cd_cdda_lba_next = -1;
		cd_cdda_lba_end  = -1;
		cd_cdda_drv      = NULL;
		cd_audio_play_until_ms = 0;
		cd_audio_timeout = -1;
	}
	return true;
}

static int akiko_prefetch_fill(drive_t *drv, int win_idx, uint32_t base_lba)
{
	prefetch_window_t *w = &cd_prefetch_windows[win_idx];
	w->base_lba = (int32_t)base_lba;
	int filled = 0;
	for (int i = 0; i < AKIKO_PREFETCH_SECTORS; i++) {
		uint32_t lba = base_lba + i;
		uint8_t *slot = &w->buf[i * AKIKO_SECTOR_BYTES];
		if (cdrom_read_raw_sector(drv, lba, slot) == 0) {
			w->valid[i] = 1;
			filled++;
		} else {
			w->valid[i] = 0;
			cd_prefetch_failed_sectors++;
		}
	}
	return filled;
}

static int akiko_prefetch_get(drive_t *drv, uint32_t lba, uint8_t *out_buf)
{
	int win = akiko_prefetch_find(lba);
	if (win < 0) {
		cd_prefetch_misses++;
		win = akiko_prefetch_pick_evict();
		uint32_t base = (lba / AKIKO_PREFETCH_SECTORS) * AKIKO_PREFETCH_SECTORS;
		if (akiko_prefetch_fill(drv, win, base) == 0) {
			akiko_diag("[akiko] prefetch fill TOTAL FAIL win=%d base=%u (req lba=%u)",
			           win, base, lba);
			return -1;
		}
		akiko_diag("[akiko] prefetch refill win=%d base=%u (req lba=%u hits=%u misses=%u failed=%u)",
		           win, base, lba, cd_prefetch_hits, cd_prefetch_misses,
		           cd_prefetch_failed_sectors);
	} else {
		cd_prefetch_hits++;
	}
	prefetch_window_t *w = &cd_prefetch_windows[win];
	w->lru_tick = ++cd_prefetch_lru_counter;
	int idx = (int)(lba - (uint32_t)w->base_lba);
	if (w->valid[idx] == 0) {
		uint8_t *slot = &w->buf[idx * AKIKO_SECTOR_BYTES];
		if (cdrom_read_raw_sector(drv, lba, slot) == 0) {
			w->valid[idx] = 1;
		} else {
			return -2;
		}
	}
	memcpy(out_buf, &w->buf[idx * AKIKO_SECTOR_BYTES], AKIKO_SECTOR_BYTES);
	return 0;
}

static void akiko_handle_sec_req(void)
{
	struct timespec ts0, ts1, ts2, ts3;
	static int64_t phase_a_us = 0, phase_b_us = 0, phase_c_us = 0;
	static uint32_t bucket_count = 0;
	const uint32_t BUCKET_SIZE = 256;

	static uint32_t last_sec_req_ms = 0;
	uint32_t now_ms = (uint32_t)GetTimer(0);
	if (last_sec_req_ms != 0 && (now_ms - last_sec_req_ms) >= 50) {
		akiko_diag("[akiko] sec_req GAP %ums (lba_base=%d)",
		           now_ms - last_sec_req_ms, cd_data_lba_base);
	}
	last_sec_req_ms = now_ms;

	clock_gettime(CLOCK_MONOTONIC, &ts0);
	uint8_t counter = akiko_read_sec_counter();
	clock_gettime(CLOCK_MONOTONIC, &ts1);

	uint8_t buf[AKIKO_SECTOR_BYTES];

	if (cd_data_lba_base == -1) {
		akiko_dbg("sec_req but no data read armed (counter=%u)\n", counter);
		memset(buf, 0, sizeof(buf));
		akiko_push_sector(buf);
		return;
	}

	drive_t *drv = cd_find_drive();
	if (!drv) {
		akiko_dbg("sec_req but no CD drive (counter=%u)\n", counter);
		memset(buf, 0, sizeof(buf));
		akiko_push_sector(buf);
		return;
	}

	int32_t signed_lba = cd_data_lba_base + (int32_t)counter;
	if (signed_lba < 0) {
		memset(buf, 0, sizeof(buf));
		akiko_push_sector(buf);
		return;
	}
	uint32_t lba = (uint32_t)signed_lba;

	int real_tracks = drv->track_cnt > 0 ? drv->track_cnt - 1 : 0;
	uint32_t lead_out = (real_tracks > 0) ? drv->track[real_tracks].start : 0;
	if (!lead_out || lba >= lead_out) {
		static uint32_t last_oob_lba = 0;
		if (lba != last_oob_lba) {
			akiko_diag("[akiko] sec_req lba=%u %s lead_out=%u — push silence, advance",
			           lba, lead_out ? ">=" : "(no track table)", lead_out);
			last_oob_lba = lba;
		}
		memset(buf, 0, sizeof(buf));
		akiko_push_sector(buf);
		return;
	}

	{
		track_t *track = NULL;
		for (int i = 0; i < real_tracks; i++) {
			uint32_t end = drv->track[i].start + drv->track[i].length;
			if (lba >= drv->track[i].start && lba < end) {
				track = &drv->track[i];
				break;
			}
		}
		if (track && !(track->attr & 0x40)) {
			static uint32_t held_audio_lba = UINT32_MAX;
			static uint32_t held_count = 0;
			const uint32_t HOLD_MAX = 50;
			if (lba != held_audio_lba) {
				held_audio_lba = lba;
				held_count = 0;
				akiko_diag("[akiko] sec_req lba=%u in audio track (attr=0x%02x) — hold",
				           lba, track->attr);
			}
			held_count++;
			if (held_count >= HOLD_MAX) {
				akiko_diag("[akiko] audio-hold cap reached at lba=%u — emit playend_notify, disarm",
				           lba);
				cd_data_lba_base = -1;
				emit_playend_notify(1);
				held_audio_lba = UINT32_MAX;
				held_count = 0;
			}
			return;
		}
	}

	int rc = akiko_prefetch_get(drv, lba, buf);
	if (rc != 0) {
		static const unsigned MAX_FAIL_RETRIES = 8;
		static uint32_t fail_lba = UINT32_MAX;
		static unsigned fail_count = 0;
		if (lba != fail_lba) { fail_lba = lba; fail_count = 0; }
		fail_count++;
		akiko_diag("[akiko] sec_req lba=%u %s (retry %u/%u)", lba,
		           (rc == -1) ? "prefetch FAIL" : "cached invalid",
		           fail_count, MAX_FAIL_RETRIES);
		if (fail_count < MAX_FAIL_RETRIES) return;
		akiko_diag("[akiko] sec_req lba=%u retry cap reached — push silence, advance", lba);
		fail_lba = UINT32_MAX;
		memset(buf, 0, sizeof(buf));
		akiko_push_sector(buf);
		return;
	}
	clock_gettime(CLOCK_MONOTONIC, &ts2);

	akiko_push_sector(buf);
	clock_gettime(CLOCK_MONOTONIC, &ts3);

	#define TS_DELTA_US(a, b) \
		(((int64_t)(b).tv_sec  - (int64_t)(a).tv_sec ) * 1000000LL + \
		 ((int64_t)(b).tv_nsec - (int64_t)(a).tv_nsec) / 1000LL)
	phase_a_us += TS_DELTA_US(ts0, ts1);
	phase_b_us += TS_DELTA_US(ts1, ts2);
	phase_c_us += TS_DELTA_US(ts2, ts3);
	#undef TS_DELTA_US
	bucket_count++;

	if (bucket_count >= BUCKET_SIZE) {
		akiko_diag("[akiko] sec_req timing N=%u: ctr_read=%lld chd_read=%lld push=%lld (us total) "
		           "= %lld/%lld/%lld us avg, last lba=%u",
		           bucket_count,
		           (long long)phase_a_us,
		           (long long)phase_b_us,
		           (long long)phase_c_us,
		           (long long)(phase_a_us / bucket_count),
		           (long long)(phase_b_us / bucket_count),
		           (long long)(phase_c_us / bucket_count),
		           lba);
		phase_a_us = phase_b_us = phase_c_us = 0;
		bucket_count = 0;
	}
}

void akiko_cd32_set_cd_path(const char *path)
{
	char new_path[256] = {0};
	if (path && *path) {
		compute_save_path(path, new_path, sizeof(new_path));
	}

	if (strcmp(new_path, cd_save_path_active) == 0) {
		return;
	}

	if (path && *path) {
		int fd = open(path, O_RDONLY | O_CLOEXEC);
		if (fd >= 0) {
			posix_fadvise(fd, 0, 16 * 1024 * 1024, POSIX_FADV_WILLNEED);
			close(fd);
			akiko_diag("[akiko] set_cd_path: fadvise WILLNEED 16MB on %s", path);
		}
	}

	if (cd_save_path_active[0] && cd_save_dirty_observed) {
		akiko_diag("[akiko] set_cd_path: flushing old save %s before swap",
		           cd_save_path_active);
		akiko_nvram_save_to_path(cd_save_path_active);
	}

	strncpy(cd_save_path_active, new_path, sizeof(cd_save_path_active) - 1);
	cd_save_path_active[sizeof(cd_save_path_active) - 1] = 0;
	cd_save_dirty_observed = false;
	cd_save_load_failed = false;

	if (!cd_save_path_active[0]) {
		cd_save_load_pending = false;
		akiko_diag("[akiko] set_cd_path: CD unmounted, no save file active");
		return;
	}

	cd_save_load_pending = true;
	akiko_diag("[akiko] set_cd_path: cd=%s -> save=%s (deferred load armed)",
	           path, cd_save_path_active);
}

void akiko_cd32_init(void)
{
	cd_initialized   = 0;
	cd_paused        = 0;
	cd_playing       = 0;
	cd_led_state     = 0;
	cd_door          = 1;
	cd_play_start_lba = 0;
	cd_play_end_lba   = 0;
	cd_data_lba_base = -1;
	cd_audio_timeout = 0;
	cd_audio_play_until_ms = 0;
	cd_cdda_lba_next = -1;
	cd_cdda_lba_end  = -1;
	cd_cdda_drv      = NULL;
	cd_last_mounted  = false;
	toc_point_count  = 0;
	toc_push_idx     = -1;
	toc_push_last_ms = 0;

	akiko_prefetch_invalidate();
	cd_post_info_media_push_pending = 0;

	cd_save_dirty_observed = false;
	cd_save_load_failed = false;
	if (cd_save_path_active[0]) {
		cd_save_load_pending = true;
		akiko_diag("[akiko] init: scheduling reload of %s after BRAM wipe",
		           cd_save_path_active);
	}

	akiko_dbg("init\n");
}

static void akiko_diag(const char *fmt, ...)
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

void akiko_cd32_poll(void)
{
	if (!cd32_active()) return;

	usleep(20);

	bool mounted = cd_is_mounted();

	if (g_akiko_pending_minimig_reset) {
		g_akiko_pending_minimig_reset = false;
		akiko_diag("[akiko] mediachange: BIOS was stuck on no-CD splash, "
		           "triggering minimig_reset()");
		minimig_reset();
		return;
	}

	static bool first_poll = true;
	if (first_poll) {
		first_poll = false;
		akiko_diag("[akiko] poll alive (first call) mounted=%d", mounted);
	}

	if (cd_save_load_pending && mounted) {
		cd_save_load_pending = false;
		if (cd_save_path_active[0]) {
			struct stat pst;
			if (stat(cd_save_path_active, &pst) != 0 &&
			    stat(AKIKO_NVRAM_FILE_LEGACY, &pst) == 0 &&
			    pst.st_size == AKIKO_NVRAM_BYTES) {
				akiko_diag("[akiko] migrating legacy save %s -> %s",
				           AKIKO_NVRAM_FILE_LEGACY, cd_save_path_active);
				akiko_nvram_load_from_path(AKIKO_NVRAM_FILE_LEGACY);
			} else {
				akiko_nvram_load_from_path(cd_save_path_active);
			}
		}
	}

	akiko_nvram_verify_load_pending();

#if AKIKO_CD32_DEBUG
	static int unmounted_dump_throttle = 0;
	if (!mounted && (unmounted_dump_throttle++ % 60) == 0 && unmounted_dump_throttle < 5*60) {
		akiko_diag(
			"[akiko] ide_inst dump: "
			"[0,0]p=%d/c=%d/chd=%p/f=%p  [0,1]p=%d/c=%d/chd=%p/f=%p",
			ide_inst[0].drive[0].present, ide_inst[0].drive[0].cd,
			(void*)ide_inst[0].drive[0].chd_f, (void*)ide_inst[0].drive[0].f,
			ide_inst[0].drive[1].present, ide_inst[0].drive[1].cd,
			(void*)ide_inst[0].drive[1].chd_f, (void*)ide_inst[0].drive[1].f
		);
	}
#endif

	static bool media_absent_push_pending = false;
	static bool mediachanged_push_pending = false;
	static bool had_prior_unmount = false;
	if (mounted != cd_last_mounted) {
		cd_data_lba_base = -1;
		akiko_prefetch_invalidate();
		cd_cdda_lba_next = -1;
		cd_cdda_lba_end  = -1;
		cd_cdda_drv      = NULL;
		toc_point_count  = 0;
		toc_push_idx     = -1;
		if (!mounted) {
			media_absent_push_pending = true;
			mediachanged_push_pending = false;
			had_prior_unmount = true;
		} else {
			media_absent_push_pending = false;
			akiko_build_toc();
			if (cd_initialized >= 2) {
				if (had_prior_unmount) {
					mediachanged_push_pending = true;
				} else {
					g_akiko_pending_minimig_reset = true;
					mediachanged_push_pending = false;
				}
			}
		}
		cd_last_mounted = mounted;
		akiko_diag("[akiko] media change -> mounted=%d cd_init=%u (absent=%d, mediachanged=%d)",
		           mounted, cd_initialized,
		           media_absent_push_pending, mediachanged_push_pending);
	}

	uint16_t status = akiko_read_status();
#if AKIKO_CD32_DEBUG
	static uint16_t last_status = 0xffff;
	static int status_log_count = 0;
	if (status != last_status && status_log_count < 200) {
		akiko_diag("[akiko] status=0x%04x (was 0x%04x)", status, last_status);
		last_status = status;
		status_log_count++;
	}
#endif
	const bool rx_idle = !(status & AKIKO_STATUS_RX_BUSY);

	if (mounted && cd_initialized == 0 && rx_idle) {
		uint8_t r[2];
		r[0] = 0x0a;
		r[1] = 0x01;
		akiko_send_response(r, 2);
		cd_initialized = 1;
		akiko_diag("[akiko] auto-init media-status push: opcode=0x0a status=0x01 (cd_initialized=1)");
	}

	if (mediachanged_push_pending && mounted && cd_initialized >= 2 && rx_idle) {
		uint8_t r[2];
		r[0] = 0x0a;
		r[1] = 0x01;
		akiko_send_response(r, 2);
		mediachanged_push_pending = false;
		akiko_build_toc();
		akiko_diag("[akiko] mediachange push: opcode=0x0a status=0x01 (TOC rebuilt 2x)");
	}

	if (media_absent_push_pending && !mounted && rx_idle) {
		uint8_t r[2];
		r[0] = 0x0a;
		r[1] = 0x00;
		akiko_send_response(r, 2);
		media_absent_push_pending = false;
		akiko_diag("[akiko] eject media-status push: opcode=0x0a status=0x00");
	}

	(void)cd_post_info_media_push_pending;

	if (cd_initialized == 2 && toc_push_idx >= 0 && rx_idle) {
		uint32_t now_ms = (uint32_t)GetTimer(0);
		if ((now_ms - toc_push_last_ms) >= AKIKO_TOC_PUSH_PERIOD_MS) {
			toc_push_last_ms = now_ms;
			akiko_push_toc_entry();
		}
	}

	{
		static uint32_t nvr_dirty_seen_at    = 0;
		static uint32_t nvr_last_save_at     = 0;
		static int      nvr_menu_was_present = 0;
		const bool nvr_dirty_now  = (status & AKIKO_STATUS_NVR_DIRTY) != 0;
		const int  nvr_menu_now   = menu_present();
		const bool osd_open_edge  = nvr_menu_now && !nvr_menu_was_present;
		nvr_menu_was_present      = nvr_menu_now;

		if (nvr_dirty_now) {
			cd_save_dirty_observed = true;
			uint32_t now_ms = (uint32_t)GetTimer(0);
			if (!nvr_dirty_seen_at) {
				nvr_dirty_seen_at = now_ms;
				akiko_diag("[akiko] NVR dirty observed at t=%u", now_ms);
			}
			const bool throttle_expired = rx_idle &&
				(now_ms - nvr_dirty_seen_at) >= AKIKO_NVRAM_DIRTY_DEBOUNCE_MS;
			const bool fire = osd_open_edge || throttle_expired;
			const bool cooldown_clear = !nvr_last_save_at ||
				(now_ms - nvr_last_save_at) >= AKIKO_NVRAM_MIN_SAVE_INTERVAL_MS;
			if (fire && cooldown_clear) {
				if (osd_open_edge) {
					akiko_diag("[akiko] NVR flush: OSD opened with dirty pending");
				}
				akiko_nvram_save_to_disk();
				nvr_dirty_seen_at = 0;
				nvr_last_save_at  = now_ms;
			}
		} else {
			nvr_dirty_seen_at = 0;
		}
	}

	if (cd_audio_timeout != 0 && rx_idle) {
		if (cd_audio_timeout > 1) {
			cd_audio_timeout--;
		} else if (cd_audio_timeout == 1) {
			emit_playend_notify(0);
			cd_audio_timeout = 0;
		} else if (cd_audio_timeout == -1) {
			cd_audio_timeout = -2;
		} else if (cd_audio_timeout == -2) {
			emit_playend_notify(1);
			cd_playing = 0;
			cd_audio_timeout = 0;
			cd_audio_play_until_ms = 0;
		} else if (cd_audio_timeout == -3) {
			emit_playend_notify(-1);
			cd_playing = 0;
			cd_audio_timeout = 0;
			cd_audio_play_until_ms = 0;
		}
		return;
	}

	if (cd_audio_play_until_ms != 0 && cd_playing && rx_idle &&
	    cd_audio_timeout == 0) {
		uint32_t now_ms = (uint32_t)GetTimer(0);
		if ((int32_t)(now_ms - cd_audio_play_until_ms) >= 0) {
			emit_playend_notify(1);
			cd_playing = 0;
			cd_audio_play_until_ms = 0;
			akiko_diag("[akiko] PLAY AUDIO natural end at t=%u", now_ms);
			return;
		}
	}

	if (akiko_cdda_pump()) {
		return;
	}

	if (status & AKIKO_STATUS_SEC_REQ) {
		akiko_handle_sec_req();
		return;
	}

	if (!(status & AKIKO_STATUS_REQ)) return;

	uint8_t cmd[AKIKO_CMD_MAX];
	int     n = akiko_drain_command(cmd);
	if (n < 1) {
		akiko_dbg("drain returned 0 bytes\n");
		return;
	}

#if AKIKO_CD32_DEBUG
	akiko_dbg("RX %d bytes:", n);
	for (int i = 0; i < n; i++) printf(" %02x", cmd[i]);
	printf("\n");
#endif

	int op = cmd[0] & 0x0f;
	int expected_len = command_lengths[op];

	if (expected_len > 0) {
		int chk_total = expected_len + 1;
		if (n < chk_total) {
			akiko_dbg("short frame: n=%d expected=%d\n", n, chk_total);
			cmd_bad(cmd, CH_ERR_CHECKSUM);
			return;
		}
		uint32_t sum = 0;
		for (int i = 0; i < chk_total; i++) sum += cmd[i];
		if ((sum & 0xff) != 0xff) {
			akiko_dbg("checksum FAIL sum=%02x\n", sum & 0xff);
			cmd_bad(cmd, CH_ERR_CHECKSUM);
			return;
		}
	}

	if (op != 0x05) {
		char hex[3 * AKIKO_CMD_MAX + 1];
		int  off = 0;
		for (int i = 0; i < n && i < 16; i++) {
			off += snprintf(hex + off, sizeof(hex) - off, "%02x ", cmd[i]);
		}
		if (off > 0) hex[off - 1] = '\0';
		akiko_diag("[akiko] CMD op=0x%02x n=%d bytes=%s", op, n, hex);
	}

	switch (op) {
		case 0x00: {
			uint8_t r[1];
			r[0] = cmd[0];
			akiko_send_response(r, 1);
			akiko_dbg("CMD 0x00 echo\n");
			break;
		}
		case 0x01: cmd_stop(cmd);    break;
		case 0x02: cmd_pause(cmd);   break;
		case 0x03: cmd_unpause(cmd); break;
		case 0x04: cmd_multi(cmd);   break;
		case 0x05: cmd_led(cmd);     break;
		case 0x06: cmd_subq(cmd);    break;
		case 0x07: cmd_info(cmd);    break;
		case 0x08:
		case 0x09:
		case 0x0a:
			akiko_dbg("CMD 0x%02x consumed (no response per WinUAE parity)\n", op);
			break;
		default:
			cmd_bad(cmd, CH_ERR_BADCOMMAND);
			break;
	}
}
