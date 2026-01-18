#include "autofire.h"
#include "input.h"
#include "cfg.h"
#include <math.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

/*
    New autofire system.
    We take the desired autofire rate in hertz and convert that to a bitmask
    0 == button released
    1 == button pressed
    We advance through a single bit of this mask every frame on the assumption that
    most games read their inputs every vsync (or is it vrefresh? once per frame.)

    We display autofire rates to the user as if the game is running at 60hz, but internally
    the rate is scaled to match the game's actual display refresh rate.

    e.g. A PAL game (running at 50hz) with 30hz autofire will internally be toggling the button at 25hz
    actual_refresh / 60 * autofire_rate_hz == real_autofire_rate_hz
*/

struct AutofireTable autofiretable[NUMPLAYERS];
struct Autofire autofire[MAX_AF_RATES];
extern int ref_count[NUMPLAYERS][NUMBUTTONS];
extern int key_state[NUMPLAYERS][KEY_CNT];

static int num_af_rates = 0;

// returns index into autofire rates array
// if a code isn't in the table at all, we return a 0
// the index could also be reset to zero
int get_autofire_code_idx(int player, int code) {
	for (int i = 0; i != autofiretable[player].count; i++)
	{
		if (autofiretable[player].autofirecodes[i] == code)
			return autofiretable[player].index[code];
	}
	return 0;
}

// set to non-zero value to enable autofire
// index 0 == autofire disabled.
// index >0 == matching rate in af_ arrays.
void set_autofire_code(int player, int code, uint32_t mask, int index)
{
	bool found = false;
	for (int i = 0; i != autofiretable[player].count; i++) {
		if (autofiretable[player].autofirecodes[i] == code) {
			found = true;
		}
	}
	if (!found && autofiretable[player].count < MAX_AF_CODES) {
		autofiretable[player].autofirecodes[autofiretable[player].count++] = code;
		found = true;
	}
	if (found) {
		autofiretable[player].index[code] = index;
		autofiretable[player].mask[code] = mask;
		autofiretable[player].frame_count[code] = 0;
	}
	else return;
}

// increase to next autofire rate, or if we're at the last
// rate, disable autofire
void inc_autofire_code(int player, uint32_t code, uint32_t mask)
{
	int index = get_autofire_code_idx(player, code) + 1;
	if (index >= num_af_rates || index < 0) index = 0;
	set_autofire_code(player, code, mask, index);
}

void autofire_tick()
{
    // should be called once per frame
    // advance all autofire patterns by 1
    for (int j = 1; j <= num_af_rates; j++)
    {
        autofire[j].bit = (autofire[j].cycle_mask >> autofire[j].frame_count) & 1u;
        if (++(autofire[j].frame_count) >= autofire[j].cycle_length)
            autofire[j].frame_count = 0;
    }
}

uint32_t get_autofire_mask(int player)
{
    // returns a pattern such as: 0011 for two frames on, two frames off.
    uint32_t autofiremask = 0xFFFFFF;
    for (int j = 0; j != autofiretable[player].count; j++) {
		int code = autofiretable[player].autofirecodes[j];
        if (key_state[player][code] && get_autofire_code_idx(player, code)) {  // does this physical key have autofire enabled?
            uint8_t rate_idx = autofiretable[player].index[code];
            printf("rate index: %d\n", rate_idx);
            uint32_t mask = autofiretable[player].mask[code];
            while (mask) {
                int bit_index = __builtin_ctz(mask);   // index of lowest set bit
                if (ref_count[player][bit_index] == 1) autofiremask = (autofiremask & ~(autofire[rate_idx].bit << bit_index));
                mask &= (mask - 1);
            }
        }
    }
    return autofiremask;
}

static inline const char *bits_to_str(uint32_t value, int cycle_length) {
    static char buf[70];   // enough for 64 bits + null
    int pos = 0;

    for (int i = cycle_length - 1; i >= 0; i--)
        buf[pos++] = (value & (1ULL << i)) ? '1' : '0';

    buf[pos] = '\0';
    return buf;
}

bool parse_autofire_cfg() {
	char *token;
	int count = 0;
	char cfg_string[256] = {};
	
	autofire[count].rate_hz = 0;
	autofire[count].cycle_length = 1;
	autofire[count].cycle_mask = 1;
	autofire[count].bit = 1;
	count++;

	snprintf(cfg_string, sizeof(cfg_string), "%s", cfg.autofire_rates);

	token = strtok(cfg_string, ",");
	while (token && count < MAX_AF_RATES) {
		// check for binary literal prefix
		if ((token[0] == '0') && (token[1] == 'b' || token[1] == 'B')) {
			// parse binary (skip "0b")
			uint32_t mask = 0;
			const char *p = token + 2;

			int valid = 1;

			while (*p) {
				if (*p == '0' || *p == '1') {
					mask = (mask << 1) | (*p - '0');
				}
				else {
					valid = 0;  // invalid binary char
					break;
				}
				p++;
			}

			if (valid) {
				// use binary literal directly
                // allows for arbitrary button press/release cycle
				autofire[count].rate_hz = 99.9f;
				autofire[count].cycle_mask = mask;
				autofire[count].cycle_length = p - (token + 2);
				count++;
			}

			token = strtok(NULL, ",");
			continue;
		}
		// pass through to float processing
		float f = strtof(token, NULL);

		// Reject 0.0 and values > 30.0
		if (f > 0.0f && f <= 30.0f) {
			autofire[count++].rate_hz = f;
		}

		token = strtok(NULL, ",");
	}

	num_af_rates = count;

	// take the requested rate in hz and generate a bitmask representing the pattern of button
	// press/release for each frame and the length of the cycle in frames
	// will choose the rate closest to requested that evenly divides into 60
	// e.g. 15hz has a bitmask of 0011 and cycle length of 4 frames (hold button for two frames,
	// release button for two frames)

	for (int i = 1; i != num_af_rates; i++) {
		if (autofire[i].rate_hz != 99.9f) {
			autofire[i].bit = 1;
			autofire[i].cycle_length = (int)lround(60 / autofire[i].rate_hz);
			autofire[i].rate_hz = 60.0 / autofire[i].cycle_length; // we rounded so update to actual rate
			int on_frames = autofire[i].cycle_length / 2;
			for (int j = 0; j != on_frames; j++) {
				autofire[i].cycle_mask |= (1 << j);
			}
		}
	}

	printf("Number of autofire rates found: %d\n", num_af_rates);
	for (int i = 1; i != num_af_rates; i++) {
		if (autofire[i].rate_hz == 99.9f) {
			printf("user-defined, bitmask %s, cycle length %u\n", bits_to_str(autofire[i].cycle_mask, autofire[i].cycle_length), autofire[i].cycle_length);
		}
		else {
			printf("%.2fhz, bitmask %s, cycle length %u\n", autofire[i].rate_hz, bits_to_str(autofire[i].cycle_mask, autofire[i].cycle_length), autofire[i].cycle_length);
		}
	}
	return true;
}