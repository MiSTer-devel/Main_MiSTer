/*
 *  ADF Library. (C) 1997-2002 Laurent Clevy
 *
 *  hd_blk.h
 *
 *  $Id$
 *
 *  hard disk blocks structures
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


#ifndef _HD_BLK_H
#define _HD_BLK_H 1

#include "adf_str.h"

/* ------- RDSK ---------*/

struct bRDSKblock {
/*000*/	char	id[4];		/* RDSK */
/*004*/	int32_t 	size; 		/* 64 int32_ts */
/*008*/	ULONG	checksum;
/*00c*/	int32_t	hostID; 	/* 7 */
/*010*/ 	int32_t 	blockSize; 	/* 512 bytes */
/*014*/ 	int32_t 	flags; 		/* 0x17 */
/*018*/ 	int32_t 	badBlockList;
/*01c*/ 	int32_t 	partitionList;
/*020*/ 	int32_t 	fileSysHdrList;
/*024*/ 	int32_t 	driveInit;
/*028*/ 	int32_t 	r1[6];		/* -1 */
/*040*/ 	int32_t 	cylinders;
/*044*/ 	int32_t 	sectors;
/*048*/ 	int32_t 	heads;
/*04c*/ 	int32_t 	interleave;
/*050*/ 	int32_t 	parkingZone;
/*054*/	int32_t 	r2[3]; 	 	/* 0 */
/*060*/	int32_t 	writePreComp;
/*064*/	int32_t 	reducedWrite;
/*068*/	int32_t 	stepRate;
/*06c*/	int32_t 	r3[5]; 		/* 0 */
/*080*/	int32_t 	rdbBlockLo;
/*084*/ 	int32_t 	rdbBlockHi;
/*088*/ 	int32_t 	loCylinder;
/*08c*/ 	int32_t 	hiCylinder;
/*090*/ 	int32_t 	cylBlocks;
/*094*/ 	int32_t 	autoParkSeconds;
/*098*/ 	int32_t 	highRDSKBlock;
/*09c*/ 	int32_t 	r4; 		/* 0 */
/*0a0*/ 	char 	diskVendor[8];
/*0a8*/ 	char 	diskProduct[16];
/*0b8*/ 	char 	diskRevision[4];
/*0bc*/	char 	controllerVendor[8];
/*0c4*/ 	char 	controllerProduct[16];
/*0d4*/	char 	controllerRevision[4];
/*0d8*/ 	int32_t 	r5[10]; 	/* 0 */
/*100*/
};


struct bBADBentry {
/*000*/	int32_t 	badBlock;
/*004*/	int32_t 	goodBlock;
};


struct bBADBblock {
/*000*/	char	id[4]; 		/* BADB */
/*004*/	int32_t 	size; 		/* 128 int32_ts */
/*008*/	ULONG	checksum; 	
/*00c*/	int32_t	hostID; 	/* 7 */
/*010*/ 	int32_t 	next;
/*014*/ 	int32_t 	r1;
/*018*/ 	struct bBADBentry blockPairs[61];
};



struct bPARTblock {
/*000*/	char	id[4]; 		/* PART */
/*004*/	int32_t 	size; 		/* 64 int32_ts */
/*008*/	ULONG	checksum;
/*00c*/	int32_t	hostID; 	/* 7 */
/*010*/ 	int32_t 	next;
/*014*/ 	int32_t 	flags;
/*018*/ 	int32_t 	r1[2];
/*020*/ 	int32_t 	devFlags;
/*024*/ 	char 	nameLen;
/*025*/ 	char 	name[31];
/*044*/ 	int32_t 	r2[15];

/*080*/ 	int32_t 	vectorSize; 	/* often 16 int32_ts */
/*084*/ 	int32_t 	blockSize; 	/* 128 int32_ts */
/*088*/ 	int32_t 	secOrg;
/*08c*/ 	int32_t 	surfaces;
/*090*/ 	int32_t 	sectorsPerBlock; /* == 1 */
/*094*/ 	int32_t 	blocksPerTrack;
/*098*/ 	int32_t 	dosReserved;
/*09c*/ 	int32_t 	dosPreAlloc;
/*0a0*/ 	int32_t 	interleave;
/*0a4*/ 	int32_t 	lowCyl;
/*0a8*/ 	int32_t 	highCyl;
/*0ac*/	int32_t 	numBuffer;
/*0b0*/ 	int32_t 	bufMemType;
/*0b4*/ 	int32_t 	maxTransfer;
/*0b8*/ 	int32_t 	mask;
/*0bc*/ 	int32_t 	bootPri;
/*0c0*/ 	char 	dosType[4];
/*0c4*/ 	int32_t 	r3[15];
};


struct bLSEGblock {
/*000*/	char	id[4]; 		/* LSEG */
/*004*/	int32_t 	size; 		/* 128 int32_ts */
/*008*/	ULONG	checksum;
/*00c*/	int32_t	hostID; 	/* 7 */
/*010*/ 	int32_t 	next;
/*014*/ 	char 	loadData[123*4];
};


struct bFSHDblock {
/*000*/	char	id[4]; 		/* FSHD */
/*004*/	int32_t 	size; 		/* 64 */
/*008*/	ULONG	checksum;
/*00c*/	int32_t	hostID; 	/* 7 */
/*010*/ 	int32_t 	next;
/*014*/ 	int32_t 	flags;
/*018*/ 	int32_t 	r1[2];
/*020*/ 	char 	dosType[4];
/*024*/ 	short 	majVersion;
/*026*/ 	short 	minVersion;
/*028*/ 	int32_t 	patchFlags;

/*02c*/ 	int32_t 	type;
/*030*/ 	int32_t 	task;
/*034*/ 	int32_t 	lock;
/*038*/ 	int32_t 	handler;
/*03c*/ 	int32_t 	stackSize;
/*040*/ 	int32_t 	priority;
/*044*/ 	int32_t 	startup;
/*048*/ 	int32_t 	segListBlock;
/*04c*/ 	int32_t 	globalVec;
/*050*/ 	int32_t 	r2[23];
/*0ac*/ 	int32_t 	r3[21];
};


#endif /* _HD_BLK_H */
/*##########################################################################*/
