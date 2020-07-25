#ifndef ADF_DIR_H
#define ADF_DIR_H 1

/*
 *  ADF Library. (C) 1997-2002 Laurent Clevy
 *
 *  adf_dir.h
 *
 *  $Id$
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

#include"adf_str.h"
#include"adf_err.h"
#include"adf_defs.h"

#include"prefix.h"

PREFIX RETCODE adfToRootDir(struct Volume *vol);
BOOL isDirEmpty(struct bDirBlock *dir);
PREFIX RETCODE adfRemoveEntry(struct Volume *vol, SECTNUM pSect, char *name);
PREFIX struct List* adfGetDirEnt(struct Volume* vol, SECTNUM nSect );
PREFIX struct List* adfGetRDirEnt(struct Volume* vol, SECTNUM nSect, BOOL recurs );
PREFIX void adfFreeDirList(struct List* list);

RETCODE adfEntBlock2Entry(struct bEntryBlock *entryBlk, struct Entry *entry);
PREFIX void adfFreeEntry(struct Entry *entry);
RETCODE adfCreateFile(struct Volume* vol, SECTNUM parent, char *name,
    struct bFileHeaderBlock *fhdr);
PREFIX RETCODE adfCreateDir(struct Volume* vol, SECTNUM parent, char* name);
SECTNUM adfCreateEntry(struct Volume *vol, struct bEntryBlock *dir, char *name, SECTNUM );
PREFIX RETCODE adfRenameEntry(struct Volume *vol, SECTNUM, char *old,SECTNUM,char *new);


RETCODE adfReadEntryBlock(struct Volume* vol, SECTNUM nSect, struct bEntryBlock* ent);
RETCODE adfWriteDirBlock(struct Volume* vol, SECTNUM nSect, struct bDirBlock *dir);
RETCODE adfWriteEntryBlock(struct Volume* vol, SECTNUM nSect, struct bEntryBlock *ent);

char* adfAccess2String(int32_t acc);
uint8_t adfIntlToUpper(uint8_t c);
int adfGetHashValue(uint8_t *name, BOOL intl);
void myToUpper( uint8_t *ostr, uint8_t *nstr, int,BOOL intl );
PREFIX RETCODE adfChangeDir(struct Volume* vol, char *name);
PREFIX RETCODE adfParentDir(struct Volume* vol);
PREFIX RETCODE adfSetEntryAccess(struct Volume*, SECTNUM, char*, int32_t);
PREFIX RETCODE adfSetEntryComment(struct Volume*, SECTNUM, char*, char*);
SECTNUM adfNameToEntryBlk(struct Volume *vol, int32_t ht[], char* name, 
    struct bEntryBlock *entry, SECTNUM *);

PREFIX void printEntry(struct Entry* entry);
void adfFreeDirList(struct List* list);

#endif /* ADF_DIR_H */

