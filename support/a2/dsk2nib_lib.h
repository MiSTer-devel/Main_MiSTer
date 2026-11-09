#ifndef DSK2NIB_LIB_H
#define DSK2NIB_LIB_H

#include <stdint.h>

typedef unsigned char uchar;

// On-the-fly 16-sector image (.dsk/.do/.po) <-> NIB track service for cores
// that read 6656-byte NIB tracks as 13 x 512-byte SD blocks (TK2000, legacy
// Apple-II floppy path, Oric/Pravetz A2 slots). 'index' is the SD slot,
// 'prodos_order' is 1 for .po images.
void a2_set_image(int index, int prodos_order);

void a2_writeDSK(fileTYPE* idx, uint64_t lba, int ack, int index);
void a2_readDSK(fileTYPE* idx, uint64_t lba, int ack, int index);

#endif
