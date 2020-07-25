#ifndef _PREFIX_H
#define _PREFIX_H 1

/*
 * prefix.h
 *
 *  $Id$
 *
 * adds symbol export directive under windows
 * does nothing under Linux
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

//#ifdef WIN32DLL
//#define PREFIX __declspec(dllexport)
//#else
#define PREFIX 
//#endif /* WIN32DLL */

#endif /* _PREFIX_H */
