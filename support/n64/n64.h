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
void n64_cheats_send(const void* buf_addr, const uint32_t size);
int n64_rom_tx(const char* name, unsigned char index, uint32_t load_addr, uint32_t& file_crc);
void n64_load_savedata(uint64_t lba, int ack, uint64_t& buffer_lba, uint8_t* buffer, uint32_t buffer_size, uint32_t blksz, uint32_t sz);
void n64_save_savedata(uint64_t lba, int ack, uint64_t& buffer_lba, uint8_t* buffer, uint32_t blksz, uint32_t sz);

#endif