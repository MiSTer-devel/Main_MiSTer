// achievements.cpp — RetroAchievements integration for MiSTer FPGA
//
// Phase 4: Full pipeline with OSD notifications — achievement popups,
// login/game status, progress indicators, status info panel.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <execinfo.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>

#include "achievements.h"
#include "achievements_console.h"
#include "ra_ramread.h"
#include "ra_http.h"
#include "user_io.h"
#include "cfg.h"
#include "file_io.h"
#include "menu.h"
#include "osd.h"
#include "hardware.h"
#include "lib/md5/md5.h"
#include "ra_cdreader_chd.h"

#ifdef HAS_RCHEEVOS
#include "rc_client.h"
#include "rc_consoles.h"
#include "rc_api_request.h"
#include "rc_hash.h"
#endif

// ---------------------------------------------------------------------------
// Debug logging
// ---------------------------------------------------------------------------

static FILE *g_logfile = NULL;
static int g_ra_debug = 0; // forward decl — defined/loaded in ra_load_credentials

#define RA_LOG(fmt, ...) ra_log_impl("RA: " fmt "\n", ##__VA_ARGS__)

void ra_log_write(const char *fmt, ...)
{
	if (!g_ra_debug) return;
	va_list args;
	va_start(args, fmt);
	printf("\033[1;35m");
	vprintf(fmt, args);
	printf("\033[0m");
	va_end(args);
	if (g_logfile) {
		va_start(args, fmt);
		vfprintf(g_logfile, fmt, args);
		va_end(args);
		fflush(g_logfile);
	}
}

static void ra_log_impl(const char *fmt, ...)
{
	if (!g_ra_debug) return;
	va_list args;

	// stdout with color
	va_start(args, fmt);
	printf("\033[1;35m");
	vprintf(fmt, args);
	printf("\033[0m");
	va_end(args);

	// log file without color
	if (g_logfile) {
		va_start(args, fmt);
		vfprintf(g_logfile, fmt, args);
		va_end(args);
		fflush(g_logfile);
	}
}

static void ra_log_open(void)
{
	if (!g_logfile) {
		g_logfile = fopen("/tmp/ra_debug.log", "w");
		if (g_logfile) {
			time_t now = time(NULL);
			fprintf(g_logfile, "=== RetroAchievements Debug Log ===\n");
			fprintf(g_logfile, "Started: %s\n", ctime(&now));
			fflush(g_logfile);
		}
	}
}

static void ra_log_close(void)
{
	if (g_logfile) {
		time_t now = time(NULL);
		fprintf(g_logfile, "Closed: %s\n", ctime(&now));
		fclose(g_logfile);
		g_logfile = NULL;
	}
}

// ---------------------------------------------------------------------------
// Crash signal handler — writes backtrace to log before dying
// ---------------------------------------------------------------------------
static void ra_crash_handler(int sig)
{
	const char *name = (sig == SIGSEGV) ? "SIGSEGV" :
	                   (sig == SIGBUS)  ? "SIGBUS"  :
	                   (sig == SIGABRT) ? "SIGABRT" :
	                   (sig == SIGFPE)  ? "SIGFPE"  : "UNKNOWN";

	// Write directly to log file (async-signal-safe is best-effort here)
	if (g_logfile) {
		fprintf(g_logfile, "\n!!! CRASH: signal %s (%d) !!!\n", name, sig);
		void *bt[32];
		int n = backtrace(bt, 32);
		backtrace_symbols_fd(bt, n, fileno(g_logfile));
		fflush(g_logfile);
	}

	// Also print to stderr
	fprintf(stderr, "\n!!! RA CRASH: signal %s (%d) !!!\n", name, sig);
	void *bt[32];
	int n = backtrace(bt, 32);
	backtrace_symbols_fd(bt, n, STDERR_FILENO);

	// Re-raise to get default behavior (core dump)
	signal(sig, SIG_DFL);
	raise(sig);
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static void *g_ra_map = NULL;        // DDRAM mirror mmap pointer
static uint32_t g_last_frame = 0;    // Last processed frame counter
static uint32_t g_first_frame = 0;   // First valid frame seen (for uptime tracking)
static int g_game_loaded = 0;        // Game is loaded and identified
static int g_mirror_validated = 0;   // DDRAM mirror has been validated at least once
static char g_rom_md5[33] = {};      // MD5 hex string of current ROM
static char g_rom_path[1024] = {};   // Path to current ROM

// Active console handler (set in achievements_init, dispatches all per-console logic)
static const console_handler_t *g_active_handler = NULL;

#ifdef HAS_RCHEEVOS
static rc_client_t *g_client = NULL;
#endif

// Credentials
static char g_ra_user[128] = {};
static char g_ra_password[128] = {};
static int g_has_credentials = 0;
static int g_logged_in = 0;
static int g_login_pending = 0;
static int g_game_load_pending = 0;
static int g_login_deferred = 0;      // login deferred until FPGA mirror validated
static int g_game_load_deferred = 0;  // game load deferred until
static int      g_mirror_confirming = 0;       // magic seen, waiting for frame counter to advance
static uint32_t g_mirror_initial_frame = 0;    // frame when magic was first seen (stale detection) FPGA mirror validated

// Debug counters
static uint32_t g_frames_processed = 0;
static uint32_t g_frames_skipped = 0;  // frames where busy flag was set
static time_t g_load_time = 0;

void ra_frame_processed(uint32_t frame)
{
	g_last_frame = frame;
	g_frames_processed++;
}

// Config file path
#define RA_CFG_PATH  "/media/fat/retroachievements.cfg"
#define RA_SFX_PATH  "/media/fat/achievement.wav"

// Popup display settings (from retroachievements.cfg)
static int g_show_challenge_show_popup = 1; // 1 = show popup on challenge SHOW event
static int g_show_challenge_hide_popup = 1; // 1 = show popup on challenge HIDE event
static int g_show_progress_popups      = 1; // 1 = show progress indicator popups
static int g_show_progress_name        = 1; // 1 = include achievement name in progress popup
static int g_leaderboards_enabled      = 1; // [deprecated] fallback only when both new leaderboard popup flags are absent
static int g_show_leaderboards_updates = 1; // 1 = show STARTED/FAILED/TRACKER SHOW/TRACKER UPDATE popups
static int g_show_leaderboards_submission = 1; // 1 = show SUBMITTED/SCOREBOARD popups
static int g_hardcore                  = 0; // 1 = hardcore mode (disables cheats & save states)
static int g_force_hardcore            = 0; // 1 = force hardcore mode even if core doesn't support it
static int g_stall_recovery            = 0; // 1 = enable OptionC stall recovery (disabled by default)
static int g_rtquery_enabled           = 1; // 1 = enable realtime queries for AddAddress resolution
static int g_gba_reset_ram             = 1; // 1 = clear IWRAM+EWRAM on game load (retroachievements.cfg: gba_reset_ram)
static int g_recollect_interval        = 600; // frames between address re-collections (PSX default 600, SNES 18000)
static int g_smart_cache               = -1; // -1 = default per console, 1 = smart cache: rtquery on cache miss, no periodic recollect
static int g_n64_snapshot              = 0;  // 1 = snapshot RDRAM at VBlank for consistent reads
static char g_ua_clause[64]            = ""; // rcheevos user-agent clause (e.g. "rcheevos/11.6")
static char g_fpga_core_version[8]     = "0.1"; // version reported by FPGA in DDRAM header

// ---------------------------------------------------------------------------
// Per-achievement event state (rate-limiting CHALLENGE, dedup PROGRESS)
// ---------------------------------------------------------------------------

#define RA_ACH_STATE_MAX 128
struct ra_ach_state_t {
	uint32_t id;
	time_t   challenge_last_popup; // monotonic: last time SHOW popup was shown
	char     progress_last[32];    // last progress string displayed
};
static ra_ach_state_t g_ach_state[RA_ACH_STATE_MAX];
static int g_ach_state_count = 0;

#define CHALLENGE_POPUP_COOLDOWN_SEC  10  // suppress CHALLENGE SHOW popup if one was shown < 10s ago
#define PROGRESS_SAME_VAL_COOLDOWN_SEC 5  // suppress PROGRESS popup if same value shown < 5s ago

static ra_ach_state_t *ra_ach_state_get(uint32_t id)
{
	for (int i = 0; i < g_ach_state_count; i++)
		if (g_ach_state[i].id == id) return &g_ach_state[i];
	if (g_ach_state_count < RA_ACH_STATE_MAX) {
		ra_ach_state_t *s = &g_ach_state[g_ach_state_count++];
		s->id                  = id;
		s->challenge_last_popup = 0;
		s->progress_last[0]    = '\0';
		return s;
	}
	return NULL;
}

// Returns 1 if CHALLENGE SHOW popup should be suppressed (fired too recently)
static int ra_challenge_popup_suppressed(uint32_t id)
{
	ra_ach_state_t *s = ra_ach_state_get(id);
	if (!s) return 0;
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	time_t elapsed = now.tv_sec - s->challenge_last_popup;
	if (elapsed < CHALLENGE_POPUP_COOLDOWN_SEC) return 1;
	s->challenge_last_popup = now.tv_sec;
	return 0;
}

// Returns 1 if PROGRESS popup should be suppressed (same value shown recently)
static int ra_progress_popup_suppressed(uint32_t id, const char *progress)
{
	ra_ach_state_t *s = ra_ach_state_get(id);
	if (!s) return 0;
	if (strcmp(s->progress_last, progress) == 0) {
		// Same value as last time — suppress unless it's been a while
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		// (future: could add time-based override here)
		return 1;
	}
	// New value — update and allow
	snprintf(s->progress_last, sizeof(s->progress_last), "%s", progress);
	return 0;
}

// ---------------------------------------------------------------------------
// Achievement Sound
// ---------------------------------------------------------------------------

static void *ra_play_thread(void *arg)
{
	(void)arg;
	// Only play if the file exists — silent no-op otherwise
	if (access(RA_SFX_PATH, R_OK) == 0)
		system("aplay -q " RA_SFX_PATH " 2>/dev/null");
	return NULL;
}

static void ra_play_achievement_sound(void)
{
	pthread_t th;
	if (pthread_create(&th, NULL, ra_play_thread, NULL) == 0)
		pthread_detach(th);
}

// ---------------------------------------------------------------------------
// OSD Notification — two-tier system
//
// Tier 1 URGENT (queued): achievement unlocked, game completed.
//   → Multiple unlocks accumulate; each is shown in order, never interrupted.
//
// Tier 2 INSTANT (single slot, last-wins): progress, challenge, etc.
//   → Shows immediately, overwriting any currently displayed instant.
//   → Silently discarded if a Tier 1 notification is on screen.
// ---------------------------------------------------------------------------

#define NOTIF_QUEUE_CAP 8
#define NOTIF_TEXT_MAX  200

struct ra_notif {
	char text[NOTIF_TEXT_MAX];
	int duration_ms;
	int play_sound;
};

// Tier 1 — urgent queue
static ra_notif s_urgent_queue[NOTIF_QUEUE_CAP];
static int s_urgent_head = 0;
static int s_urgent_tail = 0;
static int s_urgent_showing = 0;
static unsigned long s_urgent_timer = 0;

// Tier 2 — instant slot
static char s_instant_text[NOTIF_TEXT_MAX] = {0};
static int  s_instant_duration_ms = 3000;
static int  s_instant_pending = 0;
static int  s_instant_showing = 0;
static unsigned long s_instant_timer = 0;

// Add to urgent queue (never dropped by instant notifications)
static void ra_notify_urgent(const char *text, int duration_ms = 4000, int play_sound = 0)
{
	int count = s_urgent_head - s_urgent_tail;
	if (count >= NOTIF_QUEUE_CAP) {
		s_urgent_tail++;
		RA_LOG("OSD: Urgent queue full, dropping oldest");
	}
	ra_notif *n = &s_urgent_queue[s_urgent_head % NOTIF_QUEUE_CAP];
	snprintf(n->text, NOTIF_TEXT_MAX, "%s", text);
	n->duration_ms = duration_ms;
	n->play_sound  = play_sound;
	s_urgent_head++;
}

// Set instant slot — last event wins; discarded in poll if urgent is showing
static void ra_notify_instant(const char *text, int duration_ms = 3000)
{
	snprintf(s_instant_text, NOTIF_TEXT_MAX, "%s", text);
	s_instant_duration_ms = duration_ms;
	s_instant_pending = 1;
}

// Aliases kept for call-site readability
static void ra_notify(const char *text, int duration_ms = 3000)
{
	ra_notify_instant(text, duration_ms);
}

static void ra_notify_progress(const char *text)
{
	ra_notify_instant(text, 2500);
}

// Drive OSD display — called every achievements_poll() tick
static void ra_osd_poll(void)
{
	// Expire timers
	if (s_urgent_showing && CheckTimer(s_urgent_timer))
		s_urgent_showing = 0;
	if (s_instant_showing && CheckTimer(s_instant_timer))
		s_instant_showing = 0;

	if (menu_present()) return;

	// Tier 1: show next urgent as soon as previous one expires
	if (!s_urgent_showing && s_urgent_head != s_urgent_tail) {
		ra_notif *n = &s_urgent_queue[s_urgent_tail % NOTIF_QUEUE_CAP];
		s_urgent_tail++;
		Info(n->text, n->duration_ms + 500, 0, 0, 1);
		if (n->play_sound) ra_play_achievement_sound();
		s_urgent_timer    = GetTimer(n->duration_ms);
		s_urgent_showing  = 1;
		// Urgent takes over the display — discard any pending instant
		s_instant_pending = 0;
		s_instant_showing = 0;
		RA_LOG("OSD: Showing urgent notification (%dms)", n->duration_ms);
		return;
	}

	// Tier 2: instant slot — show immediately; discard if urgent is on screen
	if (s_instant_pending) {
		s_instant_pending = 0;
		if (!s_urgent_showing) {
			Info(s_instant_text, s_instant_duration_ms + 500, 0, 0, 1);
			s_instant_timer   = GetTimer(s_instant_duration_ms);
			s_instant_showing = 1;
			RA_LOG("OSD: Showing instant notification (%dms)", s_instant_duration_ms);
		} else {
			RA_LOG("OSD: Instant notification discarded (urgent showing)");
		}
	}
}

// ---------------------------------------------------------------------------
// ROM MD5 calculation
// ---------------------------------------------------------------------------

static int ra_get_console_id(void); // forward declaration
static int ra_core_supported(void); // forward declaration

// Compute the RetroAchievements MD5 for a ROM file.
// For NES: skips the 16-byte iNES header (and optional 512-byte trainer)
// so the hash matches what RetroAchievements expects.
static int ra_calculate_rom_md5(const char *path, char *md5_hex_out)
{
	fileTYPE f = {};
	if (!FileOpen(&f, path, 1)) {
		RA_LOG("ERROR: Cannot open ROM file: %s", path);
		return 0;
	}

	uint32_t file_size = f.size;
	RA_LOG("Hashing ROM: %s (%u bytes)", path, file_size);

	// Read entire file
	uint8_t *rom_data = (uint8_t *)malloc(file_size);
	if (!rom_data) {
		RA_LOG("ERROR: malloc failed for ROM buffer (%u bytes)", file_size);
		FileClose(&f);
		return 0;
	}

	int rd = FileReadAdv(&f, rom_data, file_size);
	FileClose(&f);

	if (rd <= 0 || (uint32_t)rd != file_size) {
		RA_LOG("ERROR: Failed to read ROM (got %d of %u bytes)", rd, file_size);
		free(rom_data);
		return 0;
	}

	const uint8_t *hash_data = rom_data;
	uint32_t hash_size = file_size;

	// NES: skip iNES header ("NES\x1a") + optional 512-byte trainer
	if (file_size > 16 &&
		rom_data[0] == 0x4E && rom_data[1] == 0x45 &&  // 'N' 'E'
		rom_data[2] == 0x53 && rom_data[3] == 0x1A) {  // 'S' 0x1a
		uint32_t skip = 16;
		if (rom_data[6] & 0x04) skip += 512; // trainer present
		RA_LOG("iNES header detected, skipping %u bytes (trainer=%d)",
			skip, (rom_data[6] & 0x04) ? 1 : 0);
		hash_data = rom_data + skip;
		hash_size = file_size - skip;
	}
	
	// FDS: skip fwNES FDS header ("FDS\x1a")
	if (file_size > 16 &&
		rom_data[0] == 0x46 && rom_data[1] == 0x44 &&  // 'F' 'D'
		rom_data[2] == 0x53 && rom_data[3] == 0x1A) {  // 'S' 0x1a
		RA_LOG("FDS header detected, skipping 16 bytes");
		hash_data = rom_data + 16;
		hash_size = file_size - 16;
	}

	// SNES: skip optional 512-byte SMC/SWC copier header
	if ((file_size % 1024) == 512 && file_size > 512) {
		RA_LOG("SNES SMC header detected (file_size %% 1024 == 512), skipping 512 bytes");
		hash_data = rom_data + 512;
		hash_size = file_size - 512;
	}

	struct MD5Context ctx;
	MD5Init(&ctx);
	MD5Update(&ctx, hash_data, hash_size);
	unsigned char digest[16];
	MD5Final(digest, &ctx);
	for (int i = 0; i < 16; i++)
		sprintf(md5_hex_out + i * 2, "%02x", digest[i]);
	md5_hex_out[32] = '\0';

	free(rom_data);
	RA_LOG("ROM MD5: %s", md5_hex_out);
	return 1;
}

// ---------------------------------------------------------------------------
// Credentials loading
// ---------------------------------------------------------------------------

// Config file format (/media/fat/retroachievements.cfg):
//   username=YourRAUsername
//   password=YourRAPassword
//   # Lines starting with # are comments

static int ra_load_credentials(void)
{
	g_ra_user[0] = '\0';
	g_ra_password[0] = '\0';
	int legacy_leaderboards_defined = 0;
	int show_leaderboards_updates_defined = 0;
	int show_leaderboards_submission_defined = 0;

	FILE *f = fopen(RA_CFG_PATH, "r");
	if (!f) {
		RA_LOG("Credentials file not found: %s", RA_CFG_PATH);
		RA_LOG("To enable RetroAchievements, create the file with:");
		RA_LOG("  username=YourRAUsername");
		RA_LOG("  password=YourRAPassword");
		return 0;
	}

	char line[512];
	while (fgets(line, sizeof(line), f)) {
		// Strip newline
		char *nl = strchr(line, '\n');
		if (nl) *nl = '\0';
		nl = strchr(line, '\r');
		if (nl) *nl = '\0';

		// Skip comments and empty lines
		if (line[0] == '#' || line[0] == '\0') continue;

		char *eq = strchr(line, '=');
		if (!eq) continue;

		*eq = '\0';
		const char *key = line;
		const char *val = eq + 1;

		// Trim leading spaces from value
		while (*val == ' ' || *val == '\t') val++;

		if (!strcasecmp(key, "username")) {
			snprintf(g_ra_user, sizeof(g_ra_user), "%s", val);
		} else if (!strcasecmp(key, "password")) {
			snprintf(g_ra_password, sizeof(g_ra_password), "%s", val);
		} else if (!strcasecmp(key, "show_challenge_show_popup")) {
			g_show_challenge_show_popup = atoi(val);
		} else if (!strcasecmp(key, "show_challenge_hide_popup")) {
			g_show_challenge_hide_popup = atoi(val);
		} else if (!strcasecmp(key, "show_progress_popups")) {
			g_show_progress_popups = atoi(val);
		} else if (!strcasecmp(key, "show_progress_name")) {
			g_show_progress_name = atoi(val);
		} else if (!strcasecmp(key, "show_leaderboards_updates") ||
					   !strcasecmp(key, "show-leaderboards-updates")) {
			g_show_leaderboards_updates = atoi(val);
			show_leaderboards_updates_defined = 1;
		} else if (!strcasecmp(key, "show_leaderboards_submission") ||
					   !strcasecmp(key, "show-leaderboards-submission")) {
			g_show_leaderboards_submission = atoi(val);
			show_leaderboards_submission_defined = 1;
		} else if (!strcasecmp(key, "leaderboards-enabled") ||
					!strcasecmp(key, "leaderboards_enabled")) {
				g_leaderboards_enabled = atoi(val);
				legacy_leaderboards_defined = 1;
		} else if (!strcasecmp(key, "hardcore")) {
			g_hardcore = atoi(val);
		} else if (!strcasecmp(key, "force_hardcore")) {
			g_force_hardcore = atoi(val);
		} else if (!strcasecmp(key, "stall_recovery")) {
			g_stall_recovery = atoi(val);
		} else if (!strcasecmp(key, "rtquery_enabled") ||
					!strcasecmp(key, "rtquery")) {
			g_rtquery_enabled = atoi(val);
		} else if (!strcasecmp(key, "recollect_interval")) {
			g_recollect_interval = atoi(val);
			if (g_recollect_interval < 60) g_recollect_interval = 60; // minimum 1 second
		} else if (!strcasecmp(key, "smart_cache")) {
			g_smart_cache = atoi(val);
		} else if (!strcasecmp(key, "debug")) {
			g_ra_debug = atoi(val);
		} else if (!strcasecmp(key, "n64_snapshot")) {
			g_n64_snapshot = atoi(val);
		} else if (!strcasecmp(key, "gba_reset_ram")) {
			g_gba_reset_ram = atoi(val);
		}
	}
	fclose(f);

	/* Backward compatibility: transfer deprecated value only when both new keys are absent. */
	if (!show_leaderboards_updates_defined && !show_leaderboards_submission_defined && legacy_leaderboards_defined) {
		g_show_leaderboards_updates = g_leaderboards_enabled;
		g_show_leaderboards_submission = g_leaderboards_enabled;
	}

	if (!g_ra_user[0] || !g_ra_password[0]) {
		RA_LOG("Credentials incomplete (need both username and password)");
		return 0;
	}

	RA_LOG("Credentials loaded: user=%s password=***(%zu chars)", g_ra_user, strlen(g_ra_password));
	RA_LOG("Config: show_challenge_show=%d show_challenge_hide=%d show_progress=%d show_progress_name=%d show_leaderboards_updates=%d show_leaderboards_submission=%d leaderboards_enabled(deprecated)=%d hardcore=%d force_hardcore=%d stall_recovery=%d rtquery=%d recollect=%d smart_cache=%d n64_snapshot=%d gba_reset_ram=%d debug=%d",
                g_show_challenge_show_popup, g_show_challenge_hide_popup,
                g_show_progress_popups, g_show_progress_name,
		g_show_leaderboards_updates, g_show_leaderboards_submission, g_leaderboards_enabled,
                g_hardcore, g_force_hardcore, g_stall_recovery, g_rtquery_enabled, g_recollect_interval, g_smart_cache, g_n64_snapshot, g_gba_reset_ram, g_ra_debug);
	return 1;
}


// ---------------------------------------------------------------------------
// rcheevos callbacks (compiled only if library is available)
// ---------------------------------------------------------------------------

#ifdef HAS_RCHEEVOS

static uint32_t ra_read_memory(uint32_t address, uint8_t *buffer,
	uint32_t num_bytes, rc_client_t *client)
{
	(void)client;
	if (!g_ra_map || !ra_ramread_active(g_ra_map)) {
		memset(buffer, 0, num_bytes);
		return ra_core_supported() ? num_bytes : 0;
	}

	// Dispatch to active console handler
	if (g_active_handler && g_active_handler->read_memory) {
		uint32_t r = g_active_handler->read_memory(g_ra_map, address, buffer, num_bytes);
		if (r > 0) return r;
	}

	// Fallback: VBlank-gated region reads for NES and SNES
	int console = ra_get_console_id();
	if (console == 7)  // NES
		return ra_ramread_nes_read(g_ra_map, address, buffer, num_bytes);
	if (console == 3)  // SNES VBlank-gated (handler returned 0 = not optionc)
		return ra_ramread_snes_read(g_ra_map, address, buffer, num_bytes);

	memset(buffer, 0, num_bytes);
	return num_bytes;
}

static void ra_server_call(const rc_api_request_t *request,
	rc_client_server_callback_t callback, void *callback_data,
	rc_client_t *client)
{
	(void)client;

	// Log the request (mask token for security)
	if (request->post_data) {
		const char *token_pos = strstr(request->post_data, "&t=");
		if (token_pos) {
			int prefix_len = (int)(token_pos - request->post_data);
			RA_LOG("HTTP: POST %s [%.*s&t=***]", request->url, prefix_len, request->post_data);
		} else {
			RA_LOG("HTTP: POST %s [%.80s%s]", request->url,
				request->post_data, strlen(request->post_data) > 80 ? "..." : "");
		}
	} else {
		RA_LOG("HTTP: GET %s", request->url);
	}

	// Bridge struct: passed through ra_http as opaque userdata
	struct ra_http_bridge {
		rc_client_server_callback_t rc_callback;
		void *rc_callback_data;
	};

	ra_http_bridge *bridge = (ra_http_bridge *)malloc(sizeof(ra_http_bridge));
	if (!bridge) {
		RA_LOG("ERROR: malloc failed for HTTP bridge");
		rc_api_server_response_t resp;
		memset(&resp, 0, sizeof(resp));
		resp.http_status_code = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
		resp.body = "malloc failed";
		resp.body_length = strlen(resp.body);
		callback(&resp, callback_data);
		return;
	}
	bridge->rc_callback = callback;
	bridge->rc_callback_data = callback_data;

	// The ra_http callback adapts our ra_http_resp into rc_api_server_response_t
	auto http_done = [](const void *resp_ptr, void *userdata) {
		struct ra_http_resp_view {
			int http_status;
			char *body;
			size_t body_len;
		};
		const ra_http_resp_view *hr = (const ra_http_resp_view *)resp_ptr;
		ra_http_bridge *br = (ra_http_bridge *)userdata;

		rc_api_server_response_t rc_resp;
		memset(&rc_resp, 0, sizeof(rc_resp));
		rc_resp.http_status_code = hr->http_status;
		rc_resp.body = hr->body ? hr->body : "";
		rc_resp.body_length = hr->body_len;

		// Log response body with token masked
		{
			char body_preview[220];
			snprintf(body_preview, sizeof(body_preview), "%.200s", rc_resp.body);
			// Mask "Token":"<value>" in response JSON
			char *tp = strstr(body_preview, "\"Token\":\"");
			if (tp) {
				char *val_start = tp + 9; // skip "Token":"  (9 chars)
				char *val_end = strchr(val_start, '"');
				if (val_end) {
					memmove(val_start + 3, val_end, strlen(val_end) + 1);
					memcpy(val_start, "***", 3);
				}
			}
			RA_LOG("HTTP response: status=%d body_len=%zu body=%s",
				hr->http_status, hr->body_len, body_preview);
		}

		if (hr->http_status == 0) {
			// curl failed entirely — mark as retryable
			rc_resp.http_status_code = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
		}

		br->rc_callback(&rc_resp, br->rc_callback_data);
		free(br);
	};

	ra_http_request(request->url, request->post_data, NULL,
		http_done, bridge);
}

static void ra_event_handler(const rc_client_event_t *event, rc_client_t *client)
{
	(void)client;
	RA_LOG("Event: type=%d", event->type);
	switch (event->type) {
	case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
		{
			if (event->achievement->id == 101000001) {
				ra_notify_urgent("HARDCORE mode DISABLED\nCORE still not supported", 3500);
			} else {
				RA_LOG("*** ACHIEVEMENT TRIGGERED: [%u] %s — %s ***",
					event->achievement->id, event->achievement->title,
					event->achievement->description);
					gba_dump_trigger(event->achievement->id);
				const int title_max = 28;
				const int desc_max  = 60;
				char title_buf[32];
				char desc_buf[64];
				snprintf(title_buf, title_max + 1, "%s", event->achievement->title);
				if (strlen(event->achievement->title) > (size_t)title_max)
					strcat(title_buf, "...");
				snprintf(desc_buf, desc_max + 1, "%s", event->achievement->description);
				if (strlen(event->achievement->description) > (size_t)desc_max)
					strcat(desc_buf, "...");
				char buf[NOTIF_TEXT_MAX];
				snprintf(buf, sizeof(buf),
					">> ACHIEVEMENT <<\n\n%s\n%s",
					title_buf, desc_buf);
								ra_notify_urgent(buf, 4000, 1);
			}
		}
		break;

	case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW:
		{
			RA_LOG("CHALLENGE SHOW: [%u] %s",
				event->achievement->id, event->achievement->title);
			if (g_show_challenge_show_popup &&
			    !ra_challenge_popup_suppressed(event->achievement->id)) {
				const int title_max = 28;
				char title_buf[32];
				snprintf(title_buf, title_max + 1, "%s", event->achievement->title);
				if (strlen(event->achievement->title) > (size_t)title_max)
					strcat(title_buf, "...");
				char buf[NOTIF_TEXT_MAX];
				snprintf(buf, sizeof(buf), "CHALLENGE ACTIVE\n\n%s", title_buf);
				ra_notify(buf, 3000);
			}
		}
		break;

	case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_HIDE:
		{
			RA_LOG("CHALLENGE HIDE: [%u] %s",
				event->achievement->id, event->achievement->title);
			if (g_show_challenge_hide_popup) {
				const int title_max = 28;
				char title_buf[32];
				snprintf(title_buf, title_max + 1, "%s", event->achievement->title);
				if (strlen(event->achievement->title) > (size_t)title_max)
					strcat(title_buf, "...");
				char buf[NOTIF_TEXT_MAX];
				snprintf(buf, sizeof(buf), "CHALLENGE MISSED\n\n%s", title_buf);
				ra_notify(buf, 3000);
			}
		}
		break;

	case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW:
	case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE:
		{
			RA_LOG("PROGRESS: %s — %s", event->achievement->title, event->achievement->measured_progress);
			// Dump all cached values on progress events
			if (g_ra_map) {
				int cnt = ra_snes_addrlist_count();
				const uint32_t *addrs = ra_snes_addrlist_addrs();
				for (int i = 0; i < cnt; i++) {
					uint8_t v = ra_snes_addrlist_read_cached(g_ra_map, addrs[i]);
					RA_LOG("  COND[%d] addr=0x%05X val=0x%02X", i, addrs[i], v);
				}
			}
			if (g_show_progress_popups &&
			    !ra_progress_popup_suppressed(event->achievement->id, event->achievement->measured_progress)) {
				char buf[NOTIF_TEXT_MAX];
				if (g_show_progress_name) {
					const int title_max = 28;
					char title_buf[32]; // 28 chars + "..." + null
					snprintf(title_buf, title_max + 1, "%s", event->achievement->title);
					if (strlen(event->achievement->title) > (size_t)title_max)
						strcat(title_buf, "...");
					snprintf(buf, sizeof(buf), "%s\nProgress: %s",
						title_buf, event->achievement->measured_progress);
				} else {
					snprintf(buf, sizeof(buf), "Progress: %s",
						event->achievement->measured_progress);
				}
				ra_notify_progress(buf);
			}
		}
		break;

        case RC_CLIENT_EVENT_LEADERBOARD_STARTED:
                {
                        if (!g_show_leaderboards_updates || !event->leaderboard)
                                break;

                        RA_LOG("LEADERBOARD STARTED: [%u] %s",
                                event->leaderboard->id, event->leaderboard->title);

                        const int title_max = 28;
                        char title_buf[32];
                        snprintf(title_buf, title_max + 1, "%s", event->leaderboard->title);
                        if (strlen(event->leaderboard->title) > (size_t)title_max)
                                strcat(title_buf, "...");

                        char buf[NOTIF_TEXT_MAX];
                        snprintf(buf, sizeof(buf), "LEADERBOARD START\n\n%s", title_buf);
                        ra_notify(buf, 2500);
                }
                break;

        case RC_CLIENT_EVENT_LEADERBOARD_FAILED:
                {
                        if (!g_show_leaderboards_updates || !event->leaderboard)
                                break;

                        RA_LOG("LEADERBOARD FAILED: [%u] %s",
                                event->leaderboard->id, event->leaderboard->title);

                        const int title_max = 28;
                        char title_buf[32];
                        snprintf(title_buf, title_max + 1, "%s", event->leaderboard->title);
                        if (strlen(event->leaderboard->title) > (size_t)title_max)
                                strcat(title_buf, "...");

                        char buf[NOTIF_TEXT_MAX];
                        snprintf(buf, sizeof(buf), "LEADERBOARD FAILED\n\n%s", title_buf);
                        ra_notify(buf, 2500);
                }
                break;

        case RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED:
		{
			if (!g_show_leaderboards_submission || !event->leaderboard)
                                break;

                        RA_LOG("LEADERBOARD SUBMITTED: [%u] %s",
                                event->leaderboard->id, event->leaderboard->title);

                        const int title_max = 28;
                        char title_buf[32];
                        char value_buf[RC_CLIENT_LEADERBOARD_DISPLAY_SIZE] = "-";
                        snprintf(title_buf, title_max + 1, "%s", event->leaderboard->title);
                        if (strlen(event->leaderboard->title) > (size_t)title_max)
                                strcat(title_buf, "...");

                        if (event->leaderboard->tracker_value && event->leaderboard->tracker_value[0])
                                snprintf(value_buf, sizeof(value_buf), "%s", event->leaderboard->tracker_value);

                        char buf[NOTIF_TEXT_MAX];
                        snprintf(buf, sizeof(buf), "LEADERBOARD SUBMITTED\n\n%s\nScore: %s",
                                title_buf, value_buf);
                        ra_notify_urgent(buf, 3500);
                }
                break;

        case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW:
        case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE:
                {
                        if (!g_show_leaderboards_updates || !event->leaderboard_tracker)
                                break;

                        RA_LOG("LEADERBOARD TRACKER: id=%u value=%s",
                                event->leaderboard_tracker->id,
                                event->leaderboard_tracker->display);

                        char buf[NOTIF_TEXT_MAX];
                        snprintf(buf, sizeof(buf), "LB #%u\n%s",
                                event->leaderboard_tracker->id,
                                event->leaderboard_tracker->display);
                        ra_notify_progress(buf);
                }
                break;

        case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_HIDE:
                {
                        if (!g_show_leaderboards_updates || !event->leaderboard_tracker)
                                break;

                        RA_LOG("LEADERBOARD TRACKER HIDE: id=%u",
                                event->leaderboard_tracker->id);
                }
                break;

        case RC_CLIENT_EVENT_LEADERBOARD_SCOREBOARD:
                {
                        if (!g_show_leaderboards_submission || !event->leaderboard_scoreboard)
                                break;

                        RA_LOG("LEADERBOARD SCOREBOARD: id=%u submitted=%s best=%s rank=%u entries=%u",
                                event->leaderboard_scoreboard->leaderboard_id,
                                event->leaderboard_scoreboard->submitted_score,
                                event->leaderboard_scoreboard->best_score,
                                event->leaderboard_scoreboard->new_rank,
                                event->leaderboard_scoreboard->num_entries);

                        char buf[NOTIF_TEXT_MAX];
                        snprintf(buf, sizeof(buf),
                                "LEADERBOARD RESULT\n\nRank: #%u/%u\nSubmitted: %s\nBest: %s",
                                event->leaderboard_scoreboard->new_rank,
                                event->leaderboard_scoreboard->num_entries,
                                event->leaderboard_scoreboard->submitted_score,
                                event->leaderboard_scoreboard->best_score);
                        ra_notify_urgent(buf, 4000);
                }
                break;

        case RC_CLIENT_EVENT_GAME_COMPLETED:
		RA_LOG("*** GAME COMPLETED! ***");
		ra_notify_urgent("** GAME COMPLETED! **\n\nCongratulations!", 5000);
		ra_play_achievement_sound();
		break;

	case RC_CLIENT_EVENT_SERVER_ERROR:
		{
			RA_LOG("SERVER ERROR: %s", event->server_error->error_message);
			char buf[NOTIF_TEXT_MAX];
			snprintf(buf, sizeof(buf),
				"RA Server Error\n\n%.60s",
				event->server_error->error_message);
			ra_notify(buf, 3000);
		}
		break;

	default:
		RA_LOG("EVENT: type=%d", event->type);
		break;
	}
}

static void ra_log_callback(const char *message, const rc_client_t *client)
{
	(void)client;
	RA_LOG("rcheevos: %s", message);
}

// Forward declaration (used in ra_login_callback)
static void ra_load_game_callback(int result, const char *error_message,
	rc_client_t *client, void *userdata);

static void ra_login_callback(int result, const char *error_message,
	rc_client_t *client, void *userdata)
{
	(void)client;
	(void)userdata;

	g_login_pending = 0;

	if (result == RC_OK) {
		const rc_client_user_t *user = rc_client_get_user_info(client);
		RA_LOG("LOGIN OK: %s (hardcore: %u, softcore: %u)", user->display_name, user->score, user->score_softcore);
		g_logged_in = 1;
		// Login popup is shown at game load time, not here

                // Login now happens on demand during game load. Only identify once mirror is active.
                if (g_rom_md5[0] && !g_game_loaded && !g_game_load_pending) {
                        if (g_mirror_validated) {
                                RA_LOG("Game MD5 available, loading game: %s", g_rom_md5);
                                g_game_load_pending = 1;
                                rc_client_begin_load_game(g_client, g_rom_md5,
                                        ra_load_game_callback, NULL);
                        } else {
                                RA_LOG("Login OK but mirror not validated yet, deferring game identify.");
                                g_game_load_deferred = 1;
                        }
                }
	} else {
		RA_LOG("LOGIN FAILED: result=%d error=%s", result,
			error_message ? error_message : "(none)");
	}
}

static void ra_load_game_callback(int result, const char *error_message,
	rc_client_t *client, void *userdata)
{
	(void)client;
	(void)userdata;

	g_game_load_pending = 0;

	if (result == RC_OK) {
		const rc_client_game_t *game = rc_client_get_game_info(client);
		RA_LOG("=== GAME IDENTIFIED ===");
		RA_LOG("  ID: %u", game->id);
		RA_LOG("  Title: %s", game->title);
		RA_LOG("  ROM: %s", g_rom_path);
		RA_LOG("  MD5: %s", g_rom_md5);
		g_game_loaded = 1;

		{
			// Single combined popup: game title + achievement count + logged-in user
			char buf[NOTIF_TEXT_MAX];
			// Count achievements via the list API
			rc_client_achievement_list_t *list =
				rc_client_create_achievement_list(client,
					RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
					RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);
			uint32_t total = 0;
			if (list) {
				for (uint32_t b = 0; b < list->num_buckets; b++)
					total += list->buckets[b].num_achievements;
				rc_client_destroy_achievement_list(list);
			}
			const rc_client_user_t *user = rc_client_get_user_info(client);
			int hardcore_enabled = rc_client_get_hardcore_enabled(client);
			if (user) {
				snprintf(buf, sizeof(buf),
					"%s\n(HC:%u SC:%u)",					
					user->display_name, user->score, user->score_softcore);
				ra_notify_urgent(buf, 2000);
			}
			if (total > 0) {
				snprintf(buf, sizeof(buf),
					"%s\n%u achievements",
					game->title, total);
			} else {
				snprintf(buf, sizeof(buf),
					"%s\n No achievements",
					game->title);
			}
			ra_notify_urgent(buf, 2000);
			
			hardcore_enabled = rc_client_get_hardcore_enabled(client);
			if (hardcore_enabled) {
				RA_LOG("HARDCORE mode ENABLED!");		
				ra_notify_urgent("HARDCORE mode ENABLED!", 2000);
			}
		}
	} else {
		RA_LOG("GAME LOAD FAILED: result=%d error=%s", result,
			error_message ? error_message : "(none)");
		if (result == RC_NO_GAME_LOADED) {
			RA_LOG("This ROM is not in the RetroAchievements database.");
		}
	}
}

#endif // HAS_RCHEEVOS

// ---------------------------------------------------------------------------
// Core identification
// ---------------------------------------------------------------------------

// Returns the rcheevos console ID for the current handler, or 0 if unsupported.
static int ra_get_console_id(void)
{
	return g_active_handler ? g_active_handler->console_id : 0;
}

// Returns 1 if the current core is supported for RA
static int ra_core_supported(void)
{
	return g_active_handler != NULL;
}

#ifdef HAS_RCHEEVOS
static int ra_has_internet_connectivity(void)
{
        struct addrinfo hints;
        struct addrinfo *res = NULL;
        int connected = 0;

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo("retroachievements.org", "80", &hints, &res) != 0 || !res) {
                RA_LOG("Internet check failed: DNS resolution for retroachievements.org");
                return 0;
        }

        for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
                int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
                if (fd < 0)
                        continue;

                int flags = fcntl(fd, F_GETFL, 0);
                if (flags >= 0)
                        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

                int rc = connect(fd, ai->ai_addr, ai->ai_addrlen);
                if (rc == 0) {
                        connected = 1;
                        close(fd);
                        break;
                }

                if (rc < 0 && errno == EINPROGRESS) {
                        fd_set wfds;
                        FD_ZERO(&wfds);
                        FD_SET(fd, &wfds);
                        struct timeval tv;
                        tv.tv_sec = 0;
                        tv.tv_usec = 500000;

                        int sel = select(fd + 1, NULL, &wfds, NULL, &tv);
                        if (sel > 0 && FD_ISSET(fd, &wfds)) {
                                int so_error = 0;
                                socklen_t slen = sizeof(so_error);
                                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &slen) == 0 && so_error == 0)
                                        connected = 1;
                        }
                }

                close(fd);
                if (connected)
                        break;
        }

        freeaddrinfo(res);
        return connected;
}

static void ra_show_no_internet_popup(void)
{
        ra_notify("RetroAchievements\n\nNo internet detected!\nTry again once you connected", 3000);
}
#endif

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

static void ra_hash_message(const char *msg)
{
	RA_LOG("HASH: %s", msg);
}

void achievements_init(void)
{
	ra_log_open();

	// Install crash handlers to capture backtraces
	signal(SIGSEGV, ra_crash_handler);
	signal(SIGBUS,  ra_crash_handler);
	signal(SIGABRT, ra_crash_handler);
	signal(SIGFPE,  ra_crash_handler);

	RA_LOG("=== RetroAchievements for MiSTer ===");
	RA_LOG("Build: OptionC v29-b1 (2026-04-19)");
	RA_LOG("Phase 5 — Handler dispatch: all per-console logic in separate files");

	const char *core = user_io_get_core_name(1);
	g_active_handler = core ? get_console_handler_by_name(core) : NULL;
	RA_LOG("Core: '%s' -> handler=%s console_id=%d", core ? core : "(null)",
		g_active_handler ? g_active_handler->name : "none",
		ra_get_console_id());

	if (!g_active_handler) {
		RA_LOG("Core not supported for RetroAchievements. Inactive.");
		return;
	}

	// Call handler init — N64 sets DDRAM base here, must happen before ra_ramread_map()
	g_active_handler->init();

	// Apply hardcore FPGA bits immediately so restrictions are active before any game loads
	if (achievements_hardcore_active() && g_active_handler->set_hardcore) {
		g_active_handler->set_hardcore(1);
		RA_LOG("Hardcore: FPGA bits applied at core init for %s", g_active_handler->name);
	}

#ifdef HAS_RCHEEVOS
	// Initialize rcheevos hash infrastructure (needed for disc-based consoles)
	ra_cdreader_chd_register(); // unified reader: CHD + cue/gdi fallback
	rc_hash_init_custom_filereader(NULL); // use default stdio-based reader
	rc_hash_init_error_message_callback(ra_hash_message);
	rc_hash_init_verbose_message_callback(ra_hash_message);
#endif

	// Map DDRAM mirror region
	g_ra_map = ra_ramread_map();
	if (!g_ra_map) {
		RA_LOG("ERROR: Failed to mmap DDRAM mirror at 0x%08X", ra_ramread_get_base());
		return;
	}
	RA_LOG("DDRAM mirror mapped at 0x%08X (%u bytes)", ra_ramread_get_base(), RA_DDRAM_MAP_SIZE);

	// Initial mirror status check
	if (ra_ramread_active(g_ra_map)) {
		RA_LOG("Mirror already active (magic OK). Dumping state:");
		ra_ramread_debug_dump(g_ra_map);
	} else {
		RA_LOG("Mirror not yet active (FPGA may not have started writing yet).");
	}

	// Start HTTP worker thread
	ra_http_init();

	// Load credentials
	int has_creds = ra_load_credentials();

#ifdef HAS_RCHEEVOS
	// Create rc_client
	g_client = rc_client_create(ra_read_memory, ra_server_call);
	if (!g_client) {
		RA_LOG("ERROR: rc_client_create() failed");
		return;
	}

	rc_client_enable_logging(g_client, RC_CLIENT_LOG_LEVEL_VERBOSE, ra_log_callback);
	rc_client_set_event_handler(g_client, ra_event_handler);
	int hardcore_active = achievements_hardcore_active();
	rc_client_set_hardcore_enabled(g_client, hardcore_active ? 1 : 0);
	RA_LOG("Hardcore mode: %s", hardcore_active ? "ENABLED" : "disabled");

	// Configure User-Agent: "MiSTer/1.0 rcheevos/x.y.z" (updated per-core in achievements_load_game)
	{
		rc_client_get_user_agent_clause(g_client, g_ua_clause, sizeof(g_ua_clause));
		char ua[128];
		snprintf(ua, sizeof(ua), "MiSTer/1.0 %s", g_ua_clause);
		ra_http_set_user_agent(ua);
		RA_LOG("User-Agent: %s", ua);
	}

	RA_LOG("rc_client created successfully");

	// Login is attempted on-demand in achievements_load_game, right before identify.
        g_has_credentials = has_creds;
        if (g_has_credentials) {
                RA_LOG("Credentials loaded. Login will happen on demand before game identify.");
        } else {
		RA_LOG("No credentials — running in monitor-only mode.");
		RA_LOG("Create %s to enable RetroAchievements.", RA_CFG_PATH);
	}
#else
	(void)has_creds;
	RA_LOG("Built without rcheevos library (HAS_RCHEEVOS not defined).");
	RA_LOG("Running in diagnostics-only mode: DDRAM mirror + ROM hash.");
#endif
}

static void ra_update_user_agent(void)
{
        if (g_ua_clause[0]) {
                const char *core_name = user_io_get_core_name(1);
                char ua[128];
                if (core_name && core_name[0])
                        snprintf(ua, sizeof(ua), "%s_MiSTer/%s %s", core_name, g_fpga_core_version, g_ua_clause);
                else
                        snprintf(ua, sizeof(ua), "MiSTer/%s %s", g_fpga_core_version, g_ua_clause);
                ra_http_set_user_agent(ua);
                RA_LOG("User-Agent updated: %s", ua);
        }
}

void achievements_load_game(const char *rom_path, uint32_t crc32)
{
        if (!g_active_handler) return;

        ra_update_user_agent();

        RA_LOG("--- Game Load ---");
        RA_LOG("ROM path: %s", rom_path);
        RA_LOG("CRC32: %08X", crc32);

        // Store ROM path
        snprintf(g_rom_path, sizeof(g_rom_path), "%s", rom_path);

        // Switch to FDS handler if we are NES and the ROM is an FDS file
        if (g_active_handler && (g_active_handler->console_id == 7 || g_active_handler->console_id == 81)) {
                size_t len = strlen(rom_path);
                if (len >= 4 && strcasecmp(rom_path + len - 4, ".fds") == 0) {
                        extern const console_handler_t g_console_fds;
                        g_active_handler = &g_console_fds;
                        RA_LOG("FDS ROM detected, switching handler to Famicom Disk System (ID 81)");
                } else {
                        extern const console_handler_t g_console_nes;
                        g_active_handler = &g_console_nes;
                }
        }

        // Calculate hash — try handler first, fall back to generic MD5
        g_rom_md5[0] = '\0';
        if (rom_path && rom_path[0]) {
                if (!g_active_handler->calculate_hash(rom_path, g_rom_md5)) {
                        ra_calculate_rom_md5(rom_path, g_rom_md5);
                }
        }

        // Reset frame tracking and console state
        g_last_frame = 0;
        g_first_frame = 0;
        g_mirror_validated = 0;
        g_mirror_confirming = 0;
        g_mirror_initial_frame = 0;
        g_game_load_deferred = 0;
        g_frames_processed = 0;
        g_frames_skipped = 0;
        g_game_loaded = 0;
        g_load_time = time(NULL);
        g_ach_state_count = 0;
        g_active_handler->reset();
        ra_snes_addrlist_init();

        // RetroAchievements safety: zero the DDRAM mirror data area so any leftover
        // bytes from the previous game cannot be fed to rcheevos before the FPGA
        // refreshes the mirror. The header (magic/frame/region descriptors) is
        // preserved so mirror validation logic continues to work; only the data
        // payload (offset 0x100 onwards) and the realtime-query mailbox are cleared.
        if (g_ra_map) {
                uint8_t *base = (uint8_t *)g_ra_map;
                memset(base + 0x100, 0, RA_DDRAM_MAP_SIZE - 0x100);
                RA_LOG("DDRAM mirror data area zeroed at game load");
        }

        // Check mirror state
        if (g_ra_map && ra_ramread_active(g_ra_map)) {
                RA_LOG("Mirror active at game load time. Dumping:");
                ra_ramread_debug_dump(g_ra_map);
        }

#ifdef HAS_RCHEEVOS
        if (g_client && g_rom_md5[0]) {
                if (!g_logged_in && !g_login_pending && g_has_credentials) {
                        if (!ra_has_internet_connectivity()) {
                                RA_LOG("No internet connectivity, skipping RA login for now.");
                                ra_show_no_internet_popup();
                                g_login_deferred = 1;
                        } else {
                                RA_LOG("Starting RA login for '%s' before game identify.", g_ra_user);
                                g_login_deferred = 0;
                                g_login_pending = 1;
                                g_game_load_deferred = 1;
                                rc_client_begin_login_with_password(g_client, g_ra_user, g_ra_password,
                                        ra_login_callback, NULL);
                        }
                } else if (g_logged_in && !g_game_load_pending) {
                        if (g_ra_map && ra_ramread_active(g_ra_map)) {
                                // Already logged in and FPGA mirror active — load immediately
                                RA_LOG("Logged in and FPGA mirror active, loading game by MD5: %s", g_rom_md5);
                                g_game_load_pending = 1;
                                rc_client_begin_load_game(g_client, g_rom_md5,
                                        ra_load_game_callback, NULL);
                        } else {
                                // Mirror not yet active — defer until FPGA validated
                                RA_LOG("Logged in but FPGA mirror not active — game load deferred.");
                                g_game_load_deferred = 1;
                        }
                } else if (g_login_pending || g_login_deferred) {
                        // Login still in progress or deferred — game loads after login
                        RA_LOG("Login pending/deferred, game will load when login completes.");
                } else {
                        RA_LOG("Not logged in — game identified but achievements unavailable.");
                        RA_LOG("MD5: %s (can verify at retroachievements.org)", g_rom_md5);
                }
        }
#endif

        RA_LOG("--- Game Load Complete, monitoring frames ---");

        // Hardcore mode: let handler set console-specific FPGA bits
        if (achievements_hardcore_active() && g_active_handler->set_hardcore) {
                g_active_handler->set_hardcore(1);
                RA_LOG("Hardcore: FPGA bits applied for %s", g_active_handler->name);
        }
}

void achievements_poll(void)
{
	static uint32_t poll_calls = 0;
	poll_calls++;

	// Heartbeat: log every ~10 seconds to confirm poll is alive
	{
		static struct timespec hb_last = {0, 0};
		struct timespec hb_now;
		clock_gettime(CLOCK_MONOTONIC, &hb_now);
		if (hb_last.tv_sec == 0) hb_last = hb_now;
		double hb_elapsed = (hb_now.tv_sec - hb_last.tv_sec)
			+ (hb_now.tv_nsec - hb_last.tv_nsec) / 1e9;
		if (hb_elapsed >= 10.0) {
			hb_last = hb_now;
			RA_LOG("HEARTBEAT: poll=%u map=%p validated=%d game_loaded=%d",
				poll_calls, g_ra_map, g_mirror_validated, g_game_loaded);
		}
	}

	// Always pump HTTP responses (even if mirror not validated yet,
	// because login/game-load responses need to be processed)
	ra_http_poll();

	// Show queued OSD notifications (achievement popups, etc.)
	ra_osd_poll();

	if (!g_ra_map) return;

	// Check if mirror has become active
	if (!g_mirror_validated) {
		if (ra_ramread_active(g_ra_map)) {
			uint32_t cur_frame = ra_ramread_frame(g_ra_map);
			if (!g_mirror_confirming) {
				// First time we see the magic — record frame, wait for it to advance
				g_mirror_confirming = 1;
				g_mirror_initial_frame = cur_frame;
				RA_LOG("DDRAM magic detected (frame=%u), waiting for frame to advance...", cur_frame);
			} else if (cur_frame != g_mirror_initial_frame) {
				// Frame advanced — FPGA is alive and adapted
				g_mirror_confirming = 0;
				g_mirror_validated = 1;
				RA_LOG("=== DDRAM Mirror Activated! (frame %u -> %u) ===", g_mirror_initial_frame, cur_frame);
				ra_ramread_debug_dump(g_ra_map);

				// Detect FPGA protocol — returns 1 if adapted, 0 if not supported
				int fpga_ok = 1;
				if (g_active_handler && g_active_handler->detect_protocol)
					fpga_ok = g_active_handler->detect_protocol(g_ra_map);

				if (!fpga_ok) {
					RA_LOG("FPGA core not adapted for RA — suppressing login/load.");
					g_login_deferred = 0;
					g_game_load_deferred = 0;
					g_active_handler = NULL;
					return;
				}

#ifdef HAS_RCHEEVOS
				// Read FPGA core version from DDRAM header for User-Agent reporting
				{
					uint8_t vmaj = 0, vmin = 0;
					if (ra_ramread_get_core_version(g_ra_map, &vmaj, &vmin))
						snprintf(g_fpga_core_version, sizeof(g_fpga_core_version), "%u.%u", vmaj, vmin);
					else
						snprintf(g_fpga_core_version, sizeof(g_fpga_core_version), "0.1");
					RA_LOG("FPGA core version: %s", g_fpga_core_version);
					ra_update_user_agent();
				}

                                // Trigger deferred game load (mirror activated, already logged in)
				if (g_game_load_deferred && g_logged_in && g_rom_md5[0]
						&& !g_game_load_pending) {
					g_game_load_deferred = 0;
					RA_LOG("FPGA validated — loading deferred game by MD5: %s", g_rom_md5);
					g_game_load_pending = 1;
					rc_client_begin_load_game(g_client, g_rom_md5,
						ra_load_game_callback, NULL);
				}
#endif
			}
			// else: frame still frozen — stale DDRAM from previous session, keep waiting
		} else {
			// Magic disappeared — reset confirming state
			g_mirror_confirming = 0;
			// Periodic debug while waiting for FPGA mirror
			static uint32_t wait_count = 0;
			if ((++wait_count % 18000) == 1) {
				const uint8_t *p = (const uint8_t *)g_ra_map;
				RA_LOG("Waiting for mirror... raw header: "
					"%02X %02X %02X %02X  %02X %02X %02X %02X  "
					"%02X %02X %02X %02X  %02X %02X %02X %02X",
					p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],
					p[8],p[9],p[10],p[11],p[12],p[13],p[14],p[15]);
			}
		}
		return;
	}

	// Read frame counter
	uint32_t frame = ra_ramread_frame(g_ra_map);
	int busy = ra_ramread_busy(g_ra_map);

	// Periodic diagnostics
	if ((poll_calls % 18000) == 1) {
		RA_LOG("DIAG: poll=%u frame=%u last=%u busy=%d processed=%u skipped=%u loaded=%d handler=%s addrs=%d",
			poll_calls, frame, g_last_frame, busy, g_frames_processed, g_frames_skipped,
			g_game_loaded, g_active_handler ? g_active_handler->name : "none",
			ra_snes_addrlist_count());
	}

	(void)busy;

#ifdef HAS_RCHEEVOS
	// Dispatch to active console handler (handles all Option C consoles)
	if (g_active_handler && g_active_handler->poll) {
		if (g_active_handler->poll(g_ra_map, g_client, g_game_loaded))
			return;
	}

	// --- removed per-console poll blocks (now handled by handler->poll()) ---
#endif

	// ================================================================
	// Default frame tracking (NES and other cores)
	// ================================================================

	if (frame == g_last_frame) {
		// Frame counter not advancing — still run rc_client_do_frame at a
		// throttled rate (~60 Hz) so achievements can be processed even if
		// the mirror frame counter is not implemented by the core.
		static uint32_t idle_counter = 0;
		idle_counter++;
		if (idle_counter >= 300) { // roughly every 300 polls
			idle_counter = 0;
			g_frames_processed++;
#ifdef HAS_RCHEEVOS
			if (g_client && g_game_loaded) {
				rc_client_do_frame(g_client);
			}
#endif
		}
		return;
	}

	// First frame detection
	if (g_first_frame == 0) {
		g_first_frame = frame;
		RA_LOG("First frame received: %u", frame);
		ra_ramread_debug_dump(g_ra_map);		
	}

	// Frame delta check (detect missed frames)
	// Only log if delta is in a plausible range (≤1000 missed frames),
	// to avoid spam when the counter is garbage/oscillating.
	uint32_t delta = frame - g_last_frame;
	if (delta > 1 && g_last_frame > 0 && delta <= 1000) {
		RA_LOG("WARNING: Missed %u frames (last=%u, now=%u)", delta - 1, g_last_frame, frame);
	}

	g_last_frame = frame;
	g_frames_processed++;

#ifdef HAS_RCHEEVOS
	// Process achievements if game is loaded
	if (g_client && g_game_loaded) {
		rc_client_do_frame(g_client);
	}
#endif

	// Periodic debug output every ~5 seconds (300 frames at 60fps)
	if ((g_frames_processed % 300) == 1) {
		time_t now = time(NULL);
		int uptime = (int)(now - g_load_time);
		RA_LOG("POLL: frame=%u processed=%u skipped=%u uptime=%ds",
			frame, g_frames_processed, g_frames_skipped, uptime);

		// Quick RAM summary: print first 16 bytes of each region
		for (int r = 0; r < RA_MAX_REGIONS; r++) {
			const uint8_t *data = ra_ramread_region_data(g_ra_map, r);
			uint16_t size = ra_ramread_region_size(g_ra_map, r);
			if (!data || size == 0) break;

			int n = size < 16 ? size : 16;
			char hex[16 * 3 + 1] = {};
			for (int i = 0; i < n; i++) sprintf(hex + i * 3, "%02X ", data[i]);
			RA_LOG("  Region %d [%u bytes]: %s...", r, size, hex);
		}
	}

	// Detailed dump every ~60 seconds
	if ((g_frames_processed % 3600) == 1 && g_frames_processed > 1) {
		RA_LOG("=== Periodic Full Dump (every ~60s) ===");
		ra_ramread_debug_dump(g_ra_map);
	}
}

void achievements_unload_game(void)
{
        if (!g_active_handler) return;

        RA_LOG("--- Game Unload ---");
        RA_LOG("Stats: %u frames processed, %u skipped", g_frames_processed, g_frames_skipped);

#ifdef HAS_RCHEEVOS
        if (g_client) {
                rc_client_unload_game(g_client);
        }
#endif

        g_active_handler->reset();
        ra_snes_addrlist_init();

        // Disable FPGA query mailbox polling — no game loaded, no need to poll.
        // FPGA will stop after next VBlank (reads RA_ARM_CONFIG_OFFSET once per cycle).
        if (g_ra_map) ra_rtquery_disable(g_ra_map);

        // RetroAchievements safety: clear the DDRAM mirror data area so the next
        // game does not see stale bytes from the unloaded game. The header is
        // preserved; only the payload (offset 0x100+) is wiped.
        if (g_ra_map) {
                uint8_t *base = (uint8_t *)g_ra_map;
                memset(base + 0x100, 0, RA_DDRAM_MAP_SIZE - 0x100);
                RA_LOG("DDRAM mirror data area zeroed at game unload");
        }

        g_game_loaded = 0;
        g_game_load_pending = 0;
        g_game_load_deferred = 0;
        g_last_frame = 0;
        g_mirror_validated = 0;
        g_mirror_confirming = 0;
        g_mirror_initial_frame = 0;
        g_login_deferred = 0;
        g_rom_md5[0] = 0;
        g_rom_path[0] = 0;

        // Clear pending notifications
        s_urgent_head = s_urgent_tail = 0;
        s_urgent_showing  = 0;
        s_instant_pending = 0;
        s_instant_showing = 0;
}

void achievements_notify_core_reset(void)
{
        if (!g_active_handler) return;

        RA_LOG("--- Core Reset ---");

#ifdef HAS_RCHEEVOS
        if (g_client && g_game_loaded) {
                rc_client_reset(g_client);
                RA_LOG("rc_client_reset notified");
        }
#endif

        // Keep the loaded game, but restart runtime/frame tracking state.
        g_last_frame = 0;
        g_first_frame = 0;
        g_frames_processed = 0;
        g_frames_skipped = 0;
        g_load_time = time(NULL);
        g_ach_state_count = 0;

        g_active_handler->reset();
        ra_snes_addrlist_init();

        // Drop stale queued notifications across reset boundaries.
        s_urgent_head = s_urgent_tail = 0;
        s_urgent_showing  = 0;
        s_instant_pending = 0;
        s_instant_showing = 0;

        // Force mirror to re-validate so we don't poll stale DDRAM while the FPGA is in reset
        g_mirror_validated = 0;
        g_mirror_confirming = 0;

        // Clear DDRAM payload so no old RAM is processed during the transition
        if (g_ra_map) {
                uint8_t *base = (uint8_t *)g_ra_map;
                memset(base + 0x100, 0, RA_DDRAM_MAP_SIZE - 0x100);
        }

}

void achievements_deinit(void)
{
	RA_LOG("=== Shutdown ===");

	achievements_unload_game();

#ifdef HAS_RCHEEVOS
	if (g_client) {
		rc_client_destroy(g_client);
		g_client = NULL;
		RA_LOG("rc_client destroyed");
	}
	// Reset auth state — client destroyed, so login is no longer valid
	g_logged_in = 0;
	g_login_pending = 0;
	g_login_deferred = 0;
#endif

	ra_http_deinit();

	if (g_ra_map) {
		// Clear DDRAM magic before unmapping so the next core starts clean
		uint32_t *magic_ptr = (uint32_t *)g_ra_map;
		*magic_ptr = 0;
		RA_LOG("DDRAM magic cleared");
		ra_ramread_unmap(g_ra_map);
		g_ra_map = NULL;
		RA_LOG("DDRAM mirror unmapped");
	}

	ra_log_close();
}

int achievements_active(void)
{
	return g_mirror_validated && g_ra_map != NULL;
}

int achievements_hardcore_active(void)
{
	if (g_force_hardcore) return 1;
	return g_hardcore && g_active_handler && g_active_handler->hardcore_protected;
}

int achievements_stall_recovery_enabled(void)
{
	return g_stall_recovery;
}

int achievements_rtquery_enabled(void)
{
	return g_rtquery_enabled;
}

int achievements_gba_reset_ram(void)
{
	return g_gba_reset_ram;
}

int achievements_recollect_interval(void)
{
	return g_recollect_interval;
}

int achievements_smart_cache_enabled(void)
{
	if (g_smart_cache != -1) return g_smart_cache;

#ifdef HAS_RCHEEVOS
	int cid = ra_get_console_id();
	if (cid == RC_CONSOLE_PLAYSTATION || cid == RC_CONSOLE_NINTENDO || cid == RC_CONSOLE_MEGA_DRIVE || cid == RC_CONSOLE_SUPER_NINTENDO) {
		return 1;
	}
#endif

	return 0;
}

int achievements_n64_snapshot_enabled(void)
{
	return g_n64_snapshot;
}

void achievements_info(void)
{
	if (!ra_core_supported()) {
                Info("RetroAchievements\n\nCore not supported", 2000, 0, 0, 1);
                return;
        }

#ifdef HAS_RCHEEVOS
        if (!ra_has_internet_connectivity()) {
                Info("RetroAchievements\n\nSem internet\nConecte a rede para o RA funcionar", 2500, 0, 0, 1);
                return;
        }
#endif

        char buf[NOTIF_TEXT_MAX];
	int off = 0;
	int remain = sizeof(buf);

	#define NOTIF_APPEND(fmt, ...) do { \
		int n = snprintf(buf + off, remain, fmt, ##__VA_ARGS__); \
		if (n > 0 && n < remain) { off += n; remain -= n; } \
	} while(0)

	NOTIF_APPEND("RetroAchievements\n\n");

#ifdef HAS_RCHEEVOS
	if (!g_client) {
		NOTIF_APPEND("Not initialized");
	} else if (g_login_pending) {
		NOTIF_APPEND("Logging in...");
	} else if (!g_logged_in) {
		NOTIF_APPEND("Not logged in\nCheck %s", RA_CFG_PATH);
	} else {
		const rc_client_user_t *user = rc_client_get_user_info(g_client);
		if (user) {
			NOTIF_APPEND("%s", user->display_name);
		}

		if (g_game_loaded) {
			const rc_client_game_t *game = rc_client_get_game_info(g_client);
			if (game) {
				NOTIF_APPEND("\n%s", game->title);
			}

			// Count unlocked/total achievements
			rc_client_achievement_list_t *list =
				rc_client_create_achievement_list(g_client,
					RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
					RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
			if (list) {
				uint32_t total = 0, unlocked = 0;
				for (uint32_t b = 0; b < list->num_buckets; b++) {
					for (uint32_t a = 0; a < list->buckets[b].num_achievements; a++) {
						total++;
						if (list->buckets[b].achievements[a]->state ==
							RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED)
							unlocked++;
					}
				}
				rc_client_destroy_achievement_list(list);
				NOTIF_APPEND("\n%u/%u unlocked", unlocked, total);
			}
		} else if (g_game_load_pending) {
			NOTIF_APPEND("\nLoading game...");
		} else {
			NOTIF_APPEND("\nNo game loaded");
		}
	}
#else
	NOTIF_APPEND("Diagnostics only\n(no rcheevos lib)");
#endif

	if (g_mirror_validated) {
		NOTIF_APPEND("\nMirror: OK f%u", g_last_frame);
	} else if (g_ra_map) {
		NOTIF_APPEND("\nMirror: waiting");
	}

	#undef NOTIF_APPEND

	Info(buf, 4000, 0, 0, 1);
}

// ---------------------------------------------------------------------------
// Achievement list view (F6 shortcut)
// ---------------------------------------------------------------------------

#ifdef HAS_RCHEEVOS
struct AchViewItem {
	bool is_header;
	bool is_subline;
	const char *text;
	const rc_client_achievement_t *ach;
};
static rc_client_achievement_list_t *g_ach_view_list = nullptr;
static AchViewItem *g_ach_view_items = nullptr;
#endif
static int g_ach_view_first    = 0;
static int g_ach_view_selected = 0;
static int g_ach_view_total    = 0;

int achievements_has_active_game(void)
{
#ifdef HAS_RCHEEVOS
	return g_logged_in && g_game_loaded;
#else
	return 0;
#endif
}

int achievements_list_open(void)
{
	achievements_list_close();
#ifdef HAS_RCHEEVOS
	if (!g_client || !g_logged_in || !g_game_loaded)
		return 0;

	// Use LOCK_STATE grouping to get all achievements, we will categorize them manually
	g_ach_view_list = rc_client_create_achievement_list(g_client,
		RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
		RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
	if (!g_ach_view_list)
		return 0;

	uint32_t total_ach = 0;
	for (uint32_t b = 0; b < g_ach_view_list->num_buckets; b++) {
		total_ach += g_ach_view_list->buckets[b].num_achievements;
	}

	const rc_client_achievement_t** achs_active = new const rc_client_achievement_t*[total_ach];
	const rc_client_achievement_t** achs_prog = new const rc_client_achievement_t*[total_ach];
	const rc_client_achievement_t** achs_locked = new const rc_client_achievement_t*[total_ach];
	const rc_client_achievement_t** achs_unlocked = new const rc_client_achievement_t*[total_ach];
	int n_active = 0, n_prog = 0, n_locked = 0, n_unlocked = 0;

	for (uint32_t b = 0; b < g_ach_view_list->num_buckets; b++) {
		for (uint32_t a = 0; a < g_ach_view_list->buckets[b].num_achievements; a++) {
			const rc_client_achievement_t* ach = g_ach_view_list->buckets[b].achievements[a];
			if (ach->state == RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED) {
				achs_unlocked[n_unlocked++] = ach;
			} else if (ach->bucket == RC_CLIENT_ACHIEVEMENT_BUCKET_ACTIVE_CHALLENGE) {
				achs_active[n_active++] = ach;
			} else if (ach->measured_progress[0] != '\0') {
				achs_prog[n_prog++] = ach;
			} else {
				achs_locked[n_locked++] = ach;
			}
		}
	}

	uint32_t total_items = 0;
	if (n_active > 0) total_items += 1 + n_active;
	if (n_prog > 0) total_items += 1 + (n_prog * 2);
	if (n_locked > 0) total_items += 1 + n_locked;
	if (n_unlocked > 0) total_items += 1 + n_unlocked;

	g_ach_view_items = new AchViewItem[total_items];
	int idx = 0;

	if (n_active > 0) {
		g_ach_view_items[idx].is_header = true;
		g_ach_view_items[idx].is_subline = false;
		g_ach_view_items[idx].text = "Active Challenges";
		g_ach_view_items[idx].ach = nullptr;
		idx++;
		for (int i = 0; i < n_active; i++) {
			g_ach_view_items[idx].is_header = false;
			g_ach_view_items[idx].is_subline = false;
			g_ach_view_items[idx].text = nullptr;
			g_ach_view_items[idx].ach = achs_active[i];
			idx++;
		}
	}
	if (n_prog > 0) {
		g_ach_view_items[idx].is_header = true;
		g_ach_view_items[idx].is_subline = false;
		g_ach_view_items[idx].text = "In Progress";
		g_ach_view_items[idx].ach = nullptr;
		idx++;
		for (int i = 0; i < n_prog; i++) {
			g_ach_view_items[idx].is_header = false;
			g_ach_view_items[idx].is_subline = false;
			g_ach_view_items[idx].text = nullptr;
			g_ach_view_items[idx].ach = achs_prog[i];
			idx++;
			
			g_ach_view_items[idx].is_header = false;
			g_ach_view_items[idx].is_subline = true;
			g_ach_view_items[idx].text = nullptr;
			g_ach_view_items[idx].ach = achs_prog[i];
			idx++;
		}
	}
	if (n_locked > 0) {
		g_ach_view_items[idx].is_header = true;
		g_ach_view_items[idx].is_subline = false;
		g_ach_view_items[idx].text = "Locked";
		g_ach_view_items[idx].ach = nullptr;
		idx++;
		for (int i = 0; i < n_locked; i++) {
			g_ach_view_items[idx].is_header = false;
			g_ach_view_items[idx].is_subline = false;
			g_ach_view_items[idx].text = nullptr;
			g_ach_view_items[idx].ach = achs_locked[i];
			idx++;
		}
	}
	if (n_unlocked > 0) {
		g_ach_view_items[idx].is_header = true;
		g_ach_view_items[idx].is_subline = false;
		g_ach_view_items[idx].text = "Unlocked";
		g_ach_view_items[idx].ach = nullptr;
		idx++;
		for (int i = 0; i < n_unlocked; i++) {
			g_ach_view_items[idx].is_header = false;
			g_ach_view_items[idx].is_subline = false;
			g_ach_view_items[idx].text = nullptr;
			g_ach_view_items[idx].ach = achs_unlocked[i];
			idx++;
		}
	}

	delete[] achs_active;
	delete[] achs_prog;
	delete[] achs_locked;
	delete[] achs_unlocked;

	g_ach_view_total = (int)total_items;
#else
	g_ach_view_total = 0;
#endif
	g_ach_view_first    = 0;
	g_ach_view_selected = 0;
	return g_ach_view_total;
}

void achievements_list_close(void)
{
#ifdef HAS_RCHEEVOS
	if (g_ach_view_items) {
		delete[] g_ach_view_items;
		g_ach_view_items = nullptr;
	}
	if (g_ach_view_list) {
		rc_client_destroy_achievement_list(g_ach_view_list);
		g_ach_view_list = nullptr;
	}
#endif
	g_ach_view_first    = 0;
	g_ach_view_selected = 0;
	g_ach_view_total    = 0;
}

int achievements_list_count(void)
{
	return g_ach_view_total;
}

void achievements_list_scan(int mode)
{
	int total    = g_ach_view_total;
	int osd_size = OsdGetSize();
	if (total == 0) return;

	switch (mode) {
		case SCANF_INIT:
			g_ach_view_selected = 0;
			g_ach_view_first    = 0;
			break;
		case SCANF_END:
			g_ach_view_selected = total - 1;
			g_ach_view_first    = total - osd_size;
			if (g_ach_view_first < 0) g_ach_view_first = 0;
			break;
		case SCANF_NEXT:
			if (g_ach_view_selected < total - 1) {
				g_ach_view_selected++;
				if (g_ach_view_selected >= g_ach_view_first + osd_size)
					g_ach_view_first++;
			}
			break;
		case SCANF_PREV:
			if (g_ach_view_selected > 0) {
				g_ach_view_selected--;
				if (g_ach_view_selected < g_ach_view_first)
					g_ach_view_first--;
			}
			break;
		case SCANF_NEXT_PAGE:
			g_ach_view_selected += osd_size;
			if (g_ach_view_selected >= total) g_ach_view_selected = total - 1;
			g_ach_view_first += osd_size;
			if (g_ach_view_first > total - osd_size) g_ach_view_first = total - osd_size;
			if (g_ach_view_first < 0) g_ach_view_first = 0;
			break;
		case SCANF_PREV_PAGE:
			g_ach_view_selected -= osd_size;
			if (g_ach_view_selected < 0) g_ach_view_selected = 0;
			g_ach_view_first -= osd_size;
			if (g_ach_view_first < 0) g_ach_view_first = 0;
			break;
	}
}

void achievements_list_print(void)
{
	static char s[32];
	int total    = g_ach_view_total;
	int osd_size = OsdGetSize();

	for (int i = 0; i < osd_size; i++) {
		int idx      = g_ach_view_first + i;
		char leftchar = 0;

		if (i == 0 && g_ach_view_first > 0)
			leftchar = 17;
		if (i == osd_size - 1 && (g_ach_view_first + osd_size) < total)
			leftchar = 16;

		if (idx < total) {
#ifdef HAS_RCHEEVOS
			if (g_ach_view_items) {
				const AchViewItem &item = g_ach_view_items[idx];
				if (item.is_header) {
					snprintf(s, 30, "--- %s", item.text ? item.text : "");
					s[29] = 0;
				} else if (item.is_subline) {
					snprintf(s, 30, "    \\-> (%s)", item.ach->measured_progress);
					s[29] = 0;
				} else if (item.ach) {
					const rc_client_achievement_t *ach = item.ach;
					bool unlocked = (ach->state == RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED);
					s[0] = ' ';
					s[1] = unlocked ? 0x1a : 0x1b;
					s[2] = ' ';
					
					strncpy(s + 3, ach->title, 26);
					s[29] = 0;
					int len = (int)strlen(s);
					if (len > 28) {
						s[28] = 22; // continuation marker (more text follows)
						s[29] = 0;
					}
				} else {
					memset(s, ' ', 29);
					s[29] = 0;
				}
			} else {
				memset(s, ' ', 29);
				s[29] = 0;
			}
#else
			memset(s, ' ', 29);
			s[29] = 0;
#endif
		} else {
			memset(s, ' ', 29);
			s[29] = 0;
		}

		OsdWriteOffset(i, s, (idx == g_ach_view_selected), 0, 0, leftchar);
	}
}
