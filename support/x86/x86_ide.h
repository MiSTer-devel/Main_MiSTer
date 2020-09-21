#ifndef X86_IDE_H
#define X86_IDE_H

#include "x86.h"

const char * x86_ide_parse_cd(uint32_t num, const char *filename);
void x86_ide_set(uint32_t num, uint32_t baseaddr, fileTYPE *f, int ver, int cd);
void x86_ide_io(int num, int req);
int x86_ide_is_placeholder(int num);
void x86_ide_reset(uint8_t hotswap);

#endif
