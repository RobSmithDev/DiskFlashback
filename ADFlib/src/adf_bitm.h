#ifndef ADF_BITM_H
#define ADF_BITM_H
/*
 *  ADF Library. (C) 1997-2002 Laurent Clevy
 *
 *  adf_bitm.h
 *
 *  $Id$
 *
 *  bitmap code
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

#include "adf_blk.h"
#include "adf_err.h"
#include "adf_types.h"
#include "adf_vol.h"
#include "prefix.h"

RETCODE adfReadBitmapBlock ( struct AdfVolume * const    vol,
                             const SECTNUM               nSect,
                             struct bBitmapBlock * const bitm );

RETCODE adfWriteBitmapBlock ( struct AdfVolume * const          vol,
                              const SECTNUM                     nSect,
                              const struct bBitmapBlock * const bitm );

RETCODE adfReadBitmapExtBlock ( struct AdfVolume * const       vol,
                                const SECTNUM                  nSect,
                                struct bBitmapExtBlock * const bitme );

RETCODE adfWriteBitmapExtBlock ( struct AdfVolume * const             vol,
                                 const SECTNUM                        nSect,
                                 const struct bBitmapExtBlock * const bitme );

SECTNUM adfGet1FreeBlock ( struct AdfVolume * const vol );
RETCODE adfUpdateBitmap ( struct AdfVolume * const vol );
PREFIX uint32_t adfCountFreeBlocks ( const struct AdfVolume * const vol );

RETCODE adfReadBitmap ( struct AdfVolume * const        vol,
                        const uint32_t                  nBlock,
                        const struct bRootBlock * const root );

BOOL adfIsBlockFree ( const struct AdfVolume * const vol,
                      const SECTNUM            nSect );

void adfSetBlockFree ( struct AdfVolume * const vol,
                       const SECTNUM            nSect );

void adfSetBlockUsed ( struct AdfVolume * const vol,
                       const SECTNUM            nSect );

BOOL adfGetFreeBlocks ( struct AdfVolume * const vol,
                        const int                nbSect,
                        SECTNUM * const          sectList );

RETCODE adfCreateBitmap ( struct AdfVolume * const vol );
RETCODE adfWriteNewBitmap ( struct AdfVolume * const vol );
void adfFreeBitmap ( struct AdfVolume * const vol );

#endif /* ADF_BITM_H */

/*#######################################################################################*/
