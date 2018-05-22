// hdd.h

#ifndef __MINIMIG_HDD_H__
#define __MINIMIG_HDD_H__

#include "minimig_hdd_internal.h"

// functions
void HandleHDD(unsigned char c1, unsigned char c2);
unsigned char OpenHardfile(unsigned char unit);
int checkHDF(const char* name, struct RigidDiskBlock **rdb);

#endif // __HDD_H__
