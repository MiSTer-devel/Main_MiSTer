#ifndef PC88_H
#define PC88_H

#include <stdint.h>

struct fileTYPE;

void pc88_mount(int index, fileTYPE *f, const char *name, int image, int *writable);
uint64_t pc88_size(int index, uint64_t size);
int pc88_sd_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack);

#endif
