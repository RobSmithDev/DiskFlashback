/*
 *  ADF Library. (C) 1997-2002 Laurent Clevy
 *
 *  adf_file.c
 *
 *  $Id$
 *
 *  file code
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

#include "adf_file.h"

#include "adf_bitm.h"
#include "adf_cache.h"
#include "adf_dev.h"
#include "adf_dir.h"
#include "adf_env.h"
#include "adf_file_block.h"
#include "adf_file_util.h"
#include "adf_raw.h"
#include "adf_str.h"
#include "adf_util.h"

#include <stdlib.h>
#include <string.h>



// debugging
//#define DEBUG_ADF_FILE

#ifdef DEBUG_ADF_FILE

static void show_File ( const struct AdfFile * const file );

static void show_bFileHeaderBlock (
    const struct bFileHeaderBlock * const block );

static void show_bFileExtBlock (
    const struct bFileExtBlock * const block );

#else
#define NDEBUG
#endif

#include <assert.h>


RETCODE adfFileTruncateGetBlocksToRemove ( const struct AdfFile * const file,
                                           const uint32_t               fileSizeNew,
                                           AdfVectorSectors * const     blocksToRemove )
{
    blocksToRemove->len     = 0;
    blocksToRemove->sectors = NULL;

    const unsigned fileSizeOld = file->fileHdr->byteSize;
    if ( fileSizeOld < fileSizeNew )
        return RC_OK;

    const unsigned dBlockSize  = file->volume->datablockSize;

    const unsigned nDBlocksOld = adfFileSize2Datablocks ( fileSizeOld, dBlockSize );
    const unsigned nDBlocksNew = adfFileSize2Datablocks ( fileSizeNew, dBlockSize );

    const unsigned nExtBlocksOld = adfFileDatablocks2Extblocks ( nDBlocksOld );
    const unsigned nExtBlocksNew = adfFileDatablocks2Extblocks ( nDBlocksNew );

    unsigned const nBlocksToRemove = ( nDBlocksOld + nExtBlocksOld ) -
                                     ( nDBlocksNew + nExtBlocksNew );

    if ( nBlocksToRemove < 1 )
        return RC_OK;

    blocksToRemove->sectors = malloc ( nBlocksToRemove * sizeof(SECTNUM) );
    if ( blocksToRemove->sectors == NULL )
        return RC_MALLOC;
    blocksToRemove->len = nBlocksToRemove;

#ifdef DEBUG_ADF_FILE
/*    printf (" fileSizeOld %u\n"
            " fileSizeNew %u\n"
            " dBlockSize  %u\n"
            " nDBlocksOld %u\n"
            " nDBlocksNew %u\n"
            " nExtBlocksOld %u\n"
            " nExtBlocksNew %u\n"
            " nBlocksToRemove %u\n",
            fileSizeOld,
            fileSizeNew,
            dBlockSize,
            nDBlocksOld,
            nDBlocksNew ,
            nExtBlocksOld,
            nExtBlocksNew,
            nBlocksToRemove );
    fflush ( stdout );
*/
#endif

    unsigned blocksCount = 0;
    unsigned dataBlocksCount = 0;
    if ( nExtBlocksOld < 1 ) {
        // no ext. blocks (neither in orig. or truncated)
        int32_t * const dataBlocks = file->fileHdr->dataBlocks;
        for ( unsigned i = nDBlocksNew + 1 ; i <= nDBlocksOld ; ++i ) {
            assert ( blocksCount < nBlocksToRemove );
            blocksToRemove->sectors [ blocksCount++ ] = dataBlocks [ MAX_DATABLK - i ];
            dataBlocksCount++;
        }
    } else {
        // the file to truncate has at least one ext. block

        struct bFileExtBlock * const extBlock = malloc ( sizeof ( struct bFileExtBlock ) );
        if ( extBlock == NULL ) {
            free ( blocksToRemove->sectors );
            return RC_MALLOC;
        }

        unsigned nextExt = 0;
        if ( nExtBlocksNew < 1 ) {
            // the new file will have no ext blocks - removing starts from the file header
            int32_t * const dataBlocks = file->fileHdr->dataBlocks;

            // here for sure all blocks to the end of the array will be removed
            for ( unsigned i = nDBlocksNew + 1 ; i <= MAX_DATABLK ; ++i ) {
                assert ( blocksCount < nBlocksToRemove );
                blocksToRemove->sectors [ blocksCount++ ] = dataBlocks [ MAX_DATABLK - i ];
                dataBlocksCount++;
            }
            nextExt = (unsigned) file->fileHdr->extension;
        }
        else {
            // removing starts from the new last ext. block
            const unsigned newLastExtBlockIndex = nExtBlocksNew - 1;
            RETCODE rc = adfFileReadExtBlockN ( file, (int32_t) newLastExtBlockIndex, extBlock );
            if ( rc != RC_OK ) {
                free ( extBlock );
                free ( blocksToRemove->sectors );
                return rc;
            }

            if ( nDBlocksNew / MAX_DATABLK > 0  &&   // min. one ext
                 nDBlocksNew % MAX_DATABLK != 0 )    // the last ext. is not full
            {
                const unsigned firstDBlockToRemove =
                    ( ( nDBlocksNew - MAX_DATABLK * nExtBlocksNew == MAX_DATABLK ) ?
                      MAX_DATABLK :
                      nDBlocksNew % MAX_DATABLK + 1 );

                const unsigned lastDBlockToRemove =
                    ( nExtBlocksNew == nExtBlocksOld ) ?

                    // already in the last ext. - removing data blocks only here
                    // (and no ext. to remove)
                    ( ( nDBlocksOld - MAX_DATABLK * nExtBlocksNew == MAX_DATABLK ) ?
                      MAX_DATABLK :                    // the last ext block is full (MAX_DATABLK)
                      nDBlocksOld % MAX_DATABLK ) :    // the last ext block has < MAX_DATABLK

                    // not the last ext. -> dblocks from the whole list to remove
                    MAX_DATABLK;

                //printf ( "firstDBlockToRemove %u, lastDBlockToRemove %u\n" ,
                //         firstDBlockToRemove, lastDBlockToRemove );
                //fflush(stdout);
                int32_t * const dataBlocks = extBlock->dataBlocks;
                for ( unsigned i = firstDBlockToRemove ; i <= lastDBlockToRemove ; ++i ) {
                    assert ( blocksCount < nBlocksToRemove );
                    blocksToRemove->sectors [ blocksCount++ ] = dataBlocks [ MAX_DATABLK - i ];
                    dataBlocksCount++;
                }
            }

            nextExt = (unsigned) extBlock->extension;
        }

        unsigned extBlock_i = nExtBlocksNew;

        // get blocks to remove from the following (if any remaining) ext. blocks
        while ( nextExt > 0 ) {
            RETCODE rc = adfReadFileExtBlock ( file->volume, (SECTNUM) nextExt, extBlock );
            if ( rc != RC_OK ) {
                free ( extBlock );
                free ( blocksToRemove->sectors );
                return rc;
            }

            unsigned lastDBlockToRemove =
                ( (unsigned) ( extBlock_i + 1 ) == nExtBlocksOld ) ?

                // the last ext block
                ( ( nDBlocksOld - MAX_DATABLK * ( extBlock_i + 1 ) == MAX_DATABLK ) ?
                  MAX_DATABLK :                    // the last ext block is full (MAX_DATABLK)
                  nDBlocksOld % MAX_DATABLK ) :    // the last ext block has < MAX_DATABLK

                // not the last ext - all data blocks to remove
                MAX_DATABLK;

            int32_t * const dataBlocks = extBlock->dataBlocks;
            /*for ( unsigned i = 1 ; i <= MAX_DATABLK ; ++i ) {
                SECTNUM dblock = dataBlocks [ MAX_DATABLK - i ];
                if ( dblock > 0 ) {
                    blocksToRemove->sectors [ blocksCount++ ] = dblock;
                    dataBlocksCount++;
                }
            }*/
            for ( unsigned i = 1 ; i <= lastDBlockToRemove ; ++i ) {
                assert ( dataBlocks [ MAX_DATABLK - i ] > 0 );
                blocksToRemove->sectors [ blocksCount++ ] = dataBlocks [ MAX_DATABLK - i ];
                dataBlocksCount++;
            }

            // add currently loaded ext. to remove
            blocksToRemove->sectors [ blocksCount++ ] = (SECTNUM) nextExt;

            nextExt = (unsigned) extBlock->extension;
            extBlock_i++;
        }

        free ( extBlock );
    }

#ifdef DEBUG_ADF_FILE
    if ( blocksCount != blocksToRemove->len ) {
        fprintf ( stderr,
                  "Error: blocksCount %u != blocksToRemove->len %u, datablocksCount %u\n",
                 blocksCount, blocksToRemove->len, dataBlocksCount );
        fflush ( stderr );
    }
#endif
    assert ( blocksCount == blocksToRemove->len );
    return RC_OK;
}


/*
 * adfFileTruncate
 *
 * resizes (truncates) the file to the specified size
 * and position the file pointer there (at the new EOF)
 *
 * the algorithm (for shrinking):
 * 1. make list of blocks (data and ext.) to remove
 * 2. seek to the requested position (size)
 * 3. update metadata (ie. block pointers) in blocks
 *    - file header
 *      - file size (byteSize)
 *      - if truncated file is <= 72 data blocks
 *       - update the dataBlocks[]
 *       - update highSeq (number of datablocks stored in dataBlocks[])
 *       - set the first ext. block pointer to 0 (extension)
 *
 *    - if the file after truncate has ext blocks (the file remains > than 72 data blocks)
 *      - update dataBlocks[] in the last ext (set 0 for all blocks to remove)
 *      - update highSeq (number of datablocks stored in dataBlocks[])
 *      - set the next ext. block pointer to 0 (extension)
 *    - additionally for OFS
 *      - set no next data block in data block header
 *      - set the new data size
 *  4. mark blocks to remove (adfSetBlockFree())
 *  5. update block allocation bitmap (adfUpdateBitmap())
 */

RETCODE adfFileTruncate ( struct AdfFile * const file,
                          const uint32_t         fileSizeNew )
{
    if ( ! file->modeWrite )
        return RC_ERROR;

    if ( fileSizeNew == file->fileHdr->byteSize ) {
        return adfFileSeek ( file, fileSizeNew );
    }

    const unsigned fileSizeOld = file->fileHdr->byteSize;

    if ( fileSizeNew > fileSizeOld ) {
        const unsigned enlargeSize = fileSizeNew - fileSizeOld;
        RETCODE rc = adfFileSeek ( file, fileSizeOld );
        if ( rc != RC_OK )
            return rc;
        assert ( adfEndOfFile ( file ) == TRUE );
        const unsigned bytesWritten = adfFileWriteFilled ( file, 0, enlargeSize );
        if ( enlargeSize != bytesWritten )
            return RC_ERROR;
        return RC_OK;
    }

    // 1.
    AdfVectorSectors blocksToRemove;
    RETCODE rc = adfFileTruncateGetBlocksToRemove ( file, fileSizeNew,
                                                    &blocksToRemove );
    if ( rc != RC_OK )
        return rc;

    // 2. seek to the new EOF
    rc = adfFileSeek ( file, fileSizeNew );
    if ( rc != RC_OK ) {
        free ( blocksToRemove.sectors );
        return rc;
    }
    assert ( file->pos == fileSizeNew );

    // 3.
    file->fileHdr->byteSize = fileSizeNew;
    if ( fileSizeNew == 0 ) {
        // the new file is an empty file

        file->fileHdr->firstData = 0;
        for ( unsigned i = 0 ; i < MAX_DATABLK ; ++i )
            file->fileHdr->dataBlocks[i] = 0;
        file->fileHdr->highSeq = 0;         // 0 data blocks stored in dataBlocks[]
        file->fileHdr->extension = 0;       // no ext. blocks
    } else {
        // the new file has at least one data block

        const unsigned
            nDataBlocksNew = adfFileSize2Datablocks ( fileSizeNew, file->volume->datablockSize ),
            nDataBlocksOld = adfFileSize2Datablocks ( fileSizeOld, file->volume->datablockSize ),
            nExtBlocksOld  = adfFileDatablocks2Extblocks ( nDataBlocksOld ),
            nExtBlocksNew  = adfFileDatablocks2Extblocks ( nDataBlocksNew );

        int32_t * const dataBlocks = ( nExtBlocksNew < 1 ) ?
            file->fileHdr->dataBlocks :
            file->currentExt->dataBlocks;

        /* if the new last data block array (in file header or
           the last ext. block) is not full... */
        if ( nDataBlocksNew % MAX_DATABLK != 0 ) {
            /* .., then cleaning block lists starts there */

            const unsigned firstDBlockToRemove = nDataBlocksNew % MAX_DATABLK;
            const unsigned lastDBlockToRemove =
                ( nExtBlocksNew < nExtBlocksOld || nDataBlocksOld % MAX_DATABLK == 0 ) ?
                MAX_DATABLK - 1 :
                nDataBlocksOld % MAX_DATABLK;

            for ( unsigned i = firstDBlockToRemove ; i <= lastDBlockToRemove ; ++i )
                dataBlocks [ MAX_DATABLK - 1 - i ] = 0;

            assert ( firstDBlockToRemove > 0 );  // new last ext. block cannot be an empty one
            if ( nDataBlocksNew <= MAX_DATABLK ) {   // could be: nExtBlocksNew < 1
                file->fileHdr->highSeq = (int32_t) firstDBlockToRemove;
            } else {
                file->currentExt->highSeq = (int32_t) firstDBlockToRemove;
            }
        }

        if ( isOFS ( file->volume->dosType ) ) {
            // for OFS - update also data block header
            struct bOFSDataBlock * const data =
                (struct bOFSDataBlock *) file->currentData;

            data->dataSize =   //file->posInDataBlk;
                ( fileSizeNew % file->volume->datablockSize == 0 ) ?
                file->volume->datablockSize :
                fileSizeNew % file->volume->datablockSize;

            data->nextData = 0;
            //file->currentDataBlockChanged = TRUE;
        }
        file->currentDataBlockChanged = TRUE; // could be done only for OFS - but
                                              // here it will force saving also the ext.
                                              // (required for now at least)
                                              // to improve (?)
        if ( nDataBlocksNew <= MAX_DATABLK ) {   // could be: nExtBlocksNew < 1
            file->fileHdr->extension = 0;
        } else {
            file->currentExt->extension = 0;
        }
    }

    // 4.
    // todo: add sorting blocksToRemove (to optimize disk access)
    for ( unsigned i = 0 ; i < blocksToRemove.len ; ++i ) {
        adfSetBlockFree ( file->volume, blocksToRemove.sectors[i] );
    }
    free ( blocksToRemove.sectors );

    assert ( file->pos == fileSizeNew );

    // 5.
    return adfUpdateBitmap ( file->volume );
}


/*
 * adfFileFlush
 *
 */
RETCODE adfFileFlush ( struct AdfFile * const file )
{
    if ( ! file->modeWrite )
        return RC_OK;

    RETCODE rc = RC_OK;

    if (file->currentExt) {
        rc = adfWriteFileExtBlock ( file->volume,
                                    file->currentExt->headerKey,
                                    file->currentExt );
        if ( rc != RC_OK ) {
            adfEnv.eFct ( "adfFlushfile : error writing ext block 0x%x (%d), file '%s'",
                          file->currentExt->headerKey,
                          file->currentExt->headerKey,
                          file->fileHdr->fileName );
            return rc;
        }
    }

    if ( file->fileHdr->byteSize > 0 &&
         file->currentData != NULL &&
         file->curDataPtr != 0 )
    {
        if ( isOFS ( file->volume->dosType ) ) {
            struct bOFSDataBlock *data = (struct bOFSDataBlock *) file->currentData;
            assert ( file->posInDataBlk <= file->volume->datablockSize );
            data->dataSize = file->posInDataBlk;
        }

        rc = adfWriteDataBlock ( file->volume,
                                 file->curDataPtr,
                                 file->currentData );
        if ( rc != RC_OK ) {
            adfEnv.eFct ( "adfFlushFile : error writing data block 0x%x (%u), file '%s'",
                          file->curDataPtr, file->curDataPtr,
                          file->fileHdr->fileName );
            return rc;
        }
    }

/*printf("pos=%ld\n",file->pos);*/
    adfTime2AmigaTime ( adfGiveCurrentTime(),
                        &(file->fileHdr->days),
                        &(file->fileHdr->mins),
                        &(file->fileHdr->ticks) );

    rc = adfWriteFileHdrBlock ( file->volume,
                                file->fileHdr->headerKey,
                                file->fileHdr );
    if ( rc != RC_OK ) {
        adfEnv.eFct ( "adfFlushfile : error writing file header block %d",
                      file->fileHdr->headerKey );
        return rc;
    }

    if ( isDIRCACHE ( file->volume->dosType ) ) {
/*printf("parent=%ld\n",file->fileHdr->parent);*/
        struct bEntryBlock parent;
        rc = adfReadEntryBlock ( file->volume, file->fileHdr->parent, &parent );
        if ( rc != RC_OK ) {
            adfEnv.eFct ( "adfFlushfile : error reading entry block %d",
                          file->fileHdr->parent );
            return rc;
        }

        rc = adfUpdateCache ( file->volume, &parent,
                              (struct bEntryBlock*) file->fileHdr, FALSE );
        if ( rc != RC_OK ) {
            adfEnv.eFct ( "adfFlushfile : error updating cache" );
            return rc;
        }
    }

    rc = adfUpdateBitmap ( file->volume );
    if ( rc != RC_OK ) {
        adfEnv.eFct ( "adfFlushfile : error updating volume bitmap" );
        return rc;
    }

    return rc;
}


/*
 * adfFileSeek
 *
 */

static RETCODE adfFileSeekStart_ ( struct AdfFile * const file )
{
    file->pos = 0;
    file->posInExtBlk = 0;
    file->posInDataBlk = 0;
    file->nDataBlock = 0;
    file->curDataPtr = 0;

    if ( file->fileHdr->byteSize == 0 )
        // an empty file - no data block to read
        return RC_OK;

    RETCODE rc = adfFileReadNextBlock ( file );
    if ( rc != RC_OK ) {
        file->curDataPtr = 0;  // invalidate data ptr
    }
    return rc;
}

static RETCODE adfFileSeekEOF_ ( struct AdfFile * const file )
{
    if ( file->fileHdr->byteSize == 0 )
        return adfFileSeekStart_ ( file );

    /* an ugly hack (but required for state consistency...):
       enforce updating current data and ext. blocks
       as expected to match the new position */
    RETCODE rc = adfFileSeek ( file, file->fileHdr->byteSize - 1 );
    if ( rc != RC_OK )
        return rc;

    file->pos = file->fileHdr->byteSize;
    file->posInDataBlk =
        ( file->fileHdr->byteSize % file->volume->datablockSize == 0 ) ?
        file->volume->datablockSize :
        file->fileHdr->byteSize % file->volume->datablockSize;

    assert (  file->posInDataBlk <= file->volume->datablockSize );
    return RC_OK;
}


static RETCODE adfFileSeekOFS_ ( struct AdfFile * const file,
                                 uint32_t               pos )
{
    adfFileSeekStart_ ( file );

    unsigned blockSize = file->volume->datablockSize;

    file->pos = min ( pos, file->fileHdr->byteSize );

    // EOF?
    if ( file->pos == file->fileHdr->byteSize ) {
        return adfFileSeekEOF_ ( file );
    }

    uint32_t offset = 0;
    while ( offset < pos ) {
        unsigned size = min ( pos - offset, (unsigned) ( blockSize - file->posInDataBlk ) );
        file->pos += size;
        offset += size;
        file->posInDataBlk += size;
        if ( file->posInDataBlk == blockSize && offset < pos ) {
            if ( adfFileReadNextBlock ( file ) != RC_OK ) {
                adfEnv.eFct ( "adfFileSeekOFS: error reading next data block, pos %d",
                              file->pos );
                file->curDataPtr = 0;  // invalidate data ptr
                return RC_ERROR;
            }
            file->posInDataBlk = 0;
        }
    }
    return RC_OK;
}


static RETCODE adfFileSeekExt_ ( struct AdfFile * const file,
                                 uint32_t               pos )
{
    file->pos = min ( pos, file->fileHdr->byteSize );

    if ( file->pos == file->fileHdr->byteSize ) {
        return adfFileSeekEOF_ ( file );
    }

    SECTNUM extBlock = adfPos2DataBlock ( file->pos,
                                          file->volume->datablockSize,
                                          &file->posInExtBlk,
                                          &file->posInDataBlk,
                                          &file->nDataBlock );
    if ( extBlock == -1 ) {
        file->curDataPtr = file->fileHdr->dataBlocks [
            MAX_DATABLK - 1 - file->nDataBlock ];
    } else {
        if ( ! file->currentExt ) {
            file->currentExt = ( struct bFileExtBlock * )
                malloc ( sizeof ( struct bFileExtBlock ) );
            if ( ! file->currentExt ) {
                (*adfEnv.eFct)( "adfFileSeekExt : malloc" );
                file->curDataPtr = 0;  // invalidate data ptr
                return RC_MALLOC;
            }
        }

        if ( adfFileReadExtBlockN ( file, extBlock, file->currentExt ) != RC_OK )  {
            adfEnv.eFct ( "adfFileSeekExt: error reading ext block 0x%x(%d), file '%s'",
                          extBlock, extBlock, file->fileHdr->fileName );
            file->curDataPtr = 0;  // invalidate data ptr
            return RC_ERROR;
        }

        file->curDataPtr = file->currentExt->dataBlocks [
            MAX_DATABLK - 1 - file->posInExtBlk ];
        file->posInExtBlk++;
    }

    if ( file->curDataPtr < 2 ) {
        // a data block can never be at 0-1 (bootblock)
        adfEnv.eFct ( "adfFileSeekExt: invalid data block address (%u), pos %u, file '%s'",
                      file->curDataPtr, file->pos, file->fileHdr->fileName );
        return RC_ERROR;
    }

    RETCODE rc = adfReadDataBlock ( file->volume,
                                    file->curDataPtr,
                                    file->currentData );
    if ( rc != RC_OK ) {
        adfEnv.eFct ( "adfFileSeekExt: error reading data block %d, file '%s'",
                      file->curDataPtr, file->fileHdr->fileName );
        file->curDataPtr = 0;  // invalidate data ptr
    }

    file->nDataBlock++;

    return RC_OK;
}

//#define TEST_OFS_SEEK 1

RETCODE adfFileSeek ( struct AdfFile * const file,
                      const uint32_t         pos )
{
    if ( file->pos == pos  && file->curDataPtr != 0 )
        return RC_OK;

    /* in write mode, first must write current data block before doing seek(!)
       it should be done only if necessary:
       - if the current data block was changed (no way to know this in
         the current code... to improve)
       and
       - if seek will move the file position pointer into another data block
    */
    const unsigned curDatablock = //file->pos / (unsigned) file->volume->datablockSize;
        ( file->nDataBlock > 0 ) ? file->nDataBlock - 1 : 0;
    const unsigned reqDatablock = pos / (unsigned) file->volume->datablockSize;
    if ( file->curDataPtr != 0 && curDatablock == reqDatablock ) {
        // seek in the current/same data block - just move the pointers
        file->pos = min ( pos, file->fileHdr->byteSize );
        file->posInDataBlk = file->pos % file->volume->datablockSize;
        return RC_OK;
    }

    if ( file->modeWrite && file->currentDataBlockChanged ) {
        adfFileFlush ( file );
        file->currentDataBlockChanged = FALSE;
    }

    if ( pos == 0 )
        return adfFileSeekStart_ ( file );

#ifdef TEST_OFS_SEEK
    // optional code for testing only
    // (ie. to test OFS seek, which is less optimal and not used by default)
    if ( isOFS ( file->volume->dosType ) )
        return adfFileSeekOFS_ ( file, pos );
    else
        return adfFileSeekExt_ ( file, pos );
#endif

    RETCODE status = adfFileSeekExt_ ( file, pos );
    if ( status != RC_OK && isOFS ( file->volume->dosType ) ) {
        adfEnv.wFct ( "adfFileSeek: seeking using ext blocks failed, fallback"
                      " to the OFS alt. way (traversing data blocks), "
                      "file '%s'", file->fileHdr->fileName );
        status = adfFileSeekOFS_ ( file, pos );
    }
    return status;
}


/*
 * adfFileOpen
 *
 */ 
struct AdfFile * adfFileOpen ( struct AdfVolume * const vol,
                               const char * const       name,
                               const AdfFileMode        mode )
{
    if ( ! vol ) {
        adfEnv.eFct ( "adfFileOpen : vol is NULL" );
        return NULL;
    }

    if ( ! name ) {
        adfEnv.eFct ( "adfFileOpen : name is NULL" );
        return NULL;
    }

    const BOOL modeRead  = ( mode & ADF_FILE_MODE_READ );
    const BOOL modeWrite = ( mode & ADF_FILE_MODE_WRITE );
    if ( ! ( modeRead || modeWrite ) ) {
        adfEnv.eFct ( "adfFileOpen : Incorrect mode '0x%0x' (%d)", mode, mode );
        return NULL;
    }

    if ( modeWrite && vol->dev->readOnly ) {
        (*adfEnv.wFct)("adfFileOpen : device is mounted 'read only'");
        return NULL;
    }

    struct bEntryBlock entry, parent;
    if ( adfReadEntryBlock(vol, vol->curDirPtr, &parent) != RC_OK )
        return NULL;

    BOOL fileAlreadyExists =
        ( adfNameToEntryBlk ( vol, parent.hashTable, name, &entry, NULL ) != -1 );

    if ( modeRead && ( ! modeWrite ) && ( ! fileAlreadyExists ) ) {
        adfEnv.wFct ( "adfFileOpen : file \"%s\" not found.", name );
/*fprintf(stdout,"filename %s %d, parent =%d\n",name,strlen(name),vol->curDirPtr);*/
        return NULL;
    }

    if ( modeRead && hasR ( entry.access ) ) {
        adfEnv.wFct ( "adfFileOpen : read access denied to '%s'", name );
        return NULL;
    }

    if ( fileAlreadyExists && modeWrite && hasW ( entry.access ) ) {
        adfEnv.wFct ( "adfFileOpen : write access denied to '%s'", name );
        return NULL;
    }

    if ( fileAlreadyExists &&
         entry.secType != ST_FILE &&
         entry.secType != ST_LFILE )
    {
        adfEnv.wFct ( "adfFileOpen : '%s' is not a file (or a hardlink to a file)",
                      name );
        return NULL;
    }

    if ( fileAlreadyExists ) {
        if ( entry.realEntry )  {  // ... and it is a hard-link...
            // ... load entry of the hard-linked file
            RETCODE rc = adfReadEntryBlock ( vol, entry.realEntry, &entry );
            if ( rc != RC_OK ) return NULL;
            rc = adfReadEntryBlock ( vol, entry.parent, &parent );
            if ( rc != RC_OK ) return NULL;
        }

        // entry should be a real file now
        if ( entry.realEntry != 0 ) {
            //... so if it is still a (hard)link - error...
            return NULL;
        }
    }

    struct AdfFile * file = (struct AdfFile *) malloc ( sizeof(struct AdfFile) );
    if ( file == NULL ) {
        adfEnv.eFct ( "adfFileOpen : malloc" );
        return NULL;
    }

    file->fileHdr = (struct bFileHeaderBlock *)
        malloc ( sizeof(struct bFileHeaderBlock) );
    if ( file->fileHdr == NULL ) {
        adfEnv.eFct ( "adfFileOpen : malloc" );
        free ( file );
        return NULL;
    }

    file->currentData = malloc ( 512 * sizeof(uint8_t) );
    if ( file->currentData == NULL ) {
        adfEnv.eFct ( "adfFileOpen : malloc" );
        free ( file->fileHdr );
        free ( file );
        return NULL;
    }

    file->volume = vol;
    file->pos = 0;
    file->posInExtBlk = 0;
    file->posInDataBlk = 0;
    file->currentExt = NULL;
    file->nDataBlock = 0;
    file->curDataPtr = 0;
    file->currentDataBlockChanged = FALSE;
    file->modeRead  = modeRead;
    file->modeWrite = modeWrite;

    if ( ! modeWrite ) {
        /* read-only mode */
        memcpy ( file->fileHdr, &entry, sizeof ( struct bFileHeaderBlock ) );
        if ( adfFileSeek ( file, 0 ) != RC_OK ) {
            adfEnv.eFct ( "adfFileOpen : error seeking pos. %d, file: %s",
                          0, file->fileHdr->fileName );
            goto adfOpenFile_error;
        }
    }
    else {
        /* write or read-write mode */
        if ( fileAlreadyExists ) {
            memcpy ( file->fileHdr, &entry, sizeof ( struct bFileHeaderBlock ) );
            unsigned seekpos = 0; //( mode_append ? file->fileHdr->byteSize : 0 );
            if ( adfFileSeek ( file, seekpos ) != RC_OK ) {
                adfEnv.eFct ( "adfFileOpen : error seeking pos. %d, file: %s",
                              seekpos, file->fileHdr->fileName );
                goto adfOpenFile_error;
            }
        } else {
            // a new file
            memset ( file->fileHdr, 0, 512 );
            if ( adfCreateFile ( vol, vol->curDirPtr, name, file->fileHdr ) != RC_OK ) {
                adfEnv.eFct ( "adfFileOpen : error creating file: %s",
                              file->fileHdr->fileName );
                goto adfOpenFile_error;
            }
        }
    }

    return file;

adfOpenFile_error:
    free ( file->currentData );
    free ( file->fileHdr );
    free ( file );
    return NULL;
}


/*
 * adfCloseFile
 *
 */
void adfFileClose ( struct AdfFile * file )
{

    if (file==0)
        return;
/*puts("adfCloseFile in");*/

    adfFileFlush ( file );

    if (file->currentExt)
        free(file->currentExt);
    
    if (file->currentData)
        free(file->currentData);
    
    free(file->fileHdr);
    free(file);

/*puts("adfCloseFile out");*/
}


/*
 * adfReadFile
 *
 */
uint32_t adfFileRead ( struct AdfFile * const file,
                       uint32_t               n,
                       uint8_t * const        buffer )
{
    if ( ( ! file->modeRead ) ||
         n == 0 ||
         file->fileHdr->byteSize == 0 ||
         adfEndOfFile ( file ) )
    {
        return 0;
    }

    unsigned blockSize = file->volume->datablockSize;
/*puts("adfReadFile");*/
    if (file->pos+n > file->fileHdr->byteSize)
        n = file->fileHdr->byteSize - file->pos;

    uint8_t * const dataPtr = ( isOFS ( file->volume->dosType ) ) ?
        //(uint8_t*)(file->currentData)+24 :
        ( (struct bOFSDataBlock *) file->currentData )->data :
        file->currentData;

    uint32_t bytesRead = 0;
    uint8_t *bufPtr = buffer;

    while ( bytesRead < n ) {

        if ( file->posInDataBlk == blockSize ) {
            RETCODE rc = adfFileReadNextBlock ( file );
            if ( rc != RC_OK ) {
                adfEnv.eFct ( "adfReadFile : error reading next data block, "
                              "file '%s', pos %d, data block %d",
                              file->fileHdr->fileName, file->pos, file->nDataBlock );
                file->curDataPtr = 0;  // invalidate data ptr
                return bytesRead;
            }
            file->posInDataBlk = 0;
            file->currentDataBlockChanged = FALSE;
        }

        unsigned size = min ( n - bytesRead, blockSize - file->posInDataBlk );
        memcpy(bufPtr, dataPtr+file->posInDataBlk, size);
        bufPtr += size;
        file->pos += size;
        bytesRead += size;
        file->posInDataBlk += size;
    }

    return( bytesRead );
}


/*
 * adfReadNextFileBlock
 *
 */
RETCODE adfFileReadNextBlock ( struct AdfFile * const file )
{
    SECTNUM nSect;
    struct bOFSDataBlock *data;
    RETCODE rc = RC_OK;

    data =(struct bOFSDataBlock *) file->currentData;

    if (file->nDataBlock==0) {
        nSect = file->fileHdr->firstData;
    }
    else if (isOFS(file->volume->dosType)) {
        nSect = data->nextData;
    }
    else {
        if (file->nDataBlock<MAX_DATABLK)
            nSect = file->fileHdr->dataBlocks[MAX_DATABLK-1-file->nDataBlock];
        else {
            if (file->nDataBlock==MAX_DATABLK) {

                if ( file->currentExt == NULL ) {
                    file->currentExt = (struct bFileExtBlock *)
                        malloc ( sizeof(struct bFileExtBlock) );
                    if ( file->currentExt == NULL ) {
                        adfEnv.eFct ("adfReadNextFileBlock : malloc");
                        return RC_MALLOC;
                    }
                }

                rc = adfReadFileExtBlock ( file->volume,
                                           file->fileHdr->extension,
                                           file->currentExt );
                if ( rc != RC_OK ) {
                    adfEnv.eFct ( "adfReadNextFileBlock : error reading ext block %d",
                                  file->fileHdr->extension );
                    return rc;
                }

                file->posInExtBlk = 0;
            }
            else if (file->posInExtBlk==MAX_DATABLK) {

                rc = adfReadFileExtBlock ( file->volume,
                                           file->currentExt->extension,
                                           file->currentExt );
                if ( rc != RC_OK ) {
                    adfEnv.eFct ( "adfReadNextFileBlock : error reading ext block %d",
                                  file->currentExt->extension );
                    return rc;
                }

                file->posInExtBlk = 0;
            }
            nSect = file->currentExt->dataBlocks[MAX_DATABLK-1-file->posInExtBlk];
            file->posInExtBlk++;
        }
    }

    if ( nSect < 2 ) {
        adfEnv.eFct ( "adfReadNextFileBlock : invalid data block address %u ( 0x%x ), "
                      "data block %u, file '%s'",
                      nSect, nSect, file->nDataBlock, file->fileHdr->fileName );
        //printBacktrace();
        return RC_ERROR;
    }

    rc = adfReadDataBlock ( file->volume, nSect, file->currentData );
    if ( rc != RC_OK )
        adfEnv.eFct ( "adfReadNextFileBlock : error reading data block %d / %d, file '%s'",
                       file->nDataBlock, nSect, file->fileHdr->fileName );

    if (isOFS(file->volume->dosType) && data->seqNum!=file->nDataBlock+1)
        (*adfEnv.wFct)("adfReadNextFileBlock : seqnum incorrect");

    file->curDataPtr = nSect;
    file->nDataBlock++;

    return rc;
}


/*
 * adfWriteFile
 *
 */
uint32_t adfFileWrite ( struct AdfFile * const file,
                        const uint32_t         n,
                        const uint8_t * const  buffer )
{
    if ( ! file->modeWrite )
        return 0; // RC_ERROR;

    if (n==0) return (n);
/*puts("adfWriteFile");*/
    const unsigned blockSize = file->volume->datablockSize;

    uint8_t * const dataPtr = ( isOFS ( file->volume->dosType ) ) ?
        ( (struct bOFSDataBlock *) file->currentData )->data :
        file->currentData;

    uint32_t bytesWritten = 0;
    const uint8_t *bufPtr = buffer;
    while( bytesWritten<n ) {

        if ( file->pos % blockSize == 0 )  { //file->posInDataBlk == blockSize ) {

            if ( file->pos == file->fileHdr->byteSize ) {   // at EOF ?
                // ...  create a new block
                RETCODE rc = adfFileCreateNextBlock ( file );
                file->currentDataBlockChanged = FALSE;
                if ( rc != RC_OK ) {
                    /* bug found by Rikard */
                    adfEnv.wFct ( "adfWritefile : no more free sectors available" );
                    //file->curDataPtr = 0; // invalidate data ptr
                    return bytesWritten;
                }
            }
            else if ( file->posInDataBlk == blockSize ) {
                // inside the existing data (at the end of a data block )

                // write the block stored currently in the memory
                if ( file->currentDataBlockChanged ) {
                    adfFileFlush ( file ); // to optimize (?)
                    file->currentDataBlockChanged = FALSE;
                }

                // - and read the next block
                RETCODE rc = adfFileReadNextBlock ( file );
                if ( rc != RC_OK ) {
                    adfEnv.eFct ( "adfWriteFile : error reading next data block, "
                                  "file '%s', pos %d, data block %d",
                                  file->fileHdr->fileName, file->pos, file->nDataBlock );
                    file->curDataPtr = 0;  // invalidate data ptr
                    return bytesWritten;
                }
            }

            file->posInDataBlk = 0;
        }

        const unsigned size = min ( n - bytesWritten, blockSize - file->posInDataBlk );
        memcpy(dataPtr+file->posInDataBlk, bufPtr, size);
        bufPtr += size;
        file->pos += size;
        bytesWritten += size;
        file->posInDataBlk += size;
        file->currentDataBlockChanged = TRUE;

        // update file size in the header
        file->fileHdr->byteSize = max ( file->fileHdr->byteSize,
                                        file->pos );
    }
    return( bytesWritten );
}


/*
 * adfFileWriteFilled
 *
 */
unsigned adfFileWriteFilled ( struct AdfFile * const file,
                              const uint8_t          fillValue,
                              uint32_t               size )
{
    const unsigned BUFSIZE = 4096;
    uint8_t * const buffer = malloc ( BUFSIZE );
    if ( buffer == NULL )
        return 0;
    memset ( buffer, fillValue, BUFSIZE );

    unsigned bytesWritten = 0,
             chunkLen,
             chunkBytesWritten;
    while ( size > 0 ) {
        chunkLen = min ( size, BUFSIZE );
        chunkBytesWritten = adfFileWrite ( file, chunkLen, buffer );
        bytesWritten += chunkBytesWritten;
        if ( chunkBytesWritten != chunkLen )
            break;
        size -= chunkLen;
    }
    free ( buffer );
    return bytesWritten;
}


/*
 * adfCreateNextFileBlock
 *
 */
RETCODE adfFileCreateNextBlock ( struct AdfFile * const file )
{
    SECTNUM nSect, extSect;

/*puts("adfCreateNextFileBlock");*/
    unsigned int blockSize = file->volume->datablockSize;

    /* the first data blocks pointers are inside the file header block */
    if (file->nDataBlock<MAX_DATABLK) {
        nSect = adfGet1FreeBlock(file->volume);
        if ( nSect == -1 )
            return RC_VOLFULL;
/*printf("adfCreateNextFileBlock fhdr %ld\n",nSect);*/
        if (file->nDataBlock==0)
            file->fileHdr->firstData = nSect;
        file->fileHdr->dataBlocks[MAX_DATABLK-1-file->nDataBlock] = nSect;
        file->fileHdr->highSeq++;
    }
    else {
        /* one more sector is needed for one file extension block */
        if ((file->nDataBlock%MAX_DATABLK)==0) {
            extSect = adfGet1FreeBlock(file->volume);
/*printf("extSect=%ld\n",extSect);*/
            if ( extSect == -1 )
                return RC_VOLFULL;

            /* the future block is the first file extension block */
            if (file->nDataBlock==MAX_DATABLK) {
                file->currentExt=(struct bFileExtBlock*)malloc(sizeof(struct bFileExtBlock));
                if (!file->currentExt) {
                    adfSetBlockFree(file->volume, extSect);
                    (*adfEnv.eFct)("adfCreateNextFileBlock : malloc");
                    return RC_MALLOC;
                }
                file->fileHdr->extension = extSect;
            }

            /* not the first : save the current one, and link it with the future */
            if (file->nDataBlock>=2*MAX_DATABLK) {
                file->currentExt->extension = extSect;
/*printf ("write ext=%d\n",file->currentExt->headerKey);*/
                adfWriteFileExtBlock(file->volume, file->currentExt->headerKey,
                    file->currentExt);
            }

            /* initializes a file extension block */
            for ( int i = 0 ; i < MAX_DATABLK ; i++ )
                file->currentExt->dataBlocks[i] = 0L;
            file->currentExt->headerKey = extSect;
            file->currentExt->parent = file->fileHdr->headerKey;
            file->currentExt->highSeq = 0L;
            file->currentExt->extension = 0L;
            file->posInExtBlk = 0L;
/*printf("extSect=%ld\n",extSect);*/
        }
        nSect = adfGet1FreeBlock(file->volume);
        if ( nSect == -1 )
            return RC_VOLFULL;
        
/*printf("adfCreateNextFileBlock ext %ld\n",nSect);*/

        file->currentExt->dataBlocks[MAX_DATABLK-1-file->posInExtBlk] = nSect;
        file->currentExt->highSeq++;
        file->posInExtBlk++;
    }

    /* builds OFS header */
    if (isOFS(file->volume->dosType)) {
        /* writes previous data block and link it  */
        struct bOFSDataBlock * const data = file->currentData;
        if (file->pos>=blockSize) {
            data->nextData = nSect;
            adfWriteDataBlock(file->volume, file->curDataPtr, file->currentData);
/*printf ("writedata=%d\n",file->curDataPtr);*/
        }
        /* initialize a new data block */
        for ( unsigned i = 0 ; i < blockSize ; i++ )
            data->data[i]=0;
        data->seqNum = file->nDataBlock+1;
        data->dataSize = blockSize;
        data->nextData = 0L;
        data->headerKey = file->fileHdr->headerKey;
    }
    else
        if (file->pos>=blockSize) {
            adfWriteDataBlock(file->volume, file->curDataPtr, file->currentData);
/*printf ("writedata=%d\n",file->curDataPtr);*/
            memset(file->currentData,0,512);
        }
            
/*printf("datablk=%d\n",nSect);*/
    file->curDataPtr = nSect;
    file->nDataBlock++;

    return RC_OK;
}


/*
 * adfPos2DataBlock
 *
 */
int32_t adfPos2DataBlock ( const unsigned   pos,
                           const unsigned   blockSize,
                           unsigned * const posInExtBlk,
                           unsigned * const posInDataBlk,
                           unsigned * const curDataN )
{
    *posInDataBlk = pos % blockSize;   // offset in the data block
    *curDataN     = pos / blockSize;   // index of the data block
    if ( *curDataN < MAX_DATABLK ) {
        *posInExtBlk = 0;
        return -1;
    }
    else {
        // size of data allocated in file header or by a single ext. block
        unsigned dataSizeByExtBlock = //72 * blockSize;
            blockSize * MAX_DATABLK;

        // data offset starting from the 1st allocation done in ext. blocks
        // (without data allocated in the file header)
        unsigned offsetInExt = pos - dataSizeByExtBlock;

        // ext. block index
        unsigned extBlock = offsetInExt / dataSizeByExtBlock;

        // data block index in ext. block
        *posInExtBlk = ( offsetInExt / blockSize ) % MAX_DATABLK;

        return (int32_t) extBlock;
    }
}


/*
 * adfReadFileExtBlockN
 *
 */
RETCODE adfFileReadExtBlockN ( const struct AdfFile * const file,
                               const int32_t                extBlock,
                               struct bFileExtBlock * const fext )
{
    // check if the given extBlock index is valid
    const int nExtBlocks = (int) adfFileSize2Extblocks ( file->fileHdr->byteSize,
                                                         file->volume->datablockSize );
    if ( extBlock < 0 ||
         extBlock > nExtBlocks - 1 )
    {
        adfEnv.eFct ( "adfReadFileExtBlockN: invalid ext block %d, file '%s' has %d ext. blocks.",
                      extBlock, file->fileHdr->fileName, nExtBlocks );
        return RC_BLOCKOUTOFRANGE;
    }

    // traverse the ext. blocks until finding (and reading)
    // the requested one
    SECTNUM nSect = file->fileHdr->extension;
    int32_t i = -1;
    while ( i < extBlock && nSect != 0 ) {
        if ( adfReadFileExtBlock ( file->volume, nSect, fext ) != RC_OK ) {
            adfEnv.eFct ( "adfReadFileExtBlockN: error reading ext block %d, file '%s'",
                          nSect, file->fileHdr->fileName );
            return RC_BLOCKREAD;
        }
#ifdef DEBUG_ADF_FILE
        //show_bFileExtBlock ( fext );
#endif
        nSect = fext->extension;
        i++;
    }
    if ( i != extBlock ) {
        adfEnv.eFct ( "adfReadFileExtBlockN: error reading ext block %d, file '%s'",
                      extBlock, file->fileHdr->fileName );
        return RC_BLOCKREAD;
    }
    return RC_OK;
}


/*###########################################################################*/

#ifdef DEBUG_ADF_FILE

static void show_File ( const struct AdfFile * const file )
{
    printf ( "\nstruct File:\n"
             //"volume:\t0x%x
             //fileHdr;
             //currentData;
             //struct bFileExtBlock* currentExt;
             "  nDataBlock:\t0x%x\t\t%u\n"
             "  curDataPtr:\t0x%x\t\t%u\n"
             "  pos:\t\t0x%x\t\t%u\n"
             "  posInDataBlk:\t0x%x\t\t%u\n"
             "  posInExtBlk:\t0x%x\t\t%u\n"
             "  writeMode:\t0x%x\t\t%u\n",
             //volume;
             //fileHdr;
             //currentData;
             //struct bFileExtBlock* currentExt;
             file->nDataBlock,
             file->nDataBlock,
             file->curDataPtr,
             file->curDataPtr,
             file->pos,
             file->pos,
             file->posInDataBlk,
             file->posInDataBlk,
             file->posInExtBlk,
             file->posInExtBlk,
             file->writeMode,
             file->writeMode );
}

static void show_bFileHeaderBlock (
    const struct bFileHeaderBlock * const block )
{
    printf ( "\nbFileHeaderBlock:\n"
             "  type:\t\t%d\n"
             "  headerKey:\t%d\n"
             "  highSeq:\t%d\n"
             "  dataSize:\t%d\n"
             "  firstData:\t%d\n"
             "  checkSum:\t%d\n"
             //"	dataBlocks[MAX_DATABLK]:\n"
             "  r1:\t\t%d\n"
             "  r2:\t\t%d\n"
             "  access:\t%d\n"
             "  byteSize:\t%d\n"
             "  commLen:\t%d\n"
             "  comment:\t%s\n"
             "  r3:\t\t%s\n"
             "  days:\t\t%d\n"
             "  mins:\t\t%d\n"
             "  ticks:\t%d\n"
             "  nameLen:\t%d\n"
             "  fileName:\t%s\n"
             "  r4:\t\t%d\n"
             "  real:\t\t%d\n"
             "  nextLink:\t%d\n"
             "  r5[5]:\t%d\n"
             "  nextSameHash:\t%d\n"
             "  parent:\t%d\n"
             "  extension:\t%d\n"
             "  secType:\t%d\n",
             block->type,		/* == 2 */
             block->headerKey,	/* current block number */
             block->highSeq,	/* number of data block in this hdr block */
             block->dataSize,	/* == 0 */
             block->firstData,
             block->checkSum,
             //dataBlocks[MAX_DATABLK],
             block->r1,
             block->r2,
             block->access,	/* bit0=del, 1=modif, 2=write, 3=read */
             block->byteSize,
             block->commLen,
             block->comment,
             block->r3,
             block->days,
             block->mins,
             block->ticks,
             block->nameLen,
             block->fileName,
             block->r4,
             block->real,		/* unused == 0 */
             block->nextLink,	/* link chain */
             block->r5,
             block->nextSameHash,	/* next entry with sane hash */
             block->parent,		/* parent directory */
             block->extension,	/* pointer to extension block */
             block->secType );	/* == -3 */
}

static void show_bFileExtBlock (
    const struct bFileExtBlock * const block )
{
    printf ( "\nbFileExtBlock:\n"
             "  type:\t\t0x%x\t\t%u\n"
             "  headerKey:\t0x%x\t\t%u\n"
             "  highSeq:\t0x%x\t\t%u\n"
             "  dataSize:\t0x%x\t\t%u\n"
             "  firstData:\t0x%x\t\t%u\n"
             "  checkSum:\t0x%x\t%u\n"
             //"dataBlocks[MAX_DATABLK]:\t%d\n""
             "  r[45]:\t0x%x\t%u\n"
             "  info:\t\t0x%x\t\t%u\n"
             "  nextSameHash:\t0x%x\t\t%u\n"
             "  parent:\t0x%x\t\t%u\n"
             "  extension:\t0x%x\t\t%u\n"
             "  secType:\t0x%x\t%d\n",

             block->type,		/* == 0x10 */
             block->type,
             block->headerKey,
             block->headerKey,
             block->highSeq,
             block->highSeq,
             block->dataSize,	/* == 0 */
             block->dataSize,
             block->firstData,	/* == 0 */
             block->firstData,
             block->checkSum,
             block->checkSum,
//block->dataBlocks[MAX_DATABLK],
             block->r,
             block->r,
             block->info,		/* == 0 */
             block->info,
             block->nextSameHash,	/* == 0 */
             block->nextSameHash,
             block->parent,		/* header block */
             block->parent,
             block->extension,	/* next header extension block */
             block->extension,
             block->secType,	/* -3 */
             block->secType );
}
#endif
