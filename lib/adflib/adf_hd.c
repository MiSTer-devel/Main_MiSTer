/*
 *  ADF Library. (C) 1997-2002 Laurent Clevy
 *
 *  adf_hd.c
 *
 *  $Id$
 *
 *  harddisk / device code
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



#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#include"adf_str.h"
#include"hd_blk.h"
#include"adf_raw.h"
#include"adf_hd.h"
#include"adf_util.h"
#include"adf_disk.h"
#include"adf_nativ.h"
#include"adf_dump.h"
#include"adf_err.h"

#include"defendian.h"

extern struct Env adfEnv;

/*
 * adfDevType
 *
 * returns the type of a device
 * only based of the field 'dev->size'
 */
int adfDevType(struct Device* dev)
{
    if( (dev->size==512*11*2*80) ||		/* BV */
        (dev->size==512*11*2*81) ||		/* BV */
        (dev->size==512*11*2*82) || 	/* BV */
        (dev->size==512*11*2*83) )		/* BV */
        return(DEVTYPE_FLOPDD);
    else if (dev->size==512*22*2*80)
        return(DEVTYPE_FLOPHD);
    else if (dev->size>512*22*2*80)
        return(DEVTYPE_HARDDISK);
    else {
		(*adfEnv.eFct)("adfDevType : unknown device type");
		return(-1);
	}
}


/*
 * adfDeviceInfo
 *
 * display information about the device and its volumes
 * for demonstration purpose only since the output is stdout !
 *
 * can be used before adfCreateVol() or adfMount()
 */
void adfDeviceInfo(struct Device *dev)
{
	int i;
	
	printf("Cylinders   = %d\n",dev->cylinders);
    printf("Heads       = %d\n",dev->heads);
    printf("Sectors/Cyl = %d\n\n",dev->sectors);
	if (!dev->isNativeDev)
        printf("Dump device\n\n");
    else
        printf("Real device\n\n");
    printf("Volumes     = %d\n\n",dev->nVol);
/*
    switch(dev->devType){
    case DEVTYPE_FLOPDD:
        printf("floppy dd\n"); break;
    case DEVTYPE_FLOPHD:
        printf("floppy hd\n"); break;
    case DEVTYPE_HARDDISK:
        printf("harddisk\n"); break;
    case DEVTYPE_HARDFILE:
        printf("hardfile\n"); break;
    default:
        printf("unknown devType!\n"); break;
    }
*/

    for(i=0; i<dev->nVol; i++) {
        if (dev->volList[i]->volName)
            printf("%2d :  %7d ->%7d, \"%s\"", i,
			dev->volList[i]->firstBlock,
			dev->volList[i]->lastBlock,
			dev->volList[i]->volName);
        else
            printf("%2d :  %7d ->%7d\n", i,
			dev->volList[i]->firstBlock,
			dev->volList[i]->lastBlock);
        if (dev->volList[i]->mounted)
			printf(", mounted");
        putchar('\n');
    }
}


/*
 * adfFreeTmpVolList
 *
 */
void adfFreeTmpVolList(struct List *root)
{
    struct List *cell;
    struct Volume *vol;

    cell = root;
    while(cell!=NULL) {
        vol = (struct Volume *)cell->content;
        if (vol->volName!=NULL)
			free(vol->volName);  
        cell = cell->next;
    }
    freeList(root);

}


/*
 * adfMountHdFile
 *
 */
RETCODE adfMountHdFile(struct Device *dev)
{
    struct Volume* vol;
    uint8_t buf[512];
    int32_t size;
    BOOL found;

    dev->devType = DEVTYPE_HARDFILE;
    dev->nVol = 0;
    dev->volList = (struct Volume**)malloc(sizeof(struct Volume*));
    if (!dev->volList) { 
		(*adfEnv.eFct)("adfMountHdFile : malloc");
        return RC_ERROR;
    }

    vol=(struct Volume*)malloc(sizeof(struct Volume));
    if (!vol) {
        (*adfEnv.eFct)("adfMountHdFile : malloc");
        return RC_ERROR;
    }
    dev->volList[0] = vol;
    dev->nVol++;      /* fixed by Dan, ... and by Gary */

    vol->volName=NULL;
    
    dev->cylinders = dev->size/512;
    dev->heads = 1;
    dev->sectors = 1;

    vol->firstBlock = 0;

    size = dev->size + 512-(dev->size%512);
/*printf("size=%ld\n",size);*/
    vol->rootBlock = (size/512)/2;
/*printf("root=%ld\n",vol->rootBlock);*/
    do {
        adfReadDumpSector(dev, vol->rootBlock, 512, buf);
        found = swapLong(buf)==T_HEADER && swapLong(buf+508)==ST_ROOT;
        if (!found)
            (vol->rootBlock)--;
    }while (vol->rootBlock>1 && !found);

    if (vol->rootBlock==1) {
        (*adfEnv.eFct)("adfMountHdFile : rootblock not found");
        return RC_ERROR;
    }
    vol->lastBlock = vol->rootBlock*2 - 1 ;

    return RC_OK;
}


/*
 * adfMountHd
 *
 * normal not used directly : called by adfMount()
 *
 * fills geometry fields and volumes list (dev->nVol and dev->volList[])
 */
RETCODE adfMountHd(struct Device *dev)
{
    struct bRDSKblock rdsk;
    struct bPARTblock part;
    struct bFSHDblock fshd;
	struct bLSEGblock lseg;
	int32_t next;
    struct List *vList, *listRoot;
    int i;
    struct Volume* vol;
    int len;

    if (adfReadRDSKblock( dev, &rdsk )!=RC_OK)
        return RC_ERROR;

    dev->cylinders = rdsk.cylinders;
    dev->heads = rdsk.heads;
    dev->sectors = rdsk.sectors;

    /* PART blocks */
    listRoot = NULL;
    next = rdsk.partitionList;
    dev->nVol=0;
    vList = NULL;
    while( next!=-1 ) {
        if (adfReadPARTblock( dev, next, &part )!=RC_OK) {
            adfFreeTmpVolList(listRoot);
            (*adfEnv.eFct)("adfMountHd : malloc");
            return RC_ERROR;
        }

        vol=(struct Volume*)malloc(sizeof(struct Volume));
        if (!vol) {
            adfFreeTmpVolList(listRoot);
            (*adfEnv.eFct)("adfMountHd : malloc");
            return RC_ERROR;
        }
        vol->volName=NULL;
        dev->nVol++;

        vol->firstBlock = rdsk.cylBlocks * part.lowCyl;
        vol->lastBlock = (part.highCyl+1)*rdsk.cylBlocks -1 ;
        vol->rootBlock = (vol->lastBlock - vol->firstBlock+1)/2;
        vol->blockSize = part.blockSize*4;

        len = min(31, part.nameLen);
        vol->volName = (char*)malloc(len+1);
        if (!vol->volName) { 
            adfFreeTmpVolList(listRoot);
			(*adfEnv.eFct)("adfMount : malloc");
            return RC_ERROR;
		}
        memcpy(vol->volName,part.name,len);
        vol->volName[len] = '\0';

        vol->mounted = FALSE;

        /* stores temporaly the volumes in a linked list */
        if (listRoot==NULL)
            vList = listRoot = newCell(NULL, (void*)vol);
        else
            vList = newCell(vList, (void*)vol);

        if (vList==NULL) {
            adfFreeTmpVolList(listRoot);
			(*adfEnv.eFct)("adfMount : newCell() malloc");
            return RC_ERROR;
        }

        next = part.next;
    }

    /* stores the list in an array */
    dev->volList = (struct Volume**)malloc(sizeof(struct Volume*) * dev->nVol);
    if (!dev->volList) { 
        adfFreeTmpVolList(listRoot);
		(*adfEnv.eFct)("adfMount : unknown device type");
        return RC_ERROR;
    }
    vList = listRoot;
    for(i=0; i<dev->nVol; i++) {
        dev->volList[i]=(struct Volume*)vList->content;
        vList = vList->next;
    }
    freeList(listRoot);

    next = rdsk.fileSysHdrList;
    while( next!=-1 ) {
        if (adfReadFSHDblock( dev, next, &fshd )!=RC_OK) {
            for(i=0;i<dev->nVol;i++) free(dev->volList[i]);
            free(dev->volList);
            (*adfEnv.eFct)("adfMount : adfReadFSHDblock");
            return RC_ERROR;
        }
        next = fshd.next;
    }

    next = fshd.segListBlock;
    while( next!=-1 ) {
        if (adfReadLSEGblock( dev, next, &lseg )!=RC_OK) {
            (*adfEnv.wFct)("adfMount : adfReadLSEGblock");
        }
        next = lseg.next;
    }

    return RC_OK;
}


/*
 * adfMountFlop
 *
 * normaly not used directly, called directly by adfMount()
 *
 * use dev->devType to choose between DD and HD
 * fills geometry and the volume list with one volume
 */
RETCODE adfMountFlop(struct Device* dev)
{
	struct Volume *vol;
	struct bRootBlock root;
	char diskName[35];
	
    dev->cylinders = 80;
    dev->heads = 2;
    if (dev->devType==DEVTYPE_FLOPDD)
        dev->sectors = 11;
    else 
        dev->sectors = 22;

    vol=(struct Volume*)malloc(sizeof(struct Volume));
    if (!vol) { 
		(*adfEnv.eFct)("adfMount : malloc");
        return RC_ERROR;
    }

    vol->mounted = TRUE;
    vol->firstBlock = 0;
    vol->lastBlock =(dev->cylinders * dev->heads * dev->sectors)-1;
    vol->rootBlock = (vol->lastBlock+1 - vol->firstBlock)/2;
    vol->blockSize = 512;
    vol->dev = dev;
 
	if (adfReadRootBlock(vol, vol->rootBlock, &root)!=RC_OK)
		return RC_ERROR;
	memset(diskName, 0, 35);
	memcpy(diskName, root.diskName, root.nameLen);

    vol->volName = strdup(diskName);
	
    dev->volList =(struct Volume**) malloc(sizeof(struct Volume*));
    if (!dev->volList) {
        free(vol);
		(*adfEnv.eFct)("adfMount : malloc");
        return RC_ERROR;
    }
    dev->volList[0] = vol;
    dev->nVol = 1;

/*printf("root=%d\n",vol->rootBlock);	    */
    return RC_OK;
}


/*
 * adfMountDev
 *
 * mount a dump file (.adf) or a real device (uses adf_nativ.c and .h)
 *
 * adfInitDevice() must fill dev->size !
 */
struct Device* adfMountDev( char* filename, BOOL ro)
{
    struct Device* dev;
    struct nativeFunctions *nFct;
    RETCODE rc;
    uint8_t buf[512];

    dev = (struct Device*)malloc(sizeof(struct Device));
    if (!dev) {
		(*adfEnv.eFct)("adfMountDev : malloc error");
        return NULL;
    }

    dev->readOnly = ro;

    /* switch between dump files and real devices */
    nFct = adfEnv.nativeFct;
    dev->isNativeDev = (*nFct->adfIsDevNative)(filename);
    if (dev->isNativeDev)
        rc = (*nFct->adfInitDevice)(dev, filename,ro);
    else
        rc = adfInitDumpDevice(dev,filename,ro);
    if (rc!=RC_OK) {
        free(dev); return(NULL);
    }

    dev->devType = adfDevType(dev);

    switch( dev->devType ) {

    case DEVTYPE_FLOPDD:
    case DEVTYPE_FLOPHD:
        if (adfMountFlop(dev)!=RC_OK) {
	         if (dev->isNativeDev)					/* BV */
		         (*nFct->adfReleaseDevice)(dev);	/* BV */
	         else									/* BV */
		         adfReleaseDumpDevice(dev);     	/* BV */       
            free(dev); return NULL;
        }
        break;

    case DEVTYPE_HARDDISK:
        /* to choose between hardfile or harddisk (real or dump) */
        if (dev->isNativeDev)									/* BV ...from here*/
            rc = (*nFct->adfNativeReadSector)(dev, 0, 512, buf);
        else
            rc = adfReadDumpSector(dev, 0, 512, buf);
        if( rc!=RC_OK ) {
	         nFct = adfEnv.nativeFct;
	         if (dev->isNativeDev)
		         (*nFct->adfReleaseDevice)(dev);
	         else
		         adfReleaseDumpDevice(dev);            
            (*adfEnv.eFct)("adfMountDev : adfReadDumpSector failed");
            free(dev); return NULL;
        }

        /* a file with the first three bytes equal to 'DOS' */
    	if (!dev->isNativeDev && strncmp("DOS",(char*)buf,3)==0) {
            if (adfMountHdFile(dev)!=RC_OK) {
	            if (dev->isNativeDev)
		            (*nFct->adfReleaseDevice)(dev);
	            else
		            adfReleaseDumpDevice(dev);            
                free(dev); return NULL;
            }
        }
        else if (adfMountHd(dev)!=RC_OK) {
	         if (dev->isNativeDev)
		         (*nFct->adfReleaseDevice)(dev);
	         else
		         adfReleaseDumpDevice(dev);            
            free(dev); return NULL;								/* BV ...to here.*/
        }
	    break;

    default:
        (*adfEnv.eFct)("adfMountDev : unknown device type");
	      if (dev->isNativeDev)									/* BV */
		      (*nFct->adfReleaseDevice)(dev);					/* BV */
	      else													/* BV */
		      adfReleaseDumpDevice(dev);						/* BV */
         free(dev); return NULL;								/* BV */
    }

	return dev;
}


/*
 * adfCreateHdHeader
 *
 * create PARTIALLY the sectors of the header of one harddisk : can not be mounted
 * back on a real Amiga ! It's because some device dependant values can't be guessed...
 *
 * do not use dev->volList[], but partList for partitions information : start and len are cylinders,
 *  not blocks
 * do not fill dev->volList[]
 * called by adfCreateHd()
 */
RETCODE adfCreateHdHeader(struct Device* dev, int n, struct Partition** partList )
{
    int i;
    struct bRDSKblock rdsk;
    struct bPARTblock part;
    struct bFSHDblock fshd;
    struct bLSEGblock lseg;
    SECTNUM j;
    int len;


    /* RDSK */ 
 
    memset((uint8_t*)&rdsk,0,sizeof(struct bRDSKblock));

    rdsk.rdbBlockLo = 0;
	rdsk.rdbBlockHi = (dev->sectors*dev->heads*2)-1;
	rdsk.loCylinder = 2;
	rdsk.hiCylinder = dev->cylinders-1;
	rdsk.cylBlocks = dev->sectors*dev->heads;

    rdsk.cylinders = dev->cylinders;
	rdsk.sectors = dev->sectors;
	rdsk.heads = dev->heads;
	
	rdsk.badBlockList = -1;
	rdsk.partitionList = 1;
	rdsk.fileSysHdrList = 1 + dev->nVol;
	
    if (adfWriteRDSKblock(dev, &rdsk)!=RC_OK)
        return RC_ERROR;

    /* PART */

    j=1;
    for(i=0; i<dev->nVol; i++) {
        memset(&part, 0, sizeof(struct bPARTblock));

        if (i<dev->nVol-1)
            part.next = j+1;
        else
			part.next = -1;

        len = min(MAXNAMELEN,strlen(partList[i]->volName));
        part.nameLen = len;
        strncpy(part.name, partList[i]->volName, len);

        part.surfaces = dev->heads;
        part.blocksPerTrack = dev->sectors;
		part.lowCyl = partList[i]->startCyl;
		part.highCyl = partList[i]->startCyl + partList[i]->lenCyl -1;
        strncpy(part.dosType, "DOS", 3);

        part.dosType[3] = partList[i]->volType & 0x01;
			
        if (adfWritePARTblock(dev, j, &part))
            return RC_ERROR;
        j++;
    }

    /* FSHD */

    strncpy(fshd.dosType,"DOS",3);
    fshd.dosType[3] = partList[0]->volType;
	fshd.next = -1;
	fshd.segListBlock = j+1;
    if (adfWriteFSHDblock(dev, j, &fshd)!=RC_OK)
        return RC_ERROR;
	j++;
	
	/* LSEG */
	lseg.next = -1;
    if (adfWriteLSEGblock(dev, j, &lseg)!=RC_OK)
        return RC_ERROR;

    return RC_OK;
}


/*
 * adfCreateFlop
 *
 * create a filesystem on a floppy device
 * fills dev->volList[]
 */
RETCODE adfCreateFlop(struct Device* dev, char* volName, int volType )
{
    if (dev==NULL) {
        (*adfEnv.eFct)("adfCreateFlop : dev==NULL");
        return RC_ERROR;
    }
    dev->volList =(struct Volume**) malloc(sizeof(struct Volume*));
    if (!dev->volList) { 
		(*adfEnv.eFct)("adfCreateFlop : unknown device type");
        return RC_ERROR;
    }
    dev->volList[0] = adfCreateVol( dev, 0L, 80L, volName, volType );
    if (dev->volList[0]==NULL) {
        free(dev->volList);
        return RC_ERROR;
    }
    dev->nVol = 1;
    dev->volList[0]->blockSize = 512;
    if (dev->sectors==11)
        dev->devType=DEVTYPE_FLOPDD;
    else
        dev->devType=DEVTYPE_FLOPHD;

    return RC_OK;
}


/*
 * adfCreateHd
 *
 * create a filesystem one an harddisk device (partitions==volumes, and the header)
 *
 * fills dev->volList[]
 *
 */
RETCODE adfCreateHd(struct Device* dev, int n, struct Partition** partList )
{
    int i, j;

/*struct Volume *vol;*/

    if (dev==NULL || partList==NULL || n<=0) {
        (*adfEnv.eFct)("adfCreateHd : illegal parameter(s)");
        return RC_ERROR;
    }

    dev->volList =(struct Volume**) malloc(sizeof(struct Volume*)*n);
    if (!dev->volList) {
		(*adfEnv.eFct)("adfCreateFlop : malloc");
        return RC_ERROR;
    }
    for(i=0; i<n; i++) {
        dev->volList[i] = adfCreateVol( dev, 
					partList[i]->startCyl, 
					partList[i]->lenCyl, 
					partList[i]->volName, 
					partList[i]->volType );
        if (dev->volList[i]==NULL) {
           for(j=0; j<i; j++) {
               free( dev->volList[i] );
/* pas fini */
           }
           free(dev->volList);
           (*adfEnv.eFct)("adfCreateHd : adfCreateVol() fails");
        }
        dev->volList[i]->blockSize = 512;
    }
    dev->nVol = n;
/*
vol=dev->volList[0];
printf("0first=%ld last=%ld root=%ld\n",vol->firstBlock,
 vol->lastBlock, vol->rootBlock);
*/

    if (adfCreateHdHeader(dev, n, partList )!=RC_OK)
        return RC_ERROR;
    return RC_OK;
}


/*
 * adfUnMountDev
 *
 */
void adfUnMountDev( struct Device* dev)
{
    int i;
    struct nativeFunctions *nFct;

	if (dev==0)
	   return;

    for(i=0; i<dev->nVol; i++) {
        free(dev->volList[i]->volName);
        free(dev->volList[i]);
    }
    if (dev->nVol>0)
        free(dev->volList);
    dev->nVol = 0;

    nFct = adfEnv.nativeFct;
    if (dev->isNativeDev)
        (*nFct->adfReleaseDevice)(dev);
    else
        adfReleaseDumpDevice(dev);

    free(dev);
}



/*
 * ReadRDSKblock
 *
 */
	RETCODE
adfReadRDSKblock( struct Device* dev, struct bRDSKblock* blk )
{

    UCHAR buf[256];
    struct nativeFunctions *nFct;
    RETCODE rc2;
    RETCODE rc = RC_OK;
	
    nFct = adfEnv.nativeFct;
    if (dev->isNativeDev)
        rc2 =(*nFct->adfNativeReadSector)(dev, 0, 256, buf);
    else
        rc2 = adfReadDumpSector(dev, 0, 256, buf);

    if (rc2!=RC_OK)
       return(RC_ERROR);

    memcpy(blk, buf, 256);
#ifdef LITT_ENDIAN
    /* big to little = 68000 to x86 */
    swapEndian((uint8_t*)blk, SWBL_RDSK);
#endif

    if ( strncmp(blk->id,"RDSK",4)!=0 ) {
        (*adfEnv.eFct)("ReadRDSKblock : RDSK id not found");
        return RC_ERROR;
    }

    if ( blk->size != 64 )
        (*adfEnv.wFct)("ReadRDSKBlock : size != 64");				/* BV */

    if ( blk->checksum != adfNormalSum(buf,8,256) ) {
         (*adfEnv.wFct)("ReadRDSKBlock : incorrect checksum");
         /* BV FIX: Due to malicious Win98 write to sector
         rc|=RC_BLOCKSUM;*/
    }
	
    if ( blk->blockSize != 512 )
         (*adfEnv.wFct)("ReadRDSKBlock : blockSize != 512");		/* BV */

    if ( blk->cylBlocks !=  blk->sectors*blk->heads )
        (*adfEnv.wFct)( "ReadRDSKBlock : cylBlocks != sectors*heads");

    return rc;
}


/*
 * adfWriteRDSKblock
 *
 */
    RETCODE
adfWriteRDSKblock(struct Device *dev, struct bRDSKblock* rdsk)
{
    uint8_t buf[LOGICAL_BLOCK_SIZE];
    uint32_t newSum;
    struct nativeFunctions *nFct;
    RETCODE rc2, rc = RC_OK;

    if (dev->readOnly) {
        (*adfEnv.wFct)("adfWriteRDSKblock : can't write block, read only device");
        return RC_ERROR;
    }

    memset(buf,0,LOGICAL_BLOCK_SIZE);

    strncpy(rdsk->id,"RDSK",4);
    rdsk->size = sizeof(struct bRDSKblock)/sizeof(int32_t);
    rdsk->blockSize = LOGICAL_BLOCK_SIZE;
    rdsk->badBlockList = -1;

    strncpy(rdsk->diskVendor,"ADFlib  ",8);
    strncpy(rdsk->diskProduct,"harddisk.adf    ",16);
    strncpy(rdsk->diskRevision,"v1.0",4);

    memcpy(buf, rdsk, sizeof(struct bRDSKblock));
#ifdef LITT_ENDIAN
    swapEndian(buf, SWBL_RDSK);
#endif

    newSum = adfNormalSum(buf, 8, LOGICAL_BLOCK_SIZE);
    swLong(buf+8, newSum);

    nFct = adfEnv.nativeFct;
    if (dev->isNativeDev)
        rc2=(*nFct->adfNativeWriteSector)(dev, 0, LOGICAL_BLOCK_SIZE, buf);
    else
        rc2=adfWriteDumpSector(dev, 0, LOGICAL_BLOCK_SIZE, buf);

    if (rc2!=RC_OK)
       return RC_ERROR;

    return rc;
}


/*
 * ReadPARTblock
 *
 */
    RETCODE
adfReadPARTblock( struct Device* dev, int32_t nSect, struct bPARTblock* blk )
{
    UCHAR buf[ sizeof(struct bPARTblock) ];
    struct nativeFunctions *nFct;
    RETCODE rc2, rc = RC_OK;
	
    nFct = adfEnv.nativeFct;
    if (dev->isNativeDev)
        rc2=(*nFct->adfNativeReadSector)(dev, nSect, sizeof(struct bPARTblock), buf);
    else
        rc2=adfReadDumpSector(dev, nSect, sizeof(struct bPARTblock), buf);

    if (rc2!=RC_OK)
       return RC_ERROR;

    memcpy(blk, buf, sizeof(struct bPARTblock));
#ifdef LITT_ENDIAN
    /* big to little = 68000 to x86 */
    swapEndian((uint8_t*)blk, SWBL_PART);
#endif

    if ( strncmp(blk->id,"PART",4)!=0 ) {
    	(*adfEnv.eFct)("ReadPARTblock : PART id not found");
        return RC_ERROR;
    }

    if ( blk->size != 64 )
        (*adfEnv.wFct)("ReadPARTBlock : size != 64");

    if ( blk->blockSize!=128 ) {
    	(*adfEnv.eFct)("ReadPARTblock : blockSize!=512, not supported (yet)");
        return RC_ERROR;
    }

    if ( blk->checksum != adfNormalSum(buf,8,256) )
        (*adfEnv.wFct)( "ReadPARTBlock : incorrect checksum");

    return rc;
}


/*
 * adfWritePARTblock
 *
 */
    RETCODE
adfWritePARTblock(struct Device *dev, int32_t nSect, struct bPARTblock* part)
{
    uint8_t buf[LOGICAL_BLOCK_SIZE];
    uint32_t newSum;
    struct nativeFunctions *nFct;
    RETCODE rc2, rc = RC_OK;
	
    if (dev->readOnly) {
        (*adfEnv.wFct)("adfWritePARTblock : can't write block, read only device");
        return RC_ERROR;
    }

    memset(buf,0,LOGICAL_BLOCK_SIZE);

    strncpy(part->id,"PART",4);
    part->size = sizeof(struct bPARTblock)/sizeof(int32_t);
    part->blockSize = LOGICAL_BLOCK_SIZE;
    part->vectorSize = 16;
	part->blockSize = 128;
    part->sectorsPerBlock = 1;
	part->dosReserved = 2;

    memcpy(buf, part, sizeof(struct bPARTblock));
#ifdef LITT_ENDIAN
    swapEndian(buf, SWBL_PART);
#endif

    newSum = adfNormalSum(buf, 8, LOGICAL_BLOCK_SIZE);
    swLong(buf+8, newSum);
/*    *(int32_t*)(buf+8) = swapLong((uint8_t*)&newSum);*/

    nFct = adfEnv.nativeFct;
    if (dev->isNativeDev)
        rc2=(*nFct->adfNativeWriteSector)(dev, nSect, LOGICAL_BLOCK_SIZE, buf);
    else
        rc2=adfWriteDumpSector(dev, nSect, LOGICAL_BLOCK_SIZE, buf);
    if (rc2!=RC_OK)
        return RC_ERROR;

    return rc;
}

/*
 * ReadFSHDblock
 *
 */
    RETCODE
adfReadFSHDblock( struct Device* dev, int32_t nSect, struct bFSHDblock* blk)
{
    UCHAR buf[sizeof(struct bFSHDblock)];
    struct nativeFunctions *nFct;
    RETCODE rc;
	
    nFct = adfEnv.nativeFct;
    if (dev->isNativeDev)
        rc = (*nFct->adfNativeReadSector)(dev, nSect, sizeof(struct bFSHDblock), buf);
    else
        rc = adfReadDumpSector(dev, nSect, sizeof(struct bFSHDblock), buf);
    if (rc!=RC_OK)
        return RC_ERROR;
		
    memcpy(blk, buf, sizeof(struct bFSHDblock));
#ifdef LITT_ENDIAN
    /* big to little = 68000 to x86 */
    swapEndian((uint8_t*)blk, SWBL_FSHD);
#endif

    if ( strncmp(blk->id,"FSHD",4)!=0 ) {
    	(*adfEnv.eFct)("ReadFSHDblock : FSHD id not found");
        return RC_ERROR;
    }

    if ( blk->size != 64 )
         (*adfEnv.wFct)("ReadFSHDblock : size != 64");

    if ( blk->checksum != adfNormalSum(buf,8,256) )
        (*adfEnv.wFct)( "ReadFSHDblock : incorrect checksum");

    return RC_OK;
}


/*
 *  adfWriteFSHDblock
 *
 */
    RETCODE
adfWriteFSHDblock(struct Device *dev, int32_t nSect, struct bFSHDblock* fshd)
{
    uint8_t buf[LOGICAL_BLOCK_SIZE];
    uint32_t newSum;
    struct nativeFunctions *nFct;
    RETCODE rc = RC_OK;

    if (dev->readOnly) {
        (*adfEnv.wFct)("adfWriteFSHDblock : can't write block, read only device");
        return RC_ERROR;
    }

    memset(buf,0,LOGICAL_BLOCK_SIZE);

    strncpy(fshd->id,"FSHD",4);
    fshd->size = sizeof(struct bFSHDblock)/sizeof(int32_t);

    memcpy(buf, fshd, sizeof(struct bFSHDblock));
#ifdef LITT_ENDIAN
    swapEndian(buf, SWBL_FSHD);
#endif

    newSum = adfNormalSum(buf, 8, LOGICAL_BLOCK_SIZE);
    swLong(buf+8, newSum);
/*    *(int32_t*)(buf+8) = swapLong((uint8_t*)&newSum);*/

    nFct = adfEnv.nativeFct;
    if (dev->isNativeDev)
        rc=(*nFct->adfNativeWriteSector)(dev, nSect, LOGICAL_BLOCK_SIZE, buf);
    else
        rc=adfWriteDumpSector(dev, nSect, LOGICAL_BLOCK_SIZE, buf);
    if (rc!=RC_OK)
        return RC_ERROR;

    return RC_OK;
}


/*
 * ReadLSEGblock
 *
 */
   RETCODE
adfReadLSEGblock(struct Device* dev, int32_t nSect, struct bLSEGblock* blk)
{
    UCHAR buf[sizeof(struct bLSEGblock)];
    struct nativeFunctions *nFct;
    RETCODE rc;
	
    nFct = adfEnv.nativeFct;
    if (dev->isNativeDev)
        rc=(*nFct->adfNativeReadSector)(dev, nSect, sizeof(struct bLSEGblock), buf);
    else
        rc=adfReadDumpSector(dev, nSect, sizeof(struct bLSEGblock), buf);
    if (rc!=RC_OK)
        return RC_ERROR;
		
    memcpy(blk, buf, sizeof(struct bLSEGblock));
#ifdef LITT_ENDIAN
    /* big to little = 68000 to x86 */
    swapEndian((uint8_t*)blk, SWBL_LSEG);
#endif

    if ( strncmp(blk->id,"LSEG",4)!=0 ) {
    	(*adfEnv.eFct)("ReadLSEGblock : LSEG id not found");
        return RC_ERROR;
    }

    if ( blk->checksum != adfNormalSum(buf,8,sizeof(struct bLSEGblock)) )
        (*adfEnv.wFct)("ReadLSEGBlock : incorrect checksum");

    if ( blk->next!=-1 && blk->size != 128 )
        (*adfEnv.wFct)("ReadLSEGBlock : size != 128");

    return RC_OK;
}


/*
 * adfWriteLSEGblock
 *
 */
    RETCODE
adfWriteLSEGblock(struct Device *dev, int32_t nSect, struct bLSEGblock* lseg)
{
    uint8_t buf[LOGICAL_BLOCK_SIZE];
    uint32_t newSum;
    struct nativeFunctions *nFct;
    RETCODE rc;

    if (dev->readOnly) {
        (*adfEnv.wFct)("adfWriteLSEGblock : can't write block, read only device");
        return RC_ERROR;
    }

    memset(buf,0,LOGICAL_BLOCK_SIZE);

    strncpy(lseg->id,"LSEG",4);
    lseg->size = sizeof(struct bLSEGblock)/sizeof(int32_t);

    memcpy(buf, lseg, sizeof(struct bLSEGblock));
#ifdef LITT_ENDIAN
    swapEndian(buf, SWBL_LSEG);
#endif

    newSum = adfNormalSum(buf, 8, LOGICAL_BLOCK_SIZE);
    swLong(buf+8,newSum);
/*    *(int32_t*)(buf+8) = swapLong((uint8_t*)&newSum);*/

    nFct = adfEnv.nativeFct;
    if (dev->isNativeDev)
        rc=(*nFct->adfNativeWriteSector)(dev, nSect, LOGICAL_BLOCK_SIZE, buf);
    else
        rc=adfWriteDumpSector(dev, nSect, LOGICAL_BLOCK_SIZE, buf);

    if (rc!=RC_OK)
        return RC_ERROR;

    return RC_OK;
}

/*##########################################################################*/
