/*
 *  ADF Library. (C) 1997-2002 Laurent Clevy
 *
 *  adf_raw.h
 *
 *  $Id$
 *
 *  blocks level code
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

#ifndef _ADF_RAW_H
#define _ADF_RAW_H 1

#include "adf_str.h"

#define SW_LONG  4
#define SW_SHORT 2
#define SW_CHAR  1

#define MAX_SWTYPE 11

#define SWBL_BOOT         0
#define SWBL_ROOT         1
#define SWBL_DATA         2
#define SWBL_FILE         3
#define SWBL_ENTRY        3
#define SWBL_DIR          3
#define SWBL_CACHE        4
#define SWBL_BITMAP       5
#define SWBL_FEXT         5
#define SWBL_LINK         6
#define SWBL_BITMAPE      5
#define SWBL_RDSK         7
#define SWBL_BADB         8
#define SWBL_PART         9
#define SWBL_FSHD         10 
#define SWBL_LSEG         11

RETCODE adfReadRootBlock(struct Volume*, int32_t nSect, struct bRootBlock* root);
RETCODE adfWriteRootBlock(struct Volume* vol, int32_t nSect, struct bRootBlock* root);
RETCODE adfReadBootBlock(struct Volume*, struct bBootBlock* boot);
RETCODE adfWriteBootBlock(struct Volume* vol, struct bBootBlock* boot);

uint32_t adfBootSum(uint8_t *buf);
uint32_t adfNormalSum( uint8_t *buf, int offset, int bufLen );

void swapEndian( uint8_t *buf, int type );

#endif /* _ADF_RAW_H */

/*##########################################################################*/
