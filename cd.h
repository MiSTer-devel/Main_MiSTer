#ifndef CD_H
#define CD_H

typedef struct
{
	fileTYPE f;
	int offset;
	int start;
	int end;
	int type;
	int sector_size;
} track_t;

typedef struct
{
	int end;
	int last;
	track_t tracks[100];
//	fileTYPE sub;
} toc_t;

typedef struct
{
	uint8_t m;
	uint8_t s;
	uint8_t f;
} msf_t;


typedef int (*SendDataFunc) (uint8_t* buf, int len, uint8_t index);

#endif