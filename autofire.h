#include <stdint.h>
#include <linux/input.h>

// structures and functions to track autofire
#define MAX_AF_CODES 64
#define MAX_AF_RATES 6

struct AutofireTable {
	uint32_t mask[KEY_CNT]; 	// mask of specific code
	int index[KEY_CNT];			// index of specific code in autofire rates table
	int frame_count[KEY_CNT];	// current frame in mask cycle
	int count; 					// number of entrires in autofirecodes[]
	int autofirecodes[MAX_AF_CODES]; // codes that have autofire set (or codes that had it set, then disabled)
};

struct Autofire {
	int cycle_mask;
	int cycle_length;
	float rate_hz;
	int bit;
	int frame_count;
};

int get_autofire_code_idx(int player, int code);
void inc_autofire_code(int player, uint32_t code, uint32_t mask);
void autofire_tick();
uint32_t get_autofire_mask(int player);
bool parse_autofire_cfg();