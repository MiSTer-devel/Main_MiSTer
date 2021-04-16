
#include <stdint.h>

#ifndef SHMEM_H
#define SHMEM_H

void *shmem_map(uint32_t address, uint32_t size);
int shmem_unmap(void* map, uint32_t size);
int shmem_put(uint32_t address, uint32_t size, void *buf);
int shmem_get(uint32_t address, uint32_t size, void *buf);

#define fpga_mem(x) (0x20000000 | ((x) & 0x1FFFFFFF))
#endif
