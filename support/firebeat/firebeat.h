#ifndef FIREBEAT_H
#define FIREBEAT_H

#include <stdint.h>
#include "../../file_io.h"

char is_firebeat();
int  firebeat_mount_hook(int index, const char *name, fileTYPE *f, int *writable);
void firebeat_unmount(int index);
// 0: not a Firebeat CHD slot; 1: served; -1: refused
int  firebeat_sd_service(int disk, fileTYPE *f, int op, uint64_t lba, int sz, int ack);

#endif
