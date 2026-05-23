// achievements_snes.cpp — RetroAchievements SNES-specific implementation

#include "achievements_console.h"
#include "achievements.h"
#include "ra_ramread.h"
#include "user_io.h"
#include "lib/md5/md5.h"
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef HAS_RCHEEVOS
#include "rc_client.h"
#include "rc_consoles.h"
#endif

// ---------------------------------------------------------------------------
// SNES State
// ---------------------------------------------------------------------------

static console_state_t g_snes_state = {0};
static int g_snes_rtquery = 0; // 1 if FPGA supports realtime queries

// ---------------------------------------------------------------------------
// SNES Option C Diagnostics
// ---------------------------------------------------------------------------

static void snes_optionc_dump_valcache(const char *label, void *map)
{
	if (!map) return;
	const uint8_t *base = (const uint8_t *)map;
	int addr_count = ra_snes_addrlist_count();
	const uint32_t *addrs = ra_snes_addrlist_addrs();

	const ra_val_resp_hdr_t *resp = (const ra_val_resp_hdr_t *)(base + RA_SNES_VALCACHE_OFFSET);
	const uint8_t *vals = base + RA_SNES_VALCACHE_OFFSET + 8;

	int dump_len = addr_count < 32 ? addr_count : 32;
	if (dump_len <= 0) dump_len = 32;
	char hex[200];
	int pos = 0, non_zero = 0;
	for (int i = 0; i < dump_len && pos < (int)sizeof(hex) - 4; i++) {
		pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", vals[i]);
		if (vals[i]) non_zero++;
	}

	ra_log_write("SNES DUMP[%s] resp_id=%u resp_frame=%u addrs=%d non_zero=%d\n",
		label, resp->response_id, resp->response_frame, addr_count, non_zero);
	ra_log_write("SNES DUMP[%s] VALCACHE[0..%d]: %s\n", label, dump_len - 1, hex);

	// FPGA debug words at 0x10 / 0x18
	const uint8_t *dbg8 = base + 0x10;
	uint16_t ok_cnt      = dbg8[0] | (dbg8[1] << 8);
	uint16_t timeout_cnt = dbg8[2] | (dbg8[3] << 8);
	uint8_t  dispatch_cnt = dbg8[6];
	uint8_t  fpga_ver     = dbg8[7];
	const uint8_t *dbg8b = base + 0x18;
	uint16_t wram_cnt = dbg8b[4] | (dbg8b[5] << 8);
	uint16_t bsram_cnt = dbg8b[2] | (dbg8b[3] << 8);
	uint16_t first_addr = dbg8b[6] | (dbg8b[7] << 8);

	ra_log_write("SNES DUMP[%s] FPGA ver=0x%02X ok=%u timeout=%u dispatch=%u wram=%u bsram=%u faddr=0x%04X\n",
		label, fpga_ver, ok_cnt, timeout_cnt, dispatch_cnt, wram_cnt, bsram_cnt, first_addr);

	// Address+value pairs (first 10)
	int show = addr_count < 10 ? addr_count : 10;
	for (int i = 0; i < show; i++)
		ra_log_write("SNES DUMP[%s]   [%d] addr=0x%05X val=0x%02X\n", label, i, addrs[i], vals[i]);
}

// ---------------------------------------------------------------------------
// SNES Implementation
// ---------------------------------------------------------------------------

static void snes_init(void)
{
	memset(&g_snes_state, 0, sizeof(g_snes_state));
	g_snes_rtquery = 0;
}

static void snes_reset(void)
{
	memset(&g_snes_state, 0, sizeof(g_snes_state));
	g_snes_rtquery = 0;
}


static uint32_t snes_read_memory(void *map, uint32_t address, uint8_t *buffer, uint32_t num_bytes)
{
	if (g_snes_state.optionc) {
		if (g_snes_state.collecting) {
			for (uint32_t i = 0; i < num_bytes; i++)
				ra_snes_addrlist_add(address + i);
		}
		if (g_snes_state.cache_ready) {
			if (achievements_smart_cache_enabled() && g_snes_rtquery) {
				// During reindexing, use rtquery for all reads
				if (g_snes_state.cache_reindexing) {
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
		if (g_snes_rtquery && achievements_rtquery_enabled() && !g_snes_state.collecting && num_bytes <= 4) {
			uint32_t val = ra_rtquery_read(map, address, num_bytes);
			for (uint32_t i = 0; i < num_bytes; i++)
				buffer[i] = (uint8_t)(val >> (i * 8));
			return num_bytes;
		}
		memset(buffer, 0, num_bytes);
		return num_bytes;
	} else {
		// VBlank-gated: read from full WRAM mirror
		return ra_ramread_snes_read(map, address, buffer, num_bytes);
	}
}

static int snes_poll(void *map, void *client, int game_loaded)
{
#ifdef HAS_RCHEEVOS
	if (!client || !game_loaded || !map || !g_snes_state.optionc)
		return 0;

	rc_client_t *rc_client = (rc_client_t *)client;

	// ===================================================================
	// Smart Cache path (Tier 1): rtquery handles cache misses
	// ===================================================================
	if (achievements_smart_cache_enabled() && g_snes_rtquery) {

		if (ra_snes_addrlist_count() == 0 && !g_snes_state.cache_ready) {
			// Phase 1: Bootstrap
			g_snes_state.collecting = 1;
			ra_snes_addrlist_begin_collect();
			rc_client_do_frame(rc_client);
			g_snes_state.collecting = 0;
			int changed = ra_snes_addrlist_end_collect(map);
			if (changed) {
				ra_log_write("SNES SmartCache: Bootstrap done, %d addrs written to DDRAM\n",
					ra_snes_addrlist_count());
			} else {
				ra_log_write("SNES SmartCache: No addresses collected\n");
			}
		} else if (!g_snes_state.cache_ready) {
			// Phase 2: Wait for FPGA to fill cache
			if (ra_snes_addrlist_is_ready(map)) {
				g_snes_state.cache_ready = 1;
				g_snes_state.last_resp_frame = 0;
				g_snes_state.game_frames = 0;
				g_snes_state.poll_logged = 0;
				clock_gettime(CLOCK_MONOTONIC, &g_snes_state.cache_time);
				ra_log_write("SNES SmartCache: Cache active! %d addrs monitored\n",
					ra_snes_addrlist_count());
				snes_optionc_dump_valcache("smart-active", map);
			}
		} else {
			// Phase 3: Normal — cache miss handled in read_memory
			uint32_t resp_frame = ra_snes_addrlist_response_frame(map);

			if (g_snes_state.cache_reindexing && ra_snes_addrlist_is_ready(map)) {
				g_snes_state.cache_reindexing = 0;
				ra_log_write("SNES SmartCache: Reindex complete (%d addrs)\n",
					ra_snes_addrlist_count());
			}

			if (resp_frame > g_snes_state.last_resp_frame) {
				g_snes_state.last_resp_frame = resp_frame;
				g_snes_state.game_frames++;
				ra_frame_processed(resp_frame);

				if (g_snes_state.game_frames <= 5) {
					ra_log_write("SNES SmartCache: GameFrame %u (resp_frame=%u, addrs=%d)\n",
						g_snes_state.game_frames, resp_frame, ra_snes_addrlist_count());
				}

				// Periodic cleanup: prune stale entries every ~10s
				int cleanup_frame = (g_snes_state.game_frames % 600 == 0)
					&& (g_snes_state.game_frames > 0)
					&& !g_snes_state.cache_reindexing;
				if (cleanup_frame) {
					g_snes_state.collecting = 1;
					ra_snes_addrlist_begin_collect();
				}

				rc_client_do_frame(rc_client);

				if (cleanup_frame) {
					g_snes_state.collecting = 0;
					int old_count = ra_snes_addrlist_count();
					if (ra_snes_addrlist_end_collect(map)) {
						int new_count = ra_snes_addrlist_count();
						ra_log_write("SNES SmartCache: Cleanup — pruned %d stale (%d -> %d)\n",
							old_count - new_count, old_count, new_count);
						g_snes_state.cache_reindexing = 1;
					}
				} else {
					if (ra_snes_addrlist_has_pending()) {
						ra_snes_addrlist_flush_dynamic(map);
					}
				}
			}
		}

		// Periodic logging
		uint32_t milestone = g_snes_state.game_frames / 300;
		if (milestone > 0 && milestone != g_snes_state.poll_logged) {
			g_snes_state.poll_logged = milestone;
			struct timespec now;
			clock_gettime(CLOCK_MONOTONIC, &now);
			double elapsed = (now.tv_sec - g_snes_state.cache_time.tv_sec)
				+ (now.tv_nsec - g_snes_state.cache_time.tv_nsec) / 1e9;
			double ms_per_cycle = (g_snes_state.game_frames > 0) ?
				(elapsed * 1000.0 / g_snes_state.game_frames) : 0.0;
			ra_log_write("POLL(SNES-SC): resp_frame=%u game_frames=%u elapsed=%.1fs ms/cycle=%.1f addrs=%d reindexing=%d\n",
				g_snes_state.last_resp_frame, g_snes_state.game_frames, elapsed, ms_per_cycle,
				ra_snes_addrlist_count(), g_snes_state.cache_reindexing);
			if ((g_snes_state.game_frames % 1800) < 300)
				snes_optionc_dump_valcache("periodic-sc", map);
		}

		return 1;
	}

	// ===================================================================
	// Legacy path: periodic recollect
	// ===================================================================

	if (ra_snes_addrlist_count() == 0 && !g_snes_state.cache_ready) {
		// Bootstrap: run one do_frame with zeros to discover needed addresses
		g_snes_state.collecting = 1;
		ra_snes_addrlist_begin_collect();
		rc_client_do_frame(rc_client);
		g_snes_state.collecting = 0;
		int changed = ra_snes_addrlist_end_collect(map);
		if (changed) {
			ra_log_write("SNES OptionC: Bootstrap collection done, %d addrs written to DDRAM\n",
				ra_snes_addrlist_count());
			snes_optionc_dump_valcache("bootstrap", map);
		} else {
			ra_log_write("SNES OptionC: No addresses collected — achievements may have no memory refs\n");
		}
	} else if (!g_snes_state.cache_ready) {
		// Wait for FPGA to respond with cached values
		if (ra_snes_addrlist_is_ready(map)) {
			g_snes_state.cache_ready = 1;
			g_snes_state.last_resp_frame = 0;
			g_snes_state.game_frames = 0;
			g_snes_state.poll_logged = 0;
			clock_gettime(CLOCK_MONOTONIC, &g_snes_state.cache_time);
			ra_log_write("SNES OptionC: Cache active! FPGA response matched request.\n");
			snes_optionc_dump_valcache("cache-active", map);
		}
	} else {
		// Normal frame processing from cache
		uint32_t resp_frame = ra_snes_addrlist_response_frame(map);
		if (resp_frame > g_snes_state.last_resp_frame) {
			g_snes_state.last_resp_frame = resp_frame;
			g_snes_state.game_frames++;
			ra_frame_processed(resp_frame);
			clock_gettime(CLOCK_MONOTONIC, &g_snes_state.stall_time);
			g_snes_state.stall_frame = resp_frame;

			// Dump first 5 frames after cache became active
			if (g_snes_state.game_frames <= 5) {
				ra_log_write("SNES OptionC: GameFrame %u (resp_frame=%u)\n",
					g_snes_state.game_frames, resp_frame);
				snes_optionc_dump_valcache("early-frame", map);
			}

			// Periodically re-collect to catch address changes (every ~5 min)
			// Smart cache mode: skip re-collect (no dynamic pointers in SNES)
			int re_collect = !achievements_smart_cache_enabled()
				&& (g_snes_state.game_frames % 18000 == 0) && (g_snes_state.game_frames > 0);
			if (re_collect) {
				g_snes_state.collecting = 1;
				ra_snes_addrlist_begin_collect();
			}

			rc_client_do_frame(rc_client);

			if (re_collect) {
				g_snes_state.collecting = 0;
				if (ra_snes_addrlist_end_collect(map)) {
					ra_log_write("SNES OptionC: Address list refreshed, %d addrs\n",
						ra_snes_addrlist_count());
				}
			}
		} else {
			optionc_check_stall_recovery(&g_snes_state, resp_frame, "SNES");
		}
	}

	// Periodic SNES debug — log once per 300-frame milestone
	uint32_t milestone = g_snes_state.game_frames / 300;
	if (milestone > 0 && milestone != g_snes_state.poll_logged) {
		g_snes_state.poll_logged = milestone;
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		double elapsed = (now.tv_sec - g_snes_state.cache_time.tv_sec)
			+ (now.tv_nsec - g_snes_state.cache_time.tv_nsec) / 1e9;
		double ms_per_cycle = (g_snes_state.game_frames > 0) ?
			(elapsed * 1000.0 / g_snes_state.game_frames) : 0.0;
		ra_log_write("POLL(SNES): resp_frame=%u game_frames=%u elapsed=%.1fs ms/cycle=%.1f addrs=%d\n",
			g_snes_state.last_resp_frame, g_snes_state.game_frames, elapsed, ms_per_cycle,
			ra_snes_addrlist_count());
		if ((g_snes_state.game_frames % 1800) < 300)
			snes_optionc_dump_valcache("periodic", map);
	}

	return 1; // SNES Option C handled
#else
	return 0;
#endif
}

static int snes_calculate_hash(const char *rom_path, char *md5_hex_out)
{
	// SNES: skip optional 512-byte SMC/SWC copier header
	FILE *f = fopen(rom_path, "rb");
	if (!f) {
		ra_log_write("SNES: Failed to open ROM for hashing: %s\n", rom_path);
		return 0;
	}

	fseek(f, 0, SEEK_END);
	long file_size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (file_size <= 0) { fclose(f); return 0; }

	uint8_t *rom_data = (uint8_t *)malloc(file_size);
	if (!rom_data) { fclose(f); return 0; }

	size_t nread = fread(rom_data, 1, file_size, f);
	fclose(f);

	if ((long)nread != file_size) { free(rom_data); return 0; }

	const uint8_t *hash_data = rom_data;
	long hash_size = file_size;

	if ((file_size % 1024) == 512 && file_size > 512) {
		ra_log_write("SNES: SMC header detected, skipping 512 bytes\n");
		hash_data = rom_data + 512;
		hash_size = file_size - 512;
	}

	MD5_CTX ctx;
	MD5Init(&ctx);
	MD5Update(&ctx, hash_data, hash_size);
	unsigned char md5_bin[16];
	MD5Final(md5_bin, &ctx);
	for (int i = 0; i < 16; i++)
		sprintf(&md5_hex_out[i * 2], "%02x", md5_bin[i]);
	md5_hex_out[32] = '\0';

	free(rom_data);
	return 1;
}

static void snes_set_hardcore(int enabled)
{
	user_io_status_set("[58]", enabled ? 1 : 0); // disable cheats
	user_io_status_set("[24]", enabled ? 1 : 0); // disable save states
	ra_log_write("SNES: Hardcore mode %s\n", enabled ? "enabled" : "disabled");
}

int snes_detect_protocol(void *map)
{
	if (!ra_ramread_active(map)) {
		ra_log_write("SNES: FPGA mirror not detected -- RA support unavailable\n");
		return 0;
	}
	const ra_header_t *hdr = (const ra_header_t *)map;
	if (hdr->region_count == 0) {
		g_snes_state.optionc = 1;
		ra_log_write("SNES FPGA protocol: Option C (selective address reading)\n");
	} else {
		g_snes_state.optionc = 0;
		ra_log_write("SNES FPGA protocol: VBlank-gated full mirror (region_count=%d)\n",
			hdr->region_count);
	}

	if (g_snes_state.optionc) {
		if (ra_rtquery_supported(map) && achievements_rtquery_enabled()) {
			g_snes_rtquery = 1;
			ra_rtquery_init(map);
			ra_log_write("SNES: Realtime queries supported and ENABLED\n");
		} else if (ra_rtquery_supported(map)) {
			g_snes_rtquery = 0;
			ra_log_write("SNES: Realtime queries supported but DISABLED by config\n");
		} else {
			g_snes_rtquery = 0;
			ra_log_write("SNES: Realtime queries NOT supported (FPGA v1)\n");
		}
	}
	return 1;
}

// ---------------------------------------------------------------------------
// Console handler definition
// ---------------------------------------------------------------------------

const console_handler_t g_console_snes = {
	.init = snes_init,
	.reset = snes_reset,
	.read_memory = snes_read_memory,
	.poll = snes_poll,
	.calculate_hash = snes_calculate_hash,
	.set_hardcore = snes_set_hardcore,
	.detect_protocol = snes_detect_protocol,
	.console_id = 3,  // RC_CONSOLE_SUPER_NINTENDO
	.name = "SNES",
	.hardcore_protected = 1
};
