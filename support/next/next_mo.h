#ifndef NEXT_MO_H
#define NEXT_MO_H

#include <stdint.h>

#define NEXT_MO_ECC_BLK    0x7E000000u
#define NEXT_MO_ECC_ENCODE 1
#define NEXT_MO_ECC_DECODE 2
#define NEXT_MO_ECC_BYTES  1296
#define NEXT_MO_ECC_FAIL   1296
#define NEXT_MO_ECC_COUNT  1297

void next_mo_command(uint32_t lba, const uint8_t *buf, int sz);
void next_mo_fill(uint32_t lba, uint8_t *buf, int sz);

#endif
