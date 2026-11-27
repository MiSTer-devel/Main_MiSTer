#ifndef MISTER_CHD_INCLUDED
#define MISTER_CHD_INCLUDED

#include <libchdr/chd.h>
#include <libchdr/cdrom.h>
#include "../../cd.h"

chd_error mister_chd_read_sector(chd_file *chd_f, int lba, uint32_t d_offset, uint32_t s_offset, int length, uint8_t *destbuf, uint8_t *hunkbuf, int *hunknum);
chd_error mister_load_chd(const char *filename, toc_t *cd_toc);

struct chd_hdd_file;
chd_error mister_chd_hdd_open(FILE *file, const char *path, bool writable, chd_hdd_file **disk, uint64_t *size);
void mister_chd_hdd_close(chd_hdd_file *disk);
bool mister_chd_hdd_writable(chd_hdd_file *disk);
chd_error mister_chd_hdd_read(chd_hdd_file *disk, uint64_t offset, void *buffer, uint32_t length);
chd_error mister_chd_hdd_write(chd_hdd_file *disk, uint64_t offset, const void *buffer, uint32_t length);

#endif
