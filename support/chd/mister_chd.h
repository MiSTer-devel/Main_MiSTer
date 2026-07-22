#ifndef MISTER_CHD_INCLUDED
#define MISTER_CHD_INCLUDED

#include <libchdr/chd.h>
#include <libchdr/cdrom.h>
#include "../../cd.h"

// Opaque decompress-ahead context. One per drive (IDE can have two CD drives
// mounted at once, so this cannot be global state).
struct chd_prefetch;

// hunkbytes must match the size of the hunkbuf passed to mister_chd_read_sector
// (i.e. toc_t::chd_hunksize). Returns NULL on allocation failure, which is not
// fatal -- callers pass NULL through and get the original synchronous path.
chd_prefetch *mister_chd_prefetch_create(chd_file *chd_f, uint32_t hunkbytes);

// Waits for any in-flight decompress to finish before freeing, then NULLs the
// caller's pointer. Must be called before the chd_file is closed.
void mister_chd_prefetch_destroy(chd_prefetch **pf);

chd_error mister_chd_read_sector(chd_file *chd_f, int lba, uint32_t d_offset, uint32_t s_offset, int length, uint8_t *destbuf, uint8_t *hunkbuf, int *hunknum, chd_prefetch *pf = nullptr);
chd_error mister_load_chd(const char *filename, toc_t *cd_toc);

#endif
