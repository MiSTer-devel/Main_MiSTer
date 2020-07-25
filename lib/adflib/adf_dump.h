#ifndef ADF_DUMP_H
#define ADF_DUMP_H 1

/*
 *  ADF Library. (C) 1997-2002 Laurent Clevy
 *
 *  adf_dump.h
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

PREFIX     struct Device*
adfCreateDumpDevice(char* filename, int32_t cyl, int32_t heads, int32_t sec);
PREFIX RETCODE adfCreateHdFile(struct Device* dev, char* volName, int volType);
BOOL adfInitDumpDevice(struct Device* dev, char* name,BOOL);
BOOL adfReadDumpSector(struct Device *dev, int32_t n, int size, uint8_t* buf);
BOOL adfWriteDumpSector(struct Device *dev, int32_t n, int size, uint8_t* buf);
RETCODE adfReleaseDumpDevice(struct Device *dev);


#endif /* ADF_DUMP_H */
/*##########################################################################*/
