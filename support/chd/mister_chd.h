#ifndef MISTER_CHD_INCLUDED
#define MISTER_CHD_INCLUDED

#include <libchdr/chd.h>
#include <libchdr/cdrom.h>
#include "../../cd.h"

struct chd_prefetch;

chd_prefetch *mister_chd_prefetch_create(chd_file *chd_f, uint32_t hunkbytes);
void mister_chd_prefetch_destroy(chd_prefetch **pf);

chd_error mister_chd_read_sector(chd_file *chd_f, int lba, uint32_t d_offset, uint32_t s_offset, int length, uint8_t *destbuf, uint8_t *hunkbuf, int *hunknum, chd_prefetch *pf = nullptr);
chd_error mister_load_chd(const char *filename, toc_t *cd_toc);

#endif
