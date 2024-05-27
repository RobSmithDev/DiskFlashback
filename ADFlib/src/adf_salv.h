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
 *  along with ADFLib; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef ADF_SALV_H
#define ADF_SALV_H

#include "adf_types.h"
#include "adf_err.h"
#include "adf_prefix.h"
#include "adf_vol.h"

struct GenBlock {
    ADF_SECTNUM sect;
    ADF_SECTNUM parent;
    int type;
    int secType;
    char *name;	/* if (type == 2 and (secType==2 or secType==-3)) */
};


ADF_RETCODE adfReadGenBlock ( struct AdfVolume * const vol,
                              const ADF_SECTNUM        nSect,
                              struct GenBlock * const  block );

ADF_PREFIX ADF_RETCODE adfCheckEntry ( struct AdfVolume * const vol,
                                       const ADF_SECTNUM        nSect,
                                       const int                level );

ADF_PREFIX ADF_RETCODE adfUndelEntry ( struct AdfVolume * const vol,
                                       const ADF_SECTNUM        parent,
                                       const ADF_SECTNUM        nSect );

ADF_PREFIX struct AdfList * adfGetDelEnt ( struct AdfVolume * const vol );
ADF_PREFIX void adfFreeDelList ( struct AdfList * const list );

#endif  /* ADF_SALV_H */
