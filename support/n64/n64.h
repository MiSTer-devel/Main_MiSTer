#ifndef N64_H
#define N64_H

#include <stdint.h>
#include <stdio.h>

#include "../../cfg.h"
#include "../../cheats.h"
#include "../../file_io.h"
#include "../../input.h"
#include "../../user_io.h"

struct N64SaveFile;

void n64_reset();
void n64_poll();
void n64_cheats_send(const void* buf_ptr, const uint32_t size);
int n64_rom_tx(const char* name, const unsigned char index, const uint32_t load_addr, uint32_t& file_crc);
bool n64_process_save(const bool use_save, const int op, const uint64_t sector_index, const uint32_t sector_size, const int ack_flags, uint64_t& confirmed_sector, uint8_t* buffer, const uint32_t buffer_capacity, uint32_t chunk_size);

#endif