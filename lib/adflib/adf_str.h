#ifndef _ADF_STR_H
#define _ADF_STR_H 1

/*
 *  ADF Library. (C) 1997-2002 Laurent Clevy
 *
 *  adf_str.h
 *
 *  $Id$
 *
 *  structures/constants definitions
 *
 *  This file is part of ADFLib.
 *
 *  ADFLib is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  ADFLib is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  aint32_t with Foobar; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include<stdio.h>

#include"adf_defs.h"
#include"adf_blk.h"
#include"adf_err.h"

/* ----- VOLUME ----- */

struct Volume {
    struct Device* dev;

    SECTNUM firstBlock;     /* first block of data area (from beginning of device) */
    SECTNUM lastBlock;      /* last block of data area  (from beginning of device) */
    SECTNUM rootBlock;      /* root block (from firstBlock) */

    char dosType;           /* FFS/OFS, DIRCACHE, INTERNATIONAL */
    BOOL bootCode;
    BOOL readOnly;
    int datablockSize;      /* 488 or 512 */
    int blockSize;			/* 512 */

    char *volName;

    BOOL mounted;

    int32_t bitmapSize;             /* in blocks */
    SECTNUM *bitmapBlocks;       /* bitmap blocks pointers */
    struct bBitmapBlock **bitmapTable;
    BOOL *bitmapBlocksChg;

    SECTNUM curDirPtr;
};


struct Partition {
    int32_t startCyl;
    int32_t lenCyl;
    char* volName;
    int volType;
};

/* ----- DEVICES ----- */

#define DEVTYPE_FLOPDD 		1
#define DEVTYPE_FLOPHD 		2
#define DEVTYPE_HARDDISK 	3
#define DEVTYPE_HARDFILE 	4

struct Device {
    int devType;               /* see below */
    BOOL readOnly;
    int32_t size;                 /* in bytes */

    int nVol;                  /* partitions */
    struct Volume** volList;  
	
    int32_t cylinders;            /* geometry */
    int32_t heads;
    int32_t sectors;

    BOOL isNativeDev;
    void *nativeDev;
};


/* ----- FILE ----- */

struct File {
    struct Volume *volume;

    struct bFileHeaderBlock* fileHdr;
    void *currentData;
    struct bFileExtBlock* currentExt;

    int32_t nDataBlock;
    SECTNUM curDataPtr;
    uint32_t pos;

    int posInDataBlk;
    int posInExtBlk;
    BOOL eof, writeMode;
    };


/* ----- ENTRY ---- */

struct Entry{
    int type;
    char* name;
    SECTNUM sector;
    SECTNUM real;
    SECTNUM parent;
    char* comment;
    uint32_t size;
    int32_t access;
    int year, month, days;
    int hour, mins, secs;
};

struct CacheEntry{
    int32_t header, size, protect;
    short days, mins, ticks;
    signed char type;
    char nLen, cLen;
    char name[MAXNAMELEN+1], comm[MAXCMMTLEN+1];
/*    char *name, *comm;*/

};




struct DateTime{
    int year,mon,day,hour,min,sec;
};

/* ----- ENVIRONMENT ----- */

#define PR_VFCT			1
#define PR_WFCT			2
#define PR_EFCT			3
#define PR_NOTFCT		4
#define PR_USEDIRC		5
#define PR_USE_NOTFCT 	6
#define PR_PROGBAR 		7
#define PR_USE_PROGBAR 	8
#define PR_RWACCESS 	9
#define PR_USE_RWACCESS 10

struct Env{
    void (*vFct)(char*);       /* verbose callback function */
    void (*wFct)(char*);       /* warning callback function */
    void (*eFct)(char*);       /* error callback function */

    void (*notifyFct)(SECTNUM, int);
    BOOL useNotify;

    void (*rwhAccess)(SECTNUM,SECTNUM,BOOL);
    BOOL useRWAccess;

    void (*progressBar)(int);
    BOOL useProgressBar;

    BOOL useDirCache;
	
    void *nativeFct;
};



struct List{         /* generic linked tree */
    void *content;
    struct List* subdir;
    struct List* next;
};

struct GenBlock{
    SECTNUM sect;
    SECTNUM parent;
    int type;
    int secType;
    char *name;	/* if (type == 2 and (secType==2 or secType==-3)) */
};

struct FileBlocks{
    SECTNUM header;
    int32_t nbExtens;
    SECTNUM* extens;
    int32_t nbData;
    SECTNUM* data;
};

struct bEntryBlock {
/*000*/	int32_t	type;		/* T_HEADER == 2 */
/*004*/	int32_t	headerKey;	/* current block number */
        int32_t	r1[3];
/*014*/	uint32_t	checkSum;
/*018*/	int32_t	hashTable[HT_SIZE];
        int32_t	r2[2];
/*140*/	int32_t	access;	/* bit0=del, 1=modif, 2=write, 3=read */
/*144*/	int32_t	byteSize;
/*148*/	char	commLen;
/*149*/	char	comment[MAXCMMTLEN+1];
        char	r3[91-(MAXCMMTLEN+1)];
/*1a4*/	int32_t	days;
/*1a8*/	int32_t	mins;
/*1ac*/	int32_t	ticks;
/*1b0*/	char	nameLen;
/*1b1*/	char	name[MAXNAMELEN+1];
        int32_t	r4;
/*1d4*/	int32_t	realEntry;
/*1d8*/	int32_t	nextLink;
        int32_t	r5[5];
/*1f0*/	int32_t	nextSameHash;
/*1f4*/	int32_t	parent;
/*1f8*/	int32_t	extension;
/*1fc*/	int32_t	secType;
	};


#define ENV_DECLARATION struct Env adfEnv


#endif /* _ADF_STR_H */
/*##########################################################################*/
