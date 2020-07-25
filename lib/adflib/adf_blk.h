/*
 *  ADF Library. (C) 1997-2002 Laurent Clevy
 *
 *  adf_blk.h
 *
 *  $Id$
 *
 *  general blocks structures
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
 *  along with Foobar; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */


#ifndef ADF_BLK_H
#define ADF_BLK_H 1

#define ULONG   uint32_t
#define USHORT  uint16_t
#define UCHAR   uint8_t

#define LOGICAL_BLOCK_SIZE    512

/* ----- FILE SYSTEM ----- */

#define FSMASK_FFS         1
#define FSMASK_INTL        2
#define FSMASK_DIRCACHE    4

#define isFFS(c)           ((c)&FSMASK_FFS)
#define isOFS(c)           (!((c)&FSMASK_FFS))
#define isINTL(c)          ((c)&FSMASK_INTL)
#define isDIRCACHE(c)      ((c)&FSMASK_DIRCACHE)


/* ----- ENTRIES ----- */

/* access constants */

#define ACCMASK_D	(1<<0)
#define ACCMASK_E	(1<<1)
#define ACCMASK_W	(1<<2)
#define ACCMASK_R	(1<<3)
#define ACCMASK_A	(1<<4)
#define ACCMASK_P	(1<<5)
#define ACCMASK_S	(1<<6)
#define ACCMASK_H	(1<<7)

#define hasD(c)    ((c)&ACCMASK_D)
#define hasE(c)    ((c)&ACCMASK_E)
#define hasW(c)    ((c)&ACCMASK_W)
#define hasR(c)    ((c)&ACCMASK_R)
#define hasA(c)    ((c)&ACCMASK_A)
#define hasP(c)	   ((c)&ACCMASK_P)
#define hasS(c)    ((c)&ACCMASK_S)
#define hasH(c)    ((c)&ACCMASK_H)


/* ----- BLOCKS ----- */

/* block constants */

#define BM_VALID	-1
#define BM_INVALID	0

#define HT_SIZE		72
#define BM_SIZE     25
#define MAX_DATABLK	72

#define MAXNAMELEN	30
#define MAXCMMTLEN	79


/* block primary and secondary types */

#define T_HEADER	2
#define ST_ROOT		1
#define ST_DIR		2
#define ST_FILE		-3
#define ST_LFILE	-4
#define ST_LDIR		4
#define ST_LSOFT	3
#define T_LIST		16
#define T_DATA		8
#define T_DIRC		33


/*--- blocks structures --- */


struct bBootBlock {
/*000*/	char	dosType[4];
/*004*/	ULONG	checkSum;
/*008*/	int32_t	rootBlock;
/*00c*/	UCHAR	data[500+512];
};


struct bRootBlock {
/*000*/	int32_t	type;
        int32_t	headerKey;
        int32_t	highSeq;
/*00c*/	int32_t	hashTableSize;
        int32_t	firstData;
/*014*/	ULONG	checkSum;
/*018*/	int32_t	hashTable[HT_SIZE];		/* hash table */
/*138*/	int32_t	bmFlag;				/* bitmap flag, -1 means VALID */
/*13c*/	int32_t	bmPages[BM_SIZE];
/*1a0*/	int32_t	bmExt;
/*1a4*/	int32_t	cDays; 	/* creation date FFS and OFS */
/*1a8*/	int32_t	cMins;
/*1ac*/	int32_t	cTicks;
/*1b0*/	char	nameLen;
/*1b1*/	char 	diskName[MAXNAMELEN+1];
        char	r2[8];
/*1d8*/	int32_t	days;		/* last access : days after 1 jan 1978 */
/*1dc*/	int32_t	mins;		/* hours and minutes in minutes */
/*1e0*/	int32_t	ticks;		/* 1/50 seconds */
/*1e4*/	int32_t	coDays;	/* creation date OFS */
/*1e8*/	int32_t	coMins;
/*1ec*/	int32_t	coTicks;
        int32_t	nextSameHash;	/* == 0 */
        int32_t	parent;		/* == 0 */
/*1f8*/	int32_t	extension;		/* FFS: first directory cache block */
/*1fc*/	int32_t	secType;	/* == 1 */
};


struct bFileHeaderBlock {
/*000*/	int32_t	type;		/* == 2 */
/*004*/	int32_t	headerKey;	/* current block number */
/*008*/	int32_t	highSeq;	/* number of data block in this hdr block */
/*00c*/	int32_t	dataSize;	/* == 0 */
/*010*/	int32_t	firstData;
/*014*/	ULONG	checkSum;
/*018*/	int32_t	dataBlocks[MAX_DATABLK];
/*138*/	int32_t	r1;
/*13c*/	int32_t	r2;
/*140*/	int32_t	access;	/* bit0=del, 1=modif, 2=write, 3=read */
/*144*/	uint32_t	byteSize;
/*148*/	char	commLen;
/*149*/	char	comment[MAXCMMTLEN+1];
        char	r3[91-(MAXCMMTLEN+1)];
/*1a4*/	int32_t	days;
/*1a8*/	int32_t	mins;
/*1ac*/	int32_t	ticks;
/*1b0*/	char	nameLen;
/*1b1*/	char	fileName[MAXNAMELEN+1];
        int32_t	r4;
/*1d4*/	int32_t	real;		/* unused == 0 */
/*1d8*/	int32_t	nextLink;	/* link chain */
        int32_t	r5[5];
/*1f0*/	int32_t	nextSameHash;	/* next entry with sane hash */
/*1f4*/	int32_t	parent;		/* parent directory */
/*1f8*/	int32_t	extension;	/* pointer to extension block */
/*1fc*/	int32_t	secType;	/* == -3 */
};


/*--- file header extension block structure ---*/

struct bFileExtBlock {
/*000*/	int32_t	type;		/* == 0x10 */
/*004*/	int32_t	headerKey;
/*008*/	int32_t	highSeq;
/*00c*/	int32_t	dataSize;	/* == 0 */
/*010*/	int32_t	firstData;	/* == 0 */
/*014*/	ULONG	checkSum;
/*018*/	int32_t	dataBlocks[MAX_DATABLK];
        int32_t	r[45];
        int32_t	info;		/* == 0 */
        int32_t	nextSameHash;	/* == 0 */
/*1f4*/	int32_t	parent;		/* header block */
/*1f8*/	int32_t	extension;	/* next header extension block */
/*1fc*/	int32_t	secType;	/* -3 */	
};



struct bDirBlock {
/*000*/	int32_t	type;		/* == 2 */
/*004*/	int32_t	headerKey;
/*008*/	int32_t	highSeq;	/* == 0 */
/*00c*/	int32_t	hashTableSize;	/* == 0 */
        int32_t	r1;		/* == 0 */
/*014*/	ULONG	checkSum;
/*018*/	int32_t	hashTable[HT_SIZE];		/* hash table */
        int32_t	r2[2];
/*140*/	int32_t	access;
        int32_t	r4;		/* == 0 */
/*148*/	char	commLen;
/*149*/	char	comment[MAXCMMTLEN+1];
        char	r5[91-(MAXCMMTLEN+1)];
/*1a4*/	int32_t	days;		/* last access */
/*1a8*/	int32_t	mins;
/*1ac*/	int32_t	ticks;
/*1b0*/	char	nameLen;
/*1b1*/	char 	dirName[MAXNAMELEN+1];
        int32_t	r6;
/*1d4*/	int32_t	real;		/* ==0 */
/*1d8*/	int32_t	nextLink;	/* link list */
        int32_t	r7[5];
/*1f0*/	int32_t	nextSameHash;
/*1f4*/	int32_t	parent;
/*1f8*/	int32_t	extension;		/* FFS : first directory cache */
/*1fc*/	int32_t	secType;	/* == 2 */
};



struct bOFSDataBlock{
/*000*/	int32_t	type;		/* == 8 */
/*004*/	int32_t	headerKey;	/* pointer to file_hdr block */
/*008*/	int32_t	seqNum;	/* file data block number */
/*00c*/	int32_t	dataSize;	/* <= 0x1e8 */
/*010*/	int32_t	nextData;	/* next data block */
/*014*/	ULONG	checkSum;
/*018*/	UCHAR	data[488];
/*200*/	};


/* --- bitmap --- */

struct bBitmapBlock {
/*000*/	ULONG	checkSum;
/*004*/	ULONG	map[127];
	};


struct bBitmapExtBlock {
/*000*/	int32_t	bmPages[127];
/*1fc*/	int32_t	nextBlock;
	};


struct bLinkBlock {
/*000*/	int32_t	type;		/* == 2 */
/*004*/	int32_t	headerKey;	/* self pointer */
        int32_t	r1[3];
/*014*/	ULONG	checkSum;
/*018*/	char	realName[64];
        int32_t	r2[83];
/*1a4*/	int32_t	days;		/* last access */
/*1a8*/	int32_t	mins;
/*1ac*/	int32_t	ticks;
/*1b0*/	char	nameLen;
/*1b1*/	char 	name[MAXNAMELEN+1];
        int32_t	r3;
/*1d4*/	int32_t	realEntry;
/*1d8*/	int32_t	nextLink;
        int32_t	r4[5];
/*1f0*/	int32_t	nextSameHash;
/*1f4*/	int32_t	parent;	
        int32_t	r5;
/*1fc*/	int32_t	secType;	/* == -4, 4, 3 */
	};



/*--- directory cache block structure ---*/

struct bDirCacheBlock {
/*000*/	int32_t	type;		/* == 33 */
/*004*/	int32_t	headerKey;
/*008*/	int32_t	parent;
/*00c*/	int32_t	recordsNb;
/*010*/	int32_t	nextDirC;
/*014*/	ULONG	checkSum;
/*018*/	uint8_t records[488];
	};


#endif /* ADF_BLK_H */
/*##########################################################################*/
