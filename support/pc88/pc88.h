#ifndef PC88_H
#define PC88_H

#include <stdint.h>

struct fileTYPE;

void pc88_mount(int index);
int pc88_sd_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack);

#endif
