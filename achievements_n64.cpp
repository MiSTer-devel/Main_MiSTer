// achievements_n64.cpp — RetroAchievements Nintendo 64-specific implementation

#include "achievements_console.h"
#include "achievements.h"
#include "ra_ramread.h"
#include "user_io.h"
#include "shmem.h"
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef HAS_RCHEEVOS
#include "rc_client.h"
#include "rc_consoles.h"
#include "rc_hash.h"
#endif

// ---------------------------------------------------------------------------
// N64 State
// ---------------------------------------------------------------------------

static console_state_t g_n64_state = {};
static void *g_n64_rdram_direct = NULL; // direct RDRAM mmap (8MB at 0x30000000)

// Snapshot support (n64_snapshot=1 in config)
static uint8_t *g_n64_snapshot_buf = NULL; // 8MB shadow buffer
static int      g_n64_snapshot_active = 0; // 1 while snapshot is valid for current frame
static double   g_n64_snap_copy_ms = 0.0;  // timing: last memcpy duration
static uint32_t g_n64_snap_count = 0;      // total snapshots taken

// ---------------------------------------------------------------------------
// N64 Implementation
// ---------------------------------------------------------------------------

static void n64_init(void)
{
	memset(&g_n64_state, 0, sizeof(g_n64_state));
	g_n64_rdram_direct = NULL;
	g_n64_snapshot_active = 0;
	g_n64_snap_copy_ms = 0.0;
	g_n64_snap_count = 0;

	// Allocate snapshot buffer if enabled
	if (achievements_n64_snapshot_enabled() && !g_n64_snapshot_buf) {
		g_n64_snapshot_buf = (uint8_t *)malloc(0x800000); // 8MB
		if (g_n64_snapshot_buf)
			ra_log_write("N64: Snapshot buffer allocated (8MB)\n");
		else
			ra_log_write("N64: WARNING - failed to allocate snapshot buffer!\n");
	}

	// N64 savestates occupy 0x3C000000-0x3FFFFFFF (4 slots × 16MB),
	// colliding with the default RA base at 0x3D000000.
	// Move N64 RA mirror to the unused gap at 0x38000000.
	ra_ramread_set_base(0x38000000);
	ra_log_write("N64: Using RA DDRAM base 0x38000000 (avoiding SS slot collision)\n");
}

static void n64_reset(void)
{
	memset(&g_n64_state, 0, sizeof(g_n64_state));
	g_n64_snapshot_active = 0;
	if (g_n64_rdram_direct) {
		shmem_unmap(g_n64_rdram_direct, 0x800000);
		g_n64_rdram_direct = NULL;
	}
	if (g_n64_snapshot_buf) {
		free(g_n64_snapshot_buf);
		g_n64_snapshot_buf = NULL;
	}
}

static uint32_t n64_read_memory(void *map, uint32_t address, uint8_t *buffer, uint32_t num_bytes)
{
	(void)map;

	// Choose source: snapshot buffer (if active) or live RDRAM
	const uint8_t *src = NULL;
	if (g_n64_snapshot_active && g_n64_snapshot_buf)
		src = g_n64_snapshot_buf;
	else if (g_n64_rdram_direct)
		src = (const uint8_t *)g_n64_rdram_direct;

	if (src) {
		// N64 RDRAM addresses are big-endian (rcheevos convention);
		// RDRAM in DDR3 is little-endian within 32-bit words.
		// XOR 3 on the low 2 bits converts.
		for (uint32_t i = 0; i < num_bytes; i++) {
			uint32_t ddr_addr = (address + i) ^ 3;
			buffer[i] = (ddr_addr < 0x800000) ? src[ddr_addr] : 0;
		}
		return num_bytes;
	}

	memset(buffer, 0, num_bytes);
	return num_bytes;
}

static int n64_poll(void *map, void *client, int game_loaded)
{
#ifdef HAS_RCHEEVOS
	if (!client || !game_loaded || !g_n64_rdram_direct) return 0;

	rc_client_t *rc_client = (rc_client_t *)client;

	// Gate on FPGA VBlank frame counter.
	// The FPGA uses the VI interrupt (irqVector(3)) as VBlank source,
	// which fires when VI_CURRENT matches VI_INTR — same as RAProject64.
	// This signal never stops, even during level transitions.
	uint32_t frame = ra_ramread_frame(map);
	if (frame == g_n64_state.last_resp_frame) return 1; // no new VBlank

	g_n64_state.last_resp_frame = frame;
	g_n64_state.game_frames++;
	ra_frame_processed(g_n64_state.game_frames);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	// Initialize on first frame
	if (g_n64_state.game_frames == 1 && !g_n64_state.cache_ready) {
		g_n64_state.cache_ready = 1;
		g_n64_state.poll_logged = 0;
		g_n64_state.cache_time = now;
		ra_log_write("N64: VBlank-gated polling active (snapshot=%d)\n",
			achievements_n64_snapshot_enabled());
	}

	if (g_n64_state.game_frames <= 5)
		ra_log_write("N64: Frame %u (fpga=%u)\n", g_n64_state.game_frames, frame);

	// --- Snapshot: copy RDRAM at VBlank for consistent reads ---
	g_n64_snapshot_active = 0;
	if (achievements_n64_snapshot_enabled() && g_n64_snapshot_buf && g_n64_rdram_direct) {
		struct timespec t0, t1;
		clock_gettime(CLOCK_MONOTONIC, &t0);
		memcpy(g_n64_snapshot_buf, g_n64_rdram_direct, 0x800000);
		clock_gettime(CLOCK_MONOTONIC, &t1);

		g_n64_snapshot_active = 1;
		g_n64_snap_count++;
		g_n64_snap_copy_ms = (t1.tv_sec - t0.tv_sec) * 1000.0
			+ (t1.tv_nsec - t0.tv_nsec) / 1e6;

		if (g_n64_snap_count <= 3 || (g_n64_snap_count % 300) == 0) {
			ra_log_write("N64: Snapshot #%u took %.2fms\n",
				g_n64_snap_count, g_n64_snap_copy_ms);
		}
	}

	// Process achievements — synced with VI interrupt
	rc_client_do_frame(rc_client);

	// Invalidate snapshot after processing
	g_n64_snapshot_active = 0;

	// Periodic logging
	uint32_t milestone = g_n64_state.game_frames / 300;
	if (milestone > 0 && milestone != g_n64_state.poll_logged) {
		g_n64_state.poll_logged = milestone;
		double elapsed = (now.tv_sec - g_n64_state.cache_time.tv_sec)
			+ (now.tv_nsec - g_n64_state.cache_time.tv_nsec) / 1e9;
		double ms_per_cycle = (g_n64_state.game_frames > 0) ?
			(elapsed * 1000.0 / g_n64_state.game_frames) : 0.0;
		if (achievements_n64_snapshot_enabled()) {
			ra_log_write("POLL(N64): game_frames=%u elapsed=%.1fs ms/cycle=%.1f snap_ms=%.2f\n",
				g_n64_state.game_frames, elapsed, ms_per_cycle, g_n64_snap_copy_ms);
		} else {
			ra_log_write("POLL(N64): game_frames=%u elapsed=%.1fs ms/cycle=%.1f\n",
				g_n64_state.game_frames, elapsed, ms_per_cycle);
		}
	}

	return 1;
#else
	return 0;
#endif
}

static int n64_calculate_hash(const char *rom_path, char *md5_hex_out)
{
#ifdef HAS_RCHEEVOS
	char abs_path[1024];
	if (rom_path[0] == '/') {
		snprintf(abs_path, sizeof(abs_path), "%s", rom_path);
	} else {
		extern const char *getRootDir(void);
		snprintf(abs_path, sizeof(abs_path), "%s/%s", getRootDir(), rom_path);
	}

	if (rc_hash_generate_from_file(md5_hex_out, 2, abs_path)) {
		ra_log_write("N64 hash: %s\n", md5_hex_out);
		return 1;
	}
	ra_log_write("N64: rc_hash_generate_from_file failed for %s\n", abs_path);
#endif
	return 0;
}

static void n64_set_hardcore(int enabled)
{
	user_io_status_set("[107]", enabled ? 1 : 0); // hardcore signal
	user_io_status_set("[103]", enabled ? 1 : 0); // disable cheats OSD toggle
	ra_log_write("N64: Hardcore mode %s\n", enabled ? "enabled" : "disabled");
}

static int n64_detect_protocol(void *map)
{
	if (!ra_ramread_active(map)) {
		ra_log_write("N64: FPGA mirror not detected -- RA support unavailable\n");
		return 0;
	}

	// Map RDRAM directly — N64 RDRAM is at physical 0x30000000 (8MB)
	if (!g_n64_rdram_direct) {
		g_n64_rdram_direct = shmem_map(0x30000000, 0x800000);
		if (g_n64_rdram_direct)
			ra_log_write("N64: Direct RDRAM mapped at 0x30000000 (8MB)\n");
		else
			ra_log_write("N64: WARNING - failed to map RDRAM!\n");
	}

	// Allocate snapshot buffer on demand (if config loaded after init)
	if (achievements_n64_snapshot_enabled() && !g_n64_snapshot_buf) {
		g_n64_snapshot_buf = (uint8_t *)malloc(0x800000);
		if (g_n64_snapshot_buf)
			ra_log_write("N64: Snapshot buffer allocated (8MB)\n");
	}

	ra_log_write("N64: Direct RDRAM mode (snapshot=%d)\n",
		achievements_n64_snapshot_enabled());
	return 1;
}

// ---------------------------------------------------------------------------
// Console handler definition
// ---------------------------------------------------------------------------

const console_handler_t g_console_n64 = {
	.init = n64_init,
	.reset = n64_reset,
	.read_memory = n64_read_memory,
	.poll = n64_poll,
	.calculate_hash = n64_calculate_hash,
	.set_hardcore = n64_set_hardcore,
	.detect_protocol = n64_detect_protocol,
	.console_id = 2,  // RC_CONSOLE_NINTENDO_64
	.name = "N64",
	.hardcore_protected = 1
};
