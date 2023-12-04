#ifndef N64_H
#define N64_H

#include <stdint.h>
#include "../../file_io.h"

int n64_rom_tx(const char* name, unsigned char index);
void n64_load_savedata(uint64_t lba, int ack, uint64_t& buffer_lba, uint8_t* buffer, uint32_t buffer_size, uint32_t blksz, uint32_t sz);
void n64_save_savedata(uint64_t lba, int ack, uint64_t& buffer_lba, uint8_t* buffer, uint32_t blksz, uint32_t sz);

#endif