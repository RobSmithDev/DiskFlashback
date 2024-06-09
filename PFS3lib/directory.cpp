
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <limits.h>

// own includes
#include "exec_nodes.h"
#include "debug.h"
#include "blocks.h"
#include "struct.h"
#include "directory.h"
#include "allocation.h"
#include "volume.h"
#include "disk.h"
#include "anodes.h"
#include "messages.h"
#include "update.h"
#include "lru.h"
#include "ass.h"
#include "lock.h"
#include "SmartSwap.h"
#include "kswrapper.h"

/**********************************************************************/
/*                               DEBUG                                */
/**********************************************************************/

#define DebugOn
#define DebugOff
#define DebugMsg(m)
#define DebugMsgNum(msg,num)
#define DebugMsgName(msg, name)

/*
 * Contents
 */
static bool GetParentOf(union objectinfo *path, SIPTR *error, globaldata *);
static bool GetDir(STRPTR dirname, union objectinfo *path, SIPTR *error, globaldata *);
static bool GetObject(STRPTR objectname, union objectinfo *path, SIPTR *error, globaldata *);
static bool SearchInDir(ULONG dirnodenr, STRPTR objectname, union objectinfo *info, globaldata * g);
void FillFib(struct direntry *direntry, struct FileInfoBlock *fib, globaldata * g);
#if DELDIR
static struct deldirentry *SearchInDeldir(STRPTR delname, union objectinfo *result, globaldata *g);
static bool IsDelfileValid(struct deldirentry *dde, struct cdeldirblock *ddblk, globaldata *g);
static bool BlockTaken(struct canode *anode, globaldata *g);
static void FillDelfileFib(struct deldirentry *dde, uint64_t slotnr, struct FileInfoBlock *fib, globaldata *g);
static struct deldirentry *GetDeldirEntry(IPTR *nr, globaldata *g);
static ULONG FillInDDEData(struct ExAllData *buffer, LONG type, struct deldirentry *dde, uint64_t ddenr, ULONG spaceleft, globaldata *g);
static struct cdeldirblock *GetDeldirBlock(UWORD seqnr, globaldata *g);
#endif
static void GetFirstEntry(lockentry_t *, globaldata *);
static ULONG GetFirstNonEmptyDE(ULONG anodenr, struct fileinfo *, globaldata *);
static void RemakeNextEntry(lockentry_t *, struct FileInfoBlock *, globaldata *);
static ULONG FillInData(struct ExAllData *, LONG, struct direntry *, ULONG, globaldata *);
static bool DeleteDir(union objectinfo *, SIPTR *, globaldata *);
static bool DirIsEmpty(ULONG, globaldata *);
static bool MakeDirEntry(int8_t type, UBYTE *name, UBYTE *entrybuffer, globaldata * g);
static bool IsChildOf(union objectinfo child, union objectinfo parent, globaldata * g);
static bool DeleteLink(struct fileinfo *link, SIPTR *, globaldata * g);
static bool RemapLinks(struct fileinfo *object, globaldata * g);
static void UpdateLinkDir(struct direntry *object, ULONG newdiran, globaldata * g);
static void MoveLink(struct direntry *object, ULONG newdiran, globaldata *g);
static void RenameAcrossDirs(struct fileinfo from, struct direntry *to, union objectinfo *destdir, struct fileinfo *result, globaldata * g);
static void RenameWithinDir(struct fileinfo from, struct direntry *to, struct fileinfo *newinfo, globaldata * g);
static void RenameInPlace(struct fileinfo from, struct direntry *to, struct fileinfo *result, globaldata * g);
static struct direntry *CheckFit(struct cdirblock *blok, int needed, globaldata * g);
static bool MoveToPrevious(struct fileinfo de, struct direntry *to, struct fileinfo *result, globaldata * g);
static void RemoveDirEntry(struct fileinfo info, globaldata * g);
static bool AddDirectoryEntry(union objectinfo *dir, struct direntry *newentry, struct fileinfo *newinfo, globaldata * g);
static void UpdateChangedRef(struct fileinfo from, struct fileinfo *to, int diff, globaldata * g);

#if VERSION23
static int AllocDeldirSlot(globaldata * g);
static void AddToDeldir(union objectinfo *info, int ddnr, globaldata * g);
#else
static void AddToDeldir(union objectinfo *info, globaldata * g);
static bool FreeAnodeBlocks(ULONG anodenr, enum freeblocktype freenodes, globaldata * g);
#endif

/**********************************************************************/
/*                        SEARCH IN DIRECTORY                         */
/*                        SEARCH IN DIRECTORY                         */
/*                        SEARCH IN DIRECTORY                         */
/**********************************************************************/

/* GetFullPath converts a relative path to an absolute path.
 * The fileinfo of the new path is returned in [result].
 * The return value is the filename without path.
 * Error: return 0
 *
 * Parsing Syntax:
 * : after '/' or at the beginning ==> root
 * : after [name] ==> volume [name]
 * / after / or ':' or at the beginning ==> parent
 * / after dir ==> get dir
 * / after file ==> error (ALWAYS) (AMIGADOS ok if LAST file)
 *
 * IN basispath, filename, g
 * OUT fullpath, error
 *
 * If only a partial path is found, a pointer to the unparsed part
 * will be stored in g->unparsed.
 */
UBYTE *GetFullPath(union objectinfo *basispath, STRPTR filename,
				   union objectinfo *fullpath, SIPTR *error, globaldata * g)
{
	UBYTE *pathpart, parttype;
	COUNT index;
	bool eop = false, success = true;
	struct volumedata *volume;

	// VVV Init:getrootvolume
	ENTER("GetFullPath");
	g->unparsed = NULL;

	/* Set base path */
	if (basispath)
		*fullpath = *basispath;
	else
		GetRoot(fullpath, g);

	/* The basispath should not be a file
	 * BTW: softlink is illegal too, but not possible
	 */
	if (IsFile(*fullpath) || IsDelFile(*fullpath))
	{
		*error = PFS_ERROR_OBJECT_WRONG_TYPE;
		return NULL;
	}

	/* check if device present */
	if (IsVolume(*fullpath) || IsDelDir(*fullpath))
		volume = fullpath->volume.volume;
	else
		volume = fullpath->file.dirblock->volume;

	if (!CheckVolume(volume, 0, error, g))
		return (0);

	/* extend base-path using filename and
	 * continue until path complete (eop = end of path)
	 */
	while (!eop)
	{
		pathpart = (UBYTE*) filename;
		index = (UWORD)strcspn(filename, "/:");
		parttype = filename[index];
		filename[index] = 0x0;

		switch (parttype)
		{
			case ':':
				success = false;
				break;

			case '/':
				if (*pathpart == 0x0)
				{
					// if already at root, fail with an error
					if (IsVolume(*fullpath))
					{
						*error = PFS_ERROR_OBJECT_NOT_FOUND;
						success = false;
						break;
					}
					success = GetParentOf(fullpath, error, g);
				}
				else
					success = GetDir((STRPTR)pathpart, fullpath, error, g);
				break;

			default:
				eop = true;
		}

		filename[index] = parttype;

		if (!success)
		{
			/* return pathrest for readlink() */
			if (*error == PFS_ERROR_IS_SOFT_LINK)
				g->unparsed = filename + index;
			else if (*error == PFS_ERROR_OBJECT_NOT_FOUND)
				g->unparsed = filename;
			return NULL;
		}

		if (!eop)
			filename += index + 1;
	}

	return (UBYTE*)filename;
}

bool GetRoot(union objectinfo * path, globaldata * g)
{
	UpdateCurrentDisk(g);
	path->volume.root = 0;
	path->volume.volume = g->currentvolume;
	return true;
}

/* pre: - path <> 0 and volume or directory
 * result back in path
 */
static bool GetParentOf(union objectinfo *path, SIPTR *error, globaldata * g)
{
	bool ok;
	union objectinfo info;

	info = *path;
	ok = GetParent(&info, path, error, g);
	return ok;
}

/* pre: - path <> 0 and volume of directory
 *      - dirname without path; strlen(dirname) > 0
 * result back in path
 */
static bool GetDir(STRPTR dirname, union objectinfo *path, SIPTR *error, globaldata * g)
{
	bool found;

	found = GetObject(dirname, path, error, g);

#if DELDIR
	if (g->deldirenabled && IsDelDir(*path))
		return true;
#endif

	/* check if found directory */
#if DELDIR
	if (!found || IsFile(*path) || IsDelFile(*path))
#else
	if (!found || IsFile(*path))
#endif
	{
		*error = PFS_ERROR_OBJECT_NOT_FOUND;    // DOPUS doesn't like DIR_NOT_FOUND
		return false;
	}

	/* check if softlink */
	if (IsSoftLink(*path))
	{
		*error = PFS_ERROR_IS_SOFT_LINK;
		return false;
	}

	/* resolve links */
	if (path->file.direntry->type == ST_LINKDIR)
	{
		struct extrafields extrafields;
		struct canode linknode;

		GetExtraFields(path->file.direntry, &extrafields);
		GetAnode(&linknode, path->file.direntry->anode, g);
		if (!FetchObject(linknode.clustersize, extrafields.link, path, g))
			return false;
	}

	return true;
}

/* pre: - path<>0 and volume of directory
 *      - objectname without path; strlen(objectname) > 0
 * result back in path
 */
static bool GetObject(STRPTR objectname, union objectinfo *path, SIPTR *error, globaldata *g)
{
	ULONG anodenr;
	bool found;
#if DELDIR
	if (IsDelDir(*path))
	{
		found = (SearchInDeldir(objectname, path, g) != NULL);
		goto go_error;
	}
#endif

	if (IsVolume(*path))
		anodenr = ANODE_ROOTDIR;
	else
		anodenr = path->file.direntry->anode;

	DB(Trace(1, "GetObject", "parent anodenr %lx\n", anodenr));
	found = SearchInDir(anodenr, objectname, path, g);

  go_error:
	if (!found)
	{
#if DELDIR
		if (g->deldirenabled && IsVolume(*path))
		{
			if (_stricmp((char*)(deldirname+1), objectname) == 0)
			{
				path->deldir.special = SPECIAL_DELDIR;
				path->deldir.volume = g->currentvolume;
				return true;
			}
		}
#endif
		*error = PFS_ERROR_OBJECT_NOT_FOUND;
		return false;
	}

	return true;
}

/* <FindObject> 
 *
 * FindObject searches the object 'fname' in directory 'directory'.
 * FindObject zoekt het object 'fname' in directory 'directory'. 
 * Interpret empty filename as parent and ":" as root.
 * Does not use multiple-assign-list
 *
 * input : - [directory]: the 'root' directory of the search
 *         - [objectname]: file to be found, including path
 * 
 * output: - [object]: If file found : fileinfo of object
 *                     If path found : fileinfo of directory
 *         - [error]: Errornumber as result = false; otherwise 0
 *
 * result: true  (-1) = file found (->in fileinfo)
 *          false (0)  = error
 *
 * If only a partial path is found, a pointer to the unparsed part
 * will be stored in g->unparsed.
 */
bool FindObject(union objectinfo *directory, STRPTR objectname,
				union objectinfo *object, SIPTR *error, globaldata *g)
{
	UBYTE *filename;
	bool ok;

	*error = 0;
	filename = GetFullPath(directory, objectname, object, error, g);

	if (!filename)
	{
		DB(Trace(2, "FindObject !filename %s\n", objectname));
		return false;
	}

	/* path only (dir or volume) */
	if (!*filename)
		return true;

	/* there is a filepart (file or dir)  */
	ok = GetObject((STRPTR)filename, object, error, g);
	if (!ok && (*error == PFS_ERROR_OBJECT_NOT_FOUND))
		g->unparsed = (char*)filename;

	return ok;
}


/* GetParent
 *
 * childanodenr = anodenr of start directory (the child)
 * parentanodenr = anodenr of directory containing childanodenr (the parent)
 * childfi == parentfi can be dangerous
 * in:childfi; out:parentfi, error
 */
bool GetParent(union objectinfo *childfi, union objectinfo *parentfi, SIPTR *error, globaldata *g)
{
	struct canode anode;
	struct cdirblock *dirblock = nullptr;
	struct direntry *de = nullptr;
	ULONG anodeoffset = 0;
	ULONG childanodenr, parentanodenr;
	bool eod = false, eob = false, found = false;

	// -I- Find anode of parent
	if (!childfi || IsVolume(*childfi))     // child is rootdir
	{
		*error = 0x0;           /* No error; just return NULL */
		return 0;
	}

#if DELDIR
	if (g->deldirenabled)
	{
		if (IsDelDir(*childfi))
			return GetRoot(parentfi, g);

		if (IsDelFile(*childfi))
		{
			parentfi->deldir.special = SPECIAL_DELDIR;
			parentfi->deldir.volume = g->currentvolume;
			return true;
		}
	}
#endif

	childanodenr = childfi->file.dirblock->blk.anodenr;     /* the directory 'child' is in */
	parentanodenr = childfi->file.dirblock->blk.parent;     /* the directory 'childanodenr' is in */

	// -II- check if in root
	if (parentanodenr == 0)     /* child is in rootdir */
	{
		parentfi->volume.root = 0;
		parentfi->volume.volume = childfi->file.dirblock->volume;
		return true;
	}

	// -III- get parentdirectory and find direntry
	GetAnode(&anode, parentanodenr, g);
	while (!found && !eod)
	{
		dirblock = LoadDirBlock(anode.blocknr + anodeoffset, g);
		if (dirblock)
		{
			de = FIRSTENTRY(dirblock);
			eob = 0;

			while (!found && !eob)
			{
				found = (de->anode == childanodenr);
				eob = (de->next == 0);
				if (!found && !eob)
					de = NEXTENTRY(de);
			}

			if (!found)
				eod = !NextBlock(&anode, &anodeoffset, g);
		}
		else
		{
			break;
		}
	}

	if (!found)
	{
		DB(Trace(1, "GetParent", "DiskNotValidated %ld\n", childanodenr));
		*error = PFS_ERROR_DISK_NOT_VALIDATED;
		return false;
	}

	parentfi->file.direntry = de;
	parentfi->file.dirblock = dirblock;
	LOCK(dirblock);
	return true;
}


/* SearchInDir
 *
 * Search an object in a directory and return the fileinfo
 *
 * input : - dirnodenr: anodenr of directory to search in
 *         - objectname: found object (without path)
 * 
 * output: - info: objectinfo of found object
 *
 * result: success 
 */
static bool SearchInDir(ULONG dirnodenr, STRPTR objectname, union objectinfo *info, globaldata * g)
{
	struct canode anode;
	struct cdirblock *dirblock;
	struct direntry *entry = NULL;
	bool found = false, eod = false;
	ULONG anodeoffset;
	UBYTE intl_name[PATHSIZE];

	ENTER("SearchInDir");
	ctodstr((unsigned char*)objectname, intl_name);

	/* truncate */
	if (intl_name[0] > FILENAMESIZE - 1)
		intl_name[0] = FILENAMESIZE - 1;

	intltoupper(intl_name);     /* international uppercase objectname */
	GetAnode(&anode, dirnodenr, g);
	anodeoffset = 0;
	dirblock = LoadDirBlock(anode.blocknr, g);

	while (dirblock && !found && !eod)  /* eod stands for end-of-dir */
	{
		entry = (struct direntry *)(&dirblock->blk.entries);

		/* scan block */
		while (entry->next)
		{
			found = intlcmp(intl_name, DIRENTRYNAME(entry));
			if (found)
				break;
			entry = NEXTENTRY(entry);
		}

		/* load next block */
		if (!found)
		{
			if (NextBlock(&anode, &anodeoffset, g))
				dirblock = LoadDirBlock(anode.blocknr + anodeoffset, g);
			else
				eod = true;
		}
	}

	/* make fileinfo */
	if (!dirblock)
	{
		return false;
	}
	else if (found)
	{
		info->file.direntry = entry;
		info->file.dirblock = dirblock;
		LOCK(dirblock);
		return true;
	}
	else
		return false;
}


/*
 * Search object by anodenr in directory 
 * in: diranodenr, target: anodenr of target and the anodenr of the directory to search in
 * out: result: an objectinfo to the object, if found
 * returns: success
 */
bool FetchObject(ULONG diranodenr, ULONG target, union objectinfo * result, globaldata * g)
{
	struct canode anode;
	struct cdirblock *dirblock = nullptr;
	struct direntry *de = nullptr;
	ULONG anodeoffset = 0;
	bool eod = false, found = false;

	/* Get directory and find object */
	GetAnode(&anode, diranodenr, g);
	while (!found && !eod)
	{
		dirblock = LoadDirBlock(anode.blocknr + anodeoffset, g);
		if (dirblock)
		{
			de = FIRSTENTRY(dirblock);
			while (de->next)
			{
				if (!(found = (de->anode == target)))
					de = NEXTENTRY(de);
				else
					break;
			}

			if (!found)
				eod = !NextBlock(&anode, &anodeoffset, g);
		}
		else
			break;
	}

	if (!found)
		return false;

	result->file.dirblock = dirblock;
	result->file.direntry = de;
	LOCK(dirblock);
	return true;
}

/**********************************************************************/
/*                       GET DIRECTORY CONTENTS                       */
/*                       GET DIRECTORY CONTENTS                       */
/*                       GET DIRECTORY CONTENTS                       */
/**********************************************************************/


/* ExamineFile
 *
 * Specification:
 *
 * - fill in fileinfoblock
 * - prepares for ExamineNextFile
 * - result: success
 *   *error: error if failure; else undefined
 *
 * - fib_DirEntryType must be equal to fib_EntryType
 */
bool ExamineFile(listentry_t *file, struct FileInfoBlock * fib, SIPTR *error, globaldata * g)
{
	struct volumedata *volume;
#if DELDIR
	struct crootblockextension *rext;
	union objectinfo *info;
#endif

	/* volume must be or become currentvolume */
	if (!file)
		volume = g->currentvolume;
	else
		volume = file->volume;

	/* fill in fib */
	fib->fib_DiskKey = (IPTR)file;  // I use it as dir-block-number for ExNext
	if (!file || IsVolumeEntry(file))
	{
		fib->fib_DirEntryType = \
			fib->fib_EntryType = (LONG)ST_USERDIR;
		fib->fib_Protection = (g->diskstate == ID_WRITE_PROTECTED) || g->softprotect;
		fib->fib_Size = 0;
		fib->fib_NumBlocks = 0;

		if (volume->rblkextension)
		{
			fib->fib_Date.ds_Days = (ULONG)volume->rblkextension->blk.root_date[0];
			fib->fib_Date.ds_Minute = (ULONG)volume->rblkextension->blk.root_date[1];
			fib->fib_Date.ds_Tick = (ULONG)volume->rblkextension->blk.root_date[2];
		}
		else
		{
			fib->fib_Date.ds_Days = (ULONG)volume->rootblk->creationday;
			fib->fib_Date.ds_Minute = (ULONG)volume->rootblk->creationminute;
			fib->fib_Date.ds_Tick = (ULONG)volume->rootblk->creationtick;
		}

		strncpy_s(fib->fib_FileName, volume->rootblk->diskname, volume->rootblk->diskname[0] + 1);
		fib->fib_Comment[0] = 0x0;
		//fib->fib_Reserved[0]  = 0x0;
	}
	else
#if DELDIR
	{
		info = &file->info;
		if (IsDelDir(*info))
		{
			rext = volume->rblkextension;
			fib->fib_DiskKey = 0;
			fib->fib_DirEntryType = \
				fib->fib_EntryType = (LONG)ST_USERDIR;
			fib->fib_Protection = (ULONG)rext->blk.dd_protection;
			fib->fib_Size = 0;
			fib->fib_NumBlocks = 0;
			fib->fib_Date.ds_Days = (ULONG)rext->blk.dd_creationday;
			fib->fib_Date.ds_Minute = (ULONG)rext->blk.dd_creationminute;
			fib->fib_Date.ds_Tick = (ULONG)rext->blk.dd_creationtick;
			strcpy_s(fib->fib_FileName, (char*)deldirname);
			fib->fib_Comment[0] = 0x0;
			fib->fib_OwnerUID = (ULONG)rext->blk.dd_uid;
			fib->fib_OwnerGID = (ULONG)rext->blk.dd_gid;

			/* prepare ExNext() */
			((lockentry_t *)file)->nextanode = 0;
			return true;
		}
		else if (IsDelFile(*info))
		{
			FillDelfileFib(NULL, info->delfile.slotnr, fib, g);
			return true;
		}
		else
#endif
		{
			FillFib(file->info.file.direntry, fib, g);
		}
#if DELDIR
	}
#endif

	/* prepare examinenext */
	if (!file || file->type.flags.dir)
		GetFirstEntry((lockentry_t *)file, g);

	return true;
}

/* ExamineNextFile
 *
 * Specification:
 *
 * - used to obtain info on all directory entries
 * - fill in fileinfoblock
 * 
 * Quirks:
 *
 * - fib_DirEntryType must be equal to fib_EntryType
 * - it is allowed to stop invoking before end of dir
 * - for old compatibility: only DiskKey and FileName guaranteed to be the same
 *                           on subsequent calls
 * - it is possible to receive other packets between ExNext's -> special cases
 * - the lock is passed is not always the same, but it is a lock to the same object
 *
 * Implementation:
 * 
 * - fib_DiskKey contains address of lock; different->problems
 *
 * lock->nextentry contains pointer to be fetched entry. End of dir <=>
 * lock->nextentry == {NULL, NULL}
 *
 */
bool ExamineNextFile(lockentry_t *file, struct FileInfoBlock * fib,
					 SIPTR *error, globaldata * g)

{
	struct direntry *direntry;

	*error = 0;

	/* file must be lock on dir; NULL lock not allowed */
	if (!file || !file->le.type.flags.dir)
	{
		*error = PFS_ERROR_OBJECT_WRONG_TYPE;
		return false;
	}

#if DELDIR
	if (IsDelDir(file->le.info))
	{
		struct deldirentry *dde;

		dde = GetDeldirEntry((IPTR*) &fib->fib_DiskKey, g);
		if (dde)
		{
			FillDelfileFib(dde, fib->fib_DiskKey, fib, g);
			fib->fib_DiskKey++;
			return true;
		}
		else
		{
			*error = PFS_ERROR_NO_MORE_ENTRIES;
			return false;
		}
	}
#endif

	/* check if same lock used */
	if ((IPTR)file != (IPTR)fib->fib_DiskKey)
	{
		DB(Trace(1, "ExamineNextFile", "Other lock!!\n"));
		RemakeNextEntry(file, fib, g);
	}

	/* Check if end of dir */
	if (file->nextentry.dirblock == NULL)
	{
		*error = PFS_ERROR_NO_MORE_ENTRIES;
		return false;
	}

	/* Fill infoblock */
	direntry = file->nextentry.direntry;
	if (direntry->next)
	{
		fib->fib_DiskKey = (IPTR)file;  // I use it as dir-block-number
		FillFib(direntry, fib, g);
	}
	else
	{
		*error = PFS_ERROR_NO_MORE_ENTRIES;
		ErrorMsg(AFS_ERROR_EX_NEXT_FAIL, NULL, g);
		return false;
	}

	/* get next entry */
	GetNextEntry(file, g);
	return true;
}


/* Fill fileinfoblock of a file
 */
void FillFib(struct direntry *direntry, struct FileInfoBlock *fib, globaldata * g)
{
	struct extrafields extrafields;
	UBYTE *comment;
	FSIZE size;
	
	size = GetDEFileSize(direntry, g);
	/* fib->fib_DiskKey done by Examine(Next) */
	fib->fib_DirEntryType = (LONG)direntry->type;
	fib->fib_EntryType = direntry->type;
	fib->fib_Protection = (ULONG)direntry->protection;
	fib->fib_Size = GetDEFileSize32(direntry, g);
	fib->fib_NumBlocks = (LONG)((size / BLOCKSIZE) + (size % BLOCKSIZE > 0));
	fib->fib_Date.ds_Days = direntry->creationday;
	fib->fib_Date.ds_Minute = direntry->creationminute;
	fib->fib_Date.ds_Tick = direntry->creationtick;
	strncpy_s(fib->fib_FileName, (char*)&direntry->nlength, direntry->nlength + 1);
	fib->fib_FileName[direntry->nlength + 1] = 0;

	comment = (UBYTE *)&direntry->startofname + direntry->nlength;
	strncpy_s(fib->fib_Comment, (char*)comment, min(*comment + 1, CMSIZE));
	fib->fib_Comment[*comment + 1] = 0;

	if (g->dirextension)
	{
		GetExtraFields(direntry, &extrafields);
		fib->fib_Protection |= extrafields.prot;
		fib->fib_OwnerUID = extrafields.uid;
		fib->fib_OwnerGID = extrafields.gid;

#if ROLLOVER
		if (direntry->type == ST_ROLLOVERFILE)
			fib->fib_Size = extrafields.virtualsize;
#endif
	}
}

/* Fill 'nextentry' of listentry [le]. If directory contains no entries,
 * nextentry will be set to NULL
 * > not for deldirs <
 */
static void GetFirstEntry(lockentry_t *le, globaldata * g)
{
	ULONG anodenr;

	if (!le)
		anodenr = ANODE_ROOTDIR;
	else
		anodenr = le->le.anodenr;

	le->nextanode = GetFirstNonEmptyDE(anodenr, &le->nextentry, g);
}

/* Get first non empty direntry starting from anode [anodenr] 
 * Returns {NULL, NULL} if end of dir
 */
static ULONG GetFirstNonEmptyDE(ULONG anodenr, struct fileinfo *info, globaldata *g)
{
	struct canode anode;
	ULONG nextsave;
	bool found = false;

	anode.next = anodenr;
	anode.nr = 0;
	while (!found)
	{
		nextsave = anode.next;
		if (nextsave)
		{
			GetAnode(&anode, anode.next, g);
			info->dirblock = LoadDirBlock(anode.blocknr, g);
			if (info->dirblock)
				info->direntry = FIRSTENTRY(info->dirblock);
		}

		if (!nextsave || !info->dirblock)
		{
			info->direntry = NULL;
			info->dirblock = NULL;
			found = true;
		}
		else if (info->direntry->next != 0)
		{
			LOCK(info->dirblock);
			found = true;
		}
	}

	return anode.nr;
}


static void RemakeNextEntry(lockentry_t *file, struct FileInfoBlock *fib, globaldata *g)
{
	ULONG anodenr;
	UBYTE filename[FNSIZE];

	anodenr = file->le.anodenr;
	if (ddstricmp((DSTR)fib->fib_FileName, (DSTR)file->le.volume->rootblk->diskname))
		GetFirstEntry(file, g);
	else
	{
		BCPLtoCString((STRPTR)filename, (DSTR)fib->fib_FileName);
		if (!SearchInDir(anodenr, (STRPTR)filename, (union objectinfo *)&file->nextentry, g))
		{
			file->nextentry.dirblock = NULL;
			file->nextentry.direntry = NULL;
			file->nextanode = anodenr;
		}
		else
			GetNextEntry(file, g);
	}
}

void GetNextEntry(lockentry_t *file, globaldata * g)
{
	struct canode anode;

	/* get nextentry */
	file->nextentry.direntry = NEXTENTRY(file->nextentry.direntry);

	/* no next entry? -> next block */
	if (!file->nextentry.direntry->next)
	{
		/* NB: 'nextanode' is een verwarrende naam */
		GetAnode(&anode, file->nextanode, g);
		file->nextanode = GetFirstNonEmptyDE(anode.next, &file->nextentry, g);
	}
}

/* ExamineAll
 *
 * fill buffer with directory information
 *
 * Note: +- 2000 stack needed
 */
static ULONG FillInData(struct ExAllData *buffer, LONG type,
						struct direntry *direntry, ULONG spaceleft, globaldata *g)
{
	UWORD nameoffset, commentoffset = 0;
	UBYTE *name, *comment = NULL;
	UCOUNT size;
	struct extrafields extrafields;

	/* get location to put name */
	switch (type)
	{
		case ED_NAME:
			size = offsetof(struct ExAllData, ed_Type);
			break;

		case ED_TYPE:
			size = offsetof(struct ExAllData, ed_Size);
			break;

		case ED_SIZE:
			size = offsetof(struct ExAllData, ed_Prot);
			break;

		case ED_PROTECTION:
			size = offsetof(struct ExAllData, ed_Days);
			break;

		case ED_DATE:
			size = offsetof(struct ExAllData, ed_Comment);
			break;

		case ED_COMMENT:
			size = offsetof(struct ExAllData, ed_OwnerUID);
			break;

		case ED_OWNER:
#if defined(__MORPHOS__)
			size = offsetof(struct ExAllData, ed_Size64);
			break;

		case ED_SIZE64:
#endif
			size = sizeof(struct ExAllData);
			break;

		default:
			size = offsetof(struct ExAllData, ed_Type);
			break;
	}

	/* size of name */
	nameoffset = size;
	name = &direntry->nlength;
	size += *name + 1;

	/* size of comment */
	if (type >= ED_COMMENT)
	{
		commentoffset = size;
		comment = COMMENT(direntry);
		size += *comment + 1;
	}

	/* check fit */
	size = (size + 1) & 0xfffe;
	if (size > spaceleft)
		return (0);

	/* get extra fields */
	GetExtraFields(direntry, &extrafields);

	/* copy */
	buffer->ed_Next = NULL;
	switch (type)
	{
#if defined(__MORPHOS__)
		case ED_SIZE64:
#if ROLLOVER
			if (direntry->type == ST_ROLLOVERFILE)
				buffer->ed_Size64 = extrafields.virtualsize;
			else
#endif
				buffer->ed_Size64 = GetDEFileSize(direntry, g);
#endif
		case ED_OWNER:
			buffer->ed_OwnerUID = extrafields.uid;
			buffer->ed_OwnerGID = extrafields.gid;

		case ED_COMMENT:
			buffer->ed_Comment = (UBYTE *)buffer + commentoffset;
			strncpy((char *)buffer + commentoffset, (char*)(comment + 1), *comment);
			*((UBYTE *)buffer + commentoffset + *comment) = 0x0;

		case ED_DATE:
			buffer->ed_Days = direntry->creationday;
			buffer->ed_Mins = direntry->creationminute;
			buffer->ed_Ticks = direntry->creationtick;

		case ED_PROTECTION:
			buffer->ed_Prot = (ULONG)direntry->protection | extrafields.prot;

		case ED_SIZE:
#if ROLLOVER
			if (direntry->type == ST_ROLLOVERFILE)
				buffer->ed_Size = extrafields.virtualsize;
			else
#endif
				buffer->ed_Size = GetDEFileSize32(direntry, g);

		case ED_TYPE:
			buffer->ed_Type = direntry->type;

		case ED_NAME:

			strncpy((char *)buffer + nameoffset,
					(char*)&direntry->startofname, *name);
			*((UBYTE *)buffer + nameoffset + *name) = 0x0;

		default:
			buffer->ed_Name = (UBYTE *)buffer + nameoffset;
	}

	return size;
}

bool ExamineAll(lockentry_t *object, UBYTE *buffer, ULONG buflen,
		  LONG type, struct ExAllControl * ctrl, SIPTR *error, globaldata * g)
{
	struct direntry *direntry;
	struct ExAllData *lasteaentry = NULL;
	ULONG spaceleft, size;
	UWORD j;
	bool eod;
#if DELDIR
	struct deldirentry *dde;
#endif

	ENTER("ExamineAll");

#ifdef KSWRAPPER
	if (!g->v37DOS)
		return false;
#endif

	/* init and check */
	if (!object || !object->le.type.flags.dir)
	{
		*error = PFS_ERROR_OBJECT_WRONG_TYPE;
		return false;
	}

	/* check type field */
#if defined(__MORPHOS__)
	if (type > ED_SIZE64 || type < ED_NAME)
#else
	if (type > ED_OWNER || type < ED_NAME)
#endif
	{
		*error = PFS_ERROR_BAD_NUMBER;
		return false;
	}

	/* Prepare loop */
	spaceleft = buflen;
	j = 0;                      // # direntries in buffer

#if DELDIR
	if (IsDelDir(object->le.info))
	{
		/* Main loop */
		while ((dde = GetDeldirEntry((IPTR*) &ctrl->eac_LastKey, g)))
		{
			bool wanted;

			size = FillInDDEData((struct ExAllData *)buffer, type, dde, ctrl->eac_LastKey,
									spaceleft, g);
			if (size)
			{
				wanted = true;

				/* if so update statistics */
				if (wanted)
				{
					if (lasteaentry)
						lasteaentry->ed_Next = (struct ExAllData *)buffer;

					lasteaentry = (struct ExAllData *)buffer;
					buffer += size;
					spaceleft -= size;
					j++;
				}

				ctrl->eac_LastKey++;
			}
			else
				break;          // doesn't fit

		}

		/* Finish up */
		ctrl->eac_Entries = j;
		if (!dde)
		{
			*error = PFS_ERROR_NO_MORE_ENTRIES;
			return false;
		}
		else
			return true;
	}
#endif

	/* check if it's the first call and if same lock is used */
	if (ctrl->eac_LastKey == 0)
		GetFirstEntry(object, g);
	else if ((IPTR)object != (IPTR)ctrl->eac_LastKey)
	{
		*error = PFS_ERROR_NO_MORE_ENTRIES;
		return false;
	}

	DB(Trace(1, "ExamineAll", "matchfunc = %lx matchstring = %s\n", ctrl->eac_MatchFunc, ctrl->eac_MatchString));

	/* Main loop */
	while (1)
	{
		bool wanted;

		if (!(eod = (object->nextentry.dirblock == NULL)))
			direntry = object->nextentry.direntry;
		else
			break;

		size = FillInData((struct ExAllData *)buffer, type, direntry,
							spaceleft, g);
		if (size)
		{

			wanted = true;

			/* if so update statistics */
			if (wanted)
			{
				if (lasteaentry)
					lasteaentry->ed_Next = (struct ExAllData *)buffer;

				lasteaentry = (struct ExAllData *)buffer;
				buffer += size;
				spaceleft -= size;
				j++;
			}

			GetNextEntry(object, g);
		}
		else
			break;              // doesn't fit

	}

	/* Finish up */
	ctrl->eac_LastKey = (IPTR)object;
	ctrl->eac_Entries = j;

	if (eod)
	{
		*error = PFS_ERROR_NO_MORE_ENTRIES;
		return false;
	}
	else
		return true;
}

/**********************************************************************/
/*                        ADD AND REMOVE FILES                        */
/*                        ADD AND REMOVE FILES                        */
/*                        ADD AND REMOVE FILES                        */
/**********************************************************************/

/* <NewFile>
 *
 * NewFile creates a new file in a [directory] on currentvolume
 *
 * input : - [directory]: directory of file;
 *         - [filename]: name (without path) of file
 *         - found: flag, file already present?. If so newfile == old
 *
 * output: - [newfile]: fileinfo of new file (struct is managed by caller)
 *		   - [directory]: fileinfo of parent (can have changed if hardlink) 
 *
 * result: errornr; 0 = success
 *
 * maxneeds: 1 nd + 2 na = 3 res
 *
 * Note: 'directory' and 'newfile' may point to the same.
 */
ULONG NewFile (bool found, union objectinfo *directory, STRPTR filename, union objectinfo *newfile, globaldata *g)
{
	union objectinfo info;
	ULONG anodenr;
	SIPTR error;
	UBYTE entrybuffer[MAX_ENTRYSIZE];
	struct extrafields extrafields;
	struct direntry *destentry;
	struct canode anode;
	size_t l;
#if VERSION23
	struct anodechain *achain;
#endif

	DB(Trace(10, "NewFile", "%s\n", filename));
	/* check disk-writeprotection etc */
	if (!CheckVolume(g->currentvolume, 1, &error, g))
		return (ULONG)error;

#if DELDIR
	if (IsDelDir(*directory))
		return PFS_ERROR_WRITE_PROTECTED;
#endif

	/* check reserved area lock */
	if (ReservedAreaIsLocked)
		return PFS_ERROR_DISK_FULL;

	/* truncate filename to 31 characters */
	if (!(l = strlen(filename)))
		return PFS_ERROR_INVALID_COMPONENT_NAME;
	if (l > FILENAMESIZE - 1)
		filename[FILENAMESIZE - 1] = 0x0;

	if (found)
	{
		/*
		 * new version: take over direntry
		 * (used to simply delete old and make new)
		 */
		info.file = newfile->file;
		anodenr = FIANODENR(&info.file);

		/* Check deleteprotection */
#ifndef DISABLE_PROTECT
		if (IsVolume(info) || info.file.direntry->protection & FIBF_DELETE)
			return PFS_ERROR_DELETE_PROTECTED;
#endif
		/* If link, get real object. After this it has become a
		 * ST_FILE
		 */
		if ((info.file.direntry->type == ST_LINKFILE) ||
			(info.file.direntry->type == ST_LINKDIR))
		{
			struct canode linknode;

			GetExtraFields(info.file.direntry, &extrafields);
			anodenr = extrafields.link;
			GetAnode(&linknode, info.file.direntry->anode, g);
			if (!FetchObject(linknode.clustersize, anodenr, &info, g))
				return PFS_ERROR_OBJECT_NOT_FOUND;
			
			/* have to check protection again */
#ifndef DISABLE_PROTECT
			if (info.file.direntry->protection & FIBF_DELETE)
				return PFS_ERROR_DELETE_PROTECTED;
#endif			
			/* get parent */
			if (!GetParent(&info, directory, &error, g))
				return PFS_ERROR_OBJECT_NOT_FOUND;
		}

		if (!(achain = GetAnodeChain(anodenr, g)))
			return PFS_ERROR_NO_FREE_STORE;

		/* Free used space */
		if (g->deldirenabled && info.file.direntry->type == ST_FILE)
		{
			int ddslot;

			/* free a slot to put old version in, inter. update possible */
			ddslot = AllocDeldirSlot(g);

			/* make replacement anode, because we want to reuse the old one */
			info.file.direntry->anode = achain->head.an.nr = AllocAnode(0, g);
			SaveAnode(&achain->head.an, achain->head.an.nr, g);
			AddToDeldir(&info, ddslot, g);
			info.file.direntry->anode = anodenr;
		}

		/* Rollover files are essentially just 'reset' by overwriting:
		 * only the virtualsize and offset are set to zero (extrafields)
		 * Other files are deleted and recreated as a new file.
		 */
		if (info.file.direntry->type != ST_ROLLOVERFILE)
		{
			/* Change directory entry */
			SetDEFileSize(info.file.direntry, 0, g);
			info.file.direntry->type = ST_FILE;
			MakeBlockDirty((struct cachedblock *)info.file.dirblock, g);

			/* Reclaim anode */
			anode.clustersize = 0;
			anode.blocknr = 0xffffffff;
			anode.next = 0;
			SaveAnode(&anode, anodenr, g);

			/* Delete old file (update possible) */
			if (g->deldirenabled && info.file.direntry->type == ST_FILE)
				FreeBlocksAC(achain, ULONG_MAX, keepanodes, g);
			else
				FreeBlocksAC(achain, ULONG_MAX, freeanodes, g);
			DetachAnodeChain(achain, g);
		}

		/* Clear direntry extrafields */
		destentry = (struct direntry *)entrybuffer;
		memcpy(destentry, info.file.direntry, info.file.direntry->next);
		GetExtraFields(info.file.direntry, &extrafields);
		extrafields.virtualsize = extrafields.rollpointer = 0;
		AddExtraFields(destentry, &extrafields);
		ChangeDirEntry(info.file, destentry, directory, &info.file, g);
		newfile->file = info.file;
		return 0;
	}

	/* direntry alloceren en invullen */
	if (MakeDirEntry(ST_FILE, (UBYTE*)filename, entrybuffer, g))
	{
		if (AddDirectoryEntry(directory, (struct direntry *)entrybuffer, &newfile->file, g))
			return 0;
		else
			FreeAnode(((struct direntry *)entrybuffer)->anode, g);
	}

	return PFS_ERROR_DISK_FULL;
}


/* NewDir
 *
 * Specification:
 * 
 * - make new dir 
 * - returns fileentry (!) with exclusive lock
 *
 * Implementation:
 *
 * - check if file/dir exists
 * - make direntry
 * - make first dirblock
 *
 * Similar to NewFile()
 *
 * maxneeds: 2 nd, 3 na = 2 nablk : 4 res
 */
lockentry_t *NewDir(union objectinfo *parent, STRPTR dirname, SIPTR *error, globaldata * g)
{
	union objectinfo info;
	listentry_t *fileentry;
	listtype type;
	struct cdirblock *blk;
	ULONG parentnr, blocknr;
	UBYTE entrybuffer[MAX_ENTRYSIZE];
	size_t l;

	DB(Trace(1, "NewDir", "%s\n", dirname));

	/* check disk-writeprotection etc */
	if (!CheckVolume(g->currentvolume, 1, error, g))
		return NULL;

#if DELDIR
	if (IsDelDir(*parent))
	{
		*error = PFS_ERROR_WRITE_PROTECTED;
		return NULL;
	}
#endif

	/* check reserved area lock */
	if (ReservedAreaIsLocked)
	{
		*error = PFS_ERROR_DISK_FULL;
		return NULL;
	}

	/* checkvolume */
	if (IsVolume(*parent))
		parentnr = ANODE_ROOTDIR;
	else
		parentnr = parent->file.direntry->anode;

	/* truncate dirname to 31 characters */
	if (!(l = strlen(dirname)))
	{
		*error = PFS_ERROR_INVALID_COMPONENT_NAME;
		return NULL;
	}
	if (l > FILENAMESIZE - 1)
		dirname[FILENAMESIZE - 1] = 0x0;

	/* check if object exists */
	if (SearchInDir(parentnr, dirname, &info, g))
	{
		*error = PFS_ERROR_OBJECT_EXISTS;
		return NULL;
	}

	/* allocate directory entry, fill it. Make fileentry */
	if (!MakeDirEntry(ST_USERDIR, (UBYTE*)dirname, entrybuffer, g))
		goto error1;

	if (!AddDirectoryEntry(parent, (struct direntry *)entrybuffer, &info.file, g))
	{
		FreeAnode(((struct direntry *)entrybuffer)->anode, g);
  error1: 
		*error = PFS_ERROR_DISK_FULL;
		return NULL;
	}

	type.value = ET_LOCK | ET_EXCLREAD;
	if (!(fileentry = MakeListEntry(&info, type, error, g)))
		goto error2;
	if (!AddListEntry(fileentry))   /* Should never fail, accessconflict impossible */
	{
		ErrorMsg(AFS_ERROR_NEWDIR_ADDLISTENTRY, NULL, g);
		goto error2;
	}

	/* Make first directoryblock (needed for parentfinding) */
	if (!(blocknr = AllocReservedBlock(g)))
	{
		*error = PFS_ERROR_DISK_FULL;
  error2:
		FreeAnode(info.file.direntry->anode, g);
		RemoveDirEntry(info.file, g);
		if (fileentry)
			FreeListEntry(fileentry, g);
		DB(Trace(1, "Newdir", "disk full"));
		return NULL;
	}

	blk = MakeDirBlock(blocknr, info.file.direntry->anode,
					   info.file.direntry->anode, parentnr, g);

	return (lockentry_t *)fileentry;
}

struct cdirblock *MakeDirBlock(ULONG blocknr, ULONG anodenr, ULONG rootanodenr,
							   ULONG parentnr, globaldata * g)
{
	struct canode anode;
	struct cdirblock *blk;
	struct volumedata *volume = g->currentvolume;

	DB(Trace(10,"MakeDirBlock","blocknr = %lx\n", blocknr));

	/* fill in anode (allocated by MakeDirEntry) */
	anode.clustersize = 1;
	anode.blocknr = blocknr;
	anode.next = 0;
	SaveAnode(&anode, anodenr, g);

	blk = (struct cdirblock *)AllocLRU(g);
	blk->blk.id = DBLKID;
	blk->blk.anodenr = rootanodenr;
	blk->blk.parent = parentnr;
	blk->volume = volume;
	blk->blocknr = blocknr;
	blk->oldblocknr = 0;
	blk->changeflag = true;

	Hash(blk, volume->dirblks, HASHM_DIR);
	LOCK(blk);
	return blk;
}


/* DeleteObject
 *
 * Specification:
 *
 * - The object referenced by the info structure is removed
 * - The object must be on currentvolume
 *
 * Implementation:
 *
 * - check deleteprotection
 * - if dir, check if directory is empty
 * - check if there are outstanding locks on object 
 * - remove object from directory and free anode
 * - rearrange & store directory
 *
 * Don't check dirtycount!
 * info becomes INVALID!
 */
bool DeleteObject(union objectinfo * info, SIPTR *error, globaldata * g)
{
	ULONG anodenr;

	ENTER("DeleteObject");
	/* Check deleteprotection */
#if DELDIR
	if (!info || info->deldir.special <= SPECIAL_DELFILE 
#ifndef DISABLE_PROTECT
		|| info->file.direntry->protection & FIBF_DELETE
#endif
		)
#else
	if (!info || IsVolume(*info) || info->file.direntry->protection & FIBF_DELETE)
#endif
	{
#ifndef DISABLE_PROTECT
		*error = PFS_ERROR_DELETE_PROTECTED;
		return false;
#endif
	}

	/* Check if link, links can always be removed */
	if ((info->file.direntry->type == ST_LINKFILE) ||
		(info->file.direntry->type == ST_LINKDIR))
	{
		return DeleteLink(&info->file, error, g);
	}

	anodenr = FIANODENR(&info->file);

	/* Check if object has links,
	 * if it does the object should not be deleted,
	 * just the direntry
	 */
	if (!RemapLinks(&info->file, g))
	{
		/* Remove object from directory and free anode */
		if (info->file.direntry->type == ST_USERDIR)
			return DeleteDir(info, error, g);
		else
		{
			/* ST_FILE or ST_SOFTLINK */
			struct anodechain *achain;
			int do_deldir = g->deldirenabled && info->file.direntry->type == ST_FILE;
			ULONG ddslot;

			if (!(achain = GetAnodeChain(anodenr, g)))
			{
				*error = PFS_ERROR_NO_FREE_STORE;
				return false;
			}
			if (do_deldir)
			{
				ddslot = AllocDeldirSlot(g);
				AddToDeldir(info, ddslot, g);
			}

			ChangeDirEntry(info->file, NULL, NULL, NULL, g);    /* remove direntry */
			if (do_deldir)
				FreeBlocksAC(achain, ULONG_MAX, keepanodes, g);
			else
			{
				FreeBlocksAC(achain, ULONG_MAX, freeanodes, g);
				FreeAnode(achain->head.an.nr, g);
			}
			DetachAnodeChain(achain, g);
		}
	}

	return true;
}

/* 
 * Delete directory
 */
static bool DeleteDir(union objectinfo *info, SIPTR *error, globaldata * g)
{
	struct canode anode, chnode;
	struct volumedata *volume = g->currentvolume;
	struct cdirblock *dirblk;
	UWORD t;

	anode.nr = FIANODENR(&info->file);
	if (!DirIsEmpty(anode.nr, g))
	{
		*error = PFS_ERROR_DIRECTORY_NOT_EMPTY;
		return false;
	}
	else
	{
		/* check if tobefreedcache is sufficiently large,
		 * otherwise update disk
		 */
		chnode.next = anode.nr;
		for (t=1; chnode.next; t++)
			GetAnode(&chnode, chnode.next, g);

		if (2*t + alloc_data.rtbf_index > RTBF_THRESHOLD)
			UpdateDisk (g);

		/* do it (btw: fails if dirblock contains more than 128 empty
		 * blocks)
		 */
		anode.next = anode.nr;
		while (anode.next)
		{
			GetAnode(&anode, anode.next, g);

			/* remove dirblock from list if there */
			dirblk = (struct cdirblock *)CheckCache(volume->dirblks, HASHM_DIR, anode.blocknr, g);
			if (dirblk)
			{
				MinRemove(dirblk);
				if (dirblk->changeflag)
					ResToBeFreed(dirblk->oldblocknr, g);

				FreeLRU((struct cachedblock *)dirblk);
			}
			FreeAnode(anode.nr, g);
			ResToBeFreed(anode.blocknr , g);
		}
		ChangeDirEntry(info->file, NULL, NULL, NULL, g);    // delete entry from parentdir
		return true;
	}
}

/*
 * AFS Temporary extensions: kill empty, remove dir entry
 */
bool KillEmpty(union objectinfo * parent, globaldata * g)
{
	ULONG dirnodenr;
	SIPTR error;
	union objectinfo filefi;

	if (IsVolume(*parent))
		dirnodenr = ANODE_ROOTDIR;
	else
		dirnodenr = parent->file.direntry->anode;

	const char* p = "";
	if (SearchInDir(dirnodenr, (STRPTR)p, &filefi, g) && IsDir(filefi))
		return DeleteDir(&filefi, &error, g);
	else
		return false;
}

/*
 * Remove a directory entry without freeing anything and without
 * checking validity
 */
LONG forced_RemoveDirEntry(union objectinfo *info, SIPTR *error, globaldata * g)
{
	if (!info || IsVolume(*info))
	{
		*error = PFS_ERROR_DELETE_PROTECTED;
		return false;
	}

	ChangeDirEntry(info->file, NULL, NULL, NULL, g);    /* rearrange & store done by this function */
	return true;
}

/* Check if directory with anodenr [anodenr] is empty 
 * There can be multiple empty directory blocks
 */
static bool DirIsEmpty(ULONG anodenr, globaldata * g)
{
	struct canode anode;
	struct cdirblock *dirblok;

	GetAnode(&anode, anodenr, g);
	dirblok = LoadDirBlock(anode.blocknr, g);

	while (dirblok && (dirblok->blk.entries[0] == 0) && anode.next)
	{
		GetAnode(&anode, anode.next, g);
		dirblok = LoadDirBlock(anode.blocknr, g);
	}

	if (dirblok && ((dirblok->blk.entries[0]) == 0))    /* not empty->entries present */
		return true;
	else
		return false;
}

#if DELDIR
/*
 * Frees anodes without freeing blocks
 */
void FreeAnodesInChain(ULONG anodenr, globaldata * g)
{
	struct canode anode;
	struct crootblockextension *rext = g->currentvolume->rblkextension;

	DB(Trace(1, "FreeAnodeInChain", "anodenr: %ld \n", anodenr));
	GetAnode(&anode, anodenr, g);
	while (anode.nr)            /* stops autom.: anode.nr of anode 0 == 0 */
	{
		if (IsUpdateNeeded(RTBF_THRESHOLD))
		{
			if (rext)
			{
				rext->blk.tobedone.operation_id = PP_FREEANODECHAIN;
				rext->blk.tobedone.argument1 = anode.nr;
				rext->blk.tobedone.argument2 = 0;
				rext->blk.tobedone.argument3 = 0;
			}

			UpdateDisk(g);
		}

		FreeAnode(anode.nr, g);
		GetAnode(&anode, anode.next, g);
	}

	if (rext)
	{
		rext->blk.tobedone.operation_id = 0;
		rext->blk.tobedone.argument1 = 0;
		rext->blk.tobedone.argument2 = 0;
		rext->blk.tobedone.argument3 = 0;
	}
}

#endif /* DELDIR */

/**********************************************************************/
/*                        DIRECTORYOPERATIONS                         */
/*                        DIRECTORYOPERATIONS                         */
/*                        DIRECTORYOPERATIONS                         */
/**********************************************************************/

/* RenameAndMove
 *
 * Specification:
 *
 * - rename object
 * - renaming directories into a child not allowed!
 *
 * Rename across devices tested in dd_Rename (DosToHandlerInterface)
 *
 * Implementation:
 * 
 * - source ophalen
 * - check if new name allowed
 * - destination maken
 * - remove source direntry
 * - add destination direntry
 *
 * maxneeds: 2 dblk changed, 1 new an : 3 res
 *
 * sourcedi = objectinfo of source directory
 * destdi = objectinfo of destination directory
 * srcinfo = objectinfo of source
 * destinfo = objectinfo of destination
 * src- destanodenr = anodenr of source- destination directory
 */
bool RenameAndMove (union objectinfo *sourcedi, union objectinfo *srcinfo,
					union objectinfo *destdir, STRPTR destpath, SIPTR *error,
					globaldata * g)
{
	struct direntry *srcdirentry, *destentry;
	UBYTE entrybuffer[MAX_ENTRYSIZE];
	ULONG srcanodenr, destanodenr;
	WORD srcfieldoffset, destfieldoffset, fieldsize;
	union objectinfo destinfo, destdi;
	UBYTE *srccomment, *destcomment;
	STRPTR destname;

	/* fetch source info & path and check if exists */
	if (!(destname = (STRPTR)GetFullPath (destdir, destpath, &destdi, error, g)))
	{
		*error = PFS_ERROR_OBJECT_NOT_FOUND;
		return false;
	}

	/* source nor destination may be a volume */
	if (IsVolume(*srcinfo) || !*destname)
	{
		*error = PFS_ERROR_OBJECT_WRONG_TYPE;
		return false;
	}

#if DELDIR
	if (IsDelDir(*sourcedi) || IsDelDir(destdi) || IsDelDir(*srcinfo))
	{
		*error = PFS_ERROR_WRITE_PROTECTED;
		return false;
	}
#endif

	/* check reserved area lock */
	if (ReservedAreaIsLocked)
	{
		*error = PFS_ERROR_DISK_FULL;
		return false;
	}

	srcdirentry = srcinfo->file.direntry;
	srccomment = COMMENT(srcdirentry);

	/* check if new name allowed
	 * destpath should exist and file should not
	 * %9.1 the same name IS allowed (rename 'hello' to 'Hello')
	 */
	if (FindObject(&destdi, destname, &destinfo, error, g))
	{
		if (destinfo.file.direntry != srcinfo->file.direntry)
		{
			*error = PFS_ERROR_OBJECT_EXISTS;
			return false;
		}
	}
	
	/* Test if a directory is being renamed to a child of itself. This is so
	 * if:
	 * 1) source (srcinfo) is a directory and
	 * 2) sourcepath (sourcedi) <> destinationpath (destdi) and
	 * 3) source (srcinfo) is part of destpath (destdi)
	 * Example: rename a/b to a/b/c/d:
	 * 1) a/b is dir [ok]; 2) a <> a/b/c [ok]; 3) a/b is part of a/b/c [ok]
	 * Links need special attention! 
	 */
	srcanodenr = IsRootA(*sourcedi) ? ANODE_ROOTDIR : FIANODENR(&sourcedi->file);
	destanodenr = IsRootA(destdi) ? ANODE_ROOTDIR : FIANODENR(&destdi.file);
	if (IsRealDir(*srcinfo) && (srcanodenr != destanodenr) &&
		IsChildOf(destdi, *srcinfo, g))
	{
		*error = PFS_ERROR_OBJECT_IN_USE;
		return false;
	}

	/* Make destination  */
	destentry = (struct direntry *)&entrybuffer;

	/* copy header */
	memcpy(destentry, srcdirentry, offsetof(struct direntry, nlength));

	/* copy name */
	destentry->nlength = (UBYTE)strlen(destname);
	if (destentry->nlength > FILENAMESIZE - 1)
		destentry->nlength = FILENAMESIZE - 1;
	memcpy((UBYTE *)&destentry->startofname, destname, destentry->nlength);

	/* copy comment */
	destcomment = (UBYTE *)&destentry->startofname + destentry->nlength;
	memcpy(destcomment, srccomment, *srccomment + 1);

	/* copy fields */
	srcfieldoffset = (sizeof(struct direntry) + srcdirentry->nlength + *srccomment) & 0xfffe;
	destfieldoffset = (sizeof(struct direntry) + strlen(destname) + *srccomment) & 0xfffe;
	fieldsize = srcdirentry->next - srcfieldoffset;
	if (g->dirextension)
		memcpy((UBYTE *)destentry + destfieldoffset, (UBYTE *)srcdirentry + srcfieldoffset, fieldsize);

	/* set size */
	if (g->dirextension)
		destentry->next = (UBYTE)(destfieldoffset + fieldsize);
	else
		destentry->next = (UBYTE)destfieldoffset;

	/* remove source and add new direntry 
	 * Makes srcinfo INVALID
	 */
	ChangeDirEntry(srcinfo->file, destentry, &destdi, &destinfo.file, g);   // output:destinfo

	/* Update linklist and notify source if object moved across dirs
	 */
	if (destanodenr != srcanodenr)
	{
		MoveLink (destentry, destanodenr, g);
	}
		
	/* If object is a directory and parent changed, update dirblocks */
	if (IsDir(destinfo) && (srcanodenr != destanodenr))
	{
		struct canode anode;
		ULONG anodeoffset;
		bool gadoor = true;

		anode.nr = destinfo.file.direntry->anode;
		anodeoffset = 0;
		GetAnode (&anode, anode.nr, g);
		for (anodeoffset = 0; gadoor; gadoor = NextBlock (&anode, &anodeoffset, g))
		{
			struct cdirblock *blk;

			blk = LoadDirBlock (anode.blocknr + anodeoffset, g);
			if (blk)
			{
				blk->blk.parent = destanodenr;  // destination dir
				MakeBlockDirty ((struct cachedblock *)blk, g);
			}
		}
	}

	return true;
}


/* AddComment
 *
 * - get old direntry
 * - make new direntry
 * - remove old direntry
 * - add new direntry
 *
 * maxdirty: 1d, 1a = 2 res
 */
bool AddComment(union objectinfo * info, STRPTR comment, SIPTR *error, globaldata * g)
{
	struct direntry *sourceentry, *destentry;
	union objectinfo directory;
	UBYTE *destcomment, *srccomment, entrybuffer[MAX_ENTRYSIZE];
	WORD srcfieldoffset, destfieldoffset, fieldsize;

	DB(Trace(1, "AddComment", "%s\n", comment));
#if DELDIR
	if (info->deldir.special <= SPECIAL_DELFILE)
	{
		*error = PFS_ERROR_WRITE_PROTECTED;
		return false;
	}
#endif

	if (strlen(comment) > CMSIZE)
	{
		*error = PFS_ERROR_COMMENT_TOO_BIG;
		return false;
	}

	/* check reserved area lock */
	if (ReservedAreaIsLocked)
	{
		*error = PFS_ERROR_DISK_FULL;
		return false;
	}

	/* make new direntry */
	destentry = (struct direntry *)entrybuffer;
	sourceentry = info->file.direntry;

	/* copy header & name */
	memcpy(destentry, sourceentry, sizeof(struct direntry) + sourceentry->nlength - 1);

	/* copy comment */
	destcomment = COMMENT(destentry);
	*destcomment = (UBYTE)strlen(comment);
	memcpy(destcomment + 1, comment, *destcomment);

	/* copy fields */
	srccomment = COMMENT(sourceentry);
	srcfieldoffset = (sizeof(struct direntry) + sourceentry->nlength + *srccomment) & 0xfffe;
	destfieldoffset = (sizeof(struct direntry) + sourceentry->nlength + *destcomment) & 0xfffe;
	fieldsize = sourceentry->next - srcfieldoffset;
	if (g->dirextension)
		memcpy((UBYTE *)destentry + destfieldoffset, (UBYTE *)sourceentry + srcfieldoffset, fieldsize);

	/* set size */
	if (g->dirextension)
		destentry->next = (UBYTE)(destfieldoffset + fieldsize);
	else
		destentry->next = (UBYTE)destfieldoffset;

	/* remove old directoryentry and add new */
	if (!GetParent(info, &directory, error, g))
		return false;
	else
	{
		ChangeDirEntry(info->file, destentry, &directory, &info->file, g);
		return true;
	}
}

/* ProtectFile, SetDate
 *
 * - simple direntry in cache change, no change in size 
 * - CACHEDDIRENTRY must have changeflag
 *
 * maxneeds: changes 1 block. NEVER allocates new block
 */
bool ProtectFile(struct fileinfo * file, ULONG protection, SIPTR *error, globaldata * g)
{
	ENTER("ProtectFile");

	// isvolume check already done in dostohandler..
	//
	//  if (!file || !file->direntry)   /* @XLV */
	//  {
	//      *error = PFS_ERROR_OBJECT_WRONG_TYPE;
	//      return false;
	//  }

#if DELDIR
	if ((*(union objectinfo *)file).delfile.special <= SPECIAL_DELFILE)
	{
		if ((*(union objectinfo *)file).delfile.special == SPECIAL_DELDIR)
		{
			protection &= DELENTRY_PROT_AND_MASK;
			protection |= DELENTRY_PROT_OR_MASK;
			g->currentvolume->rblkextension->blk.dd_protection = protection;
			MakeBlockDirty((struct cachedblock *)g->currentvolume->rblkextension, g);
			return true;
		}

		*error = PFS_ERROR_WRITE_PROTECTED;
		return false;
	}
#endif

	/* check reserved area lock */
	if (ReservedAreaIsLocked)
	{
		*error = PFS_ERROR_DISK_FULL;
		return false;
	}

	file->direntry->protection = protection;

	/* add second part of protection */
	if (g->dirextension)
	{
		union objectinfo directory;
		struct direntry *sourceentry, *destentry;
		struct extrafields extrafields;
		UBYTE entrybuffer[MAX_ENTRYSIZE];

		/* make new direntry */
		destentry = (struct direntry *)entrybuffer;
		sourceentry = file->direntry;

		/* copy source */
		memcpy(destentry, sourceentry, sourceentry->next);

		/* set new extrafields */
		GetExtraFields(sourceentry, &extrafields);
		extrafields.prot = protection;
		AddExtraFields(destentry, &extrafields);

		/* commit changes */
		if (!GetParent((union objectinfo *)file, &directory, error, g))
			return false;
		else
			ChangeDirEntry(*file, destentry, &directory, file, g);
	}

	/* mark block for update and return success */
	MakeBlockDirty((struct cachedblock *)file->dirblock, g);
	return true;
}

bool SetOwnerID(struct fileinfo * file, ULONG owner, SIPTR *error, globaldata * g)
{
	struct extrafields extrafields;
	union objectinfo directory;
	struct direntry *sourceentry, *destentry;
	UBYTE entrybuffer[MAX_ENTRYSIZE];

	ENTER("SetOwnerID");
#if DELDIR
	if ((*(union objectinfo *)file).delfile.special <= SPECIAL_DELFILE)
	{
		if ((*(union objectinfo *)file).delfile.special == SPECIAL_DELDIR)
		{
			struct crootblockextension *rext = g->currentvolume->rblkextension;

			rext->blk.dd_uid = owner >> 16;
			rext->blk.dd_gid = owner & 0xffff;
			MakeBlockDirty ((struct cachedblock *)rext, g);
			return true;
		}

		*error = PFS_ERROR_WRITE_PROTECTED;
		return false;
	}
#endif

	if (!g->dirextension)
	{
		*error = PFS_ERROR_ACTION_NOT_KNOWN;
		return false;
	}

	/* check reserved area lock */
	if (ReservedAreaIsLocked)
	{
		*error = PFS_ERROR_DISK_FULL;
		return false;
	}

	/* make new direntry */
	destentry = (struct direntry *)entrybuffer;
	sourceentry = file->direntry;

	/* copy source */
	memcpy(destentry, sourceentry, sourceentry->next);

	/* set new extrafields */
	GetExtraFields(sourceentry, &extrafields);
	extrafields.uid = owner >> 16;
	extrafields.gid = owner & 0xffff;
	AddExtraFields(destentry, &extrafields);

	/* commit changes */
	if (!GetParent((union objectinfo *)file, &directory, error, g))
		return false;
	else
		ChangeDirEntry(*file, destentry, &directory, file, g);

	MakeBlockDirty((struct cachedblock *)file->dirblock, g);
	return true;
}

LONG ReadSoftLink(union objectinfo *linkfi, const char *prefix, char *buffer, ULONG size, SIPTR *error, globaldata * g)
{
	struct canode anode;
	UBYTE *softblock;
	ULONG rc;
	ULONG prefixlen;
	ULONG postfixlen;
	char *ptr = buffer;

	ENTER("ReadSoftLink");
	if (!linkfi || IsVolume(*linkfi))
	{
		*error = PFS_ERROR_OBJECT_WRONG_TYPE;
		return -1;
	}

	softblock = (UBYTE*)AllocVec(1024);
	if (!softblock) {
		*error = PFS_ERROR_NO_FREE_STORE;
		return -1;
	}

	/*
	* TODO: It might be a good idea to try to compress the resulting
	* path. For example "foo/bar/bleh///meh" should become "foo/meh".
	* FFS2 does this sort of thing for instance.
	*
	* Not doing this could lead to problems easily as many callers use
	* maximum buffer of around 256 bytes. - Piru
	*/

	GetAnode(&anode, linkfi->file.direntry->anode, g);
	if ((rc = DiskRead(softblock, 1, anode.blocknr, g)))
	{
		FreeVec(softblock);
		*error = rc;
		return -1;
	}

	/* If link destination is absolute, ignore prefix */
	if (strchr((char*)softblock, ':'))
	{
		prefixlen = 0;

		/* ...Unless if the link target begins with ':' and the original
		 * name has a volume/device/assign specifier. If so, the specifier
		 * needs to be used as a prefix. If this is not done links pointing
		 * to root of a volume/device/assign would point to different
		 * targets depending on the user's current directory. That'd be
		 * horribly wrong.
		 */
		if (softblock[0] == ':')
		{
			char *colonpos = strchr((char*)prefix, ':');
			/* Note: if colon is at the beginning then prefixlen == 0
			 * which is correct.
			 */
			if (colonpos)
				prefixlen = colonpos - prefix;
		}
	}
	else
	{
		prefixlen = strlen(prefix);
	}

	postfixlen = g->unparsed ? strlen(g->unparsed) : 0;

	/* if link destination ends in a /, skip the postfix / if any */
	if (linkfi->file.direntry->fsize > 0 && softblock[linkfi->file.direntry->fsize - 1] == '/')
	{
		if (postfixlen)
			{
				g->unparsed++;
				postfixlen--;
			}
	}
	else
	{
		/* Else remove a lone trailing '/' in the postfix, if any */
		/* "/" becomes "" */
		/* "foo/" becomes "foo" */
		/* "foo//" becomes "foo//" */
		if (postfixlen > 0 && g->unparsed[postfixlen - 1] == '/' &&
		    (postfixlen == 1 || g->unparsed[postfixlen - 2] != '/'))
			postfixlen--;
	}

	if (prefixlen + linkfi->file.direntry->fsize + postfixlen + 1 > size) {
		FreeVec(softblock);
		return -2;
	}

	memcpy(ptr, (void*)prefix, prefixlen);
	ptr += prefixlen;
	memcpy(ptr, softblock, linkfi->file.direntry->fsize);
	ptr += linkfi->file.direntry->fsize;
	memcpy(ptr, g->unparsed, postfixlen);
	ptr += postfixlen;
	*ptr = '\0';

	FreeVec(softblock);

	return (LONG) (ptr - buffer);
}

bool CreateSoftLink(union objectinfo *linkdir, STRPTR linkname, STRPTR softlink,
					union objectinfo *newlink, SIPTR *error, globaldata * g)
{
	ULONG anodenr;
	UBYTE entrybuffer[MAX_ENTRYSIZE];
	struct direntry *de;
	UBYTE *softblock = NULL;
	struct canode anode;
	size_t l;

	ENTER("CreateSoftLink");
#if DELDIR
	if (IsDelDir(*linkdir))
	{
		*error = PFS_ERROR_WRITE_PROTECTED;
		return false;
	}
#endif

	/* check disk-writeprotection etc */
	if (!CheckVolume(g->currentvolume, 1, error, g))
		return false;

	/* get anodenr of directory */
	if (!linkdir || IsVolume(*linkdir))
		anodenr = ANODE_ROOTDIR;
	else
	{
		anodenr = FIANODENR(&linkdir->file);
		LOCK(linkdir->file.dirblock);
	}

	/* truncate filename to 31 characters */
	if (!(l = strlen(linkname)))
	{
		*error = PFS_ERROR_INVALID_COMPONENT_NAME;
		return false;
	}
	if (l > FILENAMESIZE - 1)
		linkname[FILENAMESIZE - 1] = 0x0;

	/* check reserved area lock */
	if (ReservedAreaIsLocked)
	{
		*error = PFS_ERROR_DISK_FULL;
		return false;
	}

	/* check if a file by that name already exists */
	if (SearchInDir(anodenr, (STRPTR)linkname, newlink, g))
	{
		*error = PFS_ERROR_OBJECT_EXISTS;
		return false;
	}

	softblock = (UBYTE*)AllocVec(1024);
	if (!softblock) {
		*error = PFS_ERROR_NO_FREE_STORE;
		return false;
	}

	/* make directory entry 
	 * the anode allocated is the link list element
	 */
	de = (struct direntry *)entrybuffer;
	if (!MakeDirEntry(ST_SOFTLINK, (UBYTE*)linkname, entrybuffer, g))
		goto error0;

	/* store directoryentry */
	if (!AddDirectoryEntry(linkdir, (struct direntry *)entrybuffer, &newlink->file, g))
		goto error1;

	/* fill directory entry */
	if (!(AllocateBlocks(de->anode, 1, g)))
	{
		*error = PFS_ERROR_DISK_FULL;
		goto error2;
	}

	GetAnode(&anode, de->anode, g);
	strcpy_s((char*)softblock, 1024, softlink);
	DiskWrite(softblock, 1, anode.blocknr, g);
	newlink->file.direntry->fsize = strlen((char*)softblock);
	FreeVec(softblock);
	return true;

	/* errors */
  error2:
	RemoveDirEntry(newlink->file, g);
  error1:
	FreeAnode(de->anode, g);
  error0:
  	FreeVec(softblock);

	return false;
}

/*
 * linkdir: directory (in) 
 * linkname: name (in)
 * object: object to link to (in) (dir must be locked)
 * newlink: result (out)
 */
bool CreateLink(union objectinfo * linkdir, STRPTR linkname, union objectinfo * object,
				union objectinfo * newlink, SIPTR *error, globaldata * g)
{
	union objectinfo info, odi;
	ULONG anodenr, linklist;
	struct direntry *objectentry, *destentry;
	UBYTE entrybuffer[MAX_ENTRYSIZE];
	struct extrafields extrafields;
	struct canode linknode;
	size_t l;

	ENTER("CreateLink");
#if DELDIR
	if (IsDelDir(*linkdir) || IsDelDir(*object) || IsDelFile(*object))
	{
		*error = PFS_ERROR_WRITE_PROTECTED;
		return false;
	}
#endif

	/* check if operation possible */
	if (!g->dirextension)
	{
		*error = PFS_ERROR_ACTION_NOT_KNOWN;
		return false;
	}

	/* check disk-writeprotection etc */
	if (!CheckVolume(g->currentvolume, 1, error, g))
		return false;

	/* get anodenr */
	if (!linkdir || IsVolume(*linkdir))
	{
		anodenr = ANODE_ROOTDIR;
	}
	else
	{
		anodenr = FIANODENR(&linkdir->file);
		LOCK(linkdir->file.dirblock);
	}

	/* truncate filename to 31 characters */
	if (!(l = strlen(linkname)))
	{
		*error = PFS_ERROR_INVALID_COMPONENT_NAME;
		return false;
	}
	if (l > FILENAMESIZE - 1)
		linkname[FILENAMESIZE - 1] = 0x0;

	/* check reserved area lock */
	if (ReservedAreaIsLocked)
	{
		*error = PFS_ERROR_DISK_FULL;
		return false;
	}

	/* check if a file by that name already exists */
	if (SearchInDir(anodenr, linkname, &info, g))
	{
		*error = PFS_ERROR_OBJECT_EXISTS;
		return false;
	}

	/* make directory entry 
	 * the anode allocated is the link list element
	 */
	if (!MakeDirEntry(ST_LINKDIR, (UBYTE*)linkname, entrybuffer, g))
		return false;

	/* add link info */
	destentry = (struct direntry *)entrybuffer;
	objectentry = object->file.direntry;
#if MULTIUSER
	GetExtraFields(destentry, &extrafields);
#else
	memset(&extrafields, 0, sizeof(struct extrafields));
#endif
	extrafields.link = objectentry->anode;
	AddExtraFields(destentry, &extrafields);

	/* copy object info */
	destentry->fsize = objectentry->fsize;
	switch (objectentry->type)
	{
		case ST_FILE:
		case ST_SOFTLINK:
		case ST_ROLLOVERFILE:
		case ST_LINKFILE:
			destentry->type = ST_LINKFILE;
			break;
		
		default:
			destentry->type = ST_LINKDIR;
			break;
	}

	/* store directoryentry */
	if (!AddDirectoryEntry(linkdir, destentry, &newlink->file, g))
	{
		FreeAnode(destentry->anode, g);
		return false;
	}

	/* make linknode */
	linknode.clustersize = object->file.dirblock->blk.anodenr;
	linknode.blocknr = newlink->file.dirblock->blk.anodenr;
	linknode.next = 0;
	SaveAnode(&linknode, newlink->file.direntry->anode, g);

	/* change objectentry */
	GetExtraFields(objectentry, &extrafields);
	if (!(linklist = extrafields.link))
	{
		/* there were no links yet ->
		 * add link field. Use entrybuffer and destentry pointer
		 */
		extrafields.link = linklist = newlink->file.direntry->anode;
		memcpy(entrybuffer, objectentry, objectentry->next);
		AddExtraFields(destentry, &extrafields);

		if (!GetParent(object, &odi, error, g))
			return false;    /* serious! should not happen */

		ChangeDirEntry(object->file, destentry, &odi, &object->file, g);
	}
	else
	{
		/* add new link to chain */
		GetAnode(&linknode, extrafields.link, g);
		while (linknode.next)
			GetAnode(&linknode, linknode.next, g);
		linknode.next = newlink->file.direntry->anode;
		SaveAnode(&linknode, linknode.nr, g);
	}

	return true;
}

bool SetDate(union objectinfo *file, struct DateStamp *date, SIPTR *error, globaldata *g)
{
	ENTER("SetDate");

#if DELDIR
	if (file->deldir.special <= SPECIAL_DELFILE)
	{
		*error = PFS_ERROR_WRITE_PROTECTED;
		return false;
	}
#endif

	if (!CheckVolume(file->file.dirblock->volume, 1, error, g))
		return false;

	file->file.direntry->creationday = (UWORD)date->ds_Days;
	file->file.direntry->creationminute = (UWORD)date->ds_Minute;
	file->file.direntry->creationtick = (UWORD)date->ds_Tick;
	MakeBlockDirty((struct cachedblock *)file->file.dirblock, g);
	return true;
}

void Touch(struct fileinfo *info, globaldata * g)   // ook archiveflag..
 {
	struct DateStamp time;

	DateTime(&time);

#if VERSION23
	if (IsVolume(*((union objectinfo *)info)) &&
		g->currentvolume->rblkextension)
	{
		g->currentvolume->rblkextension->blk.root_date[0] = (UWORD)time.ds_Days;
		g->currentvolume->rblkextension->blk.root_date[1] = (UWORD)time.ds_Minute;
		g->currentvolume->rblkextension->blk.root_date[2] = (UWORD)time.ds_Tick;
		MakeBlockDirty((struct cachedblock *)g->currentvolume->rblkextension, g);
	}
	else
#endif

	if (!IsVolume(*((union objectinfo *)info)))
	{
		info->direntry->creationday = (UWORD)time.ds_Days;
		info->direntry->creationminute = (UWORD)time.ds_Minute;
		info->direntry->creationtick = (UWORD)time.ds_Tick;
		info->direntry->protection &= ~FIBF_ARCHIVE;    // clear archivebit (eor)

		MakeBlockDirty((struct cachedblock *)info->dirblock, g);
	}
}


#if ROLLOVER
/*
 * dir: directory (in) 
 * rollname: name of rollover file (in)
 * result: created rolloverfile (out)
 */
bool CreateRollover(union objectinfo *dir, STRPTR rollname, ULONG size,
					union objectinfo *result, SIPTR *error, globaldata * g)
{
	ULONG anodenr;
	struct direntry *de;
	UBYTE entrybuffer[MAX_ENTRYSIZE];
	struct anodechain *ac;
	size_t l;

	DB(Trace(1, "CreateRollover", "name %s size %d \n", rollname, size));
#if DELDIR
	/* no write access to deldir */
	if (IsDelDir(*dir))
	{
		*error = PFS_ERROR_WRITE_PROTECTED;
		return false;
	}
#endif

	/* check if operation possible */
	if (!g->dirextension)
	{
		*error = PFS_ERROR_ACTION_NOT_KNOWN;
		return false;
	}

	/* check size */
	if (!size)
	{
		*error = PFS_ERROR_BAD_NUMBER;
		return false;
	}

	/* check disk-writeprotection etc */
	if (!CheckVolume(g->currentvolume, 1, error, g))
		return false;

	/* get anodenr of directory */
	if (!dir || IsVolume(*dir))
		anodenr = ANODE_ROOTDIR;
	else
	{
		anodenr = FIANODENR(&dir->file);
		LOCK(dir->file.dirblock);
	}

	/* truncate filename to 31 characters */
	if (!(l = strlen(rollname)))
	{
		*error = PFS_ERROR_INVALID_COMPONENT_NAME;
		return false;
	}
	if (l > FILENAMESIZE - 1)
		rollname[FILENAMESIZE - 1] = 0x0;

	/* check reserved area lock */
	if (ReservedAreaIsLocked)
	{
		*error = PFS_ERROR_DISK_FULL;
		return false;
	}

	/* check if a file by that name already exists */
	if (SearchInDir(anodenr, rollname, result, g))
	{
		*error = PFS_ERROR_OBJECT_EXISTS;
		return false;
	}

	/* make directory entry 
	 * the anode allocated is the link list element
	 */
	de = (struct direntry *)entrybuffer;
	if (!MakeDirEntry(ST_ROLLOVERFILE, (UBYTE*)rollname, entrybuffer, g))
		return false;

	/* add rollover info is not necessary, since
	 * both virtualsize and rollpointer are initially
	 * zero
	 */
	if (!(ac = GetAnodeChain(de->anode, g)))
		goto error1;

	/* store directoryentry */
	if (!AddDirectoryEntry(dir, de, &result->file, g))
		goto error2;

	/* allocate blocks
	 * in case of an intermediate update, the rollover file will
	 * be temporarily committed smaller
	 */
	if (!(AllocateBlocksAC(ac, size, &result->file, g)))
		goto error3;

	/* set real size 
	 */
	result->file.direntry->fsize = size << BLOCKSHIFT;
	return true;

	/* errors */
  error3:
	RemoveDirEntry(result->file, g);
  error2:
	DetachAnodeChain(ac, g);
  error1:
	FreeAnode(de->anode, g);
	return false;
}

/* changes/reads rollover information of rollfile using roinfo. Objectinfo of
 * 'rollfile' is updated, if so needed. Roinfo is always filled with the current
 * rollfile settings (AFTER update). The real filesize is only changed when the
 * realsize fiels is unequal zero.
 */
ULONG SetRollover(fileentry_t *rollfile, struct rolloverinfo *roinfo, globaldata *g)
{
	union objectinfo directory;
	struct extrafields extrafields;
	struct direntry *sourcede, *destde;
	UBYTE entrybuffer[MAX_ENTRYSIZE];
	SIPTR error;
	ULONG realsize;

	/* check if file is rollover file */
	if (!IsRollover(rollfile->le.info))
		return PFS_ERROR_OBJECT_WRONG_TYPE;

	destde = (struct direntry *)entrybuffer;
	sourcede = rollfile->le.info.file.direntry;
	GetExtraFields(sourcede, &extrafields);
	if (roinfo->set)
	{
		if (roinfo->realsize)
		{
			realsize = roinfo->realsize & ~BLOCKSIZEMASK;
			if (!realsize) realsize = BLOCKSIZE;
		}
		else
			realsize = sourcede->fsize;

		/* virtualsize and rollpointer can not extend past end
		 * of file. virtualsize has to be <= realsize - 1
		 */
		if (roinfo->rollpointer > realsize ||
			roinfo->virtualsize >= realsize)
			return PFS_ERROR_SEEK_ERROR;

		if (roinfo->realsize)
			ChangeFileSize(rollfile, realsize, OFFSET_BEGINNING, &error, g);
		roinfo->realsize = sourcede->fsize;
		memcpy(destde, sourcede, sourcede->next);
		extrafields.virtualsize = roinfo->virtualsize;  
		extrafields.rollpointer = roinfo->rollpointer;
		AddExtraFields(destde, &extrafields);

		if (!GetParent(&rollfile->le.info, &directory, &error, g))
			return (ULONG)error;
		else
			ChangeDirEntry(rollfile->le.info.file, destde, &directory,
						   &rollfile->le.info.file, g);
	}
	else
	{
		roinfo->realsize = sourcede->fsize;
		roinfo->virtualsize = extrafields.virtualsize;
		roinfo->rollpointer = extrafields.rollpointer;
	}

	return 0;
}

#endif /* ROLLOVER */

/**********************************************************************/
/*                         LL CHANGE DIRENTRY                         */
/*                         LL CHANGE DIRENTRY                         */
/*                         LL CHANGE DIRENTRY                         */
/**********************************************************************/


/* Change a directoryentry. Covers all reference changing too

 * If direntry==NULL no new direntry is to be added, only removed.
 * result may be NULL then as well
 *
 * in: from, to, destdir
 * out: result
 *
 * from can become INVALID..
 */

void ChangeDirEntry(struct fileinfo from, struct direntry *to,
		   union objectinfo *destdir, struct fileinfo *result, globaldata * g)
{
	ULONG destanodenr = IsRoot(destdir) ? ANODE_ROOTDIR : FIANODENR(&destdir->file);

	/* check whether a 'within dir' rename */
	if (to && destanodenr == from.dirblock->blk.anodenr)
		RenameWithinDir(from, to, result, g);
	else
		RenameAcrossDirs(from, to, destdir, result, g);
}

/*
 * Move a file from one dir to another
 * NULL = delete allowed
 */
static void RenameAcrossDirs(struct fileinfo from, struct direntry *to,
		   union objectinfo *destdir, struct fileinfo *result, globaldata * g)
{
	UWORD removedlen;

	/* remove old entry (invalidates 'destdir') */
	removedlen = from.direntry->next;
	RemoveDirEntry(from, g);
	if (to)
	{
		/* test on volume is not necessary, because file.dirblock = volume.volume !=
		 * from.dirblock
		 * restore 'destdir' (can be invalidated by RemoveDirEntry)
		 */
		if (destdir->file.dirblock == from.dirblock &&
			destdir->file.direntry > from.direntry)
		{
			destdir->file.direntry = (struct direntry *)
				((UBYTE *)destdir->file.direntry - removedlen);
		}

		/* add new entry */
		AddDirectoryEntry(destdir, to, result, g);
	}

	UpdateChangedRef(from, result, -removedlen, g);
	if (result)
		LOCK(result->dirblock);
}

/*
 * Rename file within dir
 * NULL destination not allowed
 */
static void RenameWithinDir(struct fileinfo from, struct direntry *to,
							struct fileinfo *result, globaldata * g)
{
	int spaceneeded;
	struct fileinfo mover;

	LOCK(from.dirblock);
	mover.direntry = FIRSTENTRY(from.dirblock);
	mover.dirblock = from.dirblock;
	spaceneeded = to->next - from.direntry->next;
	if (spaceneeded <= 0)
		RenameInPlace(from, to, result, g);
	else
	{
		/* make space in block
		 */
		while (!CheckFit(from.dirblock, spaceneeded, g) && from.direntry != mover.direntry)
		{
			from.direntry = (struct direntry *)((UBYTE *)from.direntry - mover.direntry->next);
			MoveToPrevious(mover, mover.direntry, result, g);
		}

		if (CheckFit(from.dirblock, spaceneeded, g))
			RenameInPlace(from, to, result, g);
		else
			MoveToPrevious(from, to, result, g);
	}

	LOCK(result->dirblock);
}



/*
 * Check if direntry will fit in,
 * returns position to place it if ok
 */
static struct direntry *CheckFit(struct cdirblock *blok, int needed, globaldata * g)
{
	struct direntry *entry;
	int i;

	/* goto end of dirblock */
	entry = FIRSTENTRY(blok);
	for (i = 0; entry->next; entry = NEXTENTRY(entry))
		i += entry->next;

	if (needed + i + 1 <= DB_ENTRYSPACE)
		return entry;
	else
		return NULL;
}

/*
 * Moves firstentry to previous block, changing it to to. To can point to de.direntry
 * if wanted.
 * Return new fileinfo in 'result'
 * NB: no need to touch parent: MTP is always followed by another function
 * on the block.
 */
static bool MoveToPrevious(struct fileinfo de, struct direntry *to, struct fileinfo *result, globaldata * g)
{
	struct direntry *dest;
	struct cdirblock *prevblock = nullptr;
	struct canode anode;
	int removedlen;
	ULONG prev;

	LOCK(de.dirblock);

	/* get previous block */
	GetAnode(&anode, de.dirblock->blk.anodenr, g);
	prev = 0;
	while (anode.blocknr != de.dirblock->blocknr && anode.next)
	{
		prev = anode.nr;
		GetAnode(&anode, anode.next, g);
	}

	/* savety check */
	if (anode.blocknr != de.dirblock->blocknr)
	{
		ErrorMsg(AFS_ERROR_CACHE_INCONSISTENCY, NULL, g);
		return false;
	}

	/* Get dirblock in question
	 * Special case : previous == 0!!->add new head!!
	 */
	if (prev)
	{
		GetAnode(&anode, prev, g);
		if (!(prevblock = LoadDirBlock(anode.blocknr, g)))
			return false;
	}

	/* Add new entry */
	if (prev && (dest = CheckFit(prevblock, to->next, g)))
	{
		memcpy(dest, to, to->next);
		*(UBYTE *)NEXTENTRY(dest) = 0;  /* end of dirblock */
		result->direntry = dest;
		result->dirblock = prevblock;
	}
	else
	{
		/* make new dirblock .. */
		ULONG parent;
		struct canode newanode;
		struct cdirblock *newblock;

		newanode.clustersize = 1;
		parent = de.dirblock->blk.parent;
		if (!(newanode.blocknr = AllocReservedBlock(g)))
			return false;

		if (!prev)
		{
			GetAnode(&anode, de.dirblock->blk.anodenr, g);
			newanode.nr = anode.nr;
			newanode.next = anode.nr = AllocAnode (anode.next ? anode.next : anode.nr, g);
		}
		else
		{
			newanode.nr = AllocAnode (anode.nr, g);
			newanode.next = anode.next;
			anode.next = newanode.nr;
		}

		SaveAnode(&anode, anode.nr, g);
		newblock = MakeDirBlock(newanode.blocknr, newanode.nr, de.dirblock->blk.anodenr, parent, g);
		SaveAnode(&newanode, newanode.nr, g);   /* MUST be done AFTER MakeDirBlock */

		/* add entry */
		dest = FIRSTENTRY(newblock);
		memcpy(dest, to, to->next);
		*(UBYTE *)NEXTENTRY(dest) = 0;  /* end of dirblock */
		result->direntry = dest;
		result->dirblock = newblock;
	}

	LOCK(result->dirblock);
	MakeBlockDirty((struct cachedblock *)result->dirblock, g);

	/* remove old entry & make blocks dirty */
	removedlen = de.direntry->next;
	RemoveDirEntry(de, g);

	/* update references */
	UpdateChangedRef(de, result, -removedlen, g);
	return true;
}

/*
 * There HAS to be sufficient space!!
 */
static void RenameInPlace(struct fileinfo from, struct direntry *to, struct fileinfo *result, globaldata * g)
{
	UBYTE *dest, *start, *end;
	SIPTR error;
	ULONG movelen;
	int diff;
	union objectinfo parent;

	LOCK(from.dirblock);

	/* change date parent */
	if (GetParent((union objectinfo *)&from, &parent, &error, g))
		Touch(&parent.file, g);

	/* make place for new entry */
	diff = to->next - from.direntry->next;
	dest = (UBYTE *)from.direntry + to->next;
	start = (UBYTE *)from.direntry + from.direntry->next;
	end = (UBYTE *)&(from.dirblock->blk) + g->rootblock->reserved_blksize;
	movelen = (diff > 0) ? (end - dest) : (end - start);
	memmove(dest, start, movelen);

	/* fill in new entry */
	memcpy((UBYTE *)from.direntry, to, to->next);

	/* fill in result and make block dirty */
	*result = from;
	MakeBlockDirty((struct cachedblock *)from.dirblock, g);

	/* update references */
	UpdateChangedRef(from, result, diff, g);
}

/* RemoveDirEntry
 *
 * Simply shift the directryentry out with memmove(dest, src, len)
 * References are not corrected (see changedirentry)
 * 
 * makes all fileinfo's in same block invalid !!
 */
static void RemoveDirEntry(struct fileinfo info, globaldata * g)
{
	UBYTE *endofblok, *startofblok, *destofblok, *startofclear;
	UWORD clearlen;
	SIPTR error;
	union objectinfo parent;

	LOCK(info.dirblock);

	/* change date parent %6.5 */
	if (GetParent((union objectinfo *)&info, &parent, &error, g))
		Touch(&parent.file, g);

	/* remove direntry */
	destofblok = (UBYTE *)info.direntry;
	startofblok = destofblok + info.direntry->next;
	endofblok = (UBYTE *)&(info.dirblock->blk) + g->rootblock->reserved_blksize;
	startofclear = endofblok - info.direntry->next;
	clearlen = info.direntry->next;
	memmove(destofblok, startofblok, endofblok - startofblok);

	/* makes info invalid!! */
	if (info.direntry->next)
		memset(startofclear, 0, clearlen);
	MakeBlockDirty((struct cachedblock *)info.dirblock, g);     // %6.2

}


/* AddDirectoryEntry
 *
 * Add a directoryentry to a directory.
 * Tries to add the directoryentry at the end of an existing directoryblock. If
 * that fails, create a new one.
 *
 * Operates on currentvolume
 *
 * input : - dir: directory to add directoryentry too
 *          - newentry: the new directoryentry
 *
 * output: - newinfo: pointer to direntry and directoryblock the entry
 *          was added to
 *
 * NB: A) there should ALWAYS be at least one dirblock
 *     B) assumes CURRENTVOLUME 
 */
static bool AddDirectoryEntry(union objectinfo *dir, struct direntry *newentry,
							  struct fileinfo *newinfo, globaldata * g)
{
	struct canode anode;
	ULONG anodeoffset = 0, diranodenr;
	struct cdirblock *blok = nullptr;
	struct direntry *entry = NULL;
	bool done = false, eof = false;
	UCOUNT i;

	if (!dir || IsVolume(*dir))
		diranodenr = ANODE_ROOTDIR;
	else
		diranodenr = dir->file.direntry->anode;

	/* check if space in existing dirblocks */
	for (GetAnode(&anode, diranodenr, g); !done && !eof; eof = !NextBlock(&anode, &anodeoffset, g))
	{
		if (!(blok = LoadDirBlock(anode.blocknr + anodeoffset, g)))
			break;

		entry = (struct direntry *)&blok->blk.entries;

		/* goto end of dirblock; i = aantal gebruikte bytes */
		for (i = 0; entry->next; entry = NEXTENTRY(entry))
			i += entry->next;

		/* does it fit in this block? (keep space for trailing 0) */
		if (i + newentry->next + 1 < DB_ENTRYSPACE)
		{
			memcpy(entry, newentry, newentry->next);
			*(UBYTE *)NEXTENTRY(entry) = 0;     // dirblock afsluiten

			done = true;
			break;
		}
	}

	/* no->new dirblock (eof <=> anode is end of chain)
	 * We will make the new dirblock at the >start< of
	 * the chain.
	 * We always allocate new anode
	 */
	if (!done && eof)
	{
		ULONG parent;
		struct canode newanode;

		newanode.clustersize = 1;
		parent = blok->blk.parent;
		if (!(newanode.blocknr = AllocReservedBlock(g)))
			return false;
		GetAnode (&anode, diranodenr, g);
		newanode.nr = diranodenr;
		newanode.next = anode.nr = AllocAnode (anode.next ? anode.next : anode.nr, g);
		SaveAnode(&anode, anode.nr, g);
		blok = MakeDirBlock (newanode.blocknr, newanode.nr, diranodenr, parent, g);
		SaveAnode (&newanode, newanode.nr, g);
		entry = (struct direntry *)&blok->blk.entries;
		memcpy(entry, newentry, newentry->next);
		*(UBYTE *)NEXTENTRY(entry) = 0;     // mark end of dirblock 

	}

	/* fill newinfo */
	newinfo->direntry = entry;
	newinfo->dirblock = blok;

	/* update notify */
	if (blok)
	{
		LOCK(blok);
		MakeBlockDirty((struct cachedblock *)blok, g);

	}
	Touch(&dir->file, g);
	return true;
}


/*
 * Update references
 * diff is direntry size difference (new - original)
 */
static void UpdateChangedRef(struct fileinfo from, struct fileinfo *to, int diff, globaldata * g)
{
	struct volumedata *volume = from.dirblock->volume;
	listentry_t *fe;

	for (fe = (listentry_t * )HeadOf(&volume->fileentries); fe->next; fe = fe->next)
	{
		/* only dirs and files can be in a directory, but the volume *
		 * of volumeinfos can never point to a cached block, so a 
		 * type != ETF_VOLUME check is not necessary. Just check the
		 * dirblock pointer
		 */
		if (fe->info.file.dirblock == from.dirblock)
		{
			/* is het de targetentry ? */
			if (fe->info.file.direntry == from.direntry)
			{
				if (to)
					fe->info.file = *to;
			}
			else
			{
				/* take only entries after target */
				if (fe->info.file.direntry > from.direntry)
				{
					fe->info.file.direntry = (struct direntry *)
						((UBYTE *)fe->info.file.direntry + diff);
				}
			}
		}

		/* check for exnext references */
		if (fe->type.flags.dir)
		{
			lockentry_t *dle = (lockentry_t *)fe;

			if (dle->nextentry.dirblock == from.dirblock)
			{
				if ((dle->nextentry.direntry == from.direntry)
					&& (!dle->nextentry.direntry->next))
				{
					GetNextEntry(dle, g);
				}
				else
				{
					/* take only entries after target */
					if (dle->nextentry.direntry > from.direntry)
					{
						dle->nextentry.direntry = (struct direntry *)
							((UBYTE *)dle->nextentry.direntry + diff);
					}
				}
			}
		}
	}
}


/* MakeDirEntry
 *
 * Used by L2.NewFile, L2.NewDir
 *
 * Make a new directoryentry. The filename is not checked. Allocates anode
 * for file/dir
 *
 * input : 
 *        - type: ST_FILE, ST_DIR etc ..
 *        - name: objectname
 *        - entrybuffer: place to put direntry (char buffer of size MAX_ENTRYSIZE)
 *
 * output: - info: objectinfo of new directoryentry
 */
static bool MakeDirEntry(int8_t type, UBYTE *name, UBYTE *entrybuffer, globaldata * g)
{
	UWORD entrysize;
	struct direntry *direntry;
	struct DateStamp time;

	entrysize = ((sizeof(struct direntry) + strlen((char*)name)) & 0xfffe);
	if (g->dirextension)
		entrysize += 2;
	direntry = (struct direntry *)entrybuffer;
	memset(direntry, 0, entrysize);

	DateTime(&time);
	if (!(direntry->anode = AllocAnode(0, g)))
		return false;

	direntry->next = (UBYTE)entrysize;
	direntry->type = type;
	// direntry->fsize        = 0;
	direntry->creationday = (UWORD)time.ds_Days;
	direntry->creationminute = (UWORD)time.ds_Minute;
	direntry->creationtick = (UWORD)time.ds_Tick;
	// direntry->protection  = 0x00;    // RWED
	direntry->nlength = (UBYTE)strlen((char*)name);

	// the trailing 0 of strcpy() creates the empty comment!
	// the flags field following this is 0 by the memset call
	strcpy((char *)&direntry->startofname, (char*)name);

	return true;
}


/**********************************************************************/
/*                              LOWLEVEL                              */
/*                              LOWLEVEL                              */
/*                              LOWLEVEL                              */
/**********************************************************************/

/* NULL => failure 
 * The loaded dirblock is locked immediately (prevents flushing)
 * 
 */
struct cdirblock *LoadDirBlock(ULONG blocknr, globaldata * g)
{
	struct cdirblock *dirblk;
	struct volumedata *volume = g->currentvolume;

	DB(Trace(1, "LoadDirBlock", "loading block %lx\n", blocknr));
	// -I- check if already in cache
	if (!(dirblk = (struct cdirblock *)CheckCache(volume->dirblks, HASHM_DIR, blocknr, g)))
	{
		// -II- not in cache -> put it in
		dirblk = (struct cdirblock *)AllocLRU(g);

		DB(Trace(10, "LoadDirBlock", "loading block %lx from disk\n", blocknr));
		if (RawRead((UBYTE *)&dirblk->blk, RESCLUSTER, blocknr, g) == 0)
		{
			SmartSwap(&dirblk->blk);
			if (dirblk->blk.id == DBLKID)
			{
				dirblk->volume = g->currentvolume;
				dirblk->blocknr = blocknr;
				dirblk->used = false;
				dirblk->changeflag = false;
				Hash(dirblk, volume->dirblks, HASHM_DIR);
				UpdateReference(blocknr, dirblk, g);    // %10
			}
			else
			{
				ULONG args[2];
				args[0] = dirblk->blk.id;
				args[1] = blocknr;
				FreeLRU((struct cachedblock *)dirblk);
				ErrorMsg(AFS_ERROR_DNV_WRONG_DIRID, args, g);
				return NULL;
			}
		}
		else
		{
			FreeLRU((struct cachedblock *)dirblk);
			DB(Trace(5, "LoadDirBlock", "loading block %lx failed\n", blocknr));
			// ErrorMsg(AFS_PFS_ERROR_DNV_LOAD_DIRBLOCK, NULL, g);    // #$%^&??
			// DebugOn;DebugMsgNum("blocknr", blocknr);
			return NULL;
		}
	}

	EXIT("LoadDirBlock");
	return dirblk;
}

static bool IsChildOf(union objectinfo child, union objectinfo parent, globaldata * g)
{
	SIPTR error;
	union objectinfo up;
	bool goon = true;

	while (goon && !IsSameOI(child, parent))
	{
		goon = GetParent(&child, &up, &error, g);
		child = up;
	}

	if (IsSameOI(child, parent))
		return true;
	else
		return false;
}

FSIZE GetDEFileSize(struct direntry *direntry, globaldata *g)
{
	if (!LARGE_FILE_SIZE || !g->largefile)
		return direntry->fsize;
#if LARGE_FILE_SIZE
	else {
		struct extrafields extrafields;
		GetExtraFields(direntry, &extrafields);
		return ((FSIZE)direntry->fsize) | ((FSIZE)extrafields.fsizex << 32);
	}
#endif
}

ULONG GetDEFileSize32(struct direntry *direntry, globaldata *g)
{
	FSIZE size;
	
	if (!LARGE_FILE_SIZE || !g->largefile) {
		return direntry->fsize;
	} else {
		size = GetDEFileSize(direntry, g);
		if (size > MAXFILESIZE32)
			size = MAXFILESIZE32;
		return (ULONG)size;
	}
}

void SetDEFileSize(struct direntry *direntry, FSIZE size, globaldata *g)
{
	if (!LARGE_FILE_SIZE || !g->largefile)
		direntry->fsize = (ULONG)size;
#if LARGE_FILE_SIZE
	else {
		struct extrafields extrafields;
		UWORD high = (UWORD)(size >> 32);
		GetExtraFields(direntry, &extrafields);
		if (extrafields.fsizex != high) {
			extrafields.fsizex = high;
			AddExtraFields(direntry, &extrafields);
		}
		direntry->fsize = (ULONG)size;
	}
#endif
}

FSIZE GetDDFileSize(struct deldirentry *dde, globaldata *g)
{
	if (!LARGE_FILE_SIZE || !g->largefile || dde->filename[0] > DELENTRYFNSIZE)
		return dde->fsize;
#if LARGE_FILE_SIZE
	else
		return ((FSIZE)dde->fsize) | ((FSIZE)dde->fsizex << 32);
#endif
}

ULONG GetDDFileSize32(struct deldirentry *dde, globaldata *g)
{
	FSIZE size = GetDDFileSize(dde, g);
	if (size > MAXFILESIZE32)
		size = MAXFILESIZE32;
	return (ULONG)size;
}

void SetDDFileSize(struct deldirentry *dde, FSIZE size, globaldata *g)
{
	dde->fsize = (ULONG)size;
#if LARGE_FILE_SIZE
	if (!LARGE_FILE_SIZE || !g->largefile) {
		dde->fsizex = 0;
		return;
	}
	dde->fsizex = (UWORD)(size >> 32);
#endif
}

/*
 * GetExtraFields (normal file only)
 */
void GetExtraFields(struct direntry *direntry, struct extrafields *extrafields)
{
	UWORD *extra = (UWORD *)extrafields;
	UWORD *fields = (UWORD *)(((UBYTE *)direntry) + direntry->next);
	UWORD flags, i;

	flags = *(--fields);
	for (i = 0; i < sizeof(struct extrafields) / 2; i++, flags >>= 1)
		*(extra++) = (flags & 1) ? *(--fields) : 0;

	/* patch protection lower 8 bits */
	extrafields->prot |= direntry->protection;
}

#if DELDIR
#if MULTIUSER
static void GetExtraFieldsDD(struct extrafields *extrafields, globaldata * g);
static void GetExtraFieldsRoot(struct extrafields *extrafields, globaldata * g);
void GetExtraFieldsOI(union objectinfo *info, struct extrafields *extrafields, globaldata * g)
{
	if (IsVolume(*info))
		GetExtraFieldsRoot(extrafields, g);
	else if (IsDelDir(*info) || IsDelFile(*info))
		GetExtraFieldsDD(extrafields, g);
	else
		GetExtraFields(info->file.direntry, extrafields);
}

static void GetExtraFieldsDD(struct extrafields *extrafields, globaldata * g)
{
	struct crootblockextension *rext = g->currentvolume->rblkextension;

	extrafields->link = 0;
	extrafields->uid = rext->blk.dd_uid;
	extrafields->gid = rext->blk.dd_gid;
	extrafields->prot = rext->blk.dd_protection;
}

static void GetExtraFieldsRoot(struct extrafields *extrafields, globaldata * g)
{
	memset(extrafields, 0, sizeof(struct extrafields));
}
#endif
#endif

void AddExtraFields(struct direntry *direntry, struct extrafields *extra)
{
	UWORD offset, *dirext;
	UWORD array[16], i = 0, j = 0;
	UWORD flags = 0, orvalue;
	UWORD *fields = (UWORD *)extra;

	/* patch protection lower 8 bits */
	extra->prot &= 0xffffff00;
	offset = (sizeof(struct direntry) + (direntry->nlength) + *COMMENT(direntry)) & 0xfffe;
	dirext = (UWORD *)((UBYTE *)(direntry) + (UBYTE)offset);

	orvalue = 1;
	/* fill packed field array */
	for (i = 0; i < sizeof(struct extrafields) / 2; i++)
	{
		if (*fields)
		{
			array[j++] = *fields++;
			flags |= orvalue;
		}
		else
		{
			fields++;
		}

		orvalue <<= 1;
	}

	/* add fields to direntry */
	i = j;
	while (i)
		*dirext++ = array[--i];
	*dirext++ = flags;

	direntry->next = offset + 2 * j + 2;
}


/*
 * Updates size field of links
 */
void UpdateLinks(struct direntry *object, globaldata * g)
{
	struct canode linklist;
	union objectinfo loi;
	struct extrafields extrafields;
	ULONG linknr;

	ENTER("UpdateLinks");
	GetExtraFields(object, &extrafields);
	linknr = extrafields.link;
	while (linknr)
	{
		/* Update link: get link object info and update size */
		GetAnode(&linklist, linknr, g);
		FetchObject(linklist.blocknr, linklist.nr, &loi, g);
		loi.file.direntry->fsize = object->fsize;
		MakeBlockDirty((struct cachedblock *)loi.file.dirblock, g);
		linknr = linklist.next;
	}
}

/*
 * Removes link from linklist and kills direntry
 */
static bool DeleteLink(struct fileinfo *link, SIPTR *error, globaldata * g)
{
	struct canode linknode, linklist;
	struct extrafields extrafields;
	union objectinfo object, directory;
	UBYTE entrybuffer[MAX_ENTRYSIZE];

	/* get node to remove */
	GetAnode(&linknode, link->direntry->anode, g);
	GetExtraFields(link->direntry, &extrafields);

	/* delete old entry */
	ChangeDirEntry(*link, NULL, NULL, NULL, g);

	/* get object */
	FetchObject(linknode.clustersize, extrafields.link, &object, g);
	GetExtraFields(object.file.direntry, &extrafields);

	/* if the object lists our link as the first link, redirect it to the next one */
	if (extrafields.link == linknode.nr)
	{
		extrafields.link = linknode.next;
		memcpy(entrybuffer, object.file.direntry, object.file.direntry->next);
		AddExtraFields((struct direntry *)entrybuffer, &extrafields);
		if (!GetParent(&object, &directory, error, g)) {
			*error = PFS_ERROR_DISK_NOT_VALIDATED;
			return false;	// should never happen
		}
		else {
			ChangeDirEntry(object.file, (struct direntry *)entrybuffer, &directory, &object.file, g);
		}
	}
	/* otherwise simply remove the link from the list of links */
	else
	{
		GetAnode(&linklist, extrafields.link, g);
		while (linklist.next != linknode.nr)
			GetAnode(&linklist, linklist.next, g);

		linklist.next = linknode.next;
		SaveAnode(&linklist, linklist.nr, g);
	}

	FreeAnode(linknode.nr, g);
	return true;
}

/*
 * Removes head of linklist and promotes first link as
 * master (NB: object is the main object, NOT a link).
 * Returns linkstate: true: a link was promoted
 *                    false: there was no link to promote
 */
static bool RemapLinks(struct fileinfo *object, globaldata * g)
{
	struct extrafields extrafields;
	struct canode linknode;
	union objectinfo link, directory;
	struct direntry *destentry;
	UBYTE entrybuffer[MAX_ENTRYSIZE];
	SIPTR error;

	ENTER("RemapLinks");
	/* get head of linklist */
	GetExtraFields(object->direntry, &extrafields);
	if (extrafields.link == 0)
		return false;

	/* the file has links; get head of list
	 * we are going to promote this link to
	 * an object 
	 */
	GetAnode(&linknode, extrafields.link, g);

	/* get direntry belonging to this linknode */
	FetchObject(linknode.blocknr, linknode.nr, &link, g);

	/* Promote it from link to object */
	destentry = (struct direntry *)entrybuffer;
	memcpy(destentry, link.file.direntry, link.file.direntry->next);
	GetExtraFields(link.file.direntry, &extrafields);
	destentry->type = object->direntry->type;		// is this necessary?
	destentry->fsize = object->direntry->fsize;		// is this necessary?
	destentry->anode = object->direntry->anode;     // is this necessary?

	extrafields.link = linknode.next;
	AddExtraFields(destentry, &extrafields);

	/* Free old linklist node */
	FreeAnode(linknode.nr, g);

	/* Remove source direntry */
	ChangeDirEntry(*object, NULL, NULL, NULL, g);

	/* Refetch new head (can have become invalid) */
	FetchObject(linknode.blocknr, linknode.nr, &link, g);
	if (GetParent(&link, &directory, &error, g))
		ChangeDirEntry(link.file, destentry, &directory, &link.file, g);

	/* object directory has changed; update link chain
	 * new directory is the old chain head was in: linknode.linkdir (== linknode.blocknr)
	 */
	UpdateLinkDir(link.file.direntry, linknode.blocknr, g);
	return true;
}

/*
 * Update linklist to reflect new directory of linked to object
 */
static void UpdateLinkDir(struct direntry *object, ULONG newdiran, globaldata * g)
{
	struct canode linklist;
	struct extrafields extrafields;
	ULONG linknr;

	ENTER("UpdateLinkDir");
	GetExtraFields(object, &extrafields);
	linknr = extrafields.link;
	while (linknr)
	{
		/* update linklist: change clustersize (== object dir) */
		GetAnode(&linklist, linknr, g);
		linklist.clustersize = newdiran;
		SaveAnode(&linklist, linklist.nr, g);
		linknr = linklist.next;
	}
}

/* 
 * Update linklist to reflect moved node
 * (is supercopy of UpdateLinkDir)
 */
static void MoveLink(struct direntry *object, ULONG newdiran, globaldata *g)
{
	struct canode linklist;
	struct extrafields extrafields;
	ULONG linknr;

	ENTER("MoveLink");
	GetExtraFields(object, &extrafields);

	/* check if is link or linked to */
	if (!(linknr = extrafields.link))
	{
		return;
	}

	/* check filetype */
	if (object->type == ST_LINKDIR || object->type == ST_LINKFILE)
	{
		/* it is a link -> just change the linkdir */
		GetAnode(&linklist, object->anode, g);
		linklist.blocknr = newdiran;
		SaveAnode(&linklist, linklist.nr, g);		
	}
	else
	{
		/* it is the head (linked to) */
		while (linknr)
		{
			/* update linklist: change clustersize (== object dir) */
			GetAnode(&linklist, linknr, g);
			linklist.clustersize = newdiran;	/* the object's directory */
			SaveAnode(&linklist, linklist.nr, g);
			linknr = linklist.next;
		}
	}
}


#if DELDIR

/**********************************************************************/
/*                               DELDIR                               */
/**********************************************************************/

/* SearchInDeldir
 *
 * Search an object in the del-directory and return the objectinfo if found
 *
 * input : - delname: name of object to be searched for
 * output: - result: the searched for object
 * result: deldirentry * or NULL
 */
static struct deldirentry *SearchInDeldir(STRPTR delname, union objectinfo *result, globaldata * g)
{
	struct deldirentry *dde;
	struct cdeldirblock *dblk;
	UBYTE *delnumptr;
	UBYTE intl_name[PATHSIZE];
	unsigned slotnr, offset;

	ENTER("SearchInDeldir");
	if (!(delnumptr = (UBYTE * )strrchr(delname, DELENTRY_SEP)))
		return NULL;               /* no delentry seperator */
	stcd_i((char*)delnumptr + 1, (int *) &slotnr);  /* retrieve the slotnr  */

	*delnumptr = 0;             /* patch string to get filename part  */
	ctodstr((unsigned char*)delname, intl_name);

	/* truncate to maximum length */
	if (intl_name[0] > FILENAMESIZE - 1)
		intl_name[0] = FILENAMESIZE - 1;

	intltoupper(intl_name);     /* international uppercase objectname */
	*delnumptr = DELENTRY_SEP;

	/* 4.3: get deldir block */
	if (!(dblk = GetDeldirBlock(slotnr/DELENTRIES_PER_BLOCK, g)))
		return NULL;

	offset = slotnr % DELENTRIES_PER_BLOCK;

	dde = &dblk->blk.entries[offset];
	if (intlcmp(intl_name, dde->filename))
	{
		if (!IsDelfileValid(dde, dblk, g))
			return NULL;

		result->delfile.special = SPECIAL_DELFILE;
		result->delfile.slotnr  = slotnr;
		LOCK(dblk);
		return dde;
	}

	return NULL;
}

/*
 * Test if delfile is valid by scanning it's blocks
 */
static bool IsDelfileValid(struct deldirentry *dde, struct cdeldirblock *ddblk,  globaldata * g)
{
	struct canode anode;

	/* check if deldirentry actually used */
	if (!dde->anodenr)
		return false;

	/* scan all blocks in the anodelist for validness */
	for (anode.nr = dde->anodenr; anode.nr; anode.nr = anode.next)
	{
		GetAnode(&anode, anode.nr, g);
		if (BlockTaken(&anode, g))
		{
			/* free attached anodechain */
			FreeAnodesInChain(dde->anodenr, g);  /* only FREE anodes, not blocks!! */
			dde->anodenr = 0;
			MakeBlockDirty((struct cachedblock *)ddblk, g);
			return false;
		}
	}

	return true;
}

/*
 * Check if the blocks referenced by an anode are taken
 */
static bool BlockTaken(struct canode *anode, globaldata * g)
{
	ULONG size, bmoffset, bmseqnr, field, i, j, blocknr;
	struct cbitmapblock *bitmap;

	i = (anode->blocknr - alloc_data.bitmapstart) / 32;     // longwordnr
	size = (anode->clustersize + 31) / 32;
	bmseqnr = i / alloc_data.longsperbmb;
	bmoffset = i % alloc_data.longsperbmb;

	while (size)
	{
		/* get first bitmapblock */
		bitmap = GetBitmapBlock(bmseqnr, g);

		/* check all blocks */
		while (bmoffset < alloc_data.longsperbmb)
		{
			/* check all bits in field */
			field = bitmap->blk.bitmap[bmoffset];
			for (i = 0, j = 1 << 31; i < 32; j >>= 1, i++)
			{
				if (!(field & j))
				{
					/* block is taken, check it out */
					blocknr = (bmseqnr * alloc_data.longsperbmb + bmoffset) * 32 + i +
						alloc_data.bitmapstart;
					if (blocknr >= anode->blocknr && blocknr < anode->blocknr + anode->clustersize)
						return true;
				}
			}
			bmoffset++;
			if (!--size)
				break;
		}

		/* get ready for next block */
		bmseqnr = (bmseqnr + 1) % (alloc_data.no_bmb);
		bmoffset = 0;
	}

	return false;
}

/*
 * fill in fib. fib->fib_DiskKey must be the deldirentry number
 */
static void FillDelfileFib(struct deldirentry *dde, uint64_t slotnr, struct FileInfoBlock *fib, globaldata *g)
{
	struct crootblockextension *rext;
	UBYTE appbuffer[6];
	UBYTE *nameptr;
	FSIZE size;

	if (!dde)
		if (!(dde = GetDeldirEntryQuick(slotnr, g)))
			dde = GetDeldirEntryQuick(0, g);

	rext = g->currentvolume->rblkextension;
	size = GetDDFileSize(dde, g);
	fib->fib_DirEntryType = \
		fib->fib_EntryType = (LONG)ST_FILE;
	fib->fib_Protection = rext->blk.dd_protection;
	fib->fib_Size = GetDDFileSize32(dde, g);
	fib->fib_NumBlocks = (LONG)((size / BLOCKSIZE) + (size % BLOCKSIZE > 0));
	fib->fib_Date.ds_Days = dde->creationday;
	fib->fib_Date.ds_Minute = dde->creationminute;
	fib->fib_Date.ds_Tick = dde->creationtick;
	fib->fib_Comment[0] = 0;
	fib->fib_OwnerUID = rext->blk.dd_uid;
	fib->fib_OwnerGID = rext->blk.dd_gid;

	/* get filename */
	nameptr = (UBYTE*) & fib->fib_FileName[1];
	strncpy((char*)nameptr, (char*) & dde->filename[1], dde->filename[0]);
	nameptr += dde->filename[0];
	*nameptr++ = DELENTRY_SEP;

	/* append number appendix */
	fib->fib_DiskKey = slotnr;

	for (int i = stcu_d((char*)appbuffer, (unsigned int)fib->fib_DiskKey); i < 3; i++)
		*nameptr++ = '0';
	strcpy((char*)nameptr, (char*)appbuffer);
	fib->fib_FileName[0] = (char)strlen(&fib->fib_FileName[1]);
}

/*
 * Get a >valid< deldirentry starting from deldirentrynr ddnr
 * deldir is assumed present and enabled
 */
static struct deldirentry *GetDeldirEntry(IPTR *ddnr, globaldata * g)
{
	struct crootblockextension *rext = g->currentvolume->rblkextension;
	struct cdeldirblock *ddblk;
	struct deldirentry *dde;
	UWORD maxdelentrynr = rext->blk.deldirsize*DELENTRIES_PER_BLOCK - 1;
	UWORD oldlock;

	while (*ddnr <= maxdelentrynr)
	{
		/* get deldirentry */
		if (!(ddblk = GetDeldirBlock((UWORD)( * ddnr / DELENTRIES_PER_BLOCK), g)))
			break;

		oldlock = ddblk->used;
		LOCK(ddblk);
		dde = &ddblk->blk.entries[*ddnr%DELENTRIES_PER_BLOCK];

		/* check if dde valid */
		if (IsDelfileValid(dde, ddblk, g))
		{
			/* later --> check if blocks retaken !! */
			/* can be done by scanning bitmap!!     */
			return dde;
		}

		(*ddnr)++;
		ddblk->used = oldlock;
	}

	/* nothing found */
	return NULL;
}

/*
 * Get deldirentry deldirentrynr (NO CHECK ON VALIDITY
 * deldir is assumed present and enabled
 */
struct deldirentry *GetDeldirEntryQuick(uint64_t ddnr, globaldata *g)
{
	struct cdeldirblock *ddblk;

	/* get deldirentry */
	if (!(ddblk = GetDeldirBlock((UWORD)(ddnr/DELENTRIES_PER_BLOCK), g)))
		return NULL;

	return &ddblk->blk.entries[ddnr%DELENTRIES_PER_BLOCK];
}

static ULONG FillInDDEData(struct ExAllData *buffer, LONG type,
		struct deldirentry *dde, uint64_t ddenr, ULONG spaceleft, globaldata *g)
{
	UWORD nameoffset, commentoffset = 0;
	UBYTE *nameptr;
	UCOUNT size;
	unsigned i;
	UBYTE appbuffer[6];

	/* get location to put name */
	switch (type)
	{
		case ED_NAME:
			size = offsetof(struct ExAllData, ed_Type);
			break;
		case ED_TYPE:
			size = offsetof(struct ExAllData, ed_Size);
			break;
		case ED_SIZE:
			size = offsetof(struct ExAllData, ed_Prot);
			break;
		case ED_PROTECTION:
			size = offsetof(struct ExAllData, ed_Days);
			break;
		case ED_DATE:
			size = offsetof(struct ExAllData, ed_Comment);
			break;
		case ED_COMMENT:
			size = offsetof(struct ExAllData, ed_OwnerUID);
			break;
		case ED_OWNER:
#if defined(__MORPHOS__)
			size = offsetof(struct ExAllData, ed_Size64);
			break;
		case ED_SIZE64:
#endif
			size = sizeof(struct ExAllData);
			break;
		default:
			size = offsetof(struct ExAllData, ed_Type);
			break;
	}

	/* size of name */
	nameoffset = size;
	size += dde->filename[0] + 4 + 1;   /* extra ddenr (3+1) and termination zero (1) */

	/* size of comment */
	if (type >= ED_COMMENT)
	{
		commentoffset = size;
		size++;
	}

	/* check fit */
	size = (size + 1) & 0xfffe;
	if (size > spaceleft)
		return 0;

	/* copy */
	buffer->ed_Next = NULL;
	switch (type)
	{
#if defined(__MORPHOS__)
		case ED_SIZE64:
			buffer->ed_Size64 = GetDDFileSize(dde, g);
#endif

		case ED_OWNER:
			buffer->ed_OwnerUID = g->currentvolume->rblkextension->blk.dd_uid;
			buffer->ed_OwnerGID = g->currentvolume->rblkextension->blk.dd_gid;

		case ED_COMMENT:
			buffer->ed_Comment = (UBYTE *)buffer + commentoffset;
			*((UBYTE *)buffer + commentoffset) = 0x0;

		case ED_DATE:
			buffer->ed_Days = dde->creationday;
			buffer->ed_Mins = dde->creationminute;
			buffer->ed_Ticks = dde->creationtick;

		case ED_PROTECTION:
			buffer->ed_Prot = g->currentvolume->rblkextension->blk.dd_protection;

		case ED_SIZE:
			buffer->ed_Size = GetDDFileSize32(dde, g);

		case ED_TYPE:
			buffer->ed_Type = ST_FILE;

		case ED_NAME:
			/* filename */
			nameptr = (UBYTE *)buffer + nameoffset;
			strncpy((char*)nameptr, (char*) & dde->filename[1], dde->filename[0]);
			nameptr += dde->filename[0];
			*nameptr++ = DELENTRY_SEP;

			/* append nr */
			for (i = stcu_d((char*)appbuffer, (unsigned int) ddenr); i < 3; i++)
				*nameptr++ = '0';
			strcpy((char*)nameptr, (char*)appbuffer);

		default:
			buffer->ed_Name = (UBYTE *)buffer + nameoffset;
	}

	return size;
}


static struct cdeldirblock *GetDeldirBlock(UWORD seqnr, globaldata *g)
{
	struct volumedata *volume = g->currentvolume;
	struct crootblockextension *rext;
	struct cdeldirblock *ddblk;
	ULONG blocknr;

	rext = volume->rblkextension;

	if (seqnr > MAXDELDIR)
	{
		DB(Trace(5,"GetDeldirBlock","seqnr out of range = %lx\n", seqnr));
		ErrorMsg (AFS_ERROR_DELDIR_INVALID, NULL, g);
		return NULL;
	}

	/* get blocknr */
	if (!(blocknr = rext->blk.deldir[seqnr]))
	{
		DB(Trace(5,"GetDeldirBlock","ERR: index zero\n"));
		ErrorMsg (AFS_ERROR_DELDIR_INVALID, NULL, g);
		return NULL;
	}

	/* check cache */
	for (ddblk = (cdeldirblock * )HeadOf(&volume->deldirblks); ddblk->next; ddblk=ddblk->next)
	{
		if (ddblk->blk.seqnr == seqnr)
		{
			MakeLRU (ddblk);
			return ddblk;
		}
	}

	/* alloc cache */
	if (!(ddblk = (struct cdeldirblock *)AllocLRU(g)))
	{
		DB(Trace(5,"GetDeldirBlock","ERR: alloclru failed\n"));
		return NULL;
	}

	/* read block */
	if (RawRead ((UBYTE*)&ddblk->blk, RESCLUSTER, blocknr, g) != 0)
	{
		DB(Trace(5,"GetDeldirBlock","Read ERR: seqnr = %d blocknr = %lx\n", seqnr, blocknr));
		FreeLRU ((struct cachedblock *)ddblk);
		return NULL;
	}
	SmartSwap(&ddblk->blk, RESCLUSTER * g->blocksize);

	/* check it */
	if (ddblk->blk.id != DELDIRID)
	{
		ErrorMsg (AFS_ERROR_DELDIR_INVALID, NULL, g);
		FreeLRU ((struct cachedblock *)ddblk);
		volume->rootblk->options ^= MODE_DELDIR;
		g->deldirenabled = false;
	}
	
	/* initialize it */
	ddblk->volume     = volume;
	ddblk->blocknr    = blocknr;
	ddblk->used       = false;
	ddblk->changeflag = false;

	/* add to cache and return */	
	MinAddHead (&volume->deldirblks, ddblk);
	return ddblk;
}

struct cdeldirblock *NewDeldirBlock(UWORD seqnr, globaldata *g)
{
	struct volumedata *volume = g->currentvolume;
	struct crootblockextension *rext;
	struct cdeldirblock *ddblk;
	ULONG blocknr;

	rext = volume->rblkextension;

	if (seqnr > MAXDELDIR)
	{
		DB(Trace(5, "NewDelDirBlock", "seqnr out of range = %lx\n", seqnr));
		return NULL;
	}

	/* alloc block and LRU slot */
	if (!(ddblk = (struct cdeldirblock *)AllocLRU(g)) ||
		!(blocknr = AllocReservedBlock(g)) )
	{
		if (ddblk)
			FreeLRU((struct cachedblock *)ddblk);
		return NULL;
	}

	/* make reference */
	rext->blk.deldir[seqnr] = blocknr;

	/* fill block */
	ddblk->volume     = volume;
	ddblk->blocknr    = blocknr;
	ddblk->used       = false;
	ddblk->blk.id     = DELDIRID;
	ddblk->blk.seqnr  = seqnr;
	ddblk->changeflag = true;
	ddblk->blk.protection		= DELENTRY_PROT;	/* re..re..re.. */
	ddblk->blk.creationday		= volume->rootblk->creationday;
	ddblk->blk.creationminute	= volume->rootblk->creationminute;
	ddblk->blk.creationtick		= volume->rootblk->creationtick;

	/* add to cache and return */
	MinAddHead(&volume->deldirblks, ddblk);
	return ddblk;
}

/* Allocate deldirslot. Free anodechain attached to slot and clear it.
 * An intermediate update is possible, due to FreeAnodesInChain()
 */
static int AllocDeldirSlot(globaldata * g)
{
	struct crootblockextension *rext = g->currentvolume->rblkextension;
	struct cdeldirblock *ddblk;
	struct deldirentry *dde;
	int ddnr = 0;
	ULONG anodenr;

	/* get deldirentry and update roving ptr */
	ddnr = rext->blk.deldirroving;
	if (!(ddblk = GetDeldirBlock(ddnr/DELENTRIES_PER_BLOCK,g)))
	{
		rext->blk.deldirroving = 0;
		return 0;
	}

	dde = &ddblk->blk.entries[ddnr%DELENTRIES_PER_BLOCK];
	rext->blk.deldirroving = (rext->blk.deldirroving + 1) %
		(rext->blk.deldirsize*DELENTRIES_PER_BLOCK);
	MakeBlockDirty((struct cachedblock *)ddblk, g);

	anodenr = dde->anodenr;
	if (anodenr)
	{
		/* clear it for reuse */
		dde->anodenr = 0;

		/* free attached anodechain */
		FreeAnodesInChain(anodenr, g);  /* only FREE anodes, not blocks!! */
	}

	DB(Trace(1, "AllocDelDirSlot", "Allocate slot %ld\n", ddnr));
	return ddnr;
}


/* Add a file to the deldir. 
 * Deldir assumed enabled here, and info assumed a file (ST_FILE)
 * ddnr is deldir slot to use. Slot is assumed to be allocated by
 * AllocDeldirSlot()
 */
static void AddToDeldir(union objectinfo *info, int ddnr, globaldata * g)
{
	struct cdeldirblock *ddblk;
	struct deldirentry *dde;
	struct direntry *de = info->file.direntry;
	struct crootblockextension *rext;
	struct DateStamp time;

	DB(Trace(1, "AddToDeldir", "slotnr %ld\n", ddnr));
	/* get deldirentry to put it in */
	ddblk = GetDeldirBlock(ddnr/DELENTRIES_PER_BLOCK, g);
	dde = &ddblk->blk.entries[ddnr%DELENTRIES_PER_BLOCK];

	/* put new one in */
	dde->anodenr = de->anode;
	SetDDFileSize(dde, GetDEFileSize(de, g), g);
	dde->creationday = de->creationday;
	dde->creationminute = de->creationminute;
	dde->creationtick = de->creationtick;
	dde->filename[0] = min(DELENTRYFNSIZE - 1, de->nlength);
	strncpy((char*) & dde->filename[1], (char*) & de->startofname, dde->filename[0]);

	/* Touch deldir block. Inserted here, simply because this the only
	 * place touching the deldir will be needed.
	 * Note: Only this copy is touched ...
	 */
	DateTime(&time);
	rext = g->currentvolume->rblkextension;
	ddblk->blk.creationday = rext->blk.dd_creationday = (UWORD)time.ds_Days;
	ddblk->blk.creationminute = rext->blk.dd_creationminute = (UWORD)time.ds_Minute;
	ddblk->blk.creationtick = rext->blk.dd_creationtick = (UWORD)time.ds_Tick;

	/* dirtify block */
	MakeBlockDirty((struct cachedblock *)ddblk, g);
}

/* Set number of deldir blocks (Has to be single threaded)
 * If 0 then deldir is disabled (but MODE_DELDIR stays;
 * InitModules() detect that the number of deldirblocks is 0)
 * There must be a currentvolume
 * Returns error (0 = success)
 */
ULONG SetDeldir(int nbr, globaldata *g) 
{
	struct crootblockextension *rext = g->currentvolume->rblkextension;
	struct cdeldirblock *ddblk, *next;
	listentry_t *list;
	int i;
	ULONG error = 0;

	/* check range */
	if (nbr < 0 || nbr > MAXDELDIR+1)
		return PFS_ERROR_BAD_NUMBER;

	/* check if there are locks on any deldir, delfile */
	for (list = (listentry_t * )HeadOf(&g->currentvolume->fileentries); list->next; list=list->next)
	{
		if (IsDelDir(list->info) || IsDelFile(list->info))
			return PFS_ERROR_OBJECT_IN_USE;
	}

	UpdateDisk(g);

	/* flush cache */
	for (ddblk = (cdeldirblock * )HeadOf(&g->currentvolume->deldirblks); (next=ddblk->next); ddblk=next)
	{
		FlushBlock((struct cachedblock *)ddblk, g);
		MinRemove(LRU_CHAIN(ddblk));
		MinAddHead(&g->glob_lrudata.LRUpool, LRU_CHAIN(ddblk));
		// i.p.v. FreeLRU((struct cachedblock *)ddblk, g);
	}

	/* free unwanted deldir blocks */
	for (i = nbr; i < rext->blk.deldirsize; i++)
	{
		FreeReservedBlock(rext->blk.deldir[i], g);
		rext->blk.deldir[i] = 0;
	}

	/* allocate wanted ones */
	for (i = rext->blk.deldirsize; i < nbr; i++)
	{
		if (!NewDeldirBlock(i,g))
		{
			nbr = i+1;
			error = PFS_ERROR_DISK_FULL;
			break;
		}
	}

	/* if deldir size increases, start roving in a the new area 
	 * if deldir size decreases, start roving from the start
	 */
	if (nbr > rext->blk.deldirsize)
		rext->blk.deldirroving = rext->blk.deldirsize * DELENTRIES_PER_BLOCK;
	else
		rext->blk.deldirroving = 0;

	/* enable/disable */
	rext->blk.deldirsize = nbr;
	g->deldirenabled = (nbr > 0);

	MakeBlockDirty((struct cachedblock *)rext, g);
	UpdateDisk(g);
	return error;
}
#endif
