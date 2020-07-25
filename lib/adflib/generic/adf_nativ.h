/*
 * adf_nativ.h
 *
 * $ID$
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

#ifndef ADF_NATIV_H
#define ADF_NATIV_H

#include<stdio.h>
#include"adf_str.h"

#define NATIVE_FILE  8001

#ifndef BOOL
#define BOOL int
#endif

#ifndef RETCODE
#define RETCODE int32_t
#endif

struct nativeDevice{
    FILE* fd;
};

struct nativeFunctions{
    /* called by adfMount() */
    RETCODE (*adfInitDevice)(struct Device*, char*,BOOL);
    /* called by adfReadBlock() */
    RETCODE (*adfNativeReadSector)(struct Device*, int32_t, int, uint8_t*);
    /* called by adfWriteBlock() */
    RETCODE (*adfNativeWriteSector)(struct Device*, int32_t, int, uint8_t*);
    /* called by adfMount() */
    BOOL (*adfIsDevNative)(char*);
    /* called by adfUnMount() */
    RETCODE (*adfReleaseDevice)();
};

void adfInitNativeFct();


RETCODE myReadSector(struct Device *dev, int32_t n, int size, uint8_t* buf);
RETCODE myWriteSector(struct Device *dev, int32_t n, int size, uint8_t* buf);
RETCODE myInitDevice(struct Device *dev, char* name,BOOL);
RETCODE myReleaseDevice(struct Device *dev);
BOOL myIsDevNative(char*);

#endif /* ADF_NATIV_H */

/*#######################################################################################*/
