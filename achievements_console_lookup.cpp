// achievements_console_lookup.cpp — Console handler lookup table

#include "achievements_console.h"
#include <string.h>
#include <strings.h>

// Console handlers from separate files
extern const console_handler_t g_console_nes;
extern const console_handler_t g_console_snes;
extern const console_handler_t g_console_genesis;
extern const console_handler_t g_console_psx;
extern const console_handler_t g_console_n64;
extern const console_handler_t g_console_gameboy;
extern const console_handler_t g_console_sms;
extern const console_handler_t g_console_neogeo;
extern const console_handler_t g_console_gba;
extern const console_handler_t g_console_megacd;
extern const console_handler_t g_console_atari2600;
extern const console_handler_t g_console_tgfx16;
extern const console_handler_t g_console_s32x;

// Master lookup table
static const console_handler_t *g_console_handlers[] = {
	&g_console_nes,
	&g_console_snes,
	&g_console_genesis,
	&g_console_psx,
	&g_console_n64,
	&g_console_gameboy,
	&g_console_sms,
	&g_console_neogeo,
	&g_console_gba,
	&g_console_megacd,
	&g_console_atari2600,
	&g_console_tgfx16,
	&g_console_s32x,
	NULL
};

const console_handler_t *get_console_handler_by_name(const char *core_name)
{
	if (!core_name) return NULL;

	// Check exact matches first
	for (int i = 0; g_console_handlers[i] != NULL; i++) {
		if (!strcasecmp(core_name, g_console_handlers[i]->name)) {
			return g_console_handlers[i];
		}
	}

	// Check aliases
	if (!strcasecmp(core_name, "MegaDrive")) {
		return &g_console_genesis;
	}
	if (!strcasecmp(core_name, "GBC")) {
		return &g_console_gameboy;  // GameBoy handler handles both GB and GBC
	}
	if (!strcasecmp(core_name, "MegaDrive32X")) {
		return &g_console_s32x;
	}

	return NULL;
}

const console_handler_t *get_console_handler_by_id(int console_id)
{
	for (int i = 0; g_console_handlers[i] != NULL; i++) {
		if (g_console_handlers[i]->console_id == console_id) {
			return g_console_handlers[i];
		}
	}

	// Special cases: GameBoy Color (ID 6) uses GameBoy handler (ID 4)
	if (console_id == 6) {
		return &g_console_gameboy;
	}
	// Game Gear (ID 15) uses SMS handler (ID 11)
	if (console_id == 15) {
		return &g_console_sms;
	}
	// PC Engine CD (ID 76) uses TG16 handler (ID 8)
	if (console_id == 76) {
		return &g_console_tgfx16;
	}
	// Famicom Disk System (ID 81) uses NES handler
	if (console_id == 81) {
		return &g_console_nes;
	}

	return NULL;
}

// Initialize all console handlers
void init_all_console_handlers(void)
{
	for (int i = 0; g_console_handlers[i] != NULL; i++) {
		if (g_console_handlers[i]->init) {
			g_console_handlers[i]->init();
		}
	}
}


// ---------------------------------------------------------------------------
// Shared OptionC stall recovery
// ---------------------------------------------------------------------------

#include "achievements.h"
#include "ra_ramread.h"

int optionc_check_stall_recovery(console_state_t *state, uint32_t resp_frame,
                                  const char *console_name)
{
        if (!achievements_stall_recovery_enabled()) return 0;

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double stall_secs = (now.tv_sec - state->stall_time.tv_sec)
                + (now.tv_nsec - state->stall_time.tv_nsec) / 1e9;

        if (stall_secs >= 5.0 && state->stall_frame == resp_frame) {
                ra_log_write("%s OptionC: STALL RECOVERY -- resp_frame=%u stuck for %.1fs, re-collecting\n",
                        console_name, resp_frame, stall_secs);
                state->cache_ready = 0;
                state->needs_recollect = 0;
                ra_snes_addrlist_init();
                clock_gettime(CLOCK_MONOTONIC, &state->stall_time);
                state->stall_frame = 0;
                return 1;
        }
        return 0;
}
