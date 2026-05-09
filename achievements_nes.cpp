// achievements_nes.cpp — RetroAchievements NES-specific implementation
// Option C + Smart Cache (Tier 1) — selective address reading with RTQuery

#include "achievements_console.h"
#include "achievements.h"
#include "ra_ramread.h"
#include "user_io.h"
#include "lib/md5/md5.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#ifdef HAS_RCHEEVOS
#include "rc_client.h"
#include "rc_consoles.h"
#endif

// ---------------------------------------------------------------------------
// NES State
// ---------------------------------------------------------------------------

static console_state_t g_nes_state = {0};
static int g_nes_rtquery = 0;

// ---------------------------------------------------------------------------
// NES Implementation
// ---------------------------------------------------------------------------

static void nes_init(void)
{
	memset(&g_nes_state, 0, sizeof(g_nes_state));
	g_nes_rtquery = 0;
}

static void nes_reset(void)
{
	memset(&g_nes_state, 0, sizeof(g_nes_state));
	g_nes_rtquery = 0;
}

static uint32_t nes_read_memory(void *map, uint32_t address, uint8_t *buffer, uint32_t num_bytes)
{
	if (g_nes_state.optionc) {
		if (g_nes_state.collecting) {
			for (uint32_t i = 0; i < num_bytes; i++)
				ra_snes_addrlist_add(address + i);
		}
		if (g_nes_state.cache_ready) {
			if (achievements_smart_cache_enabled() && g_nes_rtquery) {
				// During reindexing, use rtquery for all reads
				if (g_nes_state.cache_reindexing) {
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
				// Smart Cache: check each byte, rtquery on miss
				int any_miss = 0;
				for (uint32_t i = 0; i < num_bytes; i++) {
					if (ra_snes_addrlist_contains(address + i) < 0) {
						any_miss = 1;
						break;
					}
				}
				if (!any_miss) {
					for (uint32_t i = 0; i < num_bytes; i++)
						buffer[i] = ra_snes_addrlist_read_cached(map, address + i);
					return num_bytes;
				}
				// Cache miss: use rtquery
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
			// Legacy path: read from cache
			for (uint32_t i = 0; i < num_bytes; i++)
				buffer[i] = ra_snes_addrlist_read_cached(map, address + i);
			return num_bytes;
		}
		// RTQuery fallback for pre-cache reads
		if (g_nes_rtquery && achievements_rtquery_enabled() && !g_nes_state.collecting && num_bytes <= 4) {
			uint32_t val = ra_rtquery_read(map, address, num_bytes);
			for (uint32_t i = 0; i < num_bytes; i++)
				buffer[i] = (uint8_t)(val >> (i * 8));
			return num_bytes;
		}
		memset(buffer, 0, num_bytes);
		return num_bytes;
	} else {
		// VBlank-gated: read from full mirror (fallback)
		return ra_ramread_nes_read(map, address, buffer, num_bytes);
	}
}

static int nes_poll(void *map, void *client, int game_loaded)
{
#ifdef HAS_RCHEEVOS
	if (!client || !game_loaded || !map || !g_nes_state.optionc)
		return 0;

	rc_client_t *rc_client = (rc_client_t *)client;

	// ===================================================================
	// Smart Cache path (Tier 1)
	// ===================================================================
	if (achievements_smart_cache_enabled() && g_nes_rtquery) {

		if (ra_snes_addrlist_count() == 0 && !g_nes_state.cache_ready) {
			// Phase 1: Bootstrap
			g_nes_state.collecting = 1;
			ra_snes_addrlist_begin_collect();
			rc_client_do_frame(rc_client);
			g_nes_state.collecting = 0;
			int changed = ra_snes_addrlist_end_collect(map);
			if (changed) {
				ra_log_write("NES SmartCache: Bootstrap done, %d addrs written to DDRAM\n",
					ra_snes_addrlist_count());
			} else {
				ra_log_write("NES SmartCache: No addresses collected\n");
			}
		} else if (!g_nes_state.cache_ready) {
			// Phase 2: Wait for FPGA to fill cache
			if (ra_snes_addrlist_is_ready(map)) {
				g_nes_state.cache_ready = 1;
				g_nes_state.last_resp_frame = 0;
				g_nes_state.game_frames = 0;
				g_nes_state.poll_logged = 0;
				clock_gettime(CLOCK_MONOTONIC, &g_nes_state.cache_time);
				ra_log_write("NES SmartCache: Cache active! %d addrs monitored\n",
					ra_snes_addrlist_count());
			}
		} else {
			// Phase 3: Normal — cache miss handled in read_memory
			uint32_t resp_frame = ra_snes_addrlist_response_frame(map);

			if (g_nes_state.cache_reindexing && ra_snes_addrlist_is_ready(map)) {
				g_nes_state.cache_reindexing = 0;
				ra_log_write("NES SmartCache: Reindex complete (%d addrs)\n",
					ra_snes_addrlist_count());
			}

			if (resp_frame > g_nes_state.last_resp_frame) {
				g_nes_state.last_resp_frame = resp_frame;
				g_nes_state.game_frames++;
				ra_frame_processed(resp_frame);

				if (g_nes_state.game_frames <= 5) {
					ra_log_write("NES SmartCache: GameFrame %u (resp_frame=%u, addrs=%d)\n",
						g_nes_state.game_frames, resp_frame, ra_snes_addrlist_count());
				}

				// Periodic cleanup: prune stale entries every ~10s
				int cleanup_frame = (g_nes_state.game_frames % 600 == 0)
					&& (g_nes_state.game_frames > 0)
					&& !g_nes_state.cache_reindexing;
				if (cleanup_frame) {
					g_nes_state.collecting = 1;
					ra_snes_addrlist_begin_collect();
				}

				rc_client_do_frame(rc_client);

				if (cleanup_frame) {
					g_nes_state.collecting = 0;
					int old_count = ra_snes_addrlist_count();
					if (ra_snes_addrlist_end_collect(map)) {
						int new_count = ra_snes_addrlist_count();
						ra_log_write("NES SmartCache: Cleanup — pruned %d stale (%d -> %d)\n",
							old_count - new_count, old_count, new_count);
						g_nes_state.cache_reindexing = 1;
					}
				} else {
					if (ra_snes_addrlist_has_pending()) {
						ra_snes_addrlist_flush_dynamic(map);
					}
				}
			}
		}

		// Periodic logging
		uint32_t milestone = g_nes_state.game_frames / 300;
		if (milestone > 0 && milestone != g_nes_state.poll_logged) {
			g_nes_state.poll_logged = milestone;
			struct timespec now;
			clock_gettime(CLOCK_MONOTONIC, &now);
			double elapsed = (now.tv_sec - g_nes_state.cache_time.tv_sec)
				+ (now.tv_nsec - g_nes_state.cache_time.tv_nsec) / 1e9;
			double ms_per_cycle = (g_nes_state.game_frames > 0) ?
				(elapsed * 1000.0 / g_nes_state.game_frames) : 0.0;
			ra_log_write("POLL(NES-SC): resp_frame=%u game_frames=%u elapsed=%.1fs ms/cycle=%.1f addrs=%d reindexing=%d\n",
				g_nes_state.last_resp_frame, g_nes_state.game_frames, elapsed, ms_per_cycle,
				ra_snes_addrlist_count(), g_nes_state.cache_reindexing);
		}

		return 1;
	}

	// ===================================================================
	// Legacy path: periodic recollect (no RTQuery)
	// ===================================================================

	if (ra_snes_addrlist_count() == 0 && !g_nes_state.cache_ready) {
		g_nes_state.collecting = 1;
		ra_snes_addrlist_begin_collect();
		rc_client_do_frame(rc_client);
		g_nes_state.collecting = 0;
		int changed = ra_snes_addrlist_end_collect(map);
		if (changed) {
			ra_log_write("NES OptionC: Bootstrap collection done, %d addrs written to DDRAM\n",
				ra_snes_addrlist_count());
		}
	} else if (!g_nes_state.cache_ready) {
		if (ra_snes_addrlist_is_ready(map)) {
			g_nes_state.cache_ready = 1;
			g_nes_state.last_resp_frame = 0;
			g_nes_state.game_frames = 0;
			g_nes_state.poll_logged = 0;
			clock_gettime(CLOCK_MONOTONIC, &g_nes_state.cache_time);
			ra_log_write("NES OptionC: Cache active!\n");
		}
	} else {
		uint32_t resp_frame = ra_snes_addrlist_response_frame(map);
		if (resp_frame > g_nes_state.last_resp_frame) {
			g_nes_state.last_resp_frame = resp_frame;
			g_nes_state.game_frames++;
			ra_frame_processed(resp_frame);
			clock_gettime(CLOCK_MONOTONIC, &g_nes_state.stall_time);
			g_nes_state.stall_frame = resp_frame;

			if (g_nes_state.game_frames <= 5) {
				ra_log_write("NES OptionC: GameFrame %u (resp_frame=%u)\n",
					g_nes_state.game_frames, resp_frame);
			}

			// Periodically re-collect (~5 min)
			int re_collect = !achievements_smart_cache_enabled()
				&& (g_nes_state.game_frames % 18000 == 0) && (g_nes_state.game_frames > 0);
			if (re_collect) {
				g_nes_state.collecting = 1;
				ra_snes_addrlist_begin_collect();
			}

			rc_client_do_frame(rc_client);

			if (re_collect) {
				g_nes_state.collecting = 0;
				if (ra_snes_addrlist_end_collect(map)) {
					ra_log_write("NES OptionC: Address list refreshed, %d addrs\n",
						ra_snes_addrlist_count());
				}
			}
		} else {
			optionc_check_stall_recovery(&g_nes_state, resp_frame, "NES");
		}
	}

	// Periodic logging
	uint32_t milestone = g_nes_state.game_frames / 300;
	if (milestone > 0 && milestone != g_nes_state.poll_logged) {
		g_nes_state.poll_logged = milestone;
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		double elapsed = (now.tv_sec - g_nes_state.cache_time.tv_sec)
			+ (now.tv_nsec - g_nes_state.cache_time.tv_nsec) / 1e9;
		double ms_per_cycle = (g_nes_state.game_frames > 0) ?
			(elapsed * 1000.0 / g_nes_state.game_frames) : 0.0;
		ra_log_write("POLL(NES): resp_frame=%u game_frames=%u elapsed=%.1fs ms/cycle=%.1f addrs=%d\n",
			g_nes_state.last_resp_frame, g_nes_state.game_frames, elapsed, ms_per_cycle,
			ra_snes_addrlist_count());
	}

	return 1;
#else
	return 0;
#endif
}

static int nes_detect_protocol(void *map)
{
	if (!ra_ramread_active(map)) {
		ra_log_write("NES: FPGA mirror not detected -- RA support unavailable\n");
		return 0;
	}
	const ra_header_t *hdr = (const ra_header_t *)map;
	if (hdr->region_count == 0) {
		g_nes_state.optionc = 1;
		ra_log_write("NES FPGA protocol: Option C (selective address reading)\n");
	} else {
		g_nes_state.optionc = 0;
		ra_log_write("NES FPGA protocol: VBlank-gated full mirror (region_count=%d)\n",
			hdr->region_count);
	}

	if (g_nes_state.optionc) {
		if (ra_rtquery_supported(map) && achievements_rtquery_enabled()) {
			g_nes_rtquery = 1;
			ra_rtquery_init(map);
			ra_log_write("NES: Realtime queries supported and ENABLED\n");
		} else if (ra_rtquery_supported(map)) {
			g_nes_rtquery = 0;
			ra_log_write("NES: Realtime queries supported but DISABLED by config\n");
		} else {
			g_nes_rtquery = 0;
			ra_log_write("NES: Realtime queries NOT supported (FPGA v1)\n");
		}
	}
	return 1;
}

static int nes_calculate_hash(const char *rom_path, char *md5_hex_out)
{
	// Fallback manual hashing
	FILE *f = fopen(rom_path, "rb");
	if (!f) {
		ra_log_write("NES: Failed to open ROM for hashing: %s\n", rom_path);
		return 0;
	}

	fseek(f, 0, SEEK_END);
	long file_size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (file_size <= 0) {
		fclose(f);
		return 0;
	}

	uint8_t *rom_data = (uint8_t *)malloc(file_size);
	if (!rom_data) {
		fclose(f);
		return 0;
	}

	size_t nread = fread(rom_data, 1, file_size, f);
	fclose(f);
	
	if ((long)nread != file_size) {
		free(rom_data);
		return 0;
	}

	const uint8_t *hash_data = rom_data;
	long hash_size = file_size;

	// NES: skip iNES header ("NES\x1a") + optional 512-byte trainer
	if (file_size > 16 &&
		rom_data[0] == 0x4E && rom_data[1] == 0x45 &&  // 'N' 'E'
		rom_data[2] == 0x53 && rom_data[3] == 0x1A) {  // 'S' 0x1a
		uint32_t skip = 16;
		if (rom_data[6] & 0x04) skip += 512; // trainer present
		ra_log_write("NES: iNES header detected, skipping %u bytes (trainer=%d)\n",
			skip, (rom_data[6] & 0x04) ? 1 : 0);
		hash_data = rom_data + skip;
		hash_size = file_size - skip;
	}
	// FDS: skip fwNES FDS header ("FDS\x1a")
	else if (file_size > 16 &&
		rom_data[0] == 0x46 && rom_data[1] == 0x44 &&  // 'F' 'D'
		rom_data[2] == 0x53 && rom_data[3] == 0x1A) {  // 'S' 0x1a
		ra_log_write("NES: FDS header detected, skipping 16 bytes\n");
		hash_data = rom_data + 16;
		hash_size = file_size - 16;
	}

	// Calculate MD5
	MD5_CTX ctx;
	MD5Init(&ctx);
	MD5Update(&ctx, hash_data, hash_size);
	unsigned char md5_bin[16];
	MD5Final(md5_bin, &ctx);

	for (int i = 0; i < 16; i++) {
		sprintf(&md5_hex_out[i * 2], "%02x", md5_bin[i]);
	}
	md5_hex_out[32] = '\0';

	free(rom_data);
	return 1;
}

static void nes_set_hardcore(int enabled)
{
	if (enabled) {
		user_io_status_set("[70]", 1);  // Disable cheats
		user_io_status_set("[20]", 1);  // Disable save states
		ra_log_write("NES: Hardcore mode enabled (cheats/states disabled)\n");
	} else {
		user_io_status_set("[70]", 0);
		user_io_status_set("[20]", 0);
		ra_log_write("NES: Hardcore mode disabled\n");
	}
}

// ---------------------------------------------------------------------------
// Console handler definition
// ---------------------------------------------------------------------------

const console_handler_t g_console_nes = {
	.init = nes_init,
	.reset = nes_reset,
	.read_memory = nes_read_memory,
	.poll = nes_poll,
	.calculate_hash = nes_calculate_hash,
	.set_hardcore = nes_set_hardcore,
	.detect_protocol = nes_detect_protocol,
	.console_id = 7,  // RC_CONSOLE_NINTENDO
	.name = "NES",
	.hardcore_protected = 1
};

const console_handler_t g_console_fds = {
	.init = nes_init,
	.reset = nes_reset,
	.read_memory = nes_read_memory,
	.poll = nes_poll,
	.calculate_hash = nes_calculate_hash,
	.set_hardcore = nes_set_hardcore,
	.detect_protocol = nes_detect_protocol,
	.console_id = 81,  // RC_CONSOLE_FAMICOM_DISK_SYSTEM
	.name = "Famicom Disk System",
	.hardcore_protected = 1
};
