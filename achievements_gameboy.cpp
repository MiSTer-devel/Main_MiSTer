// achievements_gameboy.cpp — RetroAchievements GameBoy/GameBoy Color-specific implementation

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
#endif

// ---------------------------------------------------------------------------
// GameBoy State
// ---------------------------------------------------------------------------

static console_state_t g_gb_state = {};
static int g_gb_rtquery = 0;

// Value-change watchers for debugging
typedef struct {
	uint32_t addr;
	uint8_t  last_val;
	int      initialized;
	const char *name;
} gb_watcher_t;

static gb_watcher_t g_gb_watchers[] = {
	{0xD190, 0, 0, "stateChange"},
	{0xFF99, 0, 0, "gameMode"},
	{0xFFFA, 0, 0, "coins"},
	{0xFFB3, 0, 0, "gameState"},
	{0xFFB4, 0, 0, "subState"},
	{0xFFDA, 0, 0, "secState"},
	{0xC0A0, 0, 0, "scoreHi"},
	{0xC0A1, 0, 0, "scoreLo"},
};
#define GB_WATCHER_COUNT (sizeof(g_gb_watchers) / sizeof(g_gb_watchers[0]))

// ---------------------------------------------------------------------------
// GameBoy Implementation
// ---------------------------------------------------------------------------

static void gameboy_init(void)
{
	memset(&g_gb_state, 0, sizeof(g_gb_state));
	g_gb_rtquery = 0;
	for (unsigned int i = 0; i < GB_WATCHER_COUNT; i++) {
		g_gb_watchers[i].initialized = 0;
		g_gb_watchers[i].last_val = 0;
	}
}

static void gameboy_reset(void)
{
	memset(&g_gb_state, 0, sizeof(g_gb_state));
	g_gb_rtquery = 0;
	for (unsigned int i = 0; i < GB_WATCHER_COUNT; i++) {
		g_gb_watchers[i].initialized = 0;
		g_gb_watchers[i].last_val = 0;
	}
}

static uint32_t gameboy_read_memory(void *map, uint32_t address, uint8_t *buffer, uint32_t num_bytes)
{
	if (g_gb_state.optionc) {
		if (g_gb_state.collecting) {
			for (uint32_t i = 0; i < num_bytes; i++)
				ra_snes_addrlist_add(address + i);
		}
		if (g_gb_state.cache_ready) {
			if (achievements_smart_cache_enabled() && g_gb_rtquery) {
				if (g_gb_state.cache_reindexing) {
					if (num_bytes <= 4) {
						uint32_t val = ra_rtquery_read(map, address, num_bytes);
						for (uint32_t i = 0; i < num_bytes; i++)
							buffer[i] = (uint8_t)(val >> (i * 8));
						return num_bytes;
					}
					for (uint32_t i = 0; i < num_bytes; i++) {
						uint32_t val = ra_rtquery_read(map, address + i, 1);
						buffer[i] = (uint8_t)val;
					}
					return num_bytes;
				}
				int any_miss = 0;
				for (uint32_t i = 0; i < num_bytes; i++) {
					if (ra_snes_addrlist_contains(address + i) < 0) {
						any_miss = 1; break;
					}
				}
				if (!any_miss) {
					for (uint32_t i = 0; i < num_bytes; i++)
						buffer[i] = ra_snes_addrlist_read_cached(map, address + i);
					return num_bytes;
				}
				if (num_bytes <= 4) {
					uint32_t val = ra_rtquery_read(map, address, num_bytes);
					for (uint32_t i = 0; i < num_bytes; i++) {
						buffer[i] = (uint8_t)(val >> (i * 8));
						ra_snes_addrlist_add_dynamic(address + i);
					}
					return num_bytes;
				}
				for (uint32_t i = 0; i < num_bytes; i++) {
					if (ra_snes_addrlist_contains(address + i) >= 0) {
						buffer[i] = ra_snes_addrlist_read_cached(map, address + i);
					} else {
						uint32_t val = ra_rtquery_read(map, address + i, 1);
						buffer[i] = (uint8_t)val;
						ra_snes_addrlist_add_dynamic(address + i);
					}
				}
				return num_bytes;
			}
			for (uint32_t i = 0; i < num_bytes; i++)
				buffer[i] = ra_snes_addrlist_read_cached(map, address + i);
			return num_bytes;
		}
		if (g_gb_rtquery && achievements_rtquery_enabled() && !g_gb_state.collecting && num_bytes <= 4) {
			uint32_t val = ra_rtquery_read(map, address, num_bytes);
			for (uint32_t i = 0; i < num_bytes; i++)
				buffer[i] = (uint8_t)(val >> (i * 8));
			return num_bytes;
		}
		memset(buffer, 0, num_bytes);
		return num_bytes;
	}
	return 0;
}

static int gameboy_poll(void *map, void *client, int game_loaded)
{
#ifdef HAS_RCHEEVOS
	if (!client || !game_loaded || !map || !g_gb_state.optionc) return 0;

	rc_client_t *rc_client = (rc_client_t *)client;

	// ===================================================================
	// Smart Cache path (Tier 1)
	// ===================================================================
	if (achievements_smart_cache_enabled() && g_gb_rtquery) {

		if (ra_snes_addrlist_count() == 0 && !g_gb_state.cache_ready) {
			g_gb_state.collecting = 1;
			ra_snes_addrlist_begin_collect();
			rc_client_do_frame(rc_client);
			g_gb_state.collecting = 0;
			int changed = ra_snes_addrlist_end_collect(map);
			if (changed)
				ra_log_write("GB SmartCache: Bootstrap done, %d addrs\n", ra_snes_addrlist_count());
			else
				ra_log_write("GB SmartCache: No addresses collected\n");
		} else if (!g_gb_state.cache_ready) {
			if (ra_snes_addrlist_is_ready(map)) {
				g_gb_state.cache_ready = 1;
				g_gb_state.last_resp_frame = 0;
				g_gb_state.game_frames = 0;
				g_gb_state.poll_logged = 0;
				clock_gettime(CLOCK_MONOTONIC, &g_gb_state.cache_time);
				ra_log_write("GB SmartCache: Cache active! %d addrs\n", ra_snes_addrlist_count());
			}
		} else {
			uint32_t resp_frame = ra_snes_addrlist_response_frame(map);

			if (g_gb_state.cache_reindexing && ra_snes_addrlist_is_ready(map)) {
				g_gb_state.cache_reindexing = 0;
				ra_log_write("GB SmartCache: Reindex complete (%d addrs)\n", ra_snes_addrlist_count());
			}

			if (resp_frame > g_gb_state.last_resp_frame) {
				g_gb_state.last_resp_frame = resp_frame;
				g_gb_state.game_frames++;
				ra_frame_processed(resp_frame);

				if (g_gb_state.game_frames <= 5)
					ra_log_write("GB SmartCache: GameFrame %u (resp_frame=%u, addrs=%d)\n",
						g_gb_state.game_frames, resp_frame, ra_snes_addrlist_count());

				int cleanup_frame = (g_gb_state.game_frames % 600 == 0)
					&& (g_gb_state.game_frames > 0)
					&& !g_gb_state.cache_reindexing;
				if (cleanup_frame) {
					g_gb_state.collecting = 1;
					ra_snes_addrlist_begin_collect();
				}

				rc_client_do_frame(rc_client);

				if (cleanup_frame) {
					g_gb_state.collecting = 0;
					int old_count = ra_snes_addrlist_count();
					if (ra_snes_addrlist_end_collect(map)) {
						int new_count = ra_snes_addrlist_count();
						ra_log_write("GB SmartCache: Cleanup — pruned %d stale (%d -> %d)\n",
							old_count - new_count, old_count, new_count);
						g_gb_state.cache_reindexing = 1;
					}
				} else {
					if (ra_snes_addrlist_has_pending())
						ra_snes_addrlist_flush_dynamic(map);
				}
			}
		}

		uint32_t milestone = g_gb_state.game_frames / 300;
		if (milestone > 0 && milestone != g_gb_state.poll_logged) {
			g_gb_state.poll_logged = milestone;
			struct timespec now;
			clock_gettime(CLOCK_MONOTONIC, &now);
			double elapsed = (now.tv_sec - g_gb_state.cache_time.tv_sec)
				+ (now.tv_nsec - g_gb_state.cache_time.tv_nsec) / 1e9;
			double ms_per_cycle = (g_gb_state.game_frames > 0) ?
				(elapsed * 1000.0 / g_gb_state.game_frames) : 0.0;
			ra_log_write("POLL(GB-SC): resp_frame=%u game_frames=%u elapsed=%.1fs ms/cycle=%.1f addrs=%d\n",
				g_gb_state.last_resp_frame, g_gb_state.game_frames, elapsed, ms_per_cycle,
				ra_snes_addrlist_count());
		}
		return 1;
	}

	// ===================================================================
	// Legacy path
	// ===================================================================

	if (ra_snes_addrlist_count() == 0 && !g_gb_state.cache_ready) {
		// Bootstrap: run one do_frame with zeros to discover needed addresses
		g_gb_state.collecting = 1;
		ra_snes_addrlist_begin_collect();
		rc_client_do_frame(rc_client);
		g_gb_state.collecting = 0;
		int changed = ra_snes_addrlist_end_collect(map);
		if (changed) {
			ra_log_write("GB OptionC: Bootstrap collection done, %d addrs written to DDRAM\n",
				ra_snes_addrlist_count());
		} else {
			ra_log_write("GB OptionC: No addresses collected — achievements may have no memory refs\n");
		}
	} else if (!g_gb_state.cache_ready) {
		// Wait for FPGA to respond with cached values
		if (ra_snes_addrlist_is_ready(map)) {
			g_gb_state.cache_ready = 1;
			g_gb_state.last_resp_frame = 0;
			g_gb_state.game_frames = 0;
			g_gb_state.poll_logged = 0;
			clock_gettime(CLOCK_MONOTONIC, &g_gb_state.cache_time);
			ra_log_write("GB OptionC: Cache active! FPGA response matched request.\n");
		}
	} else {
		// Normal frame processing from cache
		uint32_t resp_frame = ra_snes_addrlist_response_frame(map);
		if (resp_frame > g_gb_state.last_resp_frame) {
			g_gb_state.last_resp_frame = resp_frame;
			g_gb_state.game_frames++;
			ra_frame_processed(resp_frame);
			clock_gettime(CLOCK_MONOTONIC, &g_gb_state.stall_time);
			g_gb_state.stall_frame = resp_frame;

			// Periodically re-collect to catch address changes (every ~5 min)
			// Smart cache mode: skip re-collect (no dynamic pointers in GameBoy)
			int re_collect = !achievements_smart_cache_enabled()
				&& (g_gb_state.game_frames % 18000 == 0) && (g_gb_state.game_frames > 0);
			if (re_collect) {
				g_gb_state.collecting = 1;
				ra_snes_addrlist_begin_collect();
			}

			rc_client_do_frame(rc_client);

			// Value-change watchers
			for (unsigned int i = 0; i < GB_WATCHER_COUNT; i++) {
				uint8_t cur = ra_snes_addrlist_read_cached(map, g_gb_watchers[i].addr);
				if (!g_gb_watchers[i].initialized) {
					g_gb_watchers[i].last_val = cur;
					g_gb_watchers[i].initialized = 1;
				} else if (cur != g_gb_watchers[i].last_val) {
					ra_log_write("GB Watch[%s @ 0x%04X]: 0x%02X -> 0x%02X (frame %u)\n",
						g_gb_watchers[i].name, g_gb_watchers[i].addr,
						g_gb_watchers[i].last_val, cur, g_gb_state.game_frames);
					g_gb_watchers[i].last_val = cur;
				}
			}

			if (re_collect) {
				g_gb_state.collecting = 0;
				if (ra_snes_addrlist_end_collect(map)) {
					ra_log_write("GB OptionC: Address list refreshed, %d addrs\n",
						ra_snes_addrlist_count());
				}
			}
		} else {
			optionc_check_stall_recovery(&g_gb_state, resp_frame, "GameBoy");
		}
	}

	// Periodic log
	uint32_t milestone = g_gb_state.game_frames / 300;
	if (milestone > 0 && milestone != g_gb_state.poll_logged) {
		g_gb_state.poll_logged = milestone;
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		double elapsed = (now.tv_sec - g_gb_state.cache_time.tv_sec)
			+ (now.tv_nsec - g_gb_state.cache_time.tv_nsec) / 1e9;
		double ms_per_cycle = (g_gb_state.game_frames > 0) ?
			(elapsed * 1000.0 / g_gb_state.game_frames) : 0.0;
		ra_log_write("POLL(GB): resp_frame=%u game_frames=%u elapsed=%.1fs ms/cycle=%.1f addrs=%d\n",
			g_gb_state.last_resp_frame, g_gb_state.game_frames, elapsed, ms_per_cycle,
			ra_snes_addrlist_count());
	}

	return 1; // GameBoy handled
#else
	return 0;
#endif
}

static int gameboy_calculate_hash(const char *rom_path, char *md5_hex_out)
{
	(void)rom_path;
	(void)md5_hex_out;
	return 0; // use default MD5
}

static void gameboy_set_hardcore(int enabled)
{
	user_io_status_set("[17]", enabled ? 1 : 0); // cheats off (logic inverted: 1 = disabled)
	user_io_status_set("[51]", enabled ? 1 : 0); // hardcore bit: blocks save states in FPGA
	ra_log_write("GameBoy: Hardcore mode %s\n", enabled ? "enabled" : "disabled");
}

static int gameboy_detect_protocol(void *map)
{
	if (!ra_ramread_active(map)) {
		ra_log_write("Gameboy: FPGA mirror not detected -- RA support unavailable\n");
		return 0;
	}
	g_gb_state.optionc = 1;
	ra_log_write("Gameboy FPGA protocol: Option C (selective address reading)\n");

	if (ra_rtquery_supported(map) && achievements_rtquery_enabled()) {
		g_gb_rtquery = 1;
		ra_rtquery_init(map);
		ra_log_write("Gameboy: Realtime queries supported and ENABLED\n");
	} else if (ra_rtquery_supported(map)) {
		g_gb_rtquery = 0;
		ra_log_write("Gameboy: Realtime queries supported but DISABLED by config\n");
	} else {
		g_gb_rtquery = 0;
		ra_log_write("Gameboy: Realtime queries NOT supported (FPGA v1)\n");
	}
	return 1;
}

// ---------------------------------------------------------------------------
// Console handler definition
// ---------------------------------------------------------------------------

const console_handler_t g_console_gameboy = {
	.init = gameboy_init,
	.reset = gameboy_reset,
	.read_memory = gameboy_read_memory,
	.poll = gameboy_poll,
	.calculate_hash = gameboy_calculate_hash,
	.set_hardcore = gameboy_set_hardcore,
	.detect_protocol = gameboy_detect_protocol,
	.console_id = 4,  // RC_CONSOLE_GAMEBOY (also handles GBC with ID 6)
	.name = "GAMEBOY"
};
