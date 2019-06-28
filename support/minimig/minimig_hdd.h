// hdd.h

#ifndef __MINIMIG_HDD_H__
#define __MINIMIG_HDD_H__

#include "minimig_hdd_internal.h"

// functions
void HandleHDD(uint8_t c1, uint8_t c2);
uint8_t OpenHardfile(uint8_t unit);
int checkHDF(const char* name, struct RigidDiskBlock **rdb);

#endif // __HDD_H__
