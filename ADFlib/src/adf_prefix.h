/*
 * adf_prefix.h
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
 *  along with ADFLib; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */ 

#ifndef ADF_PREFIX_H
#define ADF_PREFIX_H
//#ifdef WIN32DLL
//#ifdef _WIN32
#ifdef BUILD_DLL


/* define declaration prefix for exporting symbols compiling a DLL library,
   and importing when compiling a client code

   more info:
   https://learn.microsoft.com/en-us/cpp/build/importing-into-an-application-using-declspec-dllimport?view=msvc-170
*/
#ifdef _EXPORTING
   #define ADF_PREFIX    __declspec(dllexport)
#else
   #define ADF_PREFIX    __declspec(dllimport)
#endif

#else
#define ADF_PREFIX
#endif

#endif  /* ADF_PREFIX_H */
