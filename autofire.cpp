#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include "input.h"
#include "autofire.h"
#include "cfg.h"

/*
    New autofire system.
    We take the desired autofire rate in hertz and convert that to a bitmask
    0 == button released
    1 == button pressed
    Input tracks how many frames the button has been held for.
	We use that frame count modulo the autofire cycle length to index into the bitmask.
    most games read their inputs every vsync (or is it vrefresh? once per frame.)

    We display autofire rates to the user as if the game is running at 60hz, but internally
    the rate is scaled to match the game's actual display refresh rate.

    e.g. A PAL game (running at 50hz) with 30hz autofire will internally be toggling the button at 25hz
    actual_refresh / 60 * autofire_rate_hz == real_autofire_rate_hz
*/

#define MAX_AF_CODES 16
#define MAX_AF_RATES 6
#define AF_NAME_LEN 32

// global autofire cycle data.
struct AutofireData {
	char name[AF_NAME_LEN];				// display name
	uint64_t cycle_mask;				// bitmask representing the autofire cycle
	int cycle_length;					// length of the cycle in frames
};

// per-player autofire table; index 0 means disabled.
struct AutofireTable {
	int count; 								// number of entries
	int index[MAX_AF_CODES];				// index of matching autofire rate in autofiredata[]
	uint32_t mask[MAX_AF_CODES];			// bitmask representing which buttons this code represents 
	uint32_t autofirecodes[MAX_AF_CODES]; 	// codes that have autofire set (or codes that had it set, then disabled)
};

static struct AutofireTable autofiretable[NUMPLAYERS]; // tracks autofire state per key per player
static struct AutofireData autofiredata[MAX_AF_RATES]; // global autofire rates/masks/etc.
static struct AutofireData autofiredata_default[MAX_AF_RATES]; // hardcoded fallback rates/masks/etc.
static int num_af_rates_default = 0;
static int num_af_rates = 0;

static void set_autofire_name(struct AutofireData *data, const char *base_name) {
	float hz = data->cycle_length > 0 ? (60.0f / data->cycle_length) : 0.0f;
	if (base_name && base_name[0]) {
		snprintf(data->name, sizeof(data->name), "%s (%.1fhz)", base_name, hz);
	} else {
		snprintf(data->name, sizeof(data->name), "%.1fhz", hz);
	}
}

// returns rate index for code, or 0 if autofire is disabled/unset.
int get_autofire_code_idx(int player, uint32_t code) {
	for (int i = 0; i != autofiretable[player].count; i++)
	{
		if (autofiretable[player].autofirecodes[i] == code)
			return autofiretable[player].index[i];
	}
	return 0;
}

// autofire structs are private to this unit, so we offer a helper to clear them
void clear_autofire(int player) {
	memset(&autofiretable[player], 0, sizeof(AutofireTable));
}

// set autofire index for a code: >0 enable, 0 disable.
void set_autofire_code(int player, uint32_t code, uint32_t mask, int index) {
	for (int i = 0; i != autofiretable[player].count; i++) {
		if (autofiretable[player].autofirecodes[i] == code) {
			autofiretable[player].index[i] = index;
			autofiretable[player].mask[i] = mask;
			return;
		}
	}
	// TODO implement compactification if we run out of slots.
	// presently if user enables/disables too many codes we just stop adding new ones.
	// represented by MAX_AF_CODES.
	if (autofiretable[player].count < MAX_AF_CODES) {
		int idx = autofiretable[player].count++;
		autofiretable[player].autofirecodes[idx] = code;
		autofiretable[player].index[idx] = index;
		autofiretable[player].mask[idx] = mask;
	}
}

// step autofire rate; wrap to disabled at max.
void inc_autofire_code(int player, uint32_t code, uint32_t mask) {
	int index = get_autofire_code_idx(player, code) + 1;
	if (index <= 0) index = 1;
	if (index >= num_af_rates || index < 0) index = 0;
	set_autofire_code(player, code, mask, index);
}

// advance all autofire patterns (run once per frame)
//void autofire_tick() {
//    for (int i = 1; i < num_af_rates; i++)
//    {
        //autofiredata[i].bit = (autofiredata[i].cycle_mask >> autofiredata[i].frame_count) & 1u;
        //if (++(autofiredata[i].frame_count) >= autofiredata[i].cycle_length)
        //    autofiredata[i].frame_count = 0;
//    }
//}

// returns whether the buttons for this code should be held or released this frame.
// (updated every time we call autofire_tick)
bool get_autofire_bit(int player, uint32_t code, uint32_t frame_count) {
	int rate_idx = get_autofire_code_idx(player, code);
	if (rate_idx > 0) {
		return (autofiredata[rate_idx].cycle_mask >> frame_count % autofiredata[rate_idx].cycle_length) & 1u;
	}
	return false;
}

// display-only rate lookup for ui.
const char *get_autofire_rate_hz(int player, uint32_t code) {
	int rate_idx = get_autofire_code_idx(player, code);
	if (rate_idx <= 0) {
		return "disabled";
	}
	if (autofiredata[rate_idx].name[0]) {
		return autofiredata[rate_idx].name;
	}
	return "disabled";
}

bool is_autofire_enabled(int player, uint32_t code) {
	return get_autofire_code_idx(player, code) > 0;
}

/* autofire configuration parsing/loading */

// we accept strings as input so of course this code is about
// 500x longer and more complicated than the actual autofire code

// autofire config parsing lives here to keep cfg.cpp simple.
// accepts comma-separated float rates, or 0b patterns for custom cycles.
// example: "5.0,10.0,0b11001100,15.0".
// extremely long custom patterns could be used to simulate hold/release

// some arcade shooters have pretty odd optimal autofire patterns to manage rank
// let's hide them here for my shmup buddies
static const struct AutofireData autofire_patterns[] = {
	{ "GUNFRONTIER", 0b111100000ULL, 9 },
	{ "GAREGGA", 0b1110000ULL, 7 },
};

// helper for formatting binary literal patterns.
static inline const char *bits_to_str(uint64_t value, int cycle_length) {
    static char buf[65];   // 64 bits + null
    int pos = 0;
    int max_len = cycle_length;
    if (max_len < 0) max_len = 0;
    if (max_len > 64) max_len = 64;

    for (int i = max_len - 1; i >= 0; i--)
        buf[pos++] = (value & (1ULL << i)) ? '1' : '0';

    buf[pos] = '\0';
    return buf;
}

// slot 0 is always "autofire disabled" initialize defensively
static void init_autofire_entry(struct AutofireData *data, uint64_t mask, int length,
	const char *name) {
	if (name) {
		snprintf(data->name, sizeof(data->name), "%s", name);
	} else {
		data->name[0] = '\0';
	}
	data->cycle_length = length;
	data->cycle_mask = mask;
	if (data->cycle_length > 64) data->cycle_length = 64;
	if (data->cycle_length < 1) data->cycle_length = 1;
}

static inline struct AutofireData mask_from_hertz(double hz_target)
{
    struct AutofireData p = {{0}, 0, 0};

    if (hz_target <= 0.0) return p;
    if (hz_target > 30.0) hz_target = 30.0;

    int P = (int)ceil(60.0 / hz_target);
    if (P < 2) P = 2;
    if (P > 64) P = 64;

    p.cycle_length = P;

    int W = P / 2;
    if (W < 1) W = 1;
    if (W >= P) W = P - 1;

    if (W == 64)
        p.cycle_mask = ~0ull;
    else
        p.cycle_mask = (1ull << W) - 1ull;

    return p;
}

static void init_default_autofire_data() {
	const float default_rates[] = { 10.0f, 15.0f, 30.0f };
	int count = 0;

	init_autofire_entry(&autofiredata_default[count++], 1, 1, NULL);

	for (size_t i = 0; i < (sizeof(default_rates) / sizeof(default_rates[0])); i++) {
		if (count >= MAX_AF_RATES) break;
		struct AutofireData p = mask_from_hertz(default_rates[i]);
		init_autofire_entry(&autofiredata_default[count], p.cycle_mask,
			p.cycle_length ? p.cycle_length : 1, NULL);
		set_autofire_name(&autofiredata_default[count], NULL);
		count++;
	}
	num_af_rates_default = count;
}

// parse a 0b... binary pattern; every digit is a frame, up to 64
// some interesting stuff could be done with longer patterns (hold/release)
static bool parse_autofire_literal(const char *token, uint64_t *mask_out, int *len_out) {
	if (!token || token[0] != '0' || !token[1]) return false;

	const char *p = token + 2;
	uint64_t mask = 0;
	int len = 0;

	if (token[1] != 'b' && token[1] != 'B') return false;
	if (!*p) return false;

	for (const char *c = p; *c; c++) {
		if (*c != '0' && *c != '1') return false;
		if (len >= 64) return false;
		mask = (mask << 1) | (uint64_t)(*c - '0');
		len++;
	}

	*mask_out = mask;
	*len_out = len;
	return true;
}

static void build_autofire_data(const char *rates, struct AutofireData *out, int *out_count) {
	char *token;
	int count = 0;
	char cfg_string[256] = {};

	init_autofire_entry(&out[count++], 1, 1, NULL);

	snprintf(cfg_string, sizeof(cfg_string), "%s", rates);

	token = strtok(cfg_string, ",");
	while (token && count < MAX_AF_RATES) {
		bool handled = false;
		for (size_t i = 0; i < (sizeof(autofire_patterns) / sizeof(autofire_patterns[0])); i++) {
			if (!strcasecmp(token, autofire_patterns[i].name)) {
				init_autofire_entry(&out[count], autofire_patterns[i].cycle_mask,
					autofire_patterns[i].cycle_length, autofire_patterns[i].name);
				set_autofire_name(&out[count], autofire_patterns[i].name);
				count++;
				handled = true;
				break;
			}
		}
		if (handled) {
			token = strtok(NULL, ",");
			continue;
		}
		uint64_t literal_mask = 0;
		int literal_len = 0;
		if (parse_autofire_literal(token, &literal_mask, &literal_len)) {
			// use literal directly (allows arbitrary press/release cycles)
			init_autofire_entry(&out[count], literal_mask, literal_len, "custom");
			set_autofire_name(&out[count], "custom");
			count++;
			token = strtok(NULL, ",");
			continue;
		}
		// pass through to float processing
		char *endptr = NULL;
		float f = strtof(token, &endptr);

		// reject 0.0 and values > 30.0
		if (endptr && endptr != token && *endptr == '\0' && f > 0.0f && f <= 30.0f) {
			struct AutofireData p = mask_from_hertz(f);
			init_autofire_entry(&out[count], p.cycle_mask,
				p.cycle_length ? p.cycle_length : 1, NULL);
			set_autofire_name(&out[count], NULL);
			count++;
		}
		token = strtok(NULL, ",");
	}
	*out_count = count;
}

// parse config; fall back to defaults if no valid rates remain.
// always returns true to indicate some rate set is loaded.
bool parse_autofire_cfg() {
	printf("[AUTOFIRE INITIALIZATION]\n");
	static bool default_ready = false;
	if (!default_ready) {
		init_default_autofire_data();
		default_ready = true;
	}

	struct AutofireData parsed[MAX_AF_RATES] = {};
	int valid_count = 0;
	build_autofire_data(cfg.autofire_rates, parsed, &valid_count);

	if (valid_count > 1) {
		memcpy(autofiredata, parsed, sizeof(parsed));
		num_af_rates = valid_count;
	} else {
		memcpy(autofiredata, autofiredata_default, sizeof(autofiredata_default));
		num_af_rates = num_af_rates_default;
	}

	if (valid_count <= 1) {
		printf("Autofire configuration in .ini invalid, using default rates:\n");
	}
	
	printf("Number of autofire rates found: %d\n", num_af_rates - 1);
	for (int i = 1; i != num_af_rates; i++) {
		printf("%s, bitmask %s, cycle length %u\n",
			autofiredata[i].name[0] ? autofiredata[i].name : "custom",
			bits_to_str(autofiredata[i].cycle_mask, autofiredata[i].cycle_length),
			autofiredata[i].cycle_length);
	}
	return true;
}
