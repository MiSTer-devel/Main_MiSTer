/*
 *  ADF Library. (C) 1997-2002 Laurent Clevy
 *
 *  adf_raw.c
 *
 *  $Id$
 *
 * logical disk/volume code
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

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "adf_str.h"
#include "adf_raw.h"
#include "adf_blk.h"
#include "adf_disk.h"
#include "adf_util.h"
#include "adf_err.h"
#include "defendian.h"

extern struct Env adfEnv;

int swapTable[MAX_SWTYPE+1][15]={
    { 4, SW_CHAR, 2, SW_LONG, 1012, SW_CHAR, 0, 1024 },     /* first bytes of boot */
    { 108, SW_LONG, 40, SW_CHAR, 10, SW_LONG, 0, 512 },        /* root */
    { 6, SW_LONG, 488, SW_CHAR, 0, 512 },                      /* data */
                                                            /* file, dir, entry */
    { 82, SW_LONG, 92, SW_CHAR, 3, SW_LONG, 36, SW_CHAR, 11, SW_LONG, 0, 512 },
    { 6, SW_LONG, 0, 24 },                                       /* cache */
    { 128, SW_LONG, 0, 512 },                                /* bitmap, fext */
		                                                    /* link */                                        
    { 6, SW_LONG, 64, SW_CHAR, 86, SW_LONG, 32, SW_CHAR, 12, SW_LONG, 0, 512 },
    { 4, SW_CHAR, 39, SW_LONG, 56, SW_CHAR, 10, SW_LONG, 0, 256 }, /* RDSK */
    { 4, SW_CHAR, 127, SW_LONG, 0, 512 },                          /* BADB */
    { 4, SW_CHAR, 8, SW_LONG, 32, SW_CHAR, 31, SW_LONG, 4, SW_CHAR, /* PART */
      15, SW_LONG, 0, 256 },
    { 4, SW_CHAR, 7, SW_LONG, 4, SW_CHAR, 55, SW_LONG, 0, 256 }, /* FSHD */
    { 4, SW_CHAR, 4, SW_LONG, 492, SW_CHAR, 0, 512 }             /* LSEG */
    };


/*
 * swapEndian
 *
 * magic :-) endian swap function (big -> little for read, little to big for write)
 */

    void
swapEndian( uint8_t *buf, int type )
{
    int i,j;
    int p;

    i=0;
    p=0;

    if (type>MAX_SWTYPE || type<0)
        adfEnv.eFct("SwapEndian: type do not exist");

    while( swapTable[type][i]!=0 ) {
        for(j=0; j<swapTable[type][i]; j++) {
            switch( swapTable[type][i+1] ) {
            case SW_LONG:
                *(uint32_t*)(buf+p)=Long(buf+p);
                p+=4;
                break;
            case SW_SHORT:
                *(uint32_t*)(buf+p)=Short(buf+p);
                p+=2;
                break;
            case SW_CHAR:
                p++;
                break;
            default:
                ;
            }
        }
    i+=2;
    }
    if (p!=swapTable[type][i+1]) 
        (*adfEnv.wFct)("Warning: Endian Swapping length");		/* BV */
    

}





/*
 * adfReadRootBlock
 *
 * ENDIAN DEPENDENT
 */
RETCODE
adfReadRootBlock(struct Volume* vol, int32_t nSect, struct bRootBlock* root)
{
	uint8_t buf[LOGICAL_BLOCK_SIZE];

	if (adfReadBlock(vol, nSect, buf)!=RC_OK)
		return RC_ERROR;

	memcpy(root, buf, LOGICAL_BLOCK_SIZE);
#ifdef LITT_ENDIAN
    swapEndian((uint8_t*)root, SWBL_ROOT);    
#endif

	if (root->type!=T_HEADER || root->secType!=ST_ROOT) {
		(*adfEnv.wFct)("adfReadRootBlock : id not found");
        return RC_ERROR;
    }
	if (root->checkSum!=adfNormalSum(buf, 20, LOGICAL_BLOCK_SIZE)) {
		(*adfEnv.wFct)("adfReadRootBlock : invalid checksum");
        return RC_ERROR;
    }
		
    return RC_OK;
}


/*
 * adfWriteRootBlock
 *
 * 
 */
RETCODE adfWriteRootBlock(struct Volume* vol, int32_t nSect, struct bRootBlock* root)
{
    uint8_t buf[LOGICAL_BLOCK_SIZE];
	uint32_t newSum;


    root->type = T_HEADER;
    root->headerKey = 0L;
    root->highSeq = 0L;
    root->hashTableSize = HT_SIZE;
    root->firstData = 0L;
    /* checkSum, hashTable */
    /* bmflag */
    /* bmPages, bmExt */
    root->nextSameHash = 0L;
    root->parent = 0L;
    root->secType = ST_ROOT;

    memcpy(buf, root, LOGICAL_BLOCK_SIZE);
#ifdef LITT_ENDIAN
    swapEndian(buf, SWBL_ROOT);
#endif

	newSum = adfNormalSum(buf,20,LOGICAL_BLOCK_SIZE);
    swLong(buf+20, newSum);
/*	*(uint32_t*)(buf+20) = swapLong((uint8_t*)&newSum);*/

/* 	dumpBlock(buf);*/
	if (adfWriteBlock(vol, nSect, buf)!=RC_OK)
        return RC_ERROR;
/*printf("adfWriteRootBlock %ld\n",nSect);*/
    return RC_OK;
}


/*
 * adfReadBootBlock
 *
 * ENDIAN DEPENDENT
 */
RETCODE
adfReadBootBlock(struct Volume* vol, struct bBootBlock* boot)
{
	uint8_t buf[1024];
	
/*puts("22");*/
	if (adfReadBlock(vol, 0, buf)!=RC_OK)
		return RC_ERROR;
/*puts("11");*/
    if (adfReadBlock(vol, 1, buf+LOGICAL_BLOCK_SIZE)!=RC_OK)
		return RC_ERROR;

    memcpy(boot, buf, LOGICAL_BLOCK_SIZE*2);
#ifdef LITT_ENDIAN
    swapEndian((uint8_t*)boot,SWBL_BOOT);
#endif
	if ( strncmp("DOS",boot->dosType,3)!=0 ) {
		(*adfEnv.wFct)("adfReadBootBlock : DOS id not found");
		return RC_ERROR;
    }

	if ( boot->data[0]!=0 && adfBootSum(buf)!=boot->checkSum ) {
/*printf("compsum=%lx sum=%lx\n",	adfBootSum(buf),boot->checkSum );*/		/* BV */
		(*adfEnv.wFct)("adfReadBootBlock : incorrect checksum"); 
    }

    return RC_OK;
}

/*
 * adfWriteBootBlock
 *
 *
 *     write bootcode ?
 */
RETCODE
adfWriteBootBlock(struct Volume* vol, struct bBootBlock* boot)
{
    uint8_t buf[LOGICAL_BLOCK_SIZE*2];
	uint32_t newSum;

    boot->dosType[0] = 'D';
    boot->dosType[1] = 'O';
    boot->dosType[2] = 'S';
	memcpy(buf, boot, LOGICAL_BLOCK_SIZE*2);
#ifdef LITT_ENDIAN
    swapEndian(buf, SWBL_BOOT);
#endif

    if (boot->rootBlock==880 || boot->data[0]!=0) {
        newSum = adfBootSum(buf);
/*fprintf(stderr,"sum %x %x\n",newSum,adfBootSum2(buf));*/
        swLong(buf+4,newSum);
/*        *(uint32_t*)(buf+4) = swapLong((uint8_t*)&newSum);*/
    }

/*	dumpBlock(buf);
	dumpBlock(buf+512);
*/	
    if (adfWriteBlock(vol, 0, buf)!=RC_OK)
		return RC_ERROR;
	if (adfWriteBlock(vol, 1,  buf+512)!=RC_OK)
		return RC_ERROR;
/*puts("adfWriteBootBlock");*/
    return RC_OK;
}


/*
 * NormalSum
 *
 * buf = where the block is stored
 * offset = checksum place (in bytes)
 * bufLen = buffer length (in bytes)
 */
    uint32_t
adfNormalSum( UCHAR* buf, int offset, int bufLen )
{
    int32_t newsum;
    int i;

    newsum=0L;
    for(i=0; i < (bufLen/4); i++)
        if ( i != (offset/4) )       /* old chksum */
            newsum+=Long(buf+i*4);
    newsum=(-newsum);	/* WARNING */

    return(newsum);
}

/*
 * adfBitmapSum
 *
 */
	uint32_t
adfBitmapSum(uint8_t *buf)
{
	uint32_t newSum;
	int i;
	
	newSum = 0L;
	for(i=1; i<128; i++)
		newSum-=Long(buf+i*4);
	return(newSum);
}


/*
 * adfBootSum
 *
 */
    uint32_t
adfBootSum(uint8_t *buf)
{
    uint32_t d, newSum;
    int i;
	
    newSum=0L;
    for(i=0; i<256; i++) {
        if (i!=1) {
            d = Long(buf+i*4);
            if ( (0xffffffffU-newSum)<d )
                newSum++;
            newSum+=d;
        }
    }
    newSum = ~newSum;	/* not */

    return(newSum);
}

    uint32_t
adfBootSum2(uint8_t *buf)
{
    uint32_t prevsum, newSum;
    int i;

    prevsum = newSum=0L;
    for(i=0; i<1024/sizeof(uint32_t); i++) {
        if (i!=1) {
            prevsum = newSum;
            newSum += Long(buf+i*4);
            if (newSum < prevsum)
                newSum++;
        }
    }
    newSum = ~newSum;	/* not */

    return(newSum);
}


/*#######################################################################################*/
