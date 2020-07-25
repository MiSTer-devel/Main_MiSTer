/*
 *  ADF Library. (C) 1997-2002 Laurent Clevy
 *
 *  adf_salv.c
 *
 *  $Id$
 *
 * undelete and salvage code : EXPERIMENTAL !
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

#include<string.h>
#include<stdlib.h>

#include "adf_salv.h"
#include "adf_bitm.h"
#include "adf_util.h"
#include "adf_disk.h"
#include "adf_dir.h"
#include "adf_file.h"
#include "adf_cache.h"

extern struct Env adfEnv;

/*
 * adfFreeGenBlock
 *
 */
void adfFreeGenBlock(struct GenBlock* block)
{
    if (block->name!=NULL)
        free(block->name);
}


/*
 * adfFreeDelList
 *
 */
void adfFreeDelList(struct List* list)
{
    struct List *cell;

    cell = list;
    while(cell!=NULL) {
        adfFreeGenBlock((struct GenBlock*)cell->content);
        cell = cell->next;
    }
    freeList(list);
}


/*
 * adfGetDelEnt
 *
 */
struct List* adfGetDelEnt(struct Volume *vol)
{
    struct GenBlock *block;
    int32_t i;
    struct List *list, *head;
    BOOL delEnt;

    list = head = NULL;
    block = NULL;
    delEnt = TRUE;
    for(i=vol->firstBlock; i<=vol->lastBlock; i++) {
        if (adfIsBlockFree(vol, i)) {
            if (delEnt) {
                block = (struct GenBlock*)malloc(sizeof(struct GenBlock));
                if (!block) return NULL;
/*printf("%p\n",block);*/
            }

            adfReadGenBlock(vol, i, block);

            delEnt = (block->type==T_HEADER 
                && (block->secType==ST_DIR || block->secType==ST_FILE) );

            if (delEnt) {
                if (head==NULL)
                    list = head = newCell(NULL, (void*)block);
                else
                    list = newCell(list, (void*)block);
            }
        }
    }

    if (block!=NULL && list!=NULL && block!=list->content) {
        free(block);
/*        printf("%p\n",block);*/
    }
    return head;
}


/*
 * adfReadGenBlock
 *
 */
RETCODE adfReadGenBlock(struct Volume *vol, SECTNUM nSect, struct GenBlock *block)
{
	uint8_t buf[LOGICAL_BLOCK_SIZE];
    int len;
    char name[MAXNAMELEN+1];

	if (adfReadBlock(vol, nSect, buf)!=RC_OK)
		return RC_ERROR;

    block->type =(int) swapLong(buf);
    block->secType =(int) swapLong(buf+vol->blockSize-4);
    block->sect = nSect;
    block->name = NULL;

    if (block->type==T_HEADER) {
        switch(block->secType) {
        case ST_FILE:
        case ST_DIR:
        case ST_LFILE:
        case ST_LDIR:
            len = min(MAXNAMELEN, buf[vol->blockSize-80]);
            strncpy(name, (char*)buf+vol->blockSize-79, len);
            name[len] = '\0';
            block->name = strdup(name);
            block->parent = swapLong(buf+vol->blockSize-12);
            break;
        case ST_ROOT:
            break;
        default: 
            ;
        }
    }
    return RC_OK;
}


/*
 * adfCheckParent
 *
 */
RETCODE adfCheckParent(struct Volume* vol, SECTNUM pSect)
{
    struct GenBlock block;

    if (adfIsBlockFree(vol, pSect)) {
        (*adfEnv.wFct)("adfCheckParent : parent doesn't exists");
        return RC_ERROR;
    }

    /* verify if parent is a DIR or ROOT */
    adfReadGenBlock(vol, pSect, &block);
    if ( block.type!=T_HEADER 
        || (block.secType!=ST_DIR && block.secType!=ST_ROOT) ) {
        (*adfEnv.wFct)("adfCheckParent : parent secType is incorrect");
        return RC_ERROR;
    }

    return RC_OK;
}


/*
 * adfUndelDir
 *
 */
RETCODE adfUndelDir(struct Volume* vol, SECTNUM pSect, SECTNUM nSect, 
    struct bDirBlock* entry)
{
    RETCODE rc;
    struct bEntryBlock parent;
    char name[MAXNAMELEN+1];

    /* check if the given parent sector pointer seems OK */
    if ( (rc=adfCheckParent(vol,pSect)) != RC_OK)
        return rc;

    if (pSect!=entry->parent) {
        (*adfEnv.wFct)("adfUndelDir : the given parent sector isn't the entry parent");
        return RC_ERROR;
    }

    if (!adfIsBlockFree(vol, entry->headerKey))
        return RC_ERROR;
    if (isDIRCACHE(vol->dosType) && !adfIsBlockFree(vol,entry->extension))
        return RC_ERROR;

    if (adfReadEntryBlock(vol, pSect, &parent)!=RC_OK)
		return RC_ERROR;

    strncpy(name, entry->dirName, entry->nameLen);
    name[(int)entry->nameLen] = '\0';
    /* insert the entry in the parent hashTable, with the headerKey sector pointer */
    adfSetBlockUsed(vol,entry->headerKey);
    adfCreateEntry(vol, &parent, name, entry->headerKey);

    if (isDIRCACHE(vol->dosType)) {
        adfAddInCache(vol, &parent, (struct bEntryBlock *)entry);
        adfSetBlockUsed(vol,entry->extension);
    }

    adfUpdateBitmap(vol);

    return RC_OK;
}


/*
 * adfUndelFile
 *
 */
RETCODE adfUndelFile(struct Volume* vol, SECTNUM pSect, SECTNUM nSect, struct bFileHeaderBlock* entry)
{
    int32_t i;
    char name[MAXNAMELEN+1];
    struct bEntryBlock parent;
    RETCODE rc;
    struct FileBlocks fileBlocks;

    /* check if the given parent sector pointer seems OK */
    if ( (rc=adfCheckParent(vol,pSect)) != RC_OK)
        return rc;

    if (pSect!=entry->parent) {
        (*adfEnv.wFct)("adfUndelFile : the given parent sector isn't the entry parent");
        return RC_ERROR;
    }

    adfGetFileBlocks(vol, entry, &fileBlocks);

    for(i=0; i<fileBlocks.nbData; i++)
        if ( !adfIsBlockFree(vol,fileBlocks.data[i]) )
            return RC_ERROR;
        else
            adfSetBlockUsed(vol, fileBlocks.data[i]);
    for(i=0; i<fileBlocks.nbExtens; i++)
        if ( !adfIsBlockFree(vol,fileBlocks.extens[i]) )
            return RC_ERROR;
        else
            adfSetBlockUsed(vol, fileBlocks.extens[i]);

    free(fileBlocks.data);
    free(fileBlocks.extens);

    if (adfReadEntryBlock(vol, pSect, &parent)!=RC_OK)
		return RC_ERROR;

    strncpy(name, entry->fileName, entry->nameLen);
    name[(int)entry->nameLen] = '\0';
    /* insert the entry in the parent hashTable, with the headerKey sector pointer */
    adfCreateEntry(vol, &parent, name, entry->headerKey);

    if (isDIRCACHE(vol->dosType))
        adfAddInCache(vol, &parent, (struct bEntryBlock *)entry);

    adfUpdateBitmap(vol);

    return RC_OK;
}


/*
 * adfUndelEntry
 *
 */
RETCODE adfUndelEntry(struct Volume* vol, SECTNUM parent, SECTNUM nSect)
{
    struct bEntryBlock entry;

    adfReadEntryBlock(vol,nSect,&entry);

    switch(entry.secType) {
    case ST_FILE:
        adfUndelFile(vol, parent, nSect, (struct bFileHeaderBlock*)&entry);
        break;
    case ST_DIR:
        adfUndelDir(vol, parent, nSect, (struct bDirBlock*)&entry);
        break;
    default:
        ;
    }

    return RC_OK;
}


/*
 * adfCheckFile
 *
 */
RETCODE adfCheckFile(struct Volume* vol, SECTNUM nSect,
    struct bFileHeaderBlock* file, int level)
{
    struct bFileExtBlock extBlock;
    struct bOFSDataBlock dataBlock;
    struct FileBlocks fileBlocks;
    int n;
 
    adfGetFileBlocks(vol,file,&fileBlocks);
/*printf("data %ld ext %ld\n",fileBlocks.nbData,fileBlocks.nbExtens);*/
    if (isOFS(vol->dosType)) {
        /* checks OFS datablocks */
        for(n=0; n<fileBlocks.nbData; n++) {
/*printf("%ld\n",fileBlocks.data[n]);*/
            adfReadDataBlock(vol,fileBlocks.data[n],&dataBlock);
            if (dataBlock.headerKey!=fileBlocks.header)
                (*adfEnv.wFct)("adfCheckFile : headerKey incorrect");
            if (dataBlock.seqNum!=n+1)
                (*adfEnv.wFct)("adfCheckFile : seqNum incorrect");
            if (n<fileBlocks.nbData-1) {
                if (dataBlock.nextData!=fileBlocks.data[n+1])
                    (*adfEnv.wFct)("adfCheckFile : nextData incorrect");
                if (dataBlock.dataSize!=vol->datablockSize)
                    (*adfEnv.wFct)("adfCheckFile : dataSize incorrect");
            }
            else { /* last datablock */
                if (dataBlock.nextData!=0)
                    (*adfEnv.wFct)("adfCheckFile : nextData incorrect");
            }
        }
    }
    for(n=0; n<fileBlocks.nbExtens; n++) {
        adfReadFileExtBlock(vol,fileBlocks.extens[n],&extBlock);
        if (extBlock.parent!=file->headerKey)
            (*adfEnv.wFct)("adfCheckFile : extBlock parent incorrect");
        if (n<fileBlocks.nbExtens-1) {
            if (extBlock.extension!=fileBlocks.extens[n+1])
                (*adfEnv.wFct)("adfCheckFile : nextData incorrect");
        }
        else
            if (extBlock.extension!=0)
                (*adfEnv.wFct)("adfCheckFile : nextData incorrect");
    }

    free(fileBlocks.data);
    free(fileBlocks.extens);

    return RC_OK;
}


/*
 * adfCheckDir
 *
 */
RETCODE adfCheckDir(struct Volume* vol, SECTNUM nSect, struct bDirBlock* dir, 
    int level)
{




    return RC_OK;
}


/*
 * adfCheckEntry
 *
 */
RETCODE adfCheckEntry(struct Volume* vol, SECTNUM nSect, int level)
{
    struct bEntryBlock entry;
    RETCODE rc;

    adfReadEntryBlock(vol,nSect,&entry);

    switch(entry.secType) {
    case ST_FILE:
        rc = adfCheckFile(vol, nSect, (struct bFileHeaderBlock*)&entry, level);
        break;
    case ST_DIR:
        rc = adfCheckDir(vol, nSect, (struct bDirBlock*)&entry, level);
        break;
    default:
/*        printf("adfCheckEntry : not supported\n");*/					/* BV */
        rc = RC_ERROR;
    }

    return rc;
}


/*#############################################################################*/
