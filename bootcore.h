// bootcore.h
// 2019, Aitor Gomez Garcia (spark2k06@gmail.com)
// Thanks to Sorgelig and BBond007 for their help and advice in the development of this feature.

#ifndef __BOOTCORE_H__
#define __BOOTCORE_H__

void bootcore_init(const char *path);
void bootcore_record_file(const char *path);
void bootcore_load_file();
void bootcore_cancel();

void bootcore_launch();
bool bootcore_pending();
bool bootcore_ready();

unsigned int bootcore_remaining();
unsigned int bootcore_delay();

const char *bootcore_type();
const char *bootcore_name();

#endif // __BOOTCORE_H__
