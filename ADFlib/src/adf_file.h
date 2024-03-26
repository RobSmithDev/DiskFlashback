#ifndef ADF_FILE_H
#define ADF_FILE_H 1

/*
 *  ADF Library. (C) 1997-2002 Laurent Clevy
 *
 *  adf_file.h
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

#include "adf_blk.h"
#include "adf_vol.h"


/* ----- FILE ----- */

struct AdfFile {
    struct AdfVolume *        volume;

    struct bFileHeaderBlock * fileHdr;
    void *                    currentData;
    struct bFileExtBlock *    currentExt;

    unsigned nDataBlock;  /* current data block number */
    SECTNUM  curDataPtr;  /* sector number of current data block;
                             if == 0 -> data in the buffer (currentData) is
                                        invalid (eg. block not read correctly) */
    uint32_t pos;

    unsigned posInDataBlk;
    unsigned posInExtBlk;

    BOOL     modeRead,
             modeWrite;

    BOOL     currentDataBlockChanged;
};


typedef enum {
    ADF_FILE_MODE_READ      = 0x01,   /* 01 */
    ADF_FILE_MODE_WRITE     = 0x02,   /* 10 */
    //ADF_FILE_MODE_READWRITE = 0x03    /* 11 */
} AdfFileMode;


static inline uint32_t adfFileGetPos ( const struct AdfFile * const file ) {
    return file->pos;
}

static inline uint32_t adfFileGetSize ( const struct AdfFile * const file ) {
    return file->fileHdr->byteSize;
}

static inline BOOL adfEndOfFile ( const struct AdfFile * const file ) {
    return ( file->pos == file->fileHdr->byteSize );
}


PREFIX int32_t adfPos2DataBlock ( const unsigned   pos,
                                  const unsigned   blockSize,
                                  unsigned * const posInExtBlk,
                                  unsigned * const posInDataBlk,
                                  unsigned * const curDataN );

PREFIX struct AdfFile * adfFileOpen ( struct AdfVolume * const vol,
                                      const char * const       name,
                                      const AdfFileMode        mode );

PREFIX void adfFileClose ( struct AdfFile * const file );

PREFIX uint32_t adfFileRead ( struct AdfFile * const file,
                              const uint32_t         n,
                              uint8_t * const        buffer );

PREFIX RETCODE adfFileSeek ( struct AdfFile * const file,
                             const uint32_t         pos );		/* BV */

static inline RETCODE adfFileSeekStart ( struct AdfFile * const file ) {
    return adfFileSeek ( file, 0 );
}


static inline RETCODE adfFileSeekEOF ( struct AdfFile * const file ) {
    return adfFileSeek ( file, adfFileGetSize ( file ) );
}


RETCODE adfFileReadNextBlock ( struct AdfFile * const file );

PREFIX uint32_t adfFileWrite ( struct AdfFile * const file,
                               const uint32_t         n,
                               const uint8_t * const  buffer );

PREFIX unsigned adfFileWriteFilled ( struct AdfFile * const file,
                                     const uint8_t          fillValue,
                                     uint32_t               size );

PREFIX RETCODE adfFileTruncate ( struct AdfFile * const file,
                                 const uint32_t         fileSizeNew );

RETCODE adfFileCreateNextBlock ( struct AdfFile * const file );

PREFIX RETCODE adfFileFlush ( struct AdfFile * const file );

PREFIX RETCODE adfFileReadExtBlockN ( const struct AdfFile * const file,
                                      const int32_t                extBlock,
                                      struct bFileExtBlock * const fext );

PREFIX RETCODE adfFileTruncateGetBlocksToRemove ( const struct AdfFile * const file,
                                                  const uint32_t               fileSizeNew,
                                                  AdfVectorSectors * const     blocksToRemove );
#endif /* ADF_FILE_H */

