#ifndef ADF_BITM_H
#define ADF_BITM_H
/*
 *  ADF Library. (C) 1997-2002 Laurent Clevy
 *
 *  adf_bitm.h
 *
 *  $Id$
 *
 *  bitmap code
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
#include"prefix.h"

RETCODE adfReadBitmapBlock(struct Volume*, SECTNUM nSect, struct bBitmapBlock*);
RETCODE adfWriteBitmapBlock(struct Volume*, SECTNUM nSect, struct bBitmapBlock*);
RETCODE adfReadBitmapExtBlock(struct Volume*, SECTNUM nSect, struct bBitmapExtBlock*);
RETCODE adfWriteBitmapExtBlock(struct Volume*, SECTNUM, struct bBitmapExtBlock* );

SECTNUM adfGet1FreeBlock(struct Volume *vol);
RETCODE adfUpdateBitmap(struct Volume *vol);
PREFIX int32_t adfCountFreeBlocks(struct Volume* vol);
RETCODE adfReadBitmap(struct Volume* , SECTNUM nBlock, struct bRootBlock* root);
BOOL adfIsBlockFree(struct Volume* vol, SECTNUM nSect);
void adfSetBlockFree(struct Volume* vol, SECTNUM nSect);
void adfSetBlockUsed(struct Volume* vol, SECTNUM nSect);
BOOL adfGetFreeBlocks(struct Volume* vol, int nbSect, SECTNUM* sectList);
RETCODE adfCreateBitmap(struct Volume *vol);
RETCODE adfWriteNewBitmap(struct Volume *vol);
void adfFreeBitmap(struct Volume *vol);

#endif /* ADF_BITM_H */

/*#######################################################################################*/
