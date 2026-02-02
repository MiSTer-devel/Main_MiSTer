#ifndef AUTOFIRE_H
#define AUTOFIRE_H

#include <stdint.h>

const char *get_autofire_rate_hz(int player, uint32_t code);
bool is_autofire_enabled(int player, uint32_t code);
void clear_autofire(int player);
void inc_autofire_code(int player, uint32_t code, uint32_t mask);
//void autofire_tick();
bool parse_autofire_cfg();
bool get_autofire_bit(int player, uint32_t code, uint32_t frame_count);

#endif
