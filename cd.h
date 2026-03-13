#ifndef CD_H
#define CD_H

#include <libchdr/chd.h>
#include "file_io.h"


typedef enum
{
        SUBCODE_NONE = 0, SUBCODE_RW, SUBCODE_RW_RAW
} cd_subcode_types_t;

/// Values according to the raw value in TOC A0
enum DiscType {
	DT_CDDA = 0x00,
	DT_CDROM = 0x00,
	DT_CDI = 0x10,
	DT_CDROMXA = 0x20,
};

enum TrackType {
	TT_CDDA,
	TT_MODE1,
	TT_MODE2,
};

typedef struct
{
	fileTYPE f;
	int offset;
	int pregap;
	int start;
	int end;
	enum TrackType type;
	int sector_size;
	int indexes[100];
	int index_num;
	cd_subcode_types_t sbc_type;
} cd_track_t;

typedef struct
{
	int end;
	int last;
	int sectorSize;
	chd_file *chd_f;
	int chd_hunksize;
	cd_track_t tracks[100];
	fileTYPE sub;

	int GetTrackByLBA(int lba)
	{
		int i = 0;
		while ((this->tracks[i].end <= lba) && (i < this->last)) i++;
		return i;
	}

	int GetIndexByLBA(int track, int lba)
	{
		if (lba - this->tracks[track].start < 0) 
			return 0;

		int i = 2;
		while ((lba - this->tracks[track].start >= this->tracks[track].indexes[i]) && (i < this->tracks[track].index_num)) i++;
		i--;
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
