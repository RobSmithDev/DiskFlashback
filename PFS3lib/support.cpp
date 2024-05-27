#pragma once
#include "struct.h"
#include "kswrapper.h"
#include "ass.h"
#include "directory.h"
/*

/* SUPPORTFUNCTION dstricmp
** TRUE: dstring == cstring
** FALSE: cstring <> cstring
*/
bool dstricmp (DSTR dstring, STRPTR cstring)
{
  bool result;
  UBYTE temp[PATHSIZE];

	ctodstr ((unsigned char*)cstring, temp);
	intltoupper (temp);
	result = intlcmp (temp, dstring);  
	return result;
}

/* ddstricmp
** compare two dstrings
*/
bool ddstricmp (DSTR dstr1, DSTR dstr2)
{
  bool result;
  UBYTE temp[PATHSIZE];

	strncpy_s ((char*)temp, PATHSIZE-1,(char*)dstr1,min(* dstr1 + 1, PATHSIZE));
	intltoupper (temp);
	result = intlcmp (temp,dstr2);
	return result;
} 


// BCPLtoCString converts BCPL string to a CString. 
UBYTE *BCPLtoCString(STRPTR dest, DSTR src)
{
  UBYTE len, *to;

	len  = *(src++);
	len	 = min (len, PATHSIZE);
	to	 = (UBYTE*)dest;

	while (len--)
		*(dest++) = *(src++);
	*dest = 0x0;

	return to;
}
