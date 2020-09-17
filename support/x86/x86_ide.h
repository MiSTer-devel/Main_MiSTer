#ifndef X86_IDE_H
#define X86_IDE_H

#include "x86.h"

void x86_ide_set(uint32_t num, uint32_t baseaddr, fileTYPE *f, int ver);
void x86_ide_io(int num, int req);
int x86_ide_is_placeholder(int num);
void x86_ide_reset();

#endif
