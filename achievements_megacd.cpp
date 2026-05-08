// achievements_megacd.cpp — RetroAchievements MegaCD/MegaCD-specific implementation

#include "achievements_console.h"
#include "achievements.h"
#include "ra_ramread.h"
#include "user_io.h"
#include <string.h>
#include <time.h>
#include <stdio.h>

#ifdef HAS_RCHEEVOS
#include "rc_client.h"
#include "rc_consoles.h"
#include "rc_hash.h"
#endif

// ---------------------------------------------------------------------------
// MegaCD/MegaCD State
// ---------------------------------------------------------------------------

static console_state_t g_mcd_state = {};

// ---------------------------------------------------------------------------
// MegaCD/MegaCD Implementation
// ---------------------------------------------------------------------------

static void megacd_init(void)
{
	memset(&g_mcd_state, 0, sizeof(g_mcd_state));
}

static void megacd_reset(void)
{
	memset(&g_mcd_state, 0, sizeof(g_mcd_state));
}

static uint32_t megacd_read_memory(void *map, uint32_t address, uint8_t *buffer, uint32_t num_bytes)
{
	if (g_mcd_state.optionc) {
		if (g_mcd_state.collecting) {
			for (uint32_t i = 0; i < num_bytes; i++)
				ra_snes_addrlist_add(address + i);
		}
		if (g_mcd_state.cache_ready) {
			for (uint32_t i = 0; i < num_bytes; i++)
				buffer[i] = ra_snes_addrlist_read_cached(map, address + i);
			return num_bytes;
		}
		memset(buffer, 0, num_bytes);
		return num_bytes;
	}
	return 0;
}

static int megacd_poll(void *map, void *client, int game_loaded)
{
#ifdef HAS_RCHEEVOS
	if (!client || !game_loaded || !map || !g_mcd_state.optionc) return 0;

	rc_client_t *rc_client = (rc_client_t *)client;

	if (ra_snes_addrlist_count() == 0 && !g_mcd_state.cache_ready) {
		// Bootstrap: run one do_frame with zeros to discover needed addresses
		g_mcd_state.collecting = 1;
		ra_snes_addrlist_begin_collect();
		rc_client_do_frame(rc_client);
		g_mcd_state.collecting = 0;
		int changed = ra_snes_addrlist_end_collect(map);
		if (changed) {
			ra_log_write("MCD OptionC: Bootstrap collection done, %d addrs written to DDRAM\n",
				ra_snes_addrlist_count());
		} else {
			ra_log_write("MCD OptionC: No addresses collected\n");
		}
	} else if (!g_mcd_state.cache_ready) {
		// Wait for FPGA to respond with cached values
		if (ra_snes_addrlist_is_ready(map)) {
			g_mcd_state.cache_ready = 1;
			g_mcd_state.last_resp_frame = 0;
			g_mcd_state.game_frames = 0;
			g_mcd_state.poll_logged = 0;
			clock_gettime(CLOCK_MONOTONIC, &g_mcd_state.cache_time);
			ra_log_write("MCD OptionC: Cache active! FPGA response matched request.\n");
			// Dump address list once on activation
			const uint32_t *a0 = ra_snes_addrlist_addrs();
			int ac = ra_snes_addrlist_count();
			int ad = ac < 20 ? ac : 20;
			char ah[256]; int ap = 0;
			for (int i = 0; i < ad && ap < (int)sizeof(ah) - 8; i++)
				ap += snprintf(ah + ap, sizeof(ah) - ap, "%04X ", a0[i]);
			ra_log_write("MCD ADDRLIST[0..%d]: %s (total=%d)\n", ad - 1, ah, ac);
		}
	} else {
		// Normal frame processing from cache
		uint32_t resp_frame = ra_snes_addrlist_response_frame(map);
		if (resp_frame > g_mcd_state.last_resp_frame) {
			g_mcd_state.last_resp_frame = resp_frame;
			g_mcd_state.game_frames++;
			ra_frame_processed(resp_frame);
			clock_gettime(CLOCK_MONOTONIC, &g_mcd_state.stall_time);
			g_mcd_state.stall_frame = resp_frame;

			// Early-frame valcache dump (first 5 frames)
			if (g_mcd_state.game_frames <= 5) {
				const uint8_t *ev = (const uint8_t *)map + 0x48000 + 8;
				int ec = ra_snes_addrlist_count();
				int ed = ec < 45 ? ec : 45;
				int enz = 0;
				char eh[256]; int ep = 0;
				for (int i = 0; i < ed && ep < (int)sizeof(eh) - 4; i++) {
					ep += snprintf(eh + ep, sizeof(eh) - ep, "%02X ", ev[i]);
					if (ev[i]) enz++;
				}
				ra_log_write("MCD early[%u]: VALCACHE %s (%d nz)\n",
					g_mcd_state.game_frames, eh, enz);
			}

                        // Periodic diagnostic (every ~30s = ~1800 frames)
                        if (g_mcd_state.game_frames % 1800 == 0 && g_mcd_state.game_frames > 0) {
                                // Read FPGA debug word at DDRAM_BASE+2 (byte offset 0x10)
                                const uint8_t *dbg = (const uint8_t *)map + 0x10;
                                uint8_t ver = dbg[7], disp = dbg[6];
                                uint16_t ok = (dbg[5] << 8) | dbg[4];
                                uint16_t oob = (dbg[3] << 8) | dbg[2];
                                uint16_t first_sdram = (dbg[1] << 8) | dbg[0];
                                // Read first 16 VALCACHE bytes
                                const uint8_t *vc = (const uint8_t *)map + 0x48000 + 8;
                                int ac = ra_snes_addrlist_count();
                                int cnt = ac < 16 ? ac : 16;
                                int nz = 0;
                                char vbuf[128]; int vp = 0;
                                for (int i = 0; i < cnt && vp < (int)sizeof(vbuf) - 4; i++) {
                                        vp += snprintf(vbuf + vp, sizeof(vbuf) - vp, "%02X ", vc[i]);
                                        if (vc[i]) nz++;
                                }
                                ra_log_write("MCD DIAG: frame=%u ver=0x%02X disp=%u ok=%u oob=%u sdram0=0x%04X\n",
                                        g_mcd_state.last_resp_frame, ver, disp, ok, oob, first_sdram);
                                ra_log_write("MCD DIAG: VALCACHE[0..%d]: %s(%d nz)\n",
                                        cnt - 1, vbuf, nz);
                        }

			// Re-collect every ~5 min to catch address changes
			// Smart cache mode: skip re-collect (no dynamic pointers in MegaCD)
			int re_collect = !achievements_smart_cache_enabled()
				&& (g_mcd_state.game_frames % 18000 == 0) && (g_mcd_state.game_frames > 0);
			if (re_collect) {
				g_mcd_state.collecting = 1;
				ra_snes_addrlist_begin_collect();
			}

			rc_client_do_frame(rc_client);

			if (re_collect) {
				g_mcd_state.collecting = 0;
				if (ra_snes_addrlist_end_collect(map)) {
					ra_log_write("MCD OptionC: Address list refreshed, %d addrs\n",
						ra_snes_addrlist_count());
				}
			}
		} else {
			optionc_check_stall_recovery(&g_mcd_state, resp_frame, "MegaCD");
		}
	}

	// Periodic log
	uint32_t milestone = g_mcd_state.game_frames / 300;
	if (milestone > 0 && milestone != g_mcd_state.poll_logged) {
		g_mcd_state.poll_logged = milestone;
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		double elapsed = (now.tv_sec - g_mcd_state.cache_time.tv_sec)
			+ (now.tv_nsec - g_mcd_state.cache_time.tv_nsec) / 1e9;
		double ms_per_cycle = (g_mcd_state.game_frames > 0) ?
			(elapsed * 1000.0 / g_mcd_state.game_frames) : 0.0;
		ra_log_write("POLL(MD): resp_frame=%u game_frames=%u elapsed=%.1fs ms/cycle=%.1f addrs=%d\n",
			g_mcd_state.last_resp_frame, g_mcd_state.game_frames, elapsed, ms_per_cycle,
			ra_snes_addrlist_count());
	}

	return 1; // MegaCD handled
#else
	return 0;
#endif
}

static int megacd_calculate_hash(const char *rom_path, char *md5_hex_out)
{
#ifdef HAS_RCHEEVOS
	char abs_path[1024];
	if (rom_path[0] == '/') {
		snprintf(abs_path, sizeof(abs_path), "%s", rom_path);
	} else {
		extern const char *getRootDir(void);
		snprintf(abs_path, sizeof(abs_path), "%s/%s", getRootDir(), rom_path);
	}

	if (rc_hash_generate_from_file(md5_hex_out, 9, abs_path)) {
		ra_log_write("MCD hash: %s\n", md5_hex_out);
		return 1;
	}
	ra_log_write("MCD: rc_hash_generate_from_file failed for %s\n", abs_path);
#endif
	return 0;
}

static void megacd_set_hardcore(int enabled)
{
	user_io_status_set("[55]", enabled ? 1 : 0); // hardcore signal
	user_io_status_set("[24]", enabled ? 1 : 0); // disable cheats OSD toggle
	ra_log_write("MegaCD: Hardcore mode %s\n", enabled ? "enabled" : "disabled");
}

static int megacd_detect_protocol(void *map)
{
	if (!ra_ramread_active(map)) {
		ra_log_write("MEGACD: FPGA mirror not detected -- RA support unavailable\n");
		return 0;
	}
	// MegaCD always uses Option C (no VBlank-gated mode)
	g_mcd_state.optionc = 1;
	ra_log_write("MegaCD FPGA protocol: Option C (selective address reading)\n");
	return 1;
}

// ---------------------------------------------------------------------------
// Console handler definition
// ---------------------------------------------------------------------------

const console_handler_t g_console_megacd = {
	.init = megacd_init,
	.reset = megacd_reset,
	.read_memory = megacd_read_memory,
	.poll = megacd_poll,
	.calculate_hash = megacd_calculate_hash,
	.set_hardcore = megacd_set_hardcore,
	.detect_protocol = megacd_detect_protocol,
	.console_id = 9,  // RC_CONSOLE_SEGA_CD
	.name = "MegaCD",
	.hardcore_protected = 0
};
