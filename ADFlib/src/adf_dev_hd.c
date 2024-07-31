/*
 *  ADF Library. (C) 1997-2002 Laurent Clevy
 *
 *  adf_dev_hd.c
 *
 *  $Id$
 *
 *  harddisk / device code
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


#include "adf_dev_hd.h"

#include "adf_byteorder.h"
#include "adf_dev_driver_dump.h"
#include "adf_env.h"
#include "adf_raw.h"
#include "adf_util.h"
#include "adf_vol.h"

#include <stdlib.h>
#include <string.h>


/*
 * adfFreeTmpVolList
 *
 */
static void adfFreeTmpVolList ( struct AdfList * const root )
{
    struct AdfList *cell;
    struct AdfVolume *vol;

    cell = root;
    while(cell!=NULL) {
        vol = (struct AdfVolume *) cell->content;
        if (vol->volName!=NULL)
            free(vol->volName);  
        cell = cell->next;
    }
    adfListFree ( root );
}


/*
 * adfMountHdFile
 *
 */
ADF_RETCODE adfMountHdFile ( struct AdfDevice * const dev )
{
    struct AdfVolume * vol;

    dev->nVol = 0;
    dev->volList = (struct AdfVolume **) malloc (sizeof(struct AdfVolume *));
    if ( dev->volList == NULL ) {
        (*adfEnv.eFct)("adfMountHdFile : malloc");
        return ADF_RC_MALLOC;
    }

    vol = (struct AdfVolume *) malloc (sizeof(struct AdfVolume));
    if ( vol == NULL ) {
        free ( dev->volList );
        dev->volList = NULL;
        (*adfEnv.eFct)("adfMountHdFile : malloc");
        return ADF_RC_MALLOC;
    }
    dev->volList[0] = vol;
    dev->nVol++;      /* fixed by Dan, ... and by Gary */

    vol->dev = dev;
    vol->volName = NULL;
    vol->mounted = false;
    vol->blockSize = 512;
    
    vol->firstBlock = 0;

    unsigned size = dev->size + 512 - ( dev->size % 512 );
/*printf("size=%ld\n",size);*/

    /* set filesystem info (read from bootblock) */
    struct AdfBootBlock boot;
    ADF_RETCODE rc = adfDevReadBlock (
        dev, (uint32_t) vol->firstBlock, 512, (uint8_t *) &boot );
    if ( rc != ADF_RC_OK ) {
        adfEnv.eFct ( "adfMountHdFile : error reading BootBlock, device %s, volume %d",
                      dev->name, 0 );
        free ( dev->volList );
        dev->volList = NULL;
        return rc;
    }
    memcpy ( vol->fs.id, boot.dosType, 3 );
    vol->fs.id[3] = '\0';
    vol->fs.type = (uint8_t) boot.dosType[3];
    vol->datablockSize = adfVolIsOFS ( vol ) ? 488 : 512;

    if ( adfVolIsDosFS ( vol ) ) {
        vol->rootBlock = (int32_t) ( ( size / 512 ) / 2 );
/*printf("root=%ld\n",vol->rootBlock);*/
        uint8_t buf[512];
        bool found = false;
        do {
            rc = dev->drv->readSector ( dev, (uint32_t) vol->rootBlock, 512, buf );
            if ( rc != ADF_RC_OK ) {
                free ( dev->volList );
                dev->volList = NULL;
                return rc;
            }
            found = swapLong(buf) == ADF_T_HEADER &&
                    swapLong(buf + 508) == ADF_ST_ROOT;
            if (!found)
                (vol->rootBlock)--;
        } while (vol->rootBlock>1 && !found);

        if (vol->rootBlock==1) {
            (*adfEnv.eFct)("adfMountHdFile : rootblock not found");
            free ( dev->volList );
            dev->volList = NULL;
            free ( vol );
            dev->nVol = 0;
            return ADF_RC_ERROR;
        }
        vol->lastBlock = vol->rootBlock * 2 - 1;

        struct AdfRootBlock root;
        vol->mounted = true;    // must be set to read the root block
        rc = adfReadRootBlock ( vol, (uint32_t) vol->rootBlock, &root );
        vol->mounted = false;
        if ( rc != ADF_RC_OK ) {
            free ( vol );
            free ( dev->volList );
            dev->volList = NULL;
            dev->nVol = 0;
            return rc;
        }

        vol->volName = strndup ( root.diskName,
                                 min ( root.nameLen,
                                       (unsigned) ADF_MAX_NAME_LEN ) );
    } else { // if ( adfVolIsPFS ( vol ) ) {
        vol->datablockSize = 0; //512;
        vol->volName = NULL;
        vol->rootBlock = -1;
        vol->lastBlock = (int32_t) ( dev->cylinders * dev->heads * dev->sectors - 1 );
    }

    return ADF_RC_OK;
}


/*
 * adfMountHd
 *
 * normal not used directly : called by adfDevMount()
 *
 * fills geometry fields and volumes list (dev->nVol and dev->volList[])
 */
ADF_RETCODE adfMountHd ( struct AdfDevice * const dev, const int32_t rdskBlock )
{
    struct AdfRDSKblock rdsk;
    struct AdfPARTblock part;
    int32_t next;
    struct AdfList *vList, *listRoot;
    int i;
    struct AdfVolume * vol;
    unsigned len;

    ADF_RETCODE rc = adfReadRDSKblock ( dev, rdskBlock , &rdsk );
    if ( rc != ADF_RC_OK )
        return rc;

    /* PART blocks */
    listRoot = NULL;
    next = rdsk.partitionList;
    dev->nVol=0;
    vList = NULL;
    while( next!=-1 ) {
        rc = adfReadPARTblock ( dev, next, &part );
        if ( rc != ADF_RC_OK ) {
            adfFreeTmpVolList(listRoot);
            adfEnv.eFct ( "adfMountHd : read PART, block %d, device '%s'",
                          next, dev->name );
            return rc;
        }

        vol = (struct AdfVolume *) malloc (sizeof(struct AdfVolume));
        if ( vol == NULL ) {
            adfFreeTmpVolList(listRoot);
            (*adfEnv.eFct)("adfMountHd : malloc");
            return ADF_RC_MALLOC;
        }
        vol->dev = dev;
        vol->volName=NULL;
        dev->nVol++;

        vol->firstBlock = (int32_t) rdsk.cylBlocks * part.lowCyl;
        vol->lastBlock = ( part.highCyl + 1 ) * (int32_t) rdsk.cylBlocks - 1;
        vol->blockSize = part.blockSize*4;

        /* set filesystem info (read from bootblock) */
        struct AdfBootBlock boot;
        rc = adfDevReadBlock ( dev, (uint32_t) vol->firstBlock, 512, (uint8_t *) &boot );
        if ( rc != ADF_RC_OK ) {
            adfEnv.eFct ( "adfMountHd : error reading BootBlock, device %s, volume %d",
                          dev->name, dev->nVol - 1 );
            adfFreeTmpVolList ( listRoot );
            free ( vol );
            return rc;
        }
        memcpy ( vol->fs.id, boot.dosType, 3 );
        vol->fs.id[3] = '\0';
        vol->fs.type = (uint8_t) boot.dosType[3];
        vol->datablockSize = adfVolIsOFS ( vol ) ? 488 : 512;

        /* set volume name (from partition info) */
        len = (unsigned) min ( 31, part.nameLen );
        vol->volName = (char*)malloc(len+1);
        if ( vol->volName == NULL ) { 
            adfFreeTmpVolList(listRoot);
            free ( vol );
            (*adfEnv.eFct)("adfMount : malloc");
            return ADF_RC_MALLOC;
        }
        memcpy(vol->volName,part.name,len);
        vol->volName[len] = '\0';

        vol->mounted = false;

        /* stores temporaly the volumes in a linked list */
        if (listRoot==NULL)
            vList = listRoot = adfListNewCell ( NULL, (void *) vol);
        else
            vList = adfListNewCell ( vList, (void *) vol);

        if (vList==NULL) {
            adfFreeTmpVolList(listRoot);
            adfEnv.eFct ( "adfMount : adfListNewCell() malloc" );
            return ADF_RC_MALLOC;
        }

        vol->rootBlock = adfVolIsDosFS ( vol ) ? adfVolCalcRootBlk ( vol ) : -1;

        next = part.next;
    }

    /* stores the list in an array */
    dev->volList = (struct AdfVolume **) malloc (
        sizeof(struct AdfVolume *) * (unsigned) dev->nVol );
    if ( dev->volList == NULL ) {
        adfFreeTmpVolList(listRoot);
        (*adfEnv.eFct)("adfMount : malloc");
        return ADF_RC_MALLOC;
    }
    vList = listRoot;
    for(i=0; i<dev->nVol; i++) {
        dev->volList[i] = (struct AdfVolume *) vList->content;
        vList = vList->next;
    }
    adfListFree ( listRoot );

    /* The code below seems to only check if the FSHD and LSEG blocks can be
       read. These blocks are not required to access partitions/volumes:
       http://lclevy.free.fr/adflib/adf_info.html#p64 */

    struct AdfFSHDblock fshd;
    fshd.segListBlock = -1;

    next = rdsk.fileSysHdrList;
	if (next != -1) {
		while( next!=-1 ) {
			rc = adfReadFSHDblock ( dev, next, &fshd ); 
			if ( rc != ADF_RC_OK ) {
				/*
				for ( i = 0 ; i < dev->nVol ; i++ )
					free ( dev->volList[i] );
				free(dev->volList); */
				adfEnv.wFct ("adfMountHd : adfReadFSHDblock error, device %s, sector %d",
							 dev->name, next );
				//return rc;
				break;
			}
			next = fshd.next;
		}

		struct AdfLSEGblock lseg;
		next = fshd.segListBlock;
		while( next!=-1 ) {
			rc = adfReadLSEGblock ( dev, next, &lseg ); 
			if ( rc != ADF_RC_OK ) {
				/*for ( i = 0 ; i < dev->nVol ; i++ )
					free ( dev->volList[i] );
				free(dev->volList); */
				adfEnv.wFct ("adfMountHd : adfReadLSEGblock error, device %s, sector %s",
							 dev->name, next );
				//return rc;
				break;
			}
			next = lseg.next;
		}
	}

    return ADF_RC_OK;
}


/*
 * adfCreateHdHeader
 *
 * create PARTIALLY the sectors of the header of one harddisk : can not be mounted
 * back on a real Amiga ! It's because some device dependant values can't be guessed...
 *
 * do not use dev->volList[], but partList for partitions information : start and len are cylinders,
 *  not blocks
 * do not fill dev->volList[]
 * called by adfCreateHd()
 */
ADF_RETCODE adfCreateHdHeader ( struct AdfDevice * const               dev,
                                const int                              n,
                                const struct Partition * const * const partList )
{
    (void) n;
    int i;
    struct AdfRDSKblock rdsk;
    struct AdfPARTblock part;
    unsigned len;

    /* RDSK */ 
 
    memset ( (uint8_t *) &rdsk, 0, sizeof(struct AdfRDSKblock) );

    rdsk.rdbBlockLo = 0;
    rdsk.rdbBlockHi = (dev->sectors*dev->heads*2)-1;
    rdsk.loCylinder = 2;
    rdsk.hiCylinder = dev->cylinders-1;
    rdsk.cylBlocks  = dev->sectors*dev->heads;

    rdsk.cylinders = dev->cylinders;
    rdsk.sectors   = dev->sectors;
    rdsk.heads     = dev->heads;
	
    rdsk.badBlockList = -1;
    rdsk.partitionList = 1;
    rdsk.fileSysHdrList = 1 + dev->nVol;

    ADF_RETCODE rc = adfWriteRDSKblock ( dev, &rdsk );
    if ( rc != ADF_RC_OK )
        return rc;

    /* PART */

    ADF_SECTNUM j = 1;
    for(i=0; i<dev->nVol; i++) {
        memset ( &part, 0, sizeof(struct AdfPARTblock) );

        if (i<dev->nVol-1)
            part.next = j+1;
        else
            part.next = -1;

        len = min ( (unsigned) ADF_MAX_NAME_LEN,
                    (unsigned) strlen ( partList[i]->volName ) );
        part.nameLen = (char) len;
        strncpy(part.name, partList[i]->volName, len);

        part.surfaces       = (int32_t) dev->heads;
        part.blocksPerTrack = (int32_t) dev->sectors;
        part.lowCyl = partList[i]->startCyl;
        part.highCyl = partList[i]->startCyl + partList[i]->lenCyl -1;
        memcpy ( part.dosType, "DOS", 3 );

        part.dosType[3] = partList[i]->volType & 0x01;

        rc = adfWritePARTblock ( dev, j, &part );
        if ( rc != ADF_RC_OK )
            return rc;
        j++;
    }

    /* FSHD */
    struct AdfFSHDblock fshd;
    memcpy ( fshd.dosType, "DOS", 3 );
    fshd.dosType[3] = (char) partList[0]->volType;
    fshd.next = -1;
    fshd.segListBlock = j+1;
    rc = adfWriteFSHDblock ( dev, j, &fshd );
    if ( rc != ADF_RC_OK )
        return rc;
    j++;
	
    /* LSEG */
    struct AdfLSEGblock lseg;
    lseg.next = -1;

    return adfWriteLSEGblock ( dev, j, &lseg );
}


/*
 * adfCreateHd
 *
 * create a filesystem one an harddisk device (partitions==volumes, and the header)
 *
 * fills dev->volList[]
 *
 */
ADF_RETCODE adfCreateHd ( struct AdfDevice * const               dev,
                          const unsigned                         n,
                          const struct Partition * const * const partList )
{
    unsigned i, j;

/*struct AdfVolume *vol;*/

    if ( dev == NULL || partList == NULL ) {
        (*adfEnv.eFct)("adfCreateHd : illegal parameter(s)");
        return ADF_RC_ERROR;
    }

    dev->devType = ADF_DEVTYPE_HARDDISK;

    dev->volList = (struct AdfVolume **) malloc (
        sizeof(struct AdfVolume *) * n );
    if (!dev->volList) {
        (*adfEnv.eFct)("adfCreateFlop : malloc");
        return ADF_RC_MALLOC;
    }
    for(i=0; i<n; i++) {
        dev->volList[i] = adfVolCreate( dev,
					(uint32_t) partList[i]->startCyl,
					(uint32_t) partList[i]->lenCyl,
					partList[i]->volName, 
					partList[i]->volType );
        if (dev->volList[i]==NULL) {
           for(j=0; j<i; j++) {
               free( dev->volList[i] );
/* pas fini */
           }
           free(dev->volList);
           adfEnv.eFct ( "adfCreateHd : adfVolCreate() failed" );
        }
    }
    dev->nVol = (int) n;
/*
vol=dev->volList[0];
printf("0first=%ld last=%ld root=%ld\n",vol->firstBlock,
 vol->lastBlock, vol->rootBlock);
*/
    dev->mounted = true;

    return adfCreateHdHeader ( dev, (int) n, partList );
}


/*
 * adfCreateHdFile
 *
 */
ADF_RETCODE adfCreateHdFile ( struct AdfDevice * const dev,
                              const char * const       volName,
                              const uint8_t            volType )
{
    if (dev==NULL) {
        (*adfEnv.eFct)("adfCreateHdFile : dev==NULL");
        return ADF_RC_NULLPTR;
    }
    dev->volList = (struct AdfVolume **) malloc (sizeof(struct AdfVolume *));
    if ( dev->volList == NULL ) {
        adfEnv.eFct ( "adfCreateHdFile : malloc" );
        return ADF_RC_MALLOC;
    }

    dev->volList[0] = adfVolCreate( dev, 0L, dev->cylinders, volName, volType );
    if (dev->volList[0]==NULL) {
        free(dev->volList);
        return ADF_RC_ERROR;
    }

    dev->nVol = 1;
    dev->devType = ADF_DEVTYPE_HARDFILE;

    dev->mounted = true;

    return ADF_RC_OK;
}


/*
 * ReadRDSKblock
 *
 */
ADF_RETCODE adfReadRDSKblock ( struct AdfDevice * const    dev,
                               const int32_t rdskBlockIndex, 
                               struct AdfRDSKblock * const blk )
{
    uint8_t buf[256];

    ADF_RETCODE rc = adfDevReadBlock ( dev, rdskBlockIndex, 256, buf );
    if ( rc != ADF_RC_OK )
       return rc;

    memcpy(blk, buf, 256);
#ifdef LITT_ENDIAN
    /* big to little = 68000 to x86 */
    adfSwapEndian ( (uint8_t *) blk, ADF_SWBL_RDSK );
#endif

    if ( strncmp(blk->id,"RDSK",4)!=0 ) {
        (*adfEnv.eFct)("ReadRDSKblock : RDSK id not found");
        return ADF_RC_ERROR;
    }

    if ( blk->size != 64 )
        (*adfEnv.wFct)("ReadRDSKBlock : size != 64");				/* BV */

    const uint32_t checksumCalculated = adfNormalSum ( buf, 8, 256 );
    if ( blk->checksum != checksumCalculated ) {
        const char msg[] = "adfReadRDSKBlock : invalid checksum 0x%x != 0x%x (calculated)"
            ", block %d, device '%s'";
        if ( adfEnv.ignoreChecksumErrors ) {
            adfEnv.wFct ( msg, blk->checksum, checksumCalculated, 0, dev->name );
        } else {
            adfEnv.eFct ( msg, blk->checksum, checksumCalculated, 0, dev->name );
            return ADF_RC_BLOCKSUM;
        }
    }
	
    if ( blk->blockSize != 512 )
         (*adfEnv.wFct)("ReadRDSKBlock : blockSize != 512");		/* BV */

    if ( blk->cylBlocks !=  blk->sectors*blk->heads )
        (*adfEnv.wFct)( "ReadRDSKBlock : cylBlocks != sectors*heads");

    return rc;
}


/*
 * adfWriteRDSKblock
 *
 */
ADF_RETCODE adfWriteRDSKblock ( struct AdfDevice * const    dev,
                                struct AdfRDSKblock * const rdsk )
{
    uint8_t buf[ADF_LOGICAL_BLOCK_SIZE];
    uint32_t newSum;

    if (dev->readOnly) {
        (*adfEnv.wFct)("adfWriteRDSKblock : can't write block, read only device");
        return ADF_RC_ERROR;
    }

    memset ( buf, 0, ADF_LOGICAL_BLOCK_SIZE );

    memcpy ( rdsk->id, "RDSK", 4 );
    rdsk->size = sizeof(struct AdfRDSKblock) / sizeof(int32_t);
    rdsk->blockSize = ADF_LOGICAL_BLOCK_SIZE;
    rdsk->badBlockList = -1;

    memcpy ( rdsk->diskVendor, "ADFlib  ", 8 );
    memcpy ( rdsk->diskProduct, "harddisk.adf    ", 16 );
    memcpy ( rdsk->diskRevision, "v1.0", 4 );

    memcpy ( buf, rdsk, sizeof(struct AdfRDSKblock) );
#ifdef LITT_ENDIAN
    adfSwapEndian ( buf, ADF_SWBL_RDSK );
#endif

    newSum = adfNormalSum ( buf, 8, ADF_LOGICAL_BLOCK_SIZE );
    swLong(buf+8, newSum);

    return adfDevWriteBlock ( dev, 0, ADF_LOGICAL_BLOCK_SIZE, buf );
}


/*
 * ReadPARTblock
 *
 */
ADF_RETCODE adfReadPARTblock ( struct AdfDevice * const    dev,
                               const int32_t               nSect,
                               struct AdfPARTblock * const blk )
{
    uint8_t buf[ sizeof(struct AdfPARTblock) ];

    ADF_RETCODE rc = adfDevReadBlock ( dev, (uint32_t) nSect,
                                       sizeof(struct AdfPARTblock), buf );
    if ( rc != ADF_RC_OK )
       return rc;

    memcpy ( blk, buf, sizeof(struct AdfPARTblock) );
#ifdef LITT_ENDIAN
    /* big to little = 68000 to x86 */
    adfSwapEndian ( (uint8_t *) blk, ADF_SWBL_PART);
#endif

    if ( strncmp(blk->id,"PART",4)!=0 ) {
    	(*adfEnv.eFct)("ReadPARTblock : PART id not found");
        return ADF_RC_ERROR;
    }

    if ( blk->size != 64 )
        (*adfEnv.wFct)("ReadPARTBlock : size != 64");

    if ( blk->blockSize!=128 ) {
    	(*adfEnv.eFct)("ReadPARTblock : blockSize!=512, not supported (yet)");
        return ADF_RC_ERROR;
    }

    const uint32_t checksumCalculated = adfNormalSum ( buf, 8, 256 );
    if ( blk->checksum != checksumCalculated ) {
        const char msg[] = "adfReadPARTBlock : invalid checksum 0x%x != 0x%x (calculated)"
            ", block %d, device '%s'";
        if ( adfEnv.ignoreChecksumErrors ) {
            adfEnv.wFct ( msg, blk->checksum, checksumCalculated, nSect, dev->name );
        } else {
            adfEnv.eFct ( msg, blk->checksum, checksumCalculated, nSect, dev->name );
            return ADF_RC_BLOCKSUM;
        }
    }

    return rc;
}


/*
 * adfWritePARTblock
 *
 */
ADF_RETCODE adfWritePARTblock ( struct AdfDevice * const    dev,
                                const int32_t               nSect,
                                struct AdfPARTblock * const part )
{
    uint8_t buf[ADF_LOGICAL_BLOCK_SIZE];
    uint32_t newSum;
	
    if (dev->readOnly) {
        (*adfEnv.wFct)("adfWritePARTblock : can't write block, read only device");
        return ADF_RC_ERROR;
    }

    memset ( buf, 0, ADF_LOGICAL_BLOCK_SIZE );

    memcpy ( part->id, "PART", 4 );
    part->size = sizeof(struct AdfPARTblock) / sizeof(int32_t);
    part->blockSize = ADF_LOGICAL_BLOCK_SIZE;
    part->vectorSize = 16;
    part->blockSize = 128;
    part->sectorsPerBlock = 1;
    part->dosReserved = 2;

    memcpy ( buf, part, sizeof(struct AdfPARTblock) );
#ifdef LITT_ENDIAN
    adfSwapEndian ( buf, ADF_SWBL_PART );
#endif

    newSum = adfNormalSum ( buf, 8, ADF_LOGICAL_BLOCK_SIZE );
    swLong(buf+8, newSum);
/*    *(int32_t*)(buf+8) = swapLong((uint8_t*)&newSum);*/

    return adfDevWriteBlock ( dev, (uint32_t) nSect, ADF_LOGICAL_BLOCK_SIZE, buf );
}

/*
 * ReadFSHDblock
 *
 */
ADF_RETCODE adfReadFSHDblock ( struct AdfDevice * const    dev,
                               const int32_t               nSect,
                               struct AdfFSHDblock * const blk )
{
    uint8_t buf[ sizeof(struct AdfFSHDblock) ];

    ADF_RETCODE rc = adfDevReadBlock ( dev, (uint32_t) nSect,
                                       sizeof(struct AdfFSHDblock), buf );
    if ( rc != ADF_RC_OK )
        return rc;
		
    memcpy ( blk, buf, sizeof(struct AdfFSHDblock) );
#ifdef LITT_ENDIAN
    /* big to little = 68000 to x86 */
    adfSwapEndian ( (uint8_t *) blk, ADF_SWBL_FSHD );
#endif

    if ( strncmp(blk->id,"FSHD",4)!=0 ) {
    	(*adfEnv.eFct)("ReadFSHDblock : FSHD id not found");
        return ADF_RC_ERROR;
    }

    if ( blk->size != 64 )
         (*adfEnv.wFct)("ReadFSHDblock : size != 64");

    const uint32_t checksumCalculated = adfNormalSum ( buf, 8, 256 );
    if ( blk->checksum != checksumCalculated ) {
        const char msg[] = "adfReadFSHDBlock : invalid checksum 0x%x != 0x%x (calculated)"
            ", block %d, device '%s'";
        if ( adfEnv.ignoreChecksumErrors ) {
            adfEnv.wFct ( msg, blk->checksum, checksumCalculated, nSect, dev->name );
        } else {
            adfEnv.eFct ( msg, blk->checksum, checksumCalculated, nSect, dev->name );
            return ADF_RC_BLOCKSUM;
        }
    }
    return rc;
}


/*
 *  adfWriteFSHDblock
 *
 */
ADF_RETCODE adfWriteFSHDblock ( struct AdfDevice * const    dev,
                                const int32_t               nSect,
                                struct AdfFSHDblock * const fshd )
{
    uint8_t buf[ADF_LOGICAL_BLOCK_SIZE];
    uint32_t newSum;

    if (dev->readOnly) {
        (*adfEnv.wFct)("adfWriteFSHDblock : can't write block, read only device");
        return ADF_RC_ERROR;
    }

    memset ( buf, 0, ADF_LOGICAL_BLOCK_SIZE );

    memcpy ( fshd->id, "FSHD", 4 );
    fshd->size = sizeof(struct AdfFSHDblock) / sizeof(int32_t);

    memcpy ( buf, fshd, sizeof(struct AdfFSHDblock) );
#ifdef LITT_ENDIAN
    adfSwapEndian ( buf, ADF_SWBL_FSHD );
#endif

    newSum = adfNormalSum ( buf, 8, ADF_LOGICAL_BLOCK_SIZE );
    swLong(buf+8, newSum);
/*    *(int32_t*)(buf+8) = swapLong((uint8_t*)&newSum);*/

    return adfDevWriteBlock ( dev, (uint32_t) nSect, ADF_LOGICAL_BLOCK_SIZE, buf );
}


/*
 * ReadLSEGblock
 *
 */
ADF_RETCODE adfReadLSEGblock ( struct AdfDevice * const    dev,
                               const int32_t               nSect,
                               struct AdfLSEGblock * const blk )
{
    uint8_t buf[ sizeof(struct AdfLSEGblock) ];

    ADF_RETCODE rc = adfDevReadBlock ( dev, (uint32_t) nSect,
                                       sizeof(struct AdfLSEGblock), buf );
    if ( rc != ADF_RC_OK )
        return rc;
		
    memcpy ( blk, buf, sizeof(struct AdfLSEGblock) );
#ifdef LITT_ENDIAN
    /* big to little = 68000 to x86 */
    adfSwapEndian ( (uint8_t *) blk, ADF_SWBL_LSEG );
#endif

    if ( strncmp(blk->id,"LSEG",4)!=0 ) {
    	(*adfEnv.eFct)("ReadLSEGblock : LSEG id not found");
        return ADF_RC_ERROR;
    }

    const uint32_t checksumCalculated = adfNormalSum ( buf, 8, sizeof(struct AdfLSEGblock) );
    if ( blk->checksum != checksumCalculated ) {
        const char msg[] = "adfReadLSEGBlock : invalid checksum 0x%x != 0x%x (calculated)"
            ", block %d, device '%s'";
        if ( adfEnv.ignoreChecksumErrors ) {
            adfEnv.wFct ( msg, blk->checksum, checksumCalculated, nSect, dev->name );
        } else {
            adfEnv.eFct ( msg, blk->checksum, checksumCalculated, nSect, dev->name );
            return ADF_RC_BLOCKSUM;
        }
    }

    if ( blk->next!=-1 && blk->size != 128 )
        (*adfEnv.wFct)("ReadLSEGBlock : size != 128");

    return ADF_RC_OK;
}


/*
 * adfWriteLSEGblock
 *
 */
ADF_RETCODE adfWriteLSEGblock ( struct AdfDevice * const    dev,
                                const int32_t               nSect,
                                struct AdfLSEGblock * const lseg )
{
    uint8_t buf[ADF_LOGICAL_BLOCK_SIZE];
    uint32_t newSum;

    if (dev->readOnly) {
        (*adfEnv.wFct)("adfWriteLSEGblock : can't write block, read only device");
        return ADF_RC_ERROR;
    }

    memset ( buf, 0, ADF_LOGICAL_BLOCK_SIZE );

    memcpy ( lseg->id, "LSEG", 4 );
    lseg->size = sizeof(struct AdfLSEGblock) / sizeof(int32_t);

    memcpy ( buf, lseg, sizeof(struct AdfLSEGblock) );
#ifdef LITT_ENDIAN
    adfSwapEndian ( buf, ADF_SWBL_LSEG );
#endif

    newSum = adfNormalSum ( buf, 8, ADF_LOGICAL_BLOCK_SIZE );
    swLong(buf+8,newSum);
/*    *(int32_t*)(buf+8) = swapLong((uint8_t*)&newSum);*/

    return adfDevWriteBlock ( dev, (uint32_t) nSect, ADF_LOGICAL_BLOCK_SIZE, buf );
}

/*##########################################################################*/
