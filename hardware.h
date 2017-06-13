#ifndef HARDWARE_H
#define HARDWARE_H

#include <inttypes.h>
#include <stdio.h>

#define iprintf(...)  printf(__VA_ARGS__) 
#define siprintf(...) sprintf(__VA_ARGS__) 
#define BootPrint(text) iprintf("%s\n", text)

unsigned long GetTimer(unsigned long offset);
unsigned long CheckTimer(unsigned long t);
void WaitTimer(unsigned long time);

void hexdump(void *data, uint16_t size, uint16_t offset);

// minimig reset stuff
#define SPI_RST_USR         0x1
#define SPI_RST_CPU         0x2
#define SPI_CPU_HLT         0x4
extern uint8_t rstval;

#endif // HARDWARE_H
