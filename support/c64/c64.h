#ifndef C64_H
#define C64_H

#define G64_BLOCK_COUNT_1541 31
#define G64_BLOCK_COUNT_1571 52

int c64_openT64(const char *path, fileTYPE* f);

int c64_openGCR(const char *path, fileTYPE *f, int idx);
void c64_closeGCR(int idx);

void c64_readGCR(int idx, uint64_t lba, uint32_t blks);
void c64_writeGCR(int idx, uint64_t lba, uint32_t blks);

#endif
