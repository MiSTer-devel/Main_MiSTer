#ifndef __DISKIMAGE_H
#define __DISKIMAGE_H

#include "file_io.h"

int dsk2nib(const char *name, fileTYPE *f);
int x2trd(const char *name, fileTYPE *f);
int x2trd_ext_supp(const char *name);

//-----------------------------------------------------------------------------
#endif
