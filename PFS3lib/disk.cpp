
//#define DEBUG 1
#define __USE_SYSBASE
#include <string.h>
#include <stdio.h>
#include <math.h>

// own includes
#include "struct.h"
#include "disk.h"
#include "allocation.h"
#include "volume.h"
#include "directory.h"
#include "anodes.h"
#include "update.h"
#include "checkaccess.h"

#include "kswrapper.h"

#define PROFILE_OFF()
#define PROFILE_ON()

/**********************************************************************/
/*                               DEBUG                                */
/**********************************************************************/

#define DebugOn
#define DebugOff
#define DebugMsg(m)
#define DebugMsgNum(msg,num)
#define DebugMsgName(msg, name)
#define ENTER(x)
#define DB(x)

enum vctype {read, write};
static int CheckDataCache(ULONG blocknr, globaldata *g);
static int CachedRead(ULONG blocknr, SIPTR *error, bool fake, globaldata *g);
static UBYTE *CachedReadD(ULONG blknr, SIPTR *err, globaldata *g);
static int CachedWrite(UBYTE *data, ULONG blocknr, globaldata *g);
static void UpdateSlot(int slotnr, globaldata *g);
static ULONG ReadFromRollover(fileentry_t *file, UBYTE *buffer, ULONG size, SIPTR *error, globaldata *g);
static ULONG WriteToRollover(fileentry_t *file, UBYTE *buffer, ULONG size, SIPTR *error, globaldata *g);
static SFSIZE SeekInRollover(fileentry_t *file, SFSIZE offset, LONG mode, SIPTR *error, globaldata *g);
static SFSIZE ChangeRolloverSize(fileentry_t *file, SFSIZE releof, LONG mode, SIPTR *error, globaldata *g);
static ULONG ReadFromFile(fileentry_t *file, UBYTE *buffer, ULONG size, SIPTR *error, globaldata *g);
static ULONG WriteToFile(fileentry_t *file, UBYTE *buffer, ULONG size, SIPTR *error, globaldata *g);

/**********************************************************************/
/*                            READ & WRITE                            */
/*                            READ & WRITE                            */
/*                            READ & WRITE                            */
/**********************************************************************/

ULONG ReadFromObject(fileentry_t *file, UBYTE *buffer, ULONG size,
	SIPTR *error, globaldata *g)
{
	if (!CheckReadAccess(file,error,g))
		return -1;

	/* check anodechain, make if not there */
	if (!file->anodechain)
	{
		DB(Trace(2,"ReadFromObject","getting anodechain"));
		if (!(file->anodechain = GetAnodeChain(file->le.anodenr, g)))
		{
			*error = PFS_ERROR_NO_FREE_STORE;
			return -1;
		}
	}

#if ROLLOVER
	if (IsRollover(file->le.info))
		return (ULONG)ReadFromRollover(file,buffer,size,error,g);
	else
#endif
		return (ULONG)ReadFromFile(file,buffer,size,error,g);
}

ULONG WriteToObject(fileentry_t *file, UBYTE *buffer, ULONG size,
	SIPTR *error, globaldata *g)
{
	/* check write access */
	if (!CheckWriteAccess(file, error, g))
		return -1;

	/* check anodechain, make if not there */
	if (!file->anodechain)
	{
		if (!(file->anodechain = GetAnodeChain(file->le.anodenr, g)))
		{
			*error = PFS_ERROR_NO_FREE_STORE;
			return -1;
		}
	}

	/* changing file -> set notify flag */
	file->checknotify = 1;
	g->dirty = 1;

#if ROLLOVER
	if (IsRollover(file->le.info))
		return (ULONG)WriteToRollover(file,buffer,size,error,g);
	else
#endif
		return (ULONG)WriteToFile(file,buffer,size,error,g);
}

SFSIZE SeekInObject(fileentry_t *file, SFSIZE offset, LONG mode, SIPTR *error,
	globaldata *g)
{
	/* check access */
	if (!CheckOperateFile(file,error,g))
		return -1;

	/* check anodechain, make if not there */
	if (!file->anodechain)
	{
		if (!(file->anodechain = GetAnodeChain(file->le.anodenr, g)))
		{
			*error = PFS_ERROR_NO_FREE_STORE;
			return -1;
		}
	}

#if ROLLOVER
	if (IsRollover(file->le.info))
		return SeekInRollover(file,offset,mode,error,g);
	else
#endif
		return SeekInFile(file,offset,mode,error,g);
}

SFSIZE ChangeObjectSize(fileentry_t *file, SFSIZE releof, LONG mode,
	SIPTR *error, globaldata *g)
{
	/* check access */
	if (!CheckChangeAccess(file, error, g))
		return -1;

	/* Changing file -> set notify flag */
	file->checknotify = 1;
	*error = 0;

	/* check anodechain, make if not there */
	if (!file->anodechain)
	{
		if (!(file->anodechain = GetAnodeChain(file->le.anodenr, g)))
		{
			*error = PFS_ERROR_NO_FREE_STORE;
			return -1;
		}
	}

#if ROLLOVER
	if (IsRollover(file->le.info))
		return ChangeRolloverSize(file,releof,mode,error,g);
	else
#endif
		return ChangeFileSize(file,releof,mode,error,g);
}



/**********************************************************************
 *
 **********************************************************************/

#if ROLLOVER

/* Read from rollover: at end of file,
 * goto start
 */
static ULONG ReadFromRollover(fileentry_t *file, UBYTE *buffer, ULONG size,
	SIPTR *error, globaldata *g)
{
#define direntry_m file->le.info.file.direntry
#define filesize_m GetDEFileSize(file->le.info.file.direntry, g)

	struct extrafields extrafields;
	unsigned long long read = 0;
	long long q; // quantity
	long long end;
	long long virtualoffset;
	long long virtualend;
	long long t;

	DB(Trace(1,"ReadFromRollover","size = %lx offset = %lx\n",size,file->offset));
	if (!size) return 0;
	GetExtraFields(direntry_m,&extrafields);

	/* limit access to end of file */
	virtualoffset = file->offset - extrafields.rollpointer;
	if (virtualoffset < 0) virtualoffset += filesize_m;
	virtualend = virtualoffset + size;
	virtualend = min(virtualend, extrafields.virtualsize);
	end = virtualend - virtualoffset + file->offset;

	if (end > filesize_m)
	{
		q = filesize_m - file->offset;
		if ((read = (unsigned long long)ReadFromFile(file, buffer, (ULONG)q, error, g)) != q)
			return (ULONG)read;

		end -= filesize_m;
		buffer += q;
		SeekInFile(file, 0, OFFSET_BEGINNING, error, g);
	}

	q = end - file->offset;
	t = ReadFromFile(file, buffer, (ULONG)q, error, g);
	if (t == -1)
		return (ULONG)t;
	else
		read += t;

	return (ULONG)read;

#undef filesize_m
#undef direntry_m
}

/* Write to rollover file. First write upto end of rollover. Then
 * flip to start.
 * Max virtualsize = filesize-1
 */
static ULONG WriteToRollover(fileentry_t *file, UBYTE *buffer, ULONG size,
	SIPTR *error, globaldata *g)
{
#define direntry_m file->le.info.file.direntry
#define filesize_m GetDEFileSize(file->le.info.file.direntry, g)

	struct extrafields extrafields;
	struct direntry *destentry;
	union objectinfo directory;
	struct fileinfo fi;
	UBYTE entrybuffer[MAX_ENTRYSIZE];
	long long written = 0;
	long long q; // quantity
	long long end, virtualend, virtualoffset, t;
	bool extend =false;

	DB(Trace(1,"WriteToRollover","size = %lx offset=%lx, file=%lx\n",size,file->offset,file));
	GetExtraFields(direntry_m,&extrafields);
	end = file->offset + size;

	/* new virtual size */
	virtualoffset = file->offset - extrafields.rollpointer;
	if (virtualoffset < 0) virtualoffset += filesize_m;
	virtualend = virtualoffset + size;
	if (virtualend >= extrafields.virtualsize)
	{
		extrafields.virtualsize = (ULONG)min(filesize_m-1, virtualend);
		extend = true;
	}

	while (end > filesize_m)
	{
		q = filesize_m - file->offset;
		t = WriteToFile(file, buffer, (ULONG)q, error, g);
		if (t == -1) return (ULONG)t;
		written += t;
		if (t != q) return (ULONG)written;
		end -= filesize_m;
		buffer += q;
		SeekInFile(file, 0, OFFSET_BEGINNING, error, g);
	}

	q = end - file->offset;
	t = WriteToFile(file, buffer, (ULONG)q, error, g);
	if (t == -1)
		return (ULONG)t;
	else
		written += t;

	/* change rollpointer etc */
	if (extend && extrafields.virtualsize == filesize_m - 1)
		extrafields.rollpointer = (ULONG)(end + 1);  /* byte PAST eof is offset 0 */
	destentry = (struct direntry *)entrybuffer;
	memcpy(destentry, direntry_m, direntry_m->next);
	AddExtraFields(destentry, &extrafields);

	/* commit changes */
	if (!GetParent(&file->le.info, &directory, error, g))
		return false;
	else
		ChangeDirEntry(file->le.info.file, destentry, &directory, &fi, g);

	return (ULONG)written;

#undef direntry_m
#undef filesize_m
}

static SFSIZE SeekInRollover(fileentry_t *file, SFSIZE offset, LONG mode, SIPTR *error, globaldata *g)
{
#define filesize_m GetDEFileSize(file->le.info.file.direntry, g)
#define direntry_m file->le.info.file.direntry

	struct extrafields extrafields;
	long long oldvirtualoffset, virtualoffset;
	ULONG blockoffset;
	ULONG anodeoffset;

	DB(Trace(1,"SeekInRollover","offset = %ld mode=%ld\n",offset,mode));
	GetExtraFields(direntry_m,&extrafields);

	/* do the seeking */
	oldvirtualoffset = file->offset - extrafields.rollpointer;
	if (oldvirtualoffset < 0) oldvirtualoffset += filesize_m;

	switch (mode)
	{
		case OFFSET_BEGINNING:
			virtualoffset = offset;
			break;

		case OFFSET_END:
			virtualoffset = extrafields.virtualsize + offset;
			break;
		
		case OFFSET_CURRENT:
			virtualoffset = oldvirtualoffset + offset;
			break;
		
		default:
			*error = PFS_ERROR_SEEK_ERROR;
			return -1;
	}

	if ((virtualoffset > extrafields.virtualsize) || virtualoffset < 0)
	{
		*error = PFS_ERROR_SEEK_ERROR;
		return -1;
	}

	/* calculate real offset */
	file->offset = (FSIZE)(virtualoffset + extrafields.rollpointer);
	if (file->offset > filesize_m)
		file->offset -= filesize_m;

	/* calculate new values */
	anodeoffset = (ULONG)(file->offset >> BLOCKSHIFT);
	blockoffset = (ULONG)(file->offset & BLOCKSIZEMASK);
	file->currnode = &file->anodechain->head;
	CorrectAnodeAC(&file->currnode, &anodeoffset, g);
	
	file->anodeoffset  = anodeoffset;
	file->blockoffset  = blockoffset;

	return (SFSIZE)oldvirtualoffset;

#undef filesize_m
#undef direntry_m
}


static SFSIZE ChangeRolloverSize(fileentry_t *file, SFSIZE releof, LONG mode,
	SIPTR *error, globaldata *g)
{
#define filesize_m GetDEFileSize(file->le.info.file.direntry, g)
#define direntry_m file->le.info.file.direntry

	struct extrafields extrafields;
	SFSIZE virtualeof, virtualoffset;
	union objectinfo directory;
	struct fileinfo fi;
	struct direntry *destentry;
	UBYTE entrybuffer[MAX_ENTRYSIZE];

	DB(Trace(1,"ChangeRolloverSize","offset = %ld mode=%ld\n",releof,mode));
	GetExtraFields(direntry_m,&extrafields);

	switch (mode)
	{
		case OFFSET_BEGINNING:
			virtualeof = releof;
			break;

		case OFFSET_END:
			virtualeof = extrafields.virtualsize + releof;
			break;

		case OFFSET_CURRENT:
			virtualoffset = file->offset - extrafields.rollpointer;
			if (virtualoffset < 0) virtualoffset += filesize_m;
			virtualeof = virtualoffset + releof;
			break;
		default: /* bogus parameter -> PFS_ERROR_SEEK_ERROR */
			virtualeof = -1;
			break;
	}
  
	if (virtualeof < 0)
	{
		*error = PFS_ERROR_SEEK_ERROR;
		return -1;
	}

	/* change virtual size */
	if (virtualeof >= filesize_m)
		extrafields.virtualsize = (ULONG)(filesize_m - 1);
	else
		extrafields.virtualsize = virtualeof;

	/* we don't update other filehandles or current offset here */

	/* commit directoryentry changes */
	destentry = (struct direntry *)entrybuffer;
	memcpy(destentry, direntry_m, direntry_m->next);
	AddExtraFields(destentry, &extrafields);

	/* commit changes */
	if (!GetParent(&file->le.info, &directory, error, g))
		return false;
	else
		ChangeDirEntry(file->le.info.file, destentry, &directory, &fi, g);

	return virtualeof;

#undef filesize_m
#undef direntry_m
}

#endif /* ROLLOVER */

/* <ReadFromFile>
**
** Specification:
**
** Reads 'size' bytes from file to buffer (if not readprotected)
** result: #bytes read; -1 = error; 0 = eof
*/
static ULONG ReadFromFile(fileentry_t *file, UBYTE *buffer, ULONG size,
				SIPTR *error, globaldata *g)
{
	ULONG anodeoffset, blockoffset, blockstoread;
	ULONG fullblks, bytesleft;
	ULONG t;
	FSIZE tfs;
	UBYTE *data = NULL, *dataptr;
	bool directread = false;
	struct anodechainnode *chnode;
#if DELDIR
	struct deldirentry *dde;
#endif

	DB(Trace(1,"ReadFromFile","size = %lx offset = %lx\n",size,file->offset));
	if (!CheckReadAccess(file, error, g))
		return -1;

	/* correct size and check if zero */
#if DELDIR
	if (IsDelFile(file->le.info)) {
		if (!(dde = GetDeldirEntryQuick(file->le.info.delfile.slotnr, g)))
			return -1;
		tfs = GetDDFileSize(dde, g) - file->offset;
	}
	else
#endif
		tfs = GetDEFileSize(file->le.info.file.direntry, g) - file->offset;

	if (!(size = min(tfs, size)))
		return 0;

	/* initialize */
	anodeoffset = file->anodeoffset;
	blockoffset = file->blockoffset;
	chnode = file->currnode;
	t = blockoffset + size;
	fullblks = t>>BLOCKSHIFT;       /* # full blocks */
	bytesleft = t&BLOCKSIZEMASK;    /* # bytes in last incomplete block */

	/* check mask, both at start and end */
	bool maskState = (((IPTR)(buffer-blockoffset+BLOCKSIZE))&~g->dosenvec.de_Mask) ||
		(((IPTR)(buffer+size-bytesleft))&~g->dosenvec.de_Mask);
	maskState = !maskState;

	/* read indirect if
	 * - mask failure
	 * - too small
	 * - larger than one block (use 'direct' cached read for just one)
	 */
	if (!maskState || (fullblks<2*DIRECTSIZE && (blockoffset+size>BLOCKSIZE) &&
			  (blockoffset || (bytesleft&&fullblks<DIRECTSIZE))))
	{
		/* full indirect read */
		blockstoread = fullblks + (bytesleft>0);
		if (!(data =(UBYTE *) AllocBufmem (blockstoread<<BLOCKSHIFT, g)))
		{
			*error = PFS_ERROR_NO_FREE_STORE;
			return -1;
		}
		dataptr = data;
	}
	else
	{
		/* direct read */
		directread = true;
		blockstoread = fullblks;
		dataptr = buffer;

		/* read first blockpart */
		if (blockoffset)
		{
			data = CachedReadD(chnode->an.blocknr + anodeoffset, error, g);
			if (data)
			{
				NextBlockAC(&chnode, &anodeoffset, g);

				/* calc numbytes */
				t = BLOCKSIZE-blockoffset;
				t = min(t, size);
				memcpy(dataptr, data+blockoffset, t);
				dataptr+=t;
				if (blockstoread)
					blockstoread--;
				else
					bytesleft = 0;      /* single block access */
			}
		}
	}

	/* read middle part */
	while (blockstoread && !*error) 
	{
		if ((blockstoread + anodeoffset) >= chnode->an.clustersize)
			t = chnode->an.clustersize - anodeoffset;   /* read length */
		else
			t = blockstoread;

		*error = DiskRead(dataptr, t, chnode->an.blocknr + anodeoffset, g);
		if (!*error)
		{
			blockstoread -= t;
			dataptr      += t<<BLOCKSHIFT;
			anodeoffset  += t;
			CorrectAnodeAC(&chnode, &anodeoffset, g);
		}
	}
	
	/* read last block part/ copy read data to buffer */
	if (!*error)
	{
		if (!directread)
			memcpy(buffer, data+blockoffset, size);
		else if (bytesleft)
		{
			data = CachedReadD(chnode->an.blocknr+anodeoffset, error, g);
			if (data)
				memcpy(dataptr, data, bytesleft);
		}
	}

	if (!directread)
		FreeBufmem(data, g);
	if (!*error)
	{
		file->anodeoffset += fullblks;
		file->blockoffset = (file->blockoffset + size)&BLOCKSIZEMASK;   // not bytesleft!!
		CorrectAnodeAC(&file->currnode, &file->anodeoffset, g);
		file->offset += size;
		return size;
	}
	else
	{
		DB(Trace(1,"Read","failed\n"));
		return -1;
	}
}



/* <WriteToFile> 
**
** Specification:
**
** - Copy data in file at current position;
** - Automatic fileextension;
** - Error = bytecount <> opdracht
** - On error no position update
**
** - Clear Archivebit -> done by Touch()
**V- directory protection (amigados does not do this)
**
** result: num bytes written; DOPUS wants -1 = error;
**
** Implementation parts
**
** - Test on writeprotection; yes -> error;
** - Initialisation
** - Extend filesize
** - Write firstblockpart
** - Write all whole blocks
** - Write last block
** - | Update directory (if no errors)
**   | Deextent filesize (if error)
*/
static ULONG WriteToFile(fileentry_t *file, UBYTE *buffer, ULONG size, SIPTR *error, globaldata *g)
{
	ULONG maskok, t;
	ULONG totalblocks, oldblocksinfile;
	FSIZE oldfilesize, newfileoffset;
	ULONG newblocksinfile, bytestowrite, blockstofill;
	ULONG anodeoffset, blockoffset;
	UBYTE *data = NULL, *dataptr;
	bool directwrite =false;
	struct anodechainnode *chnode;
	int slotnr;

	DB(Trace(1,"WriteToFile","size = %lx offset=%lx, file=%lx\n",size,file->offset,file));
	/* initialization values */
	chnode = file->currnode;
	anodeoffset = file->anodeoffset;
	blockoffset = file->blockoffset;
	totalblocks = (blockoffset + size + BLOCKSIZEMASK)>>BLOCKSHIFT;   /* total # changed blocks */
	if (!(bytestowrite = size))                                     /* # bytes to be done */
		return 0;

	/* filesize extend */
	oldfilesize = GetDEFileSize(file->le.info.file.direntry, g);
	newfileoffset = file->offset + size;

	/* Check if too large (QUAD) or overflowed (ULONG)? */
	if (newfileoffset > MAX_FILE_SIZE || newfileoffset < file->offset || (LARGE_FILE_SIZE && newfileoffset > MAXFILESIZE32 && !g->largefile)) {
		*error = PFS_ERROR_DISK_FULL;
		return -1;
	}

	oldblocksinfile = (oldfilesize + BLOCKSIZEMASK)>>BLOCKSHIFT;
	newblocksinfile = (newfileoffset + BLOCKSIZEMASK)>>BLOCKSHIFT;
	if (newblocksinfile > oldblocksinfile)
	{
		t = newblocksinfile - oldblocksinfile;
		if (!AllocateBlocksAC(file->anodechain, t, &file->le.info.file, g))
		{
			SetDEFileSize(file->le.info.file.direntry, oldfilesize, g);
			*error = PFS_ERROR_DISK_FULL;
			return -1;
		}
	}
	/* BUG 980422: this CorrectAnodeAC mode because of AllocateBlockAC!! AND
	 * because anodeoffset can be outside last block! (filepointer is
	 * byte 0 new block
	 */
	CorrectAnodeAC(&chnode,&anodeoffset,g);

	/* check mask */
	maskok = (bool)((((IPTR)(buffer-blockoffset+BLOCKSIZE))&~g->dosenvec.de_Mask) ||
			 (((IPTR)(buffer-blockoffset+(totalblocks<<BLOCKSHIFT)))&~g->dosenvec.de_Mask));
	maskok = (bool)!maskok;

	/* write indirect if
	 * - mask failure
	 * - too small
	 */
	if (!maskok || (totalblocks<2*DIRECTSIZE && (blockoffset+size>BLOCKSIZE*2) &&
			  (blockoffset || totalblocks<DIRECTSIZE)))
	{
		/* indirect */
		/* allocate temporary data buffer */
		if (!(dataptr = data = (UBYTE*)AllocBufmem(totalblocks<<BLOCKSHIFT, g)))
		{
			*error = PFS_ERROR_NO_FREE_STORE;
			goto wtf_error;
		}

		/* first blockpart */
		if (blockoffset)
		{
			*error = DiskRead(dataptr, 1, chnode->an.blocknr + anodeoffset, g);
			bytestowrite += blockoffset;
			if (bytestowrite<BLOCKSIZE)
				bytestowrite = BLOCKSIZE;   /* the first could also be the last block */
		}

		/* copy all 'to be written' to databuffer */
		memcpy(dataptr+blockoffset, buffer, size);
	}
	else
	{
		/* direct */
		dataptr = buffer;
		directwrite = true;

		/* first blockpart */
		if (blockoffset || (totalblocks==1 && newfileoffset > oldfilesize))
		{
			ULONG fbp;  /* first block part */
			UBYTE *firstblock;

			if (blockoffset) 
			{
				slotnr = CachedRead(chnode->an.blocknr + anodeoffset, error,false, g);
				if (*error)
					goto wtf_error;
			}
			else
			{
				/* for one block no offset growing file */
				slotnr = CachedRead(chnode->an.blocknr + anodeoffset, error, true, g);
			}

			/* copy data to cache and mark block as dirty */
			firstblock = &g->dc.data[slotnr<<BLOCKSHIFT];
			fbp = BLOCKSIZE-blockoffset;
			fbp = min(bytestowrite, fbp);       /* the first could also be the last block */
			memcpy(firstblock+blockoffset, buffer, fbp);
			MarkDataDirty(slotnr);

			NextBlockAC(&chnode, &anodeoffset, g);
			bytestowrite -= fbp;
			dataptr += fbp;
			totalblocks--;
		}
	}

	/* write following blocks. If done, then blockoffset always 0 */
	if (newfileoffset > oldfilesize)
	{
		blockstofill = totalblocks;
	}
	else
	{
		blockstofill = bytestowrite>>BLOCKSHIFT;
	}

	while (blockstofill && !*error)
	{
		UBYTE *lastpart = NULL;
		UBYTE *writeptr;

		if (blockstofill + anodeoffset >= chnode->an.clustersize)
			t = chnode->an.clustersize - anodeoffset;   /* t is # blocks to write now */
		else
			t = blockstofill;
		
		writeptr = dataptr;
		// last write, writing to end of file and last block won't be completely filled?
		// all this just to prevent out of bounds memory read access.
		if (t == blockstofill && (bytestowrite & BLOCKSIZEMASK) && newfileoffset > oldfilesize)
		{
			// limit indirect to max 2 * DIRECTSIZE
			if (t > 2 * DIRECTSIZE) {
				// > 2 * DIRECTSIZE: write only last partial block indirectly
				t--;
			} else {
				// indirect write last block(s), including final partial block.
				if (!(lastpart =(UBYTE*) AllocBufmem(t<<BLOCKSHIFT, g)))
				{
					if (t == 1)
					{
						// no memory, do slower cached final partial block write
						goto indirectlastwrite;
					}
					t /= 2;
				} else {
					memcpy(lastpart, dataptr, bytestowrite);
					writeptr = lastpart;
				}
			}
		}

		*error = DiskWrite(writeptr, t, chnode->an.blocknr + anodeoffset, g);
		if (!*error)
		{
			blockstofill  -= t;
			dataptr       += t<<BLOCKSHIFT;
			bytestowrite  -= t<<BLOCKSHIFT;
			anodeoffset   += t;
			CorrectAnodeAC(&chnode, &anodeoffset, g);
		}
		
		if (lastpart) {
			bytestowrite = 0;
			FreeBufmem(lastpart, g);
		}
	}   

indirectlastwrite:
	/* write last block (RAW because cache direct), preserve block's old contents */
	if (bytestowrite && !*error)
	{
		UBYTE *lastblock;

		slotnr = CachedRead(chnode->an.blocknr + anodeoffset, error,false, g);
		if (!*error)
		{
			lastblock = &g->dc.data[slotnr<<BLOCKSHIFT];
			memcpy(lastblock, dataptr, bytestowrite);
			MarkDataDirty(slotnr);
		}
	}

	/* free mem for indirect write */
	if (!directwrite)
		FreeBufmem(data, g);
	if (!*error)
	{
		file->anodeoffset += (blockoffset + size)>>BLOCKSHIFT; 
		file->blockoffset  = (blockoffset + size)&BLOCKSIZEMASK;
		CorrectAnodeAC(&file->currnode, &file->anodeoffset, g);
		file->offset      += size;
		SetDEFileSize(file->le.info.file.direntry, max(oldfilesize, file->offset), g);
		MakeBlockDirty((struct cachedblock *)file->le.info.file.dirblock, g);
		return size;
	}

wtf_error:
	if (newblocksinfile>oldblocksinfile)
	{
		/* restore old state of file */
#if VERSION23
		SetDEFileSize(file->le.info.file.direntry, oldfilesize, g);
		MakeBlockDirty((struct cachedblock *)file->le.info.file.dirblock, g);
		FreeBlocksAC(file->anodechain, newblocksinfile-oldblocksinfile, freeanodes, g);
#else
		FreeBlocksAC(file->anodechain, newblocksinfile-oldblocksinfile, freeanodes, g);
		SetDEFileSize(file->le.info.file.direntry, oldfilesize, g);
		MakeBlockDirty((struct cachedblock *)file->le.info.file.dirblock, g);
#endif
	}

	DB(Trace(1,"WriteToFile","failed\n"));
	return -1;
}


/* SeekInFile
**
** Specification:
**
** - set fileposition
** - if wrong position, resultposition unknown and error
** - result = old position to start of file, -1 = error
**
** - the end of the file is 0 from end
*/
SFSIZE SeekInFile(fileentry_t *file, SFSIZE offset, LONG mode, SIPTR *error, globaldata *g)
{
	SFSIZE oldoffset, newoffset;
	ULONG anodeoffset, blockoffset;
#if DELDIR
	struct deldirentry *delfile = NULL;

	DB(Trace(1,"SeekInFile","offset = %ld mode=%ld\n",offset,mode));
	if (IsDelFile(file->le.info))
		if (!(delfile = GetDeldirEntryQuick(file->le.info.delfile.slotnr, g)))
			return -1;
#endif

	/* do the seeking */
	oldoffset = file->offset;
	newoffset = -1;

	/* TODO: 32-bit wraparound checks */

	switch (mode)
	{
		case OFFSET_BEGINNING:
			newoffset = offset;
			break;
		
		case OFFSET_END:
#if DELDIR
			if (delfile)
				newoffset = GetDDFileSize(delfile, g) + offset;
			else
#endif
				newoffset = GetDEFileSize(file->le.info.file.direntry, g) + offset;
			break;
		
		case OFFSET_CURRENT:
			newoffset = oldoffset + offset;
			break;
		
		default:
			*error = PFS_ERROR_SEEK_ERROR;
			return -1;
	}

#if DELDIR
	if ((newoffset > (delfile ? GetDDFileSize(delfile, g) :
		GetDEFileSize(file->le.info.file.direntry, g))) || (newoffset < 0))
#else
	if ((newoffset > GetDEFileSize(file->le.info.file.direntry)) || (newoffset < 0))
#endif
	{
		*error = PFS_ERROR_SEEK_ERROR;
		return -1;
	}

	/* calculate new values */
	anodeoffset = newoffset >> BLOCKSHIFT;
	blockoffset = newoffset & BLOCKSIZEMASK;
	file->currnode = &file->anodechain->head;
	CorrectAnodeAC(&file->currnode, &anodeoffset, g);
	/* DiskSeek(anode.blocknr + anodeoffset, g); */
	
	file->anodeoffset  = anodeoffset;
	file->blockoffset  = blockoffset;
	file->offset       = newoffset;
	return oldoffset;
}


/* Changes the length of a file
** Returns new length; -1 if failure
**
** Check if allowed
** 'Seek with extend'
** change other locks
** change direntry
*/
SFSIZE ChangeFileSize(fileentry_t *file, SFSIZE releof, LONG mode, SIPTR *error,
	globaldata *g)
{
	listentry_t *fe;
	SFSIZE abseof;
	LONG t;
	ULONG myanode, oldblocksinfile, newblocksinfile;
	FSIZE oldfilesize;

	/* check access */
	DB(Trace(1,"ChangeFileSize","offset = %ld mode=%ld\n",releof,mode));
	if (!CheckChangeAccess(file, error, g))
		return -1;

	/* Changing file -> set notify flag */
	file->checknotify = 1;
	*error = 0;

	/* TODO: 32-bit wraparound checks */

	/* calculate new eof (ala 'Seek') */
	switch (mode)
	{
		case OFFSET_BEGINNING:
			abseof = releof;
			break;
		
		case OFFSET_END:
			abseof = GetDEFileSize(file->le.info.file.direntry, g) + releof;
			break;
		
		case OFFSET_CURRENT:
			abseof = file->offset + releof;
			break;
		
		default:
			*error = PFS_ERROR_SEEK_ERROR;
			return false;
	}

	/* < 0 check still needed because QUAD is signed */
	if (abseof < 0 || abseof > MAX_FILE_SIZE || (LARGE_FILE_SIZE && abseof > MAXFILESIZE32 && !g->largefile))
	{
		*error = PFS_ERROR_SEEK_ERROR;
		return -1;
	}

	/* change allocation (ala WriteToFile) */
	oldfilesize = GetDEFileSize(file->le.info.file.direntry, g);
	oldblocksinfile = (GetDEFileSize(file->le.info.file.direntry, g) + BLOCKSIZEMASK)>>BLOCKSHIFT;
	newblocksinfile = (abseof+BLOCKSIZEMASK)>>BLOCKSHIFT;

	if (newblocksinfile > oldblocksinfile)
	{
		/* new blocks, 4*allocated anode, dirblock */
		t = newblocksinfile - oldblocksinfile; 
		if (!AllocateBlocksAC(file->anodechain, t, &file->le.info.file, g))
		{
			SetDEFileSize(file->le.info.file.direntry, oldfilesize, g);
			*error = PFS_ERROR_DISK_FULL;
			return -1;
		}

		/* change directory: in case of allocate this has to be done
		 * afterwards
		 */
		SetDEFileSize(file->le.info.file.direntry, abseof, g);
		MakeBlockDirty((struct cachedblock *)file->le.info.file.dirblock, g);
	}
	else if (oldblocksinfile > newblocksinfile)
	{
		/* change directoryentry beforehand (needed for postponed delete but not
		 * allowed for online updating allocate).
		 */
		SetDEFileSize(file->le.info.file.direntry, abseof, g);
		MakeBlockDirty((struct cachedblock *)file->le.info.file.dirblock, g);

		t = oldblocksinfile - newblocksinfile;
		FreeBlocksAC(file->anodechain, t, freeanodes, g);

		/* PS: there will always be an anode left, since FreeBlocksAC
		 * doesn't delete the last anode
		 */
	}
	else
	{
		/* no change in number of blocks, just change directory entry */
		SetDEFileSize(file->le.info.file.direntry, abseof, g);
		MakeBlockDirty((struct cachedblock *)file->le.info.file.dirblock, g);
	}

	/* change filehandles (including own) */
	myanode = file->le.anodenr;
	for (fe = (listentry_t*)HeadOf(&g->currentvolume->fileentries); fe->next; fe=fe->next)
	{
		if (fe->anodenr == myanode)
		{
			if (IsFileEntry(fe) && ((fileentry_t *)fe)->offset >= abseof)
				SeekInFile((fileentry_t *)fe, abseof, OFFSET_BEGINNING, error, g);
		}
	}

	return abseof;
}


/**********************************************************************/
/*                           CACHE STUFF                              */
/**********************************************************************/

void FreeDataCache(globaldata *g)
{
	FreeVec(g->dc.ref);
	FreeVec(g->dc.data);
	g->dc.ref = NULL;
	g->dc.data = NULL;
}

bool InitDataCache(globaldata *g)
{
	FreeDataCache(g);
	/* data cache */
	g->dc.size = DATACACHELEN;
	g->dc.mask = DATACACHEMASK;
	g->dc.roving = 0;
	g->dc.ref = (struct reftable* )AllocVec(DATACACHELEN * sizeof(struct reftable));
	g->dc.data = (UBYTE*)AllocVec(DATACACHELEN * BLOCKSIZE);
	if (!g->dc.ref || !g->dc.data)
		return false;

	return true;
}

/* check datacache. return cache slotnr or -1
 * if not found
 */
static int CheckDataCache(ULONG blocknr, globaldata *g)
{
	int i;

	for (i = 0; i < g->dc.size; i++)
	{
		if (g->dc.ref[i].blocknr == blocknr)
			return i;
	}

	return -1;
}

/* get block from cache or put it in cache if it wasn't
 * there already. return cache slotnr. errors are indicated by 'error'
 * (null = ok)
 */
static int CachedRead(ULONG blocknr, SIPTR *error, bool fake, globaldata *g)
{
	int i;

	*error = 0;
	i = CheckDataCache(blocknr, g);
	if (i != -1) return i;
	i = g->dc.roving;
	if (g->dc.ref[i].dirty && g->dc.ref[i].blocknr)
		UpdateSlot(i, g);

	if (fake)
		memset(&g->dc.data[i<<BLOCKSHIFT], 0xAA, BLOCKSIZE);
	else {
		*error = RawRead(&g->dc.data[i << BLOCKSHIFT], 1, blocknr, g);
	}
	g->dc.roving = (g->dc.roving+1)&g->dc.mask;
	g->dc.ref[i].dirty = 0;
	g->dc.ref[i].blocknr = blocknr;
	return i;
}

static UBYTE *CachedReadD(ULONG blknr, SIPTR *err, globaldata *g)
{ 
	int i;

	i = CachedRead(blknr, err,false, g);
	if (*err)   
		return NULL;
	else
		return &g->dc.data[i<<BLOCKSHIFT];
}

/* write block in cache. if block was already cached,
 * overwrite it. return slotnr (never fails).
 */
static int CachedWrite(UBYTE *data, ULONG blocknr, globaldata *g)
{
	int i;

	i = CheckDataCache(blocknr, g);
	if (i == -1)
	{
		i = g->dc.roving;
		g->dc.roving = (g->dc.roving+1)&g->dc.mask;
		if (g->dc.ref[i].dirty && g->dc.ref[i].blocknr)
			UpdateSlot(i, g);
	}
	memcpy(&g->dc.data[i<<BLOCKSHIFT], data, BLOCKSIZE);
	g->dc.ref[i].dirty = 1;
	g->dc.ref[i].blocknr = blocknr;
	return i;
}


/* flush all blocks in datacache (without updating them first).
 */
void FlushDataCache(globaldata *g)
{
	int i;

	for (i=0; i<g->dc.size; i++)
		g->dc.ref[i].blocknr = 0;
}

/* write all dirty blocks to disk
 */
void UpdateDataCache(globaldata *g)
{
	int i;

	for (i=0; i<g->dc.size; i++)
		if (g->dc.ref[i].dirty && g->dc.ref[i].blocknr)
			UpdateSlot (i, g);
}


/* update a data cache slot, and any adjacent blocks
 */
static void UpdateSlot(int slotnr, globaldata *g)
{
	ULONG blocknr;
	int i;
	
	blocknr = g->dc.ref[slotnr].blocknr;

	/* find out how many adjacent blocks can be written */
	for (i=slotnr; i<g->dc.size; i++)
	{
		if (g->dc.ref[i].blocknr != blocknr++)
			break;
		g->dc.ref[i].dirty = 0;
	}

	/* write them */
	RawWrite(&g->dc.data[slotnr<<BLOCKSHIFT], i-slotnr, g->dc.ref[slotnr].blocknr, g);
}

/* update cache to reflect blocks read to or written
 * from disk. to be called before disk is accessed.
 */
static void ValidateCache(ULONG blocknr, ULONG numblocks, enum vctype vctype, globaldata *g)
{
	int i;

	ENTER("ValidateCache");
	for (i=0; i<g->dc.size; i++)
	{
		if (g->dc.ref[i].blocknr >= blocknr && 
			g->dc.ref[i].blocknr < blocknr + numblocks)
		{
			if (vctype == read)
			{
				if (g->dc.ref[i].dirty)
					UpdateSlot(i, g);
			}
			else    // flush
				g->dc.ref[i].blocknr = 0;
		}
	}
}


/**********************************************************************/
/*                           DEVICECOMMANDS                           */
/*                           DEVICECOMMANDS                           */
/*                           DEVICECOMMANDS                           */
/**********************************************************************/


/* DiskRead
**
** Reads 'blocks' complete blocks in a caller supplied buffer.
**
** input : - buffer: buffer for data
**         - blockstoread: number of blocks to read
**         - blocknr: starting block
**
** global: - disk is used to get request struct
**
** result: errornr, 0=ok
*/
ULONG DiskRead(UBYTE* buffer, ULONG blockstoread, ULONG blocknr, globaldata* g)
{
	SIPTR error;
	int slotnr;

	DB(Trace(1, "DiskRead", "%ld blocks from %ld firstblock %ld\n",
		(ULONG)blockstoread, (ULONG)blocknr, g->firstblock));

	if (blocknr == (ULONG)-1)  // blocknr of uninitialised anode
		return 1;
	if (!blockstoread)
		return 0;

	if (blockstoread == 1)
	{
		slotnr = CachedRead(blocknr, &error, false, g);
		memcpy(buffer, &g->dc.data[slotnr << BLOCKSHIFT], BLOCKSIZE);
		return error;
	}
	ValidateCache(blocknr, blockstoread, read, g);
	return RawRead(buffer, blockstoread, blocknr, g);
}


/* DiskWrite
**
** Writes 'blocks' complete blocks from a buffer.
**
** input : - buffer: the data
**         - blockstowrite: number of blocks to write
**         - blocknr: starting block
**
** global: - disk is used to get request struct
**
** result: errornr, 0=ok
*/
ULONG DiskWrite(UBYTE* buffer, ULONG blockstowrite, ULONG blocknr, globaldata* g)
{
	ULONG slotnr;
	ULONG error = 0;

	DB(Trace(1, "DiskWrite", "%ld blocks from %ld + %ld\n", blockstowrite, blocknr,
		g->firstblock));

	if (blocknr == (ULONG)-1)  // blocknr of uninitialised anode
		return 1;
	if (!blockstowrite)
		return 0;

	if (blockstowrite == 1)
	{
		CachedWrite(buffer, blocknr, g);
		return 0;
	}
	ValidateCache(blocknr, blockstowrite, write, g);
	error = RawWrite(buffer, blockstowrite, blocknr, g);

	/* cache last block written */
	if (!error)
	{
		buffer += ((blockstowrite - 1) << BLOCKSHIFT);
		slotnr = CachedWrite(buffer, blocknr + blockstowrite - 1, g);
		g->dc.ref[slotnr].dirty = 0;    // we just wrote it
	}
	return error;
}


ULONG RawRead(UBYTE *buffer, ULONG blocks, ULONG blocknr, globaldata *g) {
	while (blocks) {
		if (!g->readSector(blocknr, buffer)) return 1;
		blocknr++;
		blocks--;
		buffer += g->blocksize;		
	}
	return 0;
}


volatile static bool b = 0;

ULONG RawWrite(UBYTE *buffer, ULONG blocks, ULONG blocknr, globaldata *g) {
	if (b) {
		b = false;
	}
	b = true;
	while (blocks) {
		if (!g->writeSector(blocknr, buffer)) {
			b = false;
			return 1;
		}
		blocknr++;
		blocks--;
		buffer += g->blocksize;
	}
	b = false;
	return 0;
}
