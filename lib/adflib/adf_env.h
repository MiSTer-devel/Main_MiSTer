#ifndef ADF_ENV_H
#define ADF_ENV_H 1

/*
 *  ADF Library. (C) 1997-2002 Laurent Clevy
 *
 *  adf_env.h
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

PREFIX void adfEnvInitDefault();
PREFIX void adfSetEnvFct( void(*e)(char*), void(*w)(char*), void(*v)(char*),
	void(*n)(SECTNUM,int) );
PREFIX void adfEnvCleanUp();
PREFIX void adfChgEnvProp(int prop, void *new);
PREFIX char* adfGetVersionNumber();
PREFIX char* adfGetVersionDate();

#endif /* ADF_ENV_H */
/*##########################################################################*/
