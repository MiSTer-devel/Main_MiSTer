// achievements_psx.cpp — RetroAchievements PlayStation-specific implementation

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
// PSX State
// ---------------------------------------------------------------------------

static console_state_t g_psx_state = {};
static int g_psx_rtquery = 0; // 1 if FPGA supports realtime queries
static uint32_t g_psx_rtquery_calls = 0;       // rtquery_read calls since last milestone
static uint32_t g_psx_rtquery_reindex = 0;     // calls made while cache_reindexing
static uint32_t g_psx_rtquery_miss = 0;        // calls made from cache-miss path
// ARM-side activity counters (reset each milestone log).
static uint32_t g_psx_flush_calls   = 0;       // ra_snes_addrlist_flush_dynamic calls (writes addrlist+request_id to DDR3)
static uint32_t g_psx_collect_calls = 0;       // begin_collect/end_collect cycles (cleanup_frame or recollect)
static uint32_t g_psx_list_changes  = 0;       // end_collect calls that detected list change
static uint32_t g_psx_addmiss_total = 0;       // add_dynamic invocations (total bytes that missed cache)
// rc_client_do_frame timing (ns) over current milestone window.
static uint64_t g_psx_doframe_total_ns = 0;
static uint64_t g_psx_doframe_max_ns   = 0;
static uint32_t g_psx_doframe_count    = 0;
// FPGA counters captured at previous milestone (to compute delta).
static uint16_t g_psx_prev_bram_hits   = 0;
static uint16_t g_psx_prev_bram_misses = 0;
static uint16_t g_psx_prev_qry_polls   = 0;
static uint16_t g_psx_prev_qry_serves  = 0;
// Snapshot a 16-bit FPGA counter saturating-mod arithmetic: returns frames since prev.
static uint16_t psx_dbg_delta_u16(uint16_t cur, uint16_t prev) {
	return (uint16_t)(cur - prev);
}

// ---------------------------------------------------------------------------
// Cleanup gating: skip periodic cleanup when it wouldn't help.
//
// Cleanup is only worth running when BOTH conditions are true:
//   (a) something was added via add_dynamic since the last cleanup, and
//   (b) the list has grown by more than PSX_CLEANUP_GROWTH_PCT % above the
//       size right after bootstrap.
//
// Rationale: cleanup is the most expensive ARM-side op (qsort + memcmp +
// DDR3 writes + cache_reindexing window). When the list is stable or only
// grew a little, the cleanup work doesn't pay for itself.
// ---------------------------------------------------------------------------
#define PSX_CLEANUP_GROWTH_PCT 50  // run cleanup once count > initial * 1.5
static uint32_t g_psx_initial_addr_count   = 0;
static int      g_psx_changes_since_cleanup = 0;

// ---------------------------------------------------------------------------
// Flat-array RAM mirror (PSX-specific O(1) lookup).
//
// PSX Main RAM is 2 MB. We maintain an ARM-side mirror of the bytes the
// FPGA is actively tracking. Lookup is O(1) via direct indexing, vs. the
// O(log N) binary search used by the generic ra_snes_addrlist_lookup_byte.
//
// Memory: g_psx_ram_mirror (2 MB values) + g_psx_ram_monitored (2 MB flags)
// = 4 MB of ARM RAM (trivial on a 1 GB system).
//
// The monitored flags were originally a 256 KB bitmap (1 bit per byte) but
// became a 2 MB byte array for two reasons: (a) lookup is the hot path and
// a byte load is ~4ns cheaper than the shift/mask/AND a bitmap requires;
// (b) on a 1 GB platform there is no reason to pay code complexity to save
// 1.75 MB. memset on cleanup is ~5x slower (2 MB vs 256 KB) but cleanup
// runs at most once every ~30s, so the steady-state lookup savings dominate.
//
// Synced from the FPGA's valcache on every new resp_frame so the values
// are at most one VBlank stale (same staleness as the existing valcache
// reads). Cache miss is detected by the monitored flag, not by the value
// (0 is a valid value, so we can't use it as a miss sentinel).
// ---------------------------------------------------------------------------
#define PSX_RAM_SIZE       (2u * 1024u * 1024u)
static uint8_t g_psx_ram_mirror   [PSX_RAM_SIZE];
static uint8_t g_psx_ram_monitored[PSX_RAM_SIZE];

static inline void psx_mirror_mark_monitored(uint32_t addr)
{
	if (addr >= PSX_RAM_SIZE) return;
	g_psx_ram_monitored[addr] = 1;
}

static inline uint8_t psx_mirror_lookup(uint32_t addr, int *hit)
{
	if (addr < PSX_RAM_SIZE && g_psx_ram_monitored[addr]) {
		if (hit) *hit = 1;
		return g_psx_ram_mirror[addr];
	}
	if (hit) *hit = 0;
	return 0;
}

// Rebuild the monitored flags from the current sorted address list. Called
// after bootstrap or cleanup-style end_collect, when the entire list may
// have changed in one shot.
static void psx_mirror_rebuild_flags(void)
{
	memset(g_psx_ram_monitored, 0, sizeof(g_psx_ram_monitored));
	int count = ra_snes_addrlist_count();
	const uint32_t *addrs = ra_snes_addrlist_addrs();
	for (int i = 0; i < count; i++)
		psx_mirror_mark_monitored(addrs[i]);
}

// Pull the freshest values from the FPGA's valcache (DDR3, memory-mapped)
// into our local mirror. Called once per new VBlank from psx_poll.
//
// Gated on is_ready(map): if the FPGA hasn't yet processed our latest
// request_id (after add_dynamic+flush or end_collect), the valcache still
// reflects the OLD list ordering and copying it through our NEW s_snes_addrs[]
// would corrupt the mirror. Skip the sync in that case and let the mirror
// stay one frame stale until the FPGA catches up.
static void psx_mirror_sync(const void *map)
{
	if (!map) return;
	if (!ra_snes_addrlist_is_ready(map)) return;
	const uint8_t *vals  = (const uint8_t *)map + RA_SNES_VALCACHE_OFFSET + 8;
	int            count = ra_snes_addrlist_count();
	const uint32_t *addrs = ra_snes_addrlist_addrs();
	for (int i = 0; i < count; i++) {
		uint32_t a = addrs[i];
		if (a < PSX_RAM_SIZE) g_psx_ram_mirror[a] = vals[i];
	}
}

static void psx_mirror_reset(void)
{
	memset(g_psx_ram_monitored, 0, sizeof(g_psx_ram_monitored));
	// mirror values do not need clearing — the monitored flag gates access,
	// so stale bytes are never returned for non-monitored addresses.
}


// ---------------------------------------------------------------------------
// PSX Option C Diagnostics
// ---------------------------------------------------------------------------

static void psx_optionc_dump_valcache(const char *label, void *map)
{
	if (!map) return;
	const uint8_t *base = (const uint8_t *)map;
	int addr_count = ra_snes_addrlist_count();
	const ra_val_resp_hdr_t *resp = (const ra_val_resp_hdr_t *)(base + RA_SNES_VALCACHE_OFFSET);
	const uint8_t *vals = base + RA_SNES_VALCACHE_OFFSET + 8;

	int dump_len = addr_count < 45 ? addr_count : 45;
	int non_zero = 0;
	char hex[256]; int pos = 0;
	for (int i = 0; i < dump_len && pos < (int)sizeof(hex) - 4; i++) {
		pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", vals[i]);
		if (vals[i]) non_zero++;
	}
	ra_log_write("PSX DUMP[%s] resp_id=%u resp_frame=%u addrs=%d non_zero=%d\n",
		label, resp->response_id, resp->response_frame, addr_count, non_zero);
	ra_log_write("PSX DUMP[%s] VALCACHE[0..%d]: %s\n", label, dump_len - 1, hex);

	const uint32_t *addrs = ra_snes_addrlist_addrs();
	int show = addr_count < 6 ? addr_count : 6;
	for (int i = 0; i < show; i++)
		ra_log_write("PSX DUMP[%s]   [%d] addr=0x%06X val=0x%02X\n", label, i, addrs[i], vals[i]);
}

// ---------------------------------------------------------------------------
// PSX Implementation
// ---------------------------------------------------------------------------

static void psx_init(void)
{
	memset(&g_psx_state, 0, sizeof(g_psx_state));
	g_psx_rtquery = 0;
	g_psx_rtquery_calls = 0;
	g_psx_rtquery_miss = 0;
	g_psx_rtquery_reindex = 0;
	g_psx_flush_calls = 0;
	g_psx_collect_calls = 0;
	g_psx_list_changes = 0;
	g_psx_addmiss_total = 0;
	g_psx_doframe_total_ns = 0;
	g_psx_doframe_max_ns = 0;
	g_psx_doframe_count = 0;
	g_psx_prev_bram_hits = 0;
	g_psx_prev_bram_misses = 0;
	g_psx_prev_qry_polls = 0;
	g_psx_prev_qry_serves = 0;
	g_psx_initial_addr_count   = 0;
	g_psx_changes_since_cleanup = 0;
	psx_mirror_reset();
}

static void psx_reset(void)
{
	memset(&g_psx_state, 0, sizeof(g_psx_state));
	g_psx_rtquery = 0;
	g_psx_rtquery_calls = 0;
	g_psx_rtquery_miss = 0;
	g_psx_rtquery_reindex = 0;
	g_psx_flush_calls = 0;
	g_psx_collect_calls = 0;
	g_psx_list_changes = 0;
	g_psx_addmiss_total = 0;
	g_psx_doframe_total_ns = 0;
	g_psx_doframe_max_ns = 0;
	g_psx_doframe_count = 0;
	g_psx_prev_bram_hits = 0;
	g_psx_prev_bram_misses = 0;
	g_psx_prev_qry_polls = 0;
	g_psx_prev_qry_serves = 0;
	g_psx_initial_addr_count   = 0;
	g_psx_changes_since_cleanup = 0;
	psx_mirror_reset();
}

static uint32_t psx_read_memory(void *map, uint32_t address, uint8_t *buffer, uint32_t num_bytes)
{
	if (g_psx_state.optionc) {
		if (g_psx_state.collecting) {
			for (uint32_t i = 0; i < num_bytes; i++)
				ra_snes_addrlist_add(address + i);
		}
		if (g_psx_state.cache_ready) {
			if (achievements_smart_cache_enabled() && g_psx_rtquery) {
				// During reindexing (after cleanup prune), FPGA cache indices are stale.
				// Use rtquery for all reads until FPGA responds with updated indices.
				if (g_psx_state.cache_reindexing) {
					if (num_bytes <= 4) {
						g_psx_rtquery_calls++;
						g_psx_rtquery_reindex++;
						uint32_t val = ra_rtquery_read(map, address, num_bytes);
						for (uint32_t i = 0; i < num_bytes; i++)
							buffer[i] = (uint8_t)(val >> (i * 8));
						return num_bytes;
					}
					for (uint32_t i = 0; i < num_bytes; i++) {
						g_psx_rtquery_calls++;
						g_psx_rtquery_reindex++;
						uint32_t val = ra_rtquery_read(map, address + i, 1);
						buffer[i] = (uint8_t)val;
					}
					return num_bytes;
				}
				// Flat-mirror lookup: O(1) per byte via psx_mirror_lookup
				// (monitored-flag check + value load). Replaces the binary
				// search of ra_snes_addrlist_lookup_byte. The mirror is kept
				// in sync with the FPGA's valcache by psx_mirror_sync() once
				// per VBlank.
				uint8_t miss_mask = 0;  // bit i set = byte i missed cache
				for (uint32_t i = 0; i < num_bytes; i++) {
					int hit;
					buffer[i] = psx_mirror_lookup(address + i, &hit);
					if (!hit) miss_mask |= (uint8_t)(1u << i);
				}
				if (!miss_mask) return num_bytes;
				// At least one miss: rtquery the missing bytes, mark them in the
				// mirror so subsequent reads in this frame are cache hits, and
				// schedule them for the next FPGA batch via add_dynamic.
				if (num_bytes <= 4) {
					g_psx_rtquery_calls++;
					g_psx_rtquery_miss++;
					uint32_t val = ra_rtquery_read(map, address, num_bytes);
					for (uint32_t i = 0; i < num_bytes; i++) {
						if (miss_mask & (1u << i)) {
							uint8_t b = (uint8_t)(val >> (i * 8));
							buffer[i] = b;
							g_psx_addmiss_total++;
							ra_snes_addrlist_add_dynamic(address + i);
							if ((address + i) < PSX_RAM_SIZE)
								g_psx_ram_mirror[address + i] = b;
							psx_mirror_mark_monitored(address + i);
							g_psx_changes_since_cleanup++;
						}
					}
					return num_bytes;
				}
				// num_bytes > 4: per-byte rtquery for the missing bytes only.
				for (uint32_t i = 0; i < num_bytes; i++) {
					if (miss_mask & (1u << i)) {
						g_psx_rtquery_calls++;
						g_psx_rtquery_miss++;
						uint32_t val = ra_rtquery_read(map, address + i, 1);
						uint8_t b = (uint8_t)val;
						buffer[i] = b;
						g_psx_addmiss_total++;
						ra_snes_addrlist_add_dynamic(address + i);
						if ((address + i) < PSX_RAM_SIZE)
							g_psx_ram_mirror[address + i] = b;
						psx_mirror_mark_monitored(address + i);
						g_psx_changes_since_cleanup++;
					}
				}
				return num_bytes;
			}
			// Legacy path: read from cache (no miss detection)
			for (uint32_t i = 0; i < num_bytes; i++)
				buffer[i] = ra_snes_addrlist_read_cached(map, address + i);
			return num_bytes;
		}
		// Realtime query fallback for addresses not in batch cache
		if (g_psx_rtquery && achievements_rtquery_enabled() && !g_psx_state.collecting && num_bytes <= 4) {
			g_psx_rtquery_calls++;
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

static int psx_poll(void *map, void *client, int game_loaded)
{
#ifdef HAS_RCHEEVOS
	if (!client || !game_loaded || !map || !g_psx_state.optionc) return 0;

	rc_client_t *rc_client = (rc_client_t *)client;

	// ===================================================================
	// Smart Cache path: no periodic recollect, rtquery handles cache miss
	// ===================================================================
	if (achievements_smart_cache_enabled() && g_psx_rtquery) {

		if (ra_snes_addrlist_count() == 0 && !g_psx_state.cache_ready) {
			// Phase 1: Bootstrap — collect addresses (values come from rtquery)
			g_psx_state.collecting = 1;
			ra_snes_addrlist_begin_collect();
			rc_client_do_frame(rc_client);
			g_psx_state.collecting = 0;
			int changed = ra_snes_addrlist_end_collect(map);
			if (changed) {
				// Initialize the flat mirror monitored flags from bootstrap list.
				psx_mirror_rebuild_flags();
				ra_log_write("PSX SmartCache: Bootstrap done, %d addrs written to DDRAM\n",
					ra_snes_addrlist_count());
			} else {
				ra_log_write("PSX SmartCache: No addresses collected\n");
			}
		} else if (!g_psx_state.cache_ready) {
			// Phase 2: Warm-up — wait for FPGA to fill the cache
			if (ra_snes_addrlist_is_ready(map)) {
				g_psx_state.cache_ready = 1;
				g_psx_state.last_resp_frame = 0;
				g_psx_state.game_frames = 0;
				g_psx_state.poll_logged = 0;
				clock_gettime(CLOCK_MONOTONIC, &g_psx_state.cache_time);
				// Capture the initial size as the baseline for the growth-pct
				// cleanup gate. Bootstrap-captured addresses are the "static"
				// set that should never trigger a cleanup.
				g_psx_initial_addr_count = ra_snes_addrlist_count();
				g_psx_changes_since_cleanup = 0;
				// First-time mirror fill from the valcache the FPGA just wrote.
				psx_mirror_sync(map);
				ra_log_write("PSX SmartCache: Cache active! %d addrs monitored (initial=%u)\n",
					ra_snes_addrlist_count(), g_psx_initial_addr_count);
				psx_optionc_dump_valcache("smart-active", map);
			}
		} else {
			// Phase 3: Normal — cache miss handled in read_memory via rtquery
			uint32_t resp_frame = ra_snes_addrlist_response_frame(map);

			// Check if FPGA responded after cleanup reindex
			if (g_psx_state.cache_reindexing && ra_snes_addrlist_is_ready(map)) {
				g_psx_state.cache_reindexing = 0;
				ra_log_write("PSX SmartCache: Reindex complete, FPGA cache synced (%d addrs)\n",
					ra_snes_addrlist_count());
			}

			if (resp_frame > g_psx_state.last_resp_frame) {
				g_psx_state.last_resp_frame = resp_frame;
				g_psx_state.game_frames++;
				ra_frame_processed(resp_frame);

				// Pull the freshest valcache values into the local flat mirror
				// so the upcoming rc_client_do_frame reads them via O(1) lookup.
				psx_mirror_sync(map);

				if (g_psx_state.game_frames <= 5) {
					ra_log_write("PSX SmartCache: GameFrame %u (resp_frame=%u, addrs=%d)\n",
						g_psx_state.game_frames, resp_frame, ra_snes_addrlist_count());
				}

				// Periodic cleanup: rebuild address list to prune stale entries.
				// Gated by: (a) some add_dynamic happened since last cleanup
				// and (b) current count > initial * (1 + PSX_CLEANUP_GROWTH_PCT/100).
				// Skips cleanup when the list is stable or hasn't grown enough
				// for the qsort/memcmp/DDR3 work to be worth it.
				uint32_t growth_threshold = (g_psx_initial_addr_count
					* (uint32_t)(100 + PSX_CLEANUP_GROWTH_PCT)) / 100u;
				int cleanup_frame = (g_psx_state.game_frames % 600 == 0)
					&& (g_psx_state.game_frames > 0)
					&& !g_psx_state.cache_reindexing
					&& (g_psx_changes_since_cleanup > 0)
					&& ((uint32_t)ra_snes_addrlist_count() > growth_threshold);
				if (cleanup_frame) {
					g_psx_state.collecting = 1;
					ra_snes_addrlist_begin_collect();
					g_psx_collect_calls++;
				}

				// Execute frame — cache misses resolved in psx_read_memory
				{
					struct timespec t0, t1;
					clock_gettime(CLOCK_MONOTONIC, &t0);
					rc_client_do_frame(rc_client);
					clock_gettime(CLOCK_MONOTONIC, &t1);
					uint64_t dt = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ULL
					            + (uint64_t)(t1.tv_nsec - t0.tv_nsec);
					g_psx_doframe_total_ns += dt;
					if (dt > g_psx_doframe_max_ns) g_psx_doframe_max_ns = dt;
					g_psx_doframe_count++;
				}

				if (cleanup_frame) {
					g_psx_state.collecting = 0;
					int old_count = ra_snes_addrlist_count();
					if (ra_snes_addrlist_end_collect(map)) {
						int new_count = ra_snes_addrlist_count();
						int pruned = old_count - new_count;
						g_psx_list_changes++;
						// List was replaced wholesale — rebuild the mirror
						// monitored flags from the new list (clears for pruned
						// addresses, sets for any newly captured ones).
						psx_mirror_rebuild_flags();
						ra_log_write("PSX SmartCache: Cleanup — pruned %d stale addrs (%d -> %d)\n",
							pruned, old_count, new_count);
						// FPGA cache indices are now stale — use rtquery until FPGA responds
						g_psx_state.cache_reindexing = 1;
					}
					// Reset the gate regardless of whether end_collect detected a
					// change. A cleanup attempt counts as having reconciled.
					g_psx_changes_since_cleanup = 0;
				} else {
					// Normal frame: flush any new dynamic addresses
					if (ra_snes_addrlist_has_pending()) {
						g_psx_flush_calls++;
						ra_snes_addrlist_flush_dynamic(map);
					}
				}
			}
		}

		// Periodic logging
		uint32_t milestone = g_psx_state.game_frames / 300;
		if (milestone > 0 && milestone != g_psx_state.poll_logged) {
			g_psx_state.poll_logged = milestone;
			struct timespec now;
			clock_gettime(CLOCK_MONOTONIC, &now);
			double elapsed = (now.tv_sec - g_psx_state.cache_time.tv_sec)
				+ (now.tv_nsec - g_psx_state.cache_time.tv_nsec) / 1e9;
			double ms_per_cycle = (g_psx_state.game_frames > 0) ?
				(elapsed * 1000.0 / g_psx_state.game_frames) : 0.0;
			// Read FPGA counters at DDRAM offset 0x18 (DBG2) and 0x20 (DBG3).
			const uint8_t *base = (const uint8_t *)map;
			uint16_t fpga_bram_hits   = *(const uint16_t *)(base + 0x18 + 2);
			uint16_t fpga_bram_misses = *(const uint16_t *)(base + 0x18 + 4);
			uint16_t fpga_qry_polls   = *(const uint16_t *)(base + 0x20 + 0);
			uint16_t fpga_qry_serves  = *(const uint16_t *)(base + 0x20 + 2);
			uint16_t d_hits   = psx_dbg_delta_u16(fpga_bram_hits,   g_psx_prev_bram_hits);
			uint16_t d_miss   = psx_dbg_delta_u16(fpga_bram_misses, g_psx_prev_bram_misses);
			uint16_t d_polls  = psx_dbg_delta_u16(fpga_qry_polls,   g_psx_prev_qry_polls);
			uint16_t d_serves = psx_dbg_delta_u16(fpga_qry_serves,  g_psx_prev_qry_serves);
			g_psx_prev_bram_hits   = fpga_bram_hits;
			g_psx_prev_bram_misses = fpga_bram_misses;
			g_psx_prev_qry_polls   = fpga_qry_polls;
			g_psx_prev_qry_serves  = fpga_qry_serves;

			uint64_t avg_us = g_psx_doframe_count
				? (g_psx_doframe_total_ns / g_psx_doframe_count) / 1000ULL
				: 0;
			uint64_t max_us = g_psx_doframe_max_ns / 1000ULL;
			ra_log_write("POLL(PSX-SC): resp_frame=%u game_frames=%u elapsed=%.1fs ms/cycle=%.1f addrs=%d reindexing=%d rtq=%u(m=%u r=%u) ARM[flush=%u col=%u chg=%u miss=%u dofrm_avg=%lluus max=%lluus] FPGA[bram_hit=%u bram_miss=%u qry_poll=%u qry_serve=%u]\n",
				g_psx_state.last_resp_frame, g_psx_state.game_frames, elapsed, ms_per_cycle,
				ra_snes_addrlist_count(), g_psx_state.cache_reindexing,
				g_psx_rtquery_calls, g_psx_rtquery_miss, g_psx_rtquery_reindex,
				g_psx_flush_calls, g_psx_collect_calls, g_psx_list_changes, g_psx_addmiss_total,
				(unsigned long long)avg_us, (unsigned long long)max_us,
				d_hits, d_miss, d_polls, d_serves);
			g_psx_rtquery_calls = 0;
			g_psx_rtquery_miss = 0;
			g_psx_rtquery_reindex = 0;
			g_psx_flush_calls = 0;
			g_psx_collect_calls = 0;
			g_psx_list_changes = 0;
			g_psx_addmiss_total = 0;
			g_psx_doframe_total_ns = 0;
			g_psx_doframe_max_ns = 0;
			g_psx_doframe_count = 0;
			if ((g_psx_state.game_frames % 1800) < 300)
				psx_optionc_dump_valcache("periodic-sc", map);
		}

		return 1;
	}

	// ===================================================================
	// Legacy path: periodic recollect
	// ===================================================================

	if (ra_snes_addrlist_count() == 0 && !g_psx_state.cache_ready) {
		// Phase 1: Bootstrap — collect addresses with zero values
		g_psx_state.collecting = 1;
		ra_snes_addrlist_begin_collect();
		rc_client_do_frame(rc_client);
		g_psx_state.collecting = 0;
		int changed = ra_snes_addrlist_end_collect(map);
		if (changed) {
			g_psx_state.needs_recollect = 1;
			ra_log_write("PSX OptionC: Bootstrap collection done, %d addrs written to DDRAM\n",
				ra_snes_addrlist_count());
			psx_optionc_dump_valcache("bootstrap", map);
		} else {
			ra_log_write("PSX OptionC: No addresses collected\n");
		}
	} else if (!g_psx_state.cache_ready) {
		// Phase 2/4: Wait for FPGA cache
		if (ra_snes_addrlist_is_ready(map)) {
			g_psx_state.cache_ready = 1;
			g_psx_state.last_resp_frame = 0;
			g_psx_state.game_frames = 0;
			g_psx_state.poll_logged = 0;
			clock_gettime(CLOCK_MONOTONIC, &g_psx_state.cache_time);
			if (g_psx_state.needs_recollect) {
				ra_log_write("PSX OptionC: Cache active (pre-recollect). Will resolve pointers.\n");
				psx_optionc_dump_valcache("pre-recollect", map);
			} else {
				ra_log_write("PSX OptionC: Cache active! FPGA response matched request.\n");
				psx_optionc_dump_valcache("cache-active", map);
			}
		}
	} else if (g_psx_state.needs_recollect) {
		// Phase 3: Pointer-resolution re-collection
		g_psx_state.needs_recollect = 0;
		g_psx_state.collecting = 1;
		ra_snes_addrlist_begin_collect();
		rc_client_do_frame(rc_client);
		g_psx_state.collecting = 0;
		int changed = ra_snes_addrlist_end_collect(map);
		if (changed) {
			ra_log_write("PSX OptionC: Pointer-resolve re-collection done, %d addrs (changed)\n",
				ra_snes_addrlist_count());
			g_psx_state.cache_ready = 0;
			psx_optionc_dump_valcache("ptr-resolve", map);
		} else {
			ra_log_write("PSX OptionC: Pointer-resolve complete, no address changes\n");
		}
	} else {
		// Phase 5: Normal frame processing
		uint32_t resp_frame = ra_snes_addrlist_response_frame(map);
		if (resp_frame > g_psx_state.last_resp_frame) {
			g_psx_state.last_resp_frame = resp_frame;
			g_psx_state.game_frames++;
			ra_frame_processed(resp_frame);
			clock_gettime(CLOCK_MONOTONIC, &g_psx_state.stall_time);
			g_psx_state.stall_frame = resp_frame;

			if (g_psx_state.game_frames <= 5) {
				ra_log_write("PSX OptionC: GameFrame %u (resp_frame=%u)\n",
					g_psx_state.game_frames, resp_frame);
				psx_optionc_dump_valcache("early-frame", map);
			}

			// Re-collect every N frames to track pointer changes (configurable)
			int interval = achievements_recollect_interval();
			int re_collect = (g_psx_state.game_frames % interval == 0) && (g_psx_state.game_frames > 0);
			if (re_collect) {
				g_psx_state.collecting = 1;
				ra_snes_addrlist_begin_collect();
				g_psx_collect_calls++;
			}

			{
				struct timespec t0, t1;
				clock_gettime(CLOCK_MONOTONIC, &t0);
				rc_client_do_frame(rc_client);
				clock_gettime(CLOCK_MONOTONIC, &t1);
				uint64_t dt = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ULL
				            + (uint64_t)(t1.tv_nsec - t0.tv_nsec);
				g_psx_doframe_total_ns += dt;
				if (dt > g_psx_doframe_max_ns) g_psx_doframe_max_ns = dt;
				g_psx_doframe_count++;
			}

			if (re_collect) {
				g_psx_state.collecting = 0;
				if (ra_snes_addrlist_end_collect(map)) {
					g_psx_list_changes++;
					ra_log_write("PSX OptionC: Address list refreshed, %d addrs\n",
						ra_snes_addrlist_count());
					g_psx_state.cache_ready = 0;
				}
			}
		} else {
			optionc_check_stall_recovery(&g_psx_state, resp_frame, "PSX");
		}
	}

	uint32_t milestone = g_psx_state.game_frames / 300;
	if (milestone > 0 && milestone != g_psx_state.poll_logged) {
		g_psx_state.poll_logged = milestone;
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		double elapsed = (now.tv_sec - g_psx_state.cache_time.tv_sec)
			+ (now.tv_nsec - g_psx_state.cache_time.tv_nsec) / 1e9;
		double ms_per_cycle = (g_psx_state.game_frames > 0) ?
			(elapsed * 1000.0 / g_psx_state.game_frames) : 0.0;
		// Read FPGA counters at DDRAM offset 0x18 (DBG2) and 0x20 (DBG3).
		const uint8_t *base = (const uint8_t *)map;
		uint16_t fpga_bram_hits   = *(const uint16_t *)(base + 0x18 + 2);
		uint16_t fpga_bram_misses = *(const uint16_t *)(base + 0x18 + 4);
		uint16_t fpga_qry_polls   = *(const uint16_t *)(base + 0x20 + 0);
		uint16_t fpga_qry_serves  = *(const uint16_t *)(base + 0x20 + 2);
		uint16_t d_hits   = psx_dbg_delta_u16(fpga_bram_hits,   g_psx_prev_bram_hits);
		uint16_t d_miss   = psx_dbg_delta_u16(fpga_bram_misses, g_psx_prev_bram_misses);
		uint16_t d_polls  = psx_dbg_delta_u16(fpga_qry_polls,   g_psx_prev_qry_polls);
		uint16_t d_serves = psx_dbg_delta_u16(fpga_qry_serves,  g_psx_prev_qry_serves);
		g_psx_prev_bram_hits   = fpga_bram_hits;
		g_psx_prev_bram_misses = fpga_bram_misses;
		g_psx_prev_qry_polls   = fpga_qry_polls;
		g_psx_prev_qry_serves  = fpga_qry_serves;

		uint64_t avg_us = g_psx_doframe_count
			? (g_psx_doframe_total_ns / g_psx_doframe_count) / 1000ULL
			: 0;
		uint64_t max_us = g_psx_doframe_max_ns / 1000ULL;
		ra_log_write("POLL(PSX): resp_frame=%u game_frames=%u elapsed=%.1fs ms/cycle=%.1f addrs=%d ARM[col=%u chg=%u dofrm_avg=%lluus max=%lluus] FPGA[bram_hit=%u bram_miss=%u qry_poll=%u qry_serve=%u]\n",
			g_psx_state.last_resp_frame, g_psx_state.game_frames, elapsed, ms_per_cycle,
			ra_snes_addrlist_count(),
			g_psx_collect_calls, g_psx_list_changes,
			(unsigned long long)avg_us, (unsigned long long)max_us,
			d_hits, d_miss, d_polls, d_serves);
		g_psx_collect_calls = 0;
		g_psx_list_changes = 0;
		g_psx_doframe_total_ns = 0;
		g_psx_doframe_max_ns = 0;
		g_psx_doframe_count = 0;
		if ((g_psx_state.game_frames % 1800) < 300)
			psx_optionc_dump_valcache("periodic", map);
	}

	return 1; // PSX handled
#else
	return 0;
#endif
}

static int psx_calculate_hash(const char *rom_path, char *md5_hex_out)
{
#ifdef HAS_RCHEEVOS
	char abs_path[1024];
	if (rom_path[0] == '/') {
		snprintf(abs_path, sizeof(abs_path), "%s", rom_path);
	} else {
		extern const char *getRootDir(void);
		snprintf(abs_path, sizeof(abs_path), "%s/%s", getRootDir(), rom_path);
	}

	if (rc_hash_generate_from_file(md5_hex_out, 12, abs_path)) {
		ra_log_write("PSX hash: %s\n", md5_hex_out);
		return 1;
	}
	ra_log_write("PSX: rc_hash_generate_from_file failed for %s\n", abs_path);
#endif
	return 0;
}

static void psx_set_hardcore(int enabled)
{
	user_io_status_set("[93]", enabled ? 1 : 0); // hardcore signal
	user_io_status_set("[6]",  enabled ? 1 : 0); // disable cheats OSD toggle
	ra_log_write("PSX: Hardcore mode %s\n", enabled ? "enabled" : "disabled");
}

static int psx_detect_protocol(void *map)
{
	if (!ra_ramread_active(map)) {
		ra_log_write("PSX: FPGA mirror not detected -- RA support unavailable\n");
		return 0;
	}
	// PSX always uses Option C (no VBlank-gated mode)
	g_psx_state.optionc = 1;
	ra_log_write("PSX FPGA protocol: Option C (selective address reading)\n");

	if (ra_rtquery_supported(map) && achievements_rtquery_enabled()) {
		g_psx_rtquery = 1;
		ra_rtquery_init(map);
		ra_log_write("PSX: Realtime queries supported and ENABLED (FPGA v2+)\n");
	} else if (ra_rtquery_supported(map)) {
		g_psx_rtquery = 0;
		ra_log_write("PSX: Realtime queries supported but DISABLED by config (rtquery_enabled=0)\n");
	} else {
		g_psx_rtquery = 0;
		ra_log_write("PSX: Realtime queries NOT supported (FPGA v1)\n");
	}
	return 1;
}

// ---------------------------------------------------------------------------
// Console handler definition
// ---------------------------------------------------------------------------

const console_handler_t g_console_psx = {
	.init = psx_init,
	.reset = psx_reset,
	.read_memory = psx_read_memory,
	.poll = psx_poll,
	.calculate_hash = psx_calculate_hash,
	.set_hardcore = psx_set_hardcore,
	.detect_protocol = psx_detect_protocol,
	.console_id = 12,  // RC_CONSOLE_PLAYSTATION
	.name = "PSX",
	.hardcore_protected = 0
};
