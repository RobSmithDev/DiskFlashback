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

#include "adf_types.h"
#include "adf_nativ.h"
#include "prefix.h"

/* ----- ENVIRONMENT ----- */

#define PR_VFCT	        1
#define PR_WFCT	        2
#define PR_EFCT	        3
#define PR_NOTFCT       4
#define PR_USEDIRC      5
#define PR_USE_NOTFCT   6
#define PR_PROGBAR      7
#define PR_USE_PROGBAR  8
#define PR_RWACCESS     9
#define PR_USE_RWACCESS 10

//typedef void (*AdfLogFct)(const char * const txt);
typedef void (*AdfLogFct)(const char * const format, ...);
//typedef void (*AdfLogFileFct)(FILE * file, const char * const format, ...);

typedef void (*AdfNotifyFct)(SECTNUM, int);
typedef void (*AdfRwhAccessFct)(SECTNUM,SECTNUM,BOOL);
typedef void (*AdfProgressBarFct)(int);

struct AdfEnv {
    AdfLogFct vFct;       /* verbose callback function */
    AdfLogFct wFct;       /* warning callback function */
    AdfLogFct eFct;       /* error callback function */

    AdfNotifyFct notifyFct;
    BOOL useNotify;

    AdfRwhAccessFct rwhAccess;
    BOOL useRWAccess;

    AdfProgressBarFct progressBar;
    BOOL useProgressBar;

    BOOL useDirCache;

    void *nativeFct;
};


PREFIX void adfEnvInitDefault();

PREFIX void adfSetEnvFct ( const AdfLogFct    eFct,
                           const AdfLogFct    wFct,
                           const AdfLogFct    vFct,
                           const AdfNotifyFct notifyFct );
PREFIX void adfSetNative(const struct AdfNativeFunctions* nativeFunctions);

PREFIX void adfEnvCleanUp();
PREFIX void adfChgEnvProp(int prop, void *newval);
PREFIX char* adfGetVersionNumber();
PREFIX char* adfGetVersionDate();

extern struct AdfEnv adfEnv;

#endif /* ADF_ENV_H */
/*##########################################################################*/
