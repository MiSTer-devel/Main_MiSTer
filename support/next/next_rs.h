#ifndef NEXT_RS_H
#define NEXT_RS_H

#include <stdint.h>

#define NEXT_RS_DATA 1024
#define NEXT_RS_DISK 1296

void next_rs_encode(uint8_t *sector);
int  next_rs_decode(uint8_t *sector);

#endif
