#ifndef _ADF_CACHE_H
#define _ADF_CACHE_H 1
/*
 *  ADF Library. (C) 1997-2002 Laurent Clevy
 *
 *  adf_cache.h
 *
 *  $Id$
 *
 *  directory cache code
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


#include "adf_str.h"

void adfGetCacheEntry(struct bDirCacheBlock *dirc, int *p, struct CacheEntry *cEntry);
int adfPutCacheEntry( struct bDirCacheBlock *dirc, int *p, struct CacheEntry *cEntry);

struct List* adfGetDirEntCache(struct Volume *vol, SECTNUM dir, BOOL recurs);

RETCODE adfCreateEmptyCache(struct Volume *vol, struct bEntryBlock *parent, SECTNUM nSect);
RETCODE adfAddInCache(struct Volume *vol, struct bEntryBlock *parent, struct bEntryBlock *entry);
RETCODE adfUpdateCache(struct Volume *vol, struct bEntryBlock *parent, struct bEntryBlock *entry, BOOL);
RETCODE adfDelFromCache(struct Volume *vol, struct bEntryBlock *parent, SECTNUM);

RETCODE adfReadDirCBlock(struct Volume *vol, SECTNUM nSect, struct bDirCacheBlock *dirc);
RETCODE adfWriteDirCBlock(struct Volume*, int32_t, struct bDirCacheBlock* dirc);

#endif /* _ADF_CACHE_H */

/*##########################################################################*/
