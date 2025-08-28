#ifndef C64_H
#define C64_H

#define G64_BLOCK_COUNT_1541 31
#define G64_BLOCK_COUNT_1571 52

#define G64_SUPPORT_DS    1
#define G64_SUPPORT_GCR   2
#define G64_SUPPORT_MFM   4
#define G64_SUPPORT_HD    8

int c64_openT64(const char *path, fileTYPE* f);

int c64_openGCR(const char *path, fileTYPE *f, int idx);
void c64_closeGCR(int idx);

void c64_readGCR(int idx, uint64_t lba, uint32_t blks);
void c64_writeGCR(int idx, uint64_t lba, uint32_t blks);

void c64_open_file(const char* name, unsigned char index);
void c64_save_cart(int open_menu);

#endif
