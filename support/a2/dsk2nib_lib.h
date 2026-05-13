#ifndef DSK2NIB_LIB_H
#define DSK2NIB_LIB_H

#include <stdint.h>

typedef unsigned char uchar;

// Library function for on-demand DSK to NIB conversion
void a2_readDsk2Nib(fileTYPE*fd, uint64_t offset, uchar *byte);

void a2_writeDSK(int disk, fileTYPE* idx, uint64_t lba, int ack);
void a2_readDSK(fileTYPE* idx, uint64_t lba, int ack);


#endif
