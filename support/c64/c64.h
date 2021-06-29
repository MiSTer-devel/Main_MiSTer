#ifndef C64_H
#define C64_H

int c64_openT64(const char *path, fileTYPE* f);

int c64_openGCR(const char *path, fileTYPE *f, int idx);
void c64_closeGCR(int idx);

void c64_readGCR(int idx, uint8_t track);
void c64_writeGCR(int idx, uint8_t track);

#endif
