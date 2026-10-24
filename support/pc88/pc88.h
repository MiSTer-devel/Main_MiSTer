#ifndef PC88_H
#define PC88_H

#include <stdint.h>

struct fileTYPE;

int pc88_disk_count(int index, const char *path);
const char *pc88_disk_name(int image);

int pc88_mount(int index, fileTYPE *f, const char *name, int image, int *writable);
uint64_t pc88_size(int index, uint64_t size);
int pc88_sd_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack);
const char *pc88_slot_label(int index, const char *file);

#endif
