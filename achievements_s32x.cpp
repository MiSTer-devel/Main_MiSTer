// achievements_s32x.cpp — RetroAchievements Sega 32X implementation

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
// Sega 32X State
// ---------------------------------------------------------------------------

static console_state_t g_s32x_state = {};

// ---------------------------------------------------------------------------
// Sega 32X Implementation
// ---------------------------------------------------------------------------

static void s32x_init(void)
{
memset(&g_s32x_state, 0, sizeof(g_s32x_state));
}

static void s32x_reset(void)
{
memset(&g_s32x_state, 0, sizeof(g_s32x_state));
}

static uint32_t s32x_read_memory(void *map, uint32_t address, uint8_t *buffer, uint32_t num_bytes)
{
if (g_s32x_state.optionc) {
if (g_s32x_state.collecting) {
for (uint32_t i = 0; i < num_bytes; i++)
ra_snes_addrlist_add(address + i);
}
if (g_s32x_state.cache_ready) {
for (uint32_t i = 0; i < num_bytes; i++)
buffer[i] = ra_snes_addrlist_read_cached(map, address + i);
return num_bytes;
}
memset(buffer, 0, num_bytes);
return num_bytes;
}
return 0;
}

static int s32x_poll(void *map, void *client, int game_loaded)
{
#ifdef HAS_RCHEEVOS
if (!client || !game_loaded || !map || !g_s32x_state.optionc) return 0;

rc_client_t *rc_client = (rc_client_t *)client;

if (ra_snes_addrlist_count() == 0 && !g_s32x_state.cache_ready) {
// Bootstrap: run one do_frame to discover needed addresses
g_s32x_state.collecting = 1;
ra_snes_addrlist_begin_collect();
rc_client_do_frame(rc_client);
g_s32x_state.collecting = 0;
int changed = ra_snes_addrlist_end_collect(map);
if (changed) {
ra_log_write("S32X OptionC: Bootstrap collection done, %d addrs written to DDRAM\n",
ra_snes_addrlist_count());
} else {
ra_log_write("S32X OptionC: No addresses collected\n");
}
} else if (!g_s32x_state.cache_ready) {
// Wait for FPGA to respond with cached values
if (ra_snes_addrlist_is_ready(map)) {
g_s32x_state.cache_ready = 1;
g_s32x_state.last_resp_frame = 0;
g_s32x_state.game_frames = 0;
g_s32x_state.poll_logged = 0;
clock_gettime(CLOCK_MONOTONIC, &g_s32x_state.cache_time);
ra_log_write("S32X OptionC: Cache active! FPGA response matched request.\n");
// Dump first few addresses on activation
const uint32_t *a0 = ra_snes_addrlist_addrs();
int ac = ra_snes_addrlist_count();
int ad = ac < 20 ? ac : 20;
char ah[256]; int ap = 0;
for (int i = 0; i < ad && ap < (int)sizeof(ah) - 8; i++)
ap += snprintf(ah + ap, sizeof(ah) - ap, "%04X ", a0[i]);
ra_log_write("S32X ADDRLIST[0..%d]: %s (total=%d)\n", ad - 1, ah, ac);
}
} else {
// Normal frame processing from cache
uint32_t resp_frame = ra_snes_addrlist_response_frame(map);
if (resp_frame > g_s32x_state.last_resp_frame) {
g_s32x_state.last_resp_frame = resp_frame;
g_s32x_state.game_frames++;
ra_frame_processed(resp_frame);
clock_gettime(CLOCK_MONOTONIC, &g_s32x_state.stall_time);
g_s32x_state.stall_frame = resp_frame;

// Early-frame valcache dump (first 5 frames)
if (g_s32x_state.game_frames <= 5) {
const uint8_t *ev = (const uint8_t *)map + 0x48000 + 8;
int ec = ra_snes_addrlist_count();
int ed = ec < 45 ? ec : 45;
int enz = 0;
char eh[256]; int ep = 0;
for (int i = 0; i < ed && ep < (int)sizeof(eh) - 4; i++) {
ep += snprintf(eh + ep, sizeof(eh) - ep, "%02X ", ev[i]);
if (ev[i]) enz++;
}
ra_log_write("S32X early[%u]: VALCACHE %s (%d nz)\n",
g_s32x_state.game_frames, eh, enz);
}

// Re-collect every ~5 min to catch address changes
// Smart cache mode: skip re-collect (no dynamic pointers in S32X)
int re_collect = !achievements_smart_cache_enabled()
&& (g_s32x_state.game_frames % 18000 == 0) && (g_s32x_state.game_frames > 0);
if (re_collect) {
g_s32x_state.collecting = 1;
ra_snes_addrlist_begin_collect();
}

rc_client_do_frame(rc_client);

if (re_collect) {
g_s32x_state.collecting = 0;
if (ra_snes_addrlist_end_collect(map)) {
ra_log_write("S32X OptionC: Address list refreshed, %d addrs\n",
ra_snes_addrlist_count());
}
}
} else {
optionc_check_stall_recovery(&g_s32x_state, resp_frame, "S32X");
}
}

// Periodic log every 300 frames (~5 sec)
uint32_t milestone = g_s32x_state.game_frames / 300;
if (milestone > 0 && milestone != g_s32x_state.poll_logged) {
g_s32x_state.poll_logged = milestone;
struct timespec now;
clock_gettime(CLOCK_MONOTONIC, &now);
double elapsed = (now.tv_sec - g_s32x_state.cache_time.tv_sec)
+ (now.tv_nsec - g_s32x_state.cache_time.tv_nsec) / 1e9;
double ms_per_cycle = (g_s32x_state.game_frames > 0) ?
(elapsed * 1000.0 / g_s32x_state.game_frames) : 0.0;
ra_log_write("POLL(S32X): resp_frame=%u game_frames=%u elapsed=%.1fs ms/cycle=%.1f addrs=%d\n",
g_s32x_state.last_resp_frame, g_s32x_state.game_frames, elapsed, ms_per_cycle,
ra_snes_addrlist_count());
}

return 1; // S32X handled
#else
return 0;
#endif
}

static int s32x_calculate_hash(const char *rom_path, char *md5_hex_out)
{
(void)rom_path;
(void)md5_hex_out;
return 0; // use default MD5
}

static void s32x_set_hardcore(int enabled)
{
	user_io_status_set("[56]", enabled ? 1 : 0); // hardcore signal
	user_io_status_set("[24]", enabled ? 1 : 0); // disable cheats OSD toggle
	ra_log_write("S32X: Hardcore mode %s\n", enabled ? "enabled" : "disabled");
}

static int s32x_detect_protocol(void *map)
{
        if (!ra_ramread_active(map)) {
                ra_log_write("S32X: FPGA mirror not detected -- RA support unavailable\n");
                return 0;
        }
// S32X always uses Option C
g_s32x_state.optionc = 1;
ra_log_write("S32X FPGA protocol: Option C (selective address reading)\n");
        return 1;
}

// ---------------------------------------------------------------------------
// Console handler definition
// ---------------------------------------------------------------------------

const console_handler_t g_console_s32x = {
.init = s32x_init,
.reset = s32x_reset,
.read_memory = s32x_read_memory,
.poll = s32x_poll,
.calculate_hash = s32x_calculate_hash,
.set_hardcore = s32x_set_hardcore,
.detect_protocol = s32x_detect_protocol,
.console_id = 10,  // RC_CONSOLE_SEGA_32X
	.name = "S32X",
	.hardcore_protected = 0
};
