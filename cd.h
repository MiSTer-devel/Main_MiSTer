#ifndef CD_H
#define CD_H

#include <libchdr/chd.h>
#include "file_io.h"


typedef enum
{
        SUBCODE_NONE = 0, SUBCODE_RW, SUBCODE_RW_RAW
} cd_subcode_types_t;

typedef struct
{
	fileTYPE f;
	int offset;
	int pregap;
	int start;
	int end;
	int type;
	int sector_size;
	int index1;
	cd_subcode_types_t sbc_type;
} cd_track_t;

typedef struct
{
	int end;
	int last;
	int sectorSize;
	chd_file *chd_f;
	cd_track_t tracks[100];
	fileTYPE sub;

	int GetTrackByLBA(int lba)
	{
		int i = 0;
		while ((this->tracks[i].end <= lba) && (i < this->last)) i++;
		return i;
	}
} toc_t;

typedef struct
{
	uint8_t m;
	uint8_t s;
	uint8_t f;
} msf_t;

#define BCD(v)				 ((uint8_t)((((v)/10) << 4) | ((v)%10)))

typedef int (*SendDataFunc) (uint8_t* buf, int len, uint8_t index);

#endif
