#ifndef _ADF_SALV_H
#define _ADF_SALV_H 1

/*
 *  ADF Library. (C) 1997-2002 Laurent Clevy
 *
 *  adf_salv.h
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

#include"prefix.h"

#include "adf_str.h"

RETCODE adfReadGenBlock(struct Volume *vol, SECTNUM nSect, struct GenBlock *block);
PREFIX RETCODE adfCheckEntry(struct Volume* vol, SECTNUM nSect, int level);
PREFIX RETCODE adfUndelEntry(struct Volume* vol, SECTNUM parent, SECTNUM nSect);
PREFIX struct List* adfGetDelEnt(struct Volume *vol);
PREFIX void adfFreeDelList(struct List* list);


/*##########################################################################*/
#endif /* _ADF_SALV_H */

