/*
 *  ADF Library. (C) 1997-2002 Laurent Clevy
 *
 *  adf_dir.c
 *
 *  $Id$
 *
 *  directory code
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


#include "adf_dir.h"

#include "adf_bitm.h"
#include "adf_cache.h"
#include "adf_env.h"
#include "adf_file_block.h"
#include "adf_raw.h"
#include "adf_util.h"
#include "defendian.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/*
 * adfRenameEntry
 *
 */ 
RETCODE adfRenameEntry ( struct AdfVolume * const vol,
                         const SECTNUM            pSect,
                         const char * const       oldName,
                         const SECTNUM            nPSect,
                         const char * const       newName )
{
    struct bEntryBlock parent, previous, entry, nParent;
    SECTNUM nSect2, nSect, prevSect, tmpSect;
    char name2[MAXNAMELEN+1], name3[MAXNAMELEN+1];
	BOOL intl;


    if ( pSect == nPSect  &&
         strcmp ( oldName, newName ) == 0 )
    {
        return RC_OK;
    }
    
    intl = isINTL(vol->dosType) || isDIRCACHE(vol->dosType);
    unsigned len = (unsigned) strlen ( newName );
    adfStrToUpper ( (uint8_t *) name2, (uint8_t*) newName, len, intl );
    adfStrToUpper ( (uint8_t *) name3, (uint8_t*) oldName, (unsigned) strlen(oldName), intl );
    /* newName == oldName ? */

    RETCODE rc = adfReadEntryBlock ( vol, pSect, &parent );
    if ( rc != RC_OK )
        return rc;

    unsigned hashValueO = adfGetHashValue ( (uint8_t *) oldName, intl );

    nSect = adfNameToEntryBlk(vol, parent.hashTable, oldName, &entry, &prevSect);
    if (nSect==-1) {
        (*adfEnv.wFct)("adfRenameEntry : existing entry not found");
        return RC_ERROR;
    }

    /* change name and parent dir */
    entry.nameLen = (uint8_t) min ( 31u, strlen ( newName ) );
    memcpy(entry.name, newName, entry.nameLen);
    entry.parent = nPSect;
    tmpSect = entry.nextSameHash;

    entry.nextSameHash = 0;
    rc = adfWriteEntryBlock ( vol, nSect, &entry );
    if ( rc != RC_OK )
        return rc;

    /* del from the oldname list */

    /* in hashTable */
    if (prevSect==0) {
        parent.hashTable[hashValueO] = tmpSect;
    }
    else {
        /* in linked list */
        rc = adfReadEntryBlock ( vol, prevSect, &previous );
        if ( rc != RC_OK )
            return rc;
        /* entry.nextSameHash (tmpSect) could be == 0 */
        previous.nextSameHash = tmpSect;
        rc = adfWriteEntryBlock ( vol, prevSect, &previous );
        if ( rc != RC_OK )
            return rc;
    }

    // update old parent's ctime and write its block
    adfTime2AmigaTime ( adfGiveCurrentTime(),
                        &parent.days,
                        &parent.mins,
                        &parent.ticks );

    if ( parent.secType == ST_ROOT )
        rc = adfWriteRootBlock ( vol, (uint32_t) pSect, (struct bRootBlock*) &parent );
    else
        rc = adfWriteDirBlock ( vol, pSect, (struct bDirBlock*) &parent );
    if ( rc != RC_OK )
        return rc;

    rc = adfReadEntryBlock ( vol, nPSect, &nParent );
    if ( rc != RC_OK )
        return rc;

    unsigned hashValueN = adfGetHashValue ( (uint8_t * ) newName, intl );
    nSect2 = nParent.hashTable[ hashValueN ];
    /* no list */
    if (nSect2==0) {
        nParent.hashTable[ hashValueN ] = nSect;
    }
    else {
        /* a list exists : addition at the end */
        /* len = strlen(newName);
                   * name2 == newName
                   */
        do {
            rc = adfReadEntryBlock ( vol, nSect2, &previous );
            if ( rc != RC_OK )
                return rc;
            if (previous.nameLen==len) {
                adfStrToUpper ( (uint8_t *) name3,
                                (uint8_t *) previous.name,
                                previous.nameLen, intl );
                if (strncmp(name3,name2,len)==0) {
                    (*adfEnv.wFct)("adfRenameEntry : entry already exists");
                    return RC_ERROR;
                }
            }
            nSect2 = previous.nextSameHash;
/*printf("sect=%ld\n",nSect2);*/
        }while(nSect2!=0);
        
        previous.nextSameHash = nSect;
        if (previous.secType==ST_DIR)
            rc=adfWriteDirBlock(vol, previous.headerKey, 
			       (struct bDirBlock*)&previous);
        else if (previous.secType==ST_FILE)
            rc=adfWriteFileHdrBlock(vol, previous.headerKey, 
                   (struct bFileHeaderBlock*)&previous);
        else {
            (*adfEnv.wFct)("adfRenameEntry : unknown entry type");
            rc = RC_ERROR;
        }
        if ( rc != RC_OK )
            return rc;
    }

    // update new parent's time and write its block
    adfTime2AmigaTime ( adfGiveCurrentTime(),
                        &nParent.days,
                        &nParent.mins,
                        &nParent.ticks );

    if ( nParent.secType == ST_ROOT )
        rc = adfWriteRootBlock ( vol, (uint32_t) nPSect, (struct bRootBlock*) &nParent );
    else
        rc = adfWriteDirBlock ( vol, nPSect, (struct bDirBlock*) &nParent );
    if ( rc != RC_OK )
        return rc;

    // update dircache
    if (isDIRCACHE(vol->dosType)) {
        if (pSect==nPSect) {
            rc = adfUpdateCache ( vol, &parent,
                                  (struct bEntryBlock*) &entry, TRUE );
        }
        else {
            rc = adfDelFromCache ( vol, &parent, entry.headerKey );
            if ( rc != RC_OK )
                return rc;
            rc = adfAddInCache ( vol, &nParent, &entry );
        }
    }
/*
    if (isDIRCACHE(vol->dosType) && pSect!=nPSect) {
        adfUpdateCache(vol, &nParent, (struct bEntryBlock*)&entry,TRUE);
    }
*/
    return rc;
}

/*
 * adfRemoveEntry
 *
 */
RETCODE adfRemoveEntry ( struct AdfVolume * const vol,
                         const SECTNUM            pSect,
                         const char * const       name )
{
    struct bEntryBlock parent, previous, entry;
    SECTNUM nSect2, nSect;
    BOOL intl;
    char buf[200];

    RETCODE rc = adfReadEntryBlock ( vol, pSect, &parent );
    if ( rc != RC_OK )
        return rc;
    nSect = adfNameToEntryBlk(vol, parent.hashTable, name, &entry, &nSect2);
    if (nSect==-1) {
      sprintf(buf, "adfRemoveEntry : entry '%s' not found", name);
        (*adfEnv.wFct)(buf);
        return RC_ERROR;
    }
    /* if it is a directory, is it empty ? */
    if ( entry.secType==ST_DIR && !isDirEmpty((struct bDirBlock*)&entry) ) {
      sprintf(buf, "adfRemoveEntry : directory '%s' not empty", name);
        (*adfEnv.wFct)(buf);
        return RC_ERROR;
    }
/*    printf("name=%s  nSect2=%ld\n",name, nSect2);*/

    /* in parent hashTable */
    if (nSect2==0) {
        intl = isINTL(vol->dosType) || isDIRCACHE(vol->dosType);
        unsigned hashVal = adfGetHashValue ( (uint8_t *) name, intl );
/*printf("hashTable=%d nexthash=%d\n",parent.hashTable[hashVal],
 entry.nextSameHash);*/
        parent.hashTable[hashVal] = entry.nextSameHash;
        rc = adfWriteEntryBlock ( vol, pSect, &parent );
        if ( rc != RC_OK )
            return rc;
    }
    /* in linked list */
    else {
        rc = adfReadEntryBlock ( vol, nSect2, &previous );
        if ( rc != RC_OK )
            return rc;
        previous.nextSameHash = entry.nextSameHash;
        rc = adfWriteEntryBlock ( vol, nSect2, &previous );
        if ( rc != RC_OK )
            return rc;
    }

    if (entry.secType==ST_FILE) {
        rc = adfFreeFileBlocks ( vol, (struct bFileHeaderBlock*) &entry );
        if ( rc != RC_OK )
            return rc;
    	adfSetBlockFree(vol, nSect); //marks the FileHeaderBlock as free in BitmapBlock
    	if (adfEnv.useNotify)
             (*adfEnv.notifyFct)(pSect,ST_FILE);
    }
    else if (entry.secType==ST_DIR) {
        adfSetBlockFree(vol, nSect);
        /* free dir cache block : the directory must be empty, so there's only one cache block */
        if (isDIRCACHE(vol->dosType))
            adfSetBlockFree(vol, entry.extension);

        if (adfEnv.useNotify)
            (*adfEnv.notifyFct)(pSect,ST_DIR);
    }
    else {
      sprintf(buf, "adfRemoveEntry : secType %d not supported", entry.secType);
        (*adfEnv.wFct)(buf);
        return RC_ERROR;
    }

    if (isDIRCACHE(vol->dosType)) {
        rc = adfDelFromCache ( vol, &parent, entry.headerKey );
        if ( rc != RC_OK )
            return rc;
    }

    rc = adfUpdateBitmap ( vol );

    return rc;
}


/*
 * adfSetEntryComment
 *
 */
RETCODE adfSetEntryComment ( struct AdfVolume * const vol,
                             const SECTNUM            parSect,
                             const char * const       name,
                             const char * const       newCmt )
{
    struct bEntryBlock parent, entry;
    SECTNUM nSect;

    RETCODE rc = adfReadEntryBlock ( vol, parSect, &parent );
    if ( rc != RC_OK )
        return rc;
    nSect = adfNameToEntryBlk(vol, parent.hashTable, name, &entry, NULL);
    if (nSect==-1) {
        (*adfEnv.wFct)("adfSetEntryComment : entry not found");
        return RC_ERROR;
    }

    entry.commLen = (uint8_t) min ( (unsigned) MAXCMMTLEN, strlen ( newCmt ) );
    memcpy(entry.comment, newCmt, entry.commLen);

    if ( entry.secType == ST_DIR ) {
        rc = adfWriteDirBlock ( vol, nSect, (struct bDirBlock*) &entry );
        if ( rc != RC_OK )
            return rc;
    }
    else if ( entry.secType == ST_FILE ) {
        rc = adfWriteFileHdrBlock ( vol, nSect, (struct bFileHeaderBlock*) &entry );
        if ( rc != RC_OK )
            return rc;
    }
    else {
        (*adfEnv.wFct)("adfSetEntryComment : entry secType incorrect");
        // abort here?
    }

    if (isDIRCACHE(vol->dosType))
        rc = adfUpdateCache ( vol, &parent, (struct bEntryBlock*) &entry, TRUE );

    return rc;
}


/*
 * adfSetEntryAccess
 *
 */
RETCODE adfSetEntryAccess ( struct AdfVolume * const vol,
                            const SECTNUM            parSect,
                            const char * const       name,
                            const int32_t            newAcc )
{
    struct bEntryBlock parent, entry;
    SECTNUM nSect;

    RETCODE rc = adfReadEntryBlock ( vol, parSect, &parent );
    if ( rc != RC_OK )
        return rc;

    nSect = adfNameToEntryBlk(vol, parent.hashTable, name, &entry, NULL);
    if (nSect==-1) {
        (*adfEnv.wFct)("adfSetEntryAccess : entry not found");
        return RC_ERROR;
    }

    entry.access = newAcc;
    if ( entry.secType == ST_DIR ) {
        rc = adfWriteDirBlock ( vol, nSect, (struct bDirBlock*) &entry );
        if ( rc != RC_OK )
            return rc;
    }
    else if ( entry.secType == ST_FILE) {
        adfWriteFileHdrBlock(vol, nSect, (struct bFileHeaderBlock*)&entry);
        if ( rc != RC_OK )
            return rc;
    }
    else {
        (*adfEnv.wFct)("adfSetEntryAccess : entry secType incorrect");
        // abort here?
    }

    if (isDIRCACHE(vol->dosType))
        rc = adfUpdateCache ( vol, &parent, (struct bEntryBlock*) &entry, FALSE );

    return rc;
}


/*
 * isDirEmpty
 *
 */
BOOL isDirEmpty ( const struct bDirBlock * const dir )
{
    int i;
	
    for(i=0; i<HT_SIZE; i++)
        if (dir->hashTable[i]!=0)
           return FALSE;

    return TRUE;
}


/*
 * adfFreeDirList
 *
 */
void adfFreeDirList ( struct AdfList * const list )
{
    struct AdfList *root, *cell;

    root = cell = list;
    while(cell!=NULL) {
        adfFreeEntry(cell->content);
        if (cell->subdir!=NULL)
            adfFreeDirList(cell->subdir);
        cell = cell->next;
    }
    freeList(root);
}


/*
 * adfGetRDirEnt
 *
 */
struct AdfList * adfGetRDirEnt ( struct AdfVolume * const vol,
                                 const SECTNUM            nSect,
                                 const BOOL               recurs )
{
    struct bEntryBlock entryBlk;
    struct AdfList *cell, *head;
    int i;
    struct AdfEntry * entry;
    SECTNUM nextSector;
    int32_t *hashTable;
    struct bEntryBlock parent;


    if (adfEnv.useDirCache && isDIRCACHE(vol->dosType))
        return (adfGetDirEntCache(vol, nSect, recurs ));


    if (adfReadEntryBlock(vol,nSect,&parent)!=RC_OK)
		return NULL;

    hashTable = parent.hashTable;
    cell = head = NULL;
    for(i=0; i<HT_SIZE; i++) {
        if (hashTable[i]!=0) {
             entry = ( struct AdfEntry * ) malloc ( sizeof ( struct AdfEntry ) );
             if (!entry) {
                 adfFreeDirList(head);
				 (*adfEnv.eFct)("adfGetDirEnt : malloc");
                 return NULL;
             }
             if (adfReadEntryBlock(vol, hashTable[i], &entryBlk)!=RC_OK) {
				 adfFreeDirList(head);
                 return NULL;
             }
             if (adfEntBlock2Entry(&entryBlk, entry)!=RC_OK) {
				 adfFreeDirList(head); return NULL;
             }
             entry->sector = hashTable[i];
	
             if (head==NULL)
                 head = cell = newCell(0, (void*)entry);
             else
                 cell = newCell(cell, (void*)entry);
             if (cell==NULL) {
                 adfFreeDirList(head); return NULL;
             }

             if (recurs && entry->type==ST_DIR)
                 cell->subdir = adfGetRDirEnt(vol,entry->sector,recurs);

             /* same hashcode linked list */
             nextSector = entryBlk.nextSameHash;
             while( nextSector!=0 ) {
                 entry = ( struct AdfEntry * ) malloc ( sizeof ( struct AdfEntry ) );
                 if (!entry) {
                     adfFreeDirList(head);
					 (*adfEnv.eFct)("adfGetDirEnt : malloc");
                     return NULL;
                 }
                 if (adfReadEntryBlock(vol, nextSector, &entryBlk)!=RC_OK) {
					 adfFreeDirList(head); return NULL;
                 }

                 if (adfEntBlock2Entry(&entryBlk, entry)!=RC_OK) {
					 adfFreeDirList(head);
                     return NULL;
                 }
                 entry->sector = nextSector;
	
                 cell = newCell(cell, (void*)entry);
                 if (cell==NULL) {
                     adfFreeDirList(head); return NULL;
                 }
				 
                 if (recurs && entry->type==ST_DIR)
                     cell->subdir = adfGetRDirEnt(vol,entry->sector,recurs);
				 
                 nextSector = entryBlk.nextSameHash;
             }
        }
    }

/*    if (parent.extension && isDIRCACHE(vol->dosType) )
        adfReadDirCache(vol,parent.extension);
*/
    return head;
}


/*
 * adfGetDirEnt
 *
 */
struct AdfList * adfGetDirEnt ( struct AdfVolume * const vol,
                                const SECTNUM            nSect )
{
    return adfGetRDirEnt(vol, nSect, FALSE);
}


/*
 * adfFreeEntry
 *
 */
void adfFreeEntry ( struct AdfEntry * const entry )
{
	if (entry==NULL)
       return;
    if (entry->name)
        free(entry->name);
    if (entry->comment)
        free(entry->comment);
    free(entry);    
}


/*
 * adfDirCountEntries
 *
 */
int adfDirCountEntries ( struct AdfVolume * const vol,
                         const SECTNUM            dirPtr )
{
    struct AdfList *list, *cell;

    int nentries = 0;
    cell = list = adfGetDirEnt ( vol, dirPtr );
    while ( cell ) {
        //adfEntryPrint ( cell->content );
        cell = cell->next;
        nentries++;
    }
    adfFreeDirList ( list );
    return nentries;
}


/*
 * adfToRootDir
 *
 */
RETCODE adfToRootDir ( struct AdfVolume * const vol )
{
    vol->curDirPtr = vol->rootBlock;

    return RC_OK;
}


/*
 * adfChangeDir
 *
 */
RETCODE adfChangeDir ( struct AdfVolume * const vol,
                       const char * const       name )
{
    struct bEntryBlock entry;
    SECTNUM nSect;

    RETCODE rc = adfReadEntryBlock ( vol, vol->curDirPtr, &entry );
    if ( rc != RC_OK )
        return rc;

    nSect = adfNameToEntryBlk(vol, entry.hashTable, name, &entry, NULL);
    if ( nSect == -1 )
        return RC_ERROR;

    // if current entry is a hard-link - load entry of the hard-linked directory
    rc = adfReadEntryBlock ( vol, nSect, &entry );
    if ( rc != RC_OK )
        return rc;
    if ( entry.realEntry )  {
        nSect = entry.realEntry;
    }

/*printf("adfChangeDir=%d\n",nSect);*/
    if (nSect!=-1) {
        vol->curDirPtr = nSect;
/*        if (*adfEnv.useNotify)
            (*adfEnv.notifyFct)(0,ST_ROOT);*/
        return RC_OK;
    }
    else
		return RC_ERROR;
}


/*
 * adfParentDir
 *
 */
SECTNUM adfParentDir ( struct AdfVolume * const vol )
{
    struct bEntryBlock entry;

    if (vol->curDirPtr!=vol->rootBlock) {
        RETCODE rc = adfReadEntryBlock ( vol, vol->curDirPtr, &entry );
        if ( rc != RC_OK )
            return rc;
        vol->curDirPtr = entry.parent;
    }
    return RC_OK;
}


/*
 * adfEntBlock2Entry
 *
 */
RETCODE adfEntBlock2Entry ( const struct bEntryBlock * const entryBlk,
                            struct AdfEntry * const          entry )
{
    char buf[MAXCMMTLEN+1];

	entry->type = entryBlk->secType;
    entry->parent = entryBlk->parent;

    unsigned len = min ( entryBlk->nameLen, (unsigned) MAXNAMELEN );
    strncpy(buf, entryBlk->name, len);
    buf[len] = '\0';
    entry->name = _strdup(buf);
    if (entry->name==NULL)
        return RC_MALLOC;
/*printf("len=%d name=%s parent=%ld\n",entryBlk->nameLen, entry->name,entry->parent );*/
    adfDays2Date( entryBlk->days, &(entry->year), &(entry->month), &(entry->days));
	entry->hour = entryBlk->mins/60;
    entry->mins = entryBlk->mins%60;
    entry->secs = entryBlk->ticks/50;

    entry->access = -1;
    entry->size = 0L;
    entry->comment = NULL;
    entry->real = 0L;
    switch(entryBlk->secType) {
    case ST_ROOT:
        break;
    case ST_DIR:
        entry->access = entryBlk->access;
        len = min ( entryBlk->commLen, (unsigned) MAXCMMTLEN );
        strncpy(buf, entryBlk->comment, len);
        buf[len] = '\0';
        entry->comment = _strdup(buf);
        if (entry->comment==NULL) {
            free(entry->name);
            return RC_MALLOC;
        }
        break;
    case ST_FILE:
        entry->access = entryBlk->access;
        entry->size = entryBlk->byteSize;
        len = min ( entryBlk->commLen, (unsigned) MAXCMMTLEN );
        strncpy(buf, entryBlk->comment, len);
        buf[len] = '\0';
        entry->comment = _strdup(buf);
        if (entry->comment==NULL) {
            free(entry->name);
            return RC_MALLOC;
        }
        break;
    case ST_LFILE:
    case ST_LDIR:
        entry->real = entryBlk->realEntry;
    case ST_LSOFT:
        break;
    default:
        (*adfEnv.wFct)("unknown entry type");
    }
	
    return RC_OK;
}


SECTNUM adfGetEntryByName ( struct AdfVolume * const   vol,
                            const SECTNUM              dirPtr,
                            const char * const         name,
                            struct bEntryBlock * const entry )
{
    // get parent
    struct bEntryBlock parent;
    RETCODE rc = adfReadEntryBlock ( vol, dirPtr, &parent );
    if ( rc != RC_OK ) {
        adfEnv.eFct ( "adfGetEntryByName: error reading parent entry "
                      "(block %d)\n", dirPtr );
        return rc;
    }

    // get entry
    SECTNUM nUpdSect;
    SECTNUM sectNum = adfNameToEntryBlk ( vol, parent.hashTable, name,
                                          entry, &nUpdSect );
    return sectNum;
}



/*
 * adfNameToEntryBlk
 *
 */
SECTNUM adfNameToEntryBlk ( struct AdfVolume * const   vol,
                            const int32_t              ht[],
                            const char * const         name,
                            struct bEntryBlock * const entry,
                            SECTNUM * const            nUpdSect )
{
    uint8_t upperName[MAXNAMELEN+1];
    uint8_t upperName2[MAXNAMELEN+1];
    SECTNUM nSect;
    BOOL found;
    SECTNUM updSect;
    BOOL intl;

    intl = isINTL(vol->dosType) || isDIRCACHE(vol->dosType);
    unsigned hashVal = adfGetHashValue ( (uint8_t *) name, intl );
    unsigned nameLen = min ( (unsigned) strlen ( name ),
                             (unsigned) MAXNAMELEN );
    adfStrToUpper ( upperName, (uint8_t *) name, nameLen, intl );

    nSect = ht[hashVal];
/*printf("name=%s ht[%d]=%d upper=%s len=%d\n",name,hashVal,nSect,upperName,nameLen);
printf("hashVal=%u\n",adfGetHashValue(upperName, intl ));
if (!strcmp("españa.country",name)) {
int i;
for(i=0; i<HT_SIZE; i++) printf("ht[%d]=%d    ",i,ht[i]);
}*/
    if (nSect==0)
        return -1;

    updSect = 0;
    found = FALSE;
    do {
        if (adfReadEntryBlock(vol, nSect, entry)!=RC_OK)
			return -1;
        if (nameLen==entry->nameLen) {
            adfStrToUpper ( upperName2, (uint8_t *) entry->name, nameLen, intl );
/*printf("2=%s %s\n",upperName2,upperName);*/
            found = strncmp((char*)upperName, (char*)upperName2, nameLen)==0;
        }
        if (!found) {
            updSect = nSect;
            nSect = entry->nextSameHash; 
        }
    }while( !found && nSect!=0 );

    if ( nSect==0 && !found )
        return -1;
    else {
        if (nUpdSect!=NULL)
            *nUpdSect = updSect;
        return nSect;
    }
}


/*
 * Access2String
 *
 */
    char* 
adfAccess2String(int32_t acc)
{
    static char ret[8+1];

    strcpy(ret,"----rwed");
    if (hasD(acc)) ret[7]='-';
    if (hasE(acc)) ret[6]='-';
    if (hasW(acc)) ret[5]='-';
    if (hasR(acc)) ret[4]='-';
    if (hasA(acc)) ret[3]='a';
    if (hasP(acc)) ret[2]='p';
    if (hasS(acc)) ret[1]='s';
    if (hasH(acc)) ret[0]='h';

    return(ret);
}


/*
 * adfCreateEntry
 *
 * if 'thisSect'==-1, allocate a sector, and insert its pointer into the hashTable of 'dir', using the 
 * name 'name'. if 'thisSect'!=-1, insert this sector pointer  into the hashTable 
 * (here 'thisSect' must be allocated before in the bitmap).
 */
SECTNUM adfCreateEntry ( struct AdfVolume * const   vol,
                         struct bEntryBlock * const dir,
                         const char * const         name,
                         const SECTNUM              thisSect )
{
    BOOL intl;
    struct bEntryBlock updEntry;
    RETCODE rc;
    char name2[MAXNAMELEN+1], name3[MAXNAMELEN+1];
    SECTNUM nSect, newSect, newSect2;
    struct bRootBlock* root;

/*puts("adfCreateEntry in");*/

    intl = isINTL(vol->dosType) || isDIRCACHE(vol->dosType);
    unsigned len = min ( (unsigned) strlen(name),
                         (unsigned) MAXNAMELEN );
    adfStrToUpper ( (uint8_t *) name2, (uint8_t *) name, len, intl );
    unsigned hashValue = adfGetHashValue ( (uint8_t *) name, intl );
    nSect = dir->hashTable[ hashValue ];

    if ( nSect==0 ) {
        if (thisSect!=-1)
            newSect = thisSect;
        else {
            newSect = adfGet1FreeBlock(vol);
            if (newSect==-1) {
               (*adfEnv.wFct)("adfCreateEntry : nSect==-1");
               return -1;
            }
        }

        dir->hashTable[ hashValue ] = newSect;
        if (dir->secType==ST_ROOT) {
            root = (struct bRootBlock*)dir;
            adfTime2AmigaTime(adfGiveCurrentTime(),
                &(root->cDays),&(root->cMins),&(root->cTicks));
            rc = adfWriteRootBlock ( vol, (uint32_t) vol->rootBlock, root );
        }
        else {
            adfTime2AmigaTime(adfGiveCurrentTime(),&(dir->days),&(dir->mins),&(dir->ticks));
            rc=adfWriteDirBlock(vol, dir->headerKey, (struct bDirBlock*)dir);
        }
/*puts("adfCreateEntry out, dir");*/
        if (rc!=RC_OK) {
            adfSetBlockFree(vol, newSect);    
            return -1;
        }
        else
            return( newSect );
    }

    do {
        if (adfReadEntryBlock(vol, nSect, &updEntry)!=RC_OK)
			return -1;
        if (updEntry.nameLen==len) {
            adfStrToUpper ( (uint8_t *) name3,
                            (uint8_t *) updEntry.name,
                            updEntry.nameLen, intl );
            if (strncmp(name3,name2,len)==0) {
                (*adfEnv.wFct)("adfCreateEntry : entry already exists");
                return -1;
            }
        }
        nSect = updEntry.nextSameHash;
    }while(nSect!=0);

    if (thisSect!=-1)
        newSect2 = thisSect;
    else {
        newSect2 = adfGet1FreeBlock(vol);
        if (newSect2==-1) {
            (*adfEnv.wFct)("adfCreateEntry : nSect==-1");
            return -1;
        }
    }
	 
    rc = RC_OK;
    updEntry.nextSameHash = newSect2;
    if (updEntry.secType==ST_DIR)
        rc=adfWriteDirBlock(vol, updEntry.headerKey, (struct bDirBlock*)&updEntry);
    else if (updEntry.secType==ST_FILE)
        rc=adfWriteFileHdrBlock(vol, updEntry.headerKey, 
		    (struct bFileHeaderBlock*)&updEntry);
    else
        (*adfEnv.wFct)("adfCreateEntry : unknown entry type");

/*puts("adfCreateEntry out, hash");*/
    if (rc!=RC_OK) {
        adfSetBlockFree(vol, newSect2);    
        return -1;
    }
    else
        return(newSect2);
}




/*
 * adfIntlToUpper
 *
 */
uint8_t adfIntlToUpper ( const uint8_t c )
{
    return ( ( c >= 'a' && c <= 'z' ) ||
             ( c >= 224 && c <= 254 && c != 247 ) ) ? c - ('a'-'A') : c ;
}

uint8_t adfToUpper ( const uint8_t c )
{
    return ( c >= 'a' && c <= 'z' ) ? c - ( 'a' - 'A' ) : c ;
}

/*
 * adfStrToUpper
 *
 */
void adfStrToUpper ( uint8_t * const       nstr,
                     const uint8_t * const ostr,
                     const unsigned        nlen,
                     const BOOL            intl )
{
    if (intl)
        for ( unsigned i = 0 ; i < nlen ; i++ )
            nstr[i]=adfIntlToUpper(ostr[i]);
    else
        for ( unsigned i = 0 ; i < nlen ; i++ )
            nstr[i]=adfToUpper(ostr[i]);
    nstr[nlen]='\0';
}


/*
 * adfGetHashValue
 * 
 */
unsigned adfGetHashValue ( const uint8_t * const name,
                           const BOOL            intl )
{
    uint32_t hash, len;
    unsigned int i;
    uint8_t upper;

    len = hash = (uint32_t) strlen ( (const char * const) name );
    for(i=0; i<len; i++) {
        if (intl)
            upper = adfIntlToUpper(name[i]);
        else
            upper = (uint8_t) toupper ( name[i] );
        hash = (hash * 13 + upper) & 0x7ff;
    }
    hash = hash % HT_SIZE;

    return(hash);
}


/*
 * adfEntryPrint
 *
 */
void adfEntryPrint ( const struct AdfEntry * const entry )
{
    printf("%-30s %2d %6d ", entry->name, entry->type, entry->sector);
    printf("%2d/%02d/%04d %2d:%02d:%02d",entry->days, entry->month, entry->year,
        entry->hour, entry->mins, entry->secs);
    if (entry->type==ST_FILE)
        printf("%8d ",entry->size);
    else
        printf("         ");
    if (entry->type==ST_FILE || entry->type==ST_DIR)
        printf("%-s ",adfAccess2String(entry->access));
    if (entry->comment!=NULL)
        printf("%s ",entry->comment);
    putchar('\n');
}


/*
 * adfCreateDir
 *
 */
RETCODE adfCreateDir ( struct AdfVolume * const vol,
                       const SECTNUM            nParent,
                       const char * const       name )
{
    SECTNUM nSect;
    struct bDirBlock dir;
    struct bEntryBlock parent;

    RETCODE rc = adfReadEntryBlock ( vol, nParent, &parent );
    if ( rc != RC_OK )
        return rc;

    /* -1 : do not use a specific, already allocated sector */
    nSect = adfCreateEntry(vol, &parent, name, -1);
    if (nSect==-1) {
        (*adfEnv.wFct)("adfCreateDir : no sector available");
        return RC_ERROR;
    }
    memset(&dir, 0, sizeof(struct bDirBlock));
    dir.nameLen = (uint8_t) min ( (unsigned) MAXNAMELEN, (unsigned) strlen ( name ) );
    memcpy(dir.dirName,name,dir.nameLen);
    dir.headerKey = nSect;

    if (parent.secType==ST_ROOT)
        dir.parent = vol->rootBlock;
    else
        dir.parent = parent.headerKey;
    adfTime2AmigaTime(adfGiveCurrentTime(),&(dir.days),&(dir.mins),&(dir.ticks));

    if (isDIRCACHE(vol->dosType)) {
        /* for adfCreateEmptyCache, will be added by adfWriteDirBlock */
        dir.secType = ST_DIR;
        rc = adfAddInCache ( vol, &parent, (struct bEntryBlock *) &dir );
        if ( rc != RC_OK )
            return rc;
        rc = adfCreateEmptyCache ( vol, (struct bEntryBlock *)&dir, -1 );
        if ( rc != RC_OK )
            return rc;        
    }

    /* writes the dirblock, with the possible dircache assiocated */
    rc = adfWriteDirBlock ( vol, nSect, &dir );
    if ( rc != RC_OK )
        return rc;

    rc = adfUpdateBitmap ( vol );

    if (adfEnv.useNotify)
        (*adfEnv.notifyFct)(nParent,ST_DIR);

    return rc;
}


/*
 * adfCreateFile
 *
 */
RETCODE adfCreateFile ( struct AdfVolume * const        vol,
                        const SECTNUM                   nParent,
                        const char * const              name,
                        struct bFileHeaderBlock * const fhdr )
{
    SECTNUM nSect;
    struct bEntryBlock parent;
/*puts("adfCreateFile in");*/

    RETCODE rc = adfReadEntryBlock ( vol, nParent, &parent );
    if ( rc != RC_OK )
        return rc;

    /* -1 : do not use a specific, already allocated sector */
    nSect = adfCreateEntry(vol, &parent, name, -1);
    if (nSect==-1) return RC_ERROR;
/*printf("new fhdr=%d\n",nSect);*/
    memset(fhdr,0,512);
    fhdr->nameLen = (uint8_t) min ( (unsigned) MAXNAMELEN, (unsigned) strlen ( name ) );
    memcpy(fhdr->fileName,name,fhdr->nameLen);
    fhdr->headerKey = nSect;
    if (parent.secType==ST_ROOT)
        fhdr->parent = vol->rootBlock;
    else if (parent.secType==ST_DIR)
        fhdr->parent = parent.headerKey;
    else
        (*adfEnv.wFct)("adfCreateFile : unknown parent secType");
    adfTime2AmigaTime(adfGiveCurrentTime(),
        &(fhdr->days),&(fhdr->mins),&(fhdr->ticks));

    rc = adfWriteFileHdrBlock ( vol, nSect, fhdr );
    if ( rc != RC_OK )
        return rc;

    if ( isDIRCACHE ( vol->dosType ) ) {
        rc = adfAddInCache ( vol, &parent, (struct bEntryBlock *) fhdr );
        if ( rc != RC_OK )
            return rc;
    }

    rc = adfUpdateBitmap ( vol );

    if (adfEnv.useNotify)
        (*adfEnv.notifyFct)(nParent,ST_FILE);

    return rc;
}


/*
 * adfReadEntryBlock
 *
 */
RETCODE adfReadEntryBlock ( struct AdfVolume * const   vol,
                            const SECTNUM              nSect,
                            struct bEntryBlock * const ent )
{
    uint8_t buf[512];

    RETCODE rc = adfReadBlock ( vol, (uint32_t) nSect, buf );
    if ( rc != RC_OK )
        return rc;

    memcpy(ent, buf, 512);
#ifdef LITT_ENDIAN
    int32_t secType = (int32_t) swapLong ( ( uint8_t * ) &ent->secType );
    if ( secType == ST_LFILE ||
         secType == ST_LDIR ||
         secType == ST_LSOFT  )
    {
        swapEndian((uint8_t*)ent, SWBL_LINK);
    } else {
        swapEndian((uint8_t*)ent, SWBL_ENTRY);
    }
#endif
/*printf("readentry=%d\n",nSect);*/
    if (ent->checkSum!=adfNormalSum((uint8_t*)buf,20,512)) {
        (*adfEnv.wFct)("adfReadEntryBlock : invalid checksum");
        return RC_BLOCKSUM;
    }
    if (ent->type!=T_HEADER) {
        (*adfEnv.wFct)("adfReadEntryBlock : T_HEADER id not found");
        return RC_ERROR;
    }
    if ( ent->nameLen > MAXNAMELEN ||
         ent->commLen > MAXCMMTLEN )
    {
        (*adfEnv.wFct)("adfReadEntryBlock : nameLen or commLen incorrect"); 
        printf("nameLen=%d, commLen=%d, name=%s sector%d\n",
            ent->nameLen,ent->commLen,ent->name, ent->headerKey);
    }

    return RC_OK;
}


/*
 * adfWriteEntryBlock
 *
 */
RETCODE adfWriteEntryBlock ( struct AdfVolume * const         vol,
                             const SECTNUM                    nSect,
                             const struct bEntryBlock * const ent )
{
    uint8_t buf[512];
    uint32_t newSum;

    memcpy(buf, ent, sizeof(struct bEntryBlock));

#ifdef LITT_ENDIAN
    swapEndian(buf, SWBL_ENTRY);
#endif
    newSum = adfNormalSum(buf,20,sizeof(struct bEntryBlock));
    swLong(buf+20, newSum);

    return adfWriteBlock ( vol, (uint32_t) nSect, buf );
}


/*
 * adfWriteDirBlock
 *
 */
RETCODE adfWriteDirBlock ( struct AdfVolume * const vol,
                           const SECTNUM            nSect,
                           struct bDirBlock * const dir )
{
    uint8_t buf[512];
    uint32_t newSum;
    

/*printf("wdirblk=%d\n",nSect);*/
    dir->type = T_HEADER;
    dir->highSeq = 0;
    dir->hashTableSize = 0;
    dir->secType = ST_DIR;

    memcpy(buf, dir, sizeof(struct bDirBlock));
#ifdef LITT_ENDIAN
    swapEndian(buf, SWBL_DIR);
#endif
    newSum = adfNormalSum(buf,20,sizeof(struct bDirBlock));
    swLong(buf+20, newSum);

    if ( adfWriteBlock ( vol, (uint32_t) nSect, buf ) != RC_OK )
        return RC_ERROR;

    return RC_OK;
}



/*###########################################################################*/
