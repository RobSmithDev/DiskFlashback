#include "debug.h"

// own includes
#include "blocks.h"
#include "struct.h"
#include "lock.h"
#include "directory.h"
#include "volume.h"
#include "anodes.h"
#include "disk.h"

#define DebugOn
#define DebugOff
#define DebugMsg(m)
#define DebugMsgNum(msg,num)
#define DebugMsgName(msg, name)

/**********************************************************************/
/*                          MAKE/REMOVE/FREE                          */
/*                          MAKE/REMOVE/FREE                          */
/*                          MAKE/REMOVE/FREE                          */
/**********************************************************************/

/* MakeListEntry
**
** Allocated filentry structure and fill it with data from objectinfo and
** listtype. The result should be freed with FreeListEntry.
**
** input : - info: objectinfo of object 
**		 - type: desired type (readlock, writelock, readfe, writefe)
**
** result: the fileentry, or NULL if failure
*/
struct listentry *MakeListEntry (union objectinfo *info, listtype type, SIPTR *error, globaldata *g)
{
  listentry_t *listentry;
  union objectinfo newinfo;
  ULONG size;
  struct extrafields extrafields;
#if DELDIR
  struct deldirentry *dde = 0;
#endif

	ENTER("MakeListEntry");

	// alloceren fileentry
	switch (type.flags.type)
	{
		case ETF_FILEENTRY:	size = sizeof(fileentry_t); break;
		case ETF_VOLUME:
		case ETF_LOCK:		size = sizeof(lockentry_t); break;
		default:			listentry = NULL; return listentry;
	}

	DB(Trace(1,"MakeListEntry","size = %lx\n",size));

#if DELDIR
	if (IsDelDir(*info) || IsVolume(*info) || IsDir(*info))
#else
	if (IsVolume(*info) || IsDir(*info))
#endif
		type.flags.dir = 1;

	/* softlinks cannot directly be opened */
#if DELDIR
	if (info->deldir.special > SPECIAL_DELFILE && info->file.direntry->type == ST_SOFTLINK)
#else	
	if (!IsVolume(*info) && info->file.direntry->type == ST_SOFTLINK)
#endif
	{
		*error = PFS_ERROR_IS_SOFT_LINK;
		return NULL;
	}

	if (!(listentry = (listentry_t * )AllocMemP (size, g)))
	{
		*error = PFS_ERROR_NO_FREE_STORE;
		return NULL;
	}
	memset(listentry, 0, size);

	/* go after link and fetch the fileinfo of the real object
	 * (stored in 'newinfo'
	 */
#if DELDIR
	if (info->deldir.special > SPECIAL_DELFILE && (
#else
	if (!IsVolume(*info) && (
#endif
		(info->file.direntry->type == ST_LINKFILE) ||
		(info->file.direntry->type == ST_LINKDIR)))
	{
		struct canode linknode;

		/* The clustersize of the linknode (direntry.anode) 
		 * actually is the anodenr of the directory the linked to
		 * object is in. The object can be found by searching for
		 * 'anode == objectid'. This objectid can be found in
		 * the extrafields
		 */
		GetExtraFields(info->file.direntry, &extrafields);
		GetAnode(&linknode, info->file.direntry->anode, g);
		if (!FetchObject(linknode.clustersize, extrafields.link, &newinfo, g))
		{
			*error = PFS_ERROR_OBJECT_NOT_FOUND;
			return NULL;
		}
	}
	else
	{
		newinfo = *info;
	}

	// general
	listentry->type = type;
#if DELDIR
	switch (newinfo.delfile.special)
	{
		case 0:
			listentry->anodenr = ANODE_ROOTDIR; break;

		case SPECIAL_DELDIR:
			listentry->anodenr = 0; break;

		case SPECIAL_DELFILE:
			dde = GetDeldirEntryQuick(newinfo.delfile.slotnr, g);	
			listentry->anodenr = dde->anodenr;
			break;

		default:
			listentry->anodenr = newinfo.file.direntry->anode; break;
	}
#else
	listentry->anodenr = (newinfo.file.direntry) ? (newinfo.file.direntry->anode) : ANODE_ROOTDIR;
#endif

	listentry->info = newinfo;
	listentry->volume = g->currentvolume;

	// type specific
	switch (type.flags.type)
	{
		case ETF_VOLUME:
			// listentry->lock.fl_Volume = MKBADDR(newinfo.volume.volume->devlist);
			// listentry->volume		  = newinfo.volume.volume;
			break;

		case ETF_LOCK:
			/* every dirlock MUST have a different fl_Key (DOPUS!) */
			// listentry->lock.fl_Volume = MKBADDR(newinfo.file.dirblock->volume->devlist);
			// listentry->volume = newinfo.file.dirblock->volume;
			break;

		case ETF_FILEENTRY:
#define fe ((fileentry_t *)listentry)
			// listentry->lock.fl_Volume = MKBADDR(MKBADDR(newinfo.file.dirblock->volume->devlist);
			// listentry->volume = newinfo.file.dirblock->volume;
			fe->originalsize = IsDelFile(newinfo) ? GetDDFileSize(dde, g) : GetDEFileSize(newinfo.file.direntry, g);

			/* Get anodechain. If it fails anodechain will become NULL. This has to be
			 * taken into account by functions that use the chain
			 */
			fe->anodechain = GetAnodeChain (listentry->anodenr, g);
			fe->currnode = &fe->anodechain->head;

#if ROLLOVER
			/* Rollover file: set offset to rollfileoffset */
			/* check for rollover files */
			if (IsRollover(newinfo))
			{
				GetExtraFields(newinfo.file.direntry, &extrafields);
				SeekInFile(fe,extrafields.rollpointer,OFFSET_BEGINNING,error,g);
			}
#endif /* ROLLOVER */
#undef fe
			break;

		default:
			listentry = NULL;
			return listentry;
	}

	return listentry;
}


/* AddListEntry
**
** Checks if the listentry causes access conflicts
** Adds the entry to the locklist 
*/
bool _AddListEntry (listentry_t *entry, globaldata *g)
{
  struct volumedata *volume;

	DB(Trace(1,"AddListEntry","fe = %lx\n", entry->volume->fileentries.mlh_Head));

	if (entry==NULL)
		return false;

	if (AccessConflict(entry))
	{
		DB(Trace(1,"AddListEntry","found accessconflict!"));
		return false;
	}

	volume = entry->volume;

	MinAddHead (&volume->fileentries, entry);

	return true; 
}

/* RemoveListEntry
**
** removes 'entry' from the list and frees entry with FreeFileEntry 
**
** also think about empty lists: kill volume if empty and not present
** also	makes lock-links
*/
void RemoveListEntry (listentry_t *entry, globaldata *g)
{
  struct volumedata *volume;
  struct MinList *previous;

	/* get volume */
	volume = entry->volume;

	/* remove from list */
	MinRemove (entry);

	/* update FileLock link */
	previous = (struct MinList *)entry->prev;	
	FreeListEntry (entry, g);
}

void FreeListEntry(listentry_t *entry, globaldata *g)
{
#define fe ((fileentry_t *)entry)
	if (IsFileEntry(entry) && fe->anodechain)
		DetachAnodeChain(fe->anodechain, g);
	FreeMemP(entry, g);
#undef fe
}

/**********************************************************************/
/*                               CHANGE                               */
/*                               CHANGE                               */
/*                               CHANGE                               */
/**********************************************************************/

bool _ChangeAccessMode(listentry_t *file, LONG mode, SIPTR *error, globaldata *g)
{
  UWORD oldmode, newmode;

	// -I- make dosmode compatible with listtype-mode
	switch(mode)
	{
		case MODE_READWRITE:
		case MODE_NEWFILE:
			newmode = ET_EXCLWRITE;	break;

		default:			
			newmode = ET_SHAREDREAD; break;
	}

	// -II- check if mode is already correct		
	oldmode = file->type.flags.access;
	if (oldmode == newmode) 
		return true;
	
	// -III- operation always ok if oldmode exclusive or newmode shared
	if (oldmode>1 || newmode<=1)
	{
		file->type.flags.access = newmode;
		return true;
	}

	// -IV- otherwise: check for accessconflict
	MinRemove(file);	// prevents access conflict on itself 
	file->type.flags.access = newmode;
	if (!AccessConflict(file))
	{
		MinAddHead(&file->volume->fileentries, file);
		return true;
	}
	else
	{
		file->type.flags.access = oldmode;
		MinAddHead(&file->volume->fileentries, file);
		*error = PFS_ERROR_OBJECT_IN_USE;
		return false;
	}
}

/**********************************************************************/
/*                               CHECK                                */
/*                               CHECK                                */
/*                               CHECK                                */
/**********************************************************************/

/* AccessConflict
**
** input : - [entry]: the object to be granted access
**    This object should contain valid references
**
** result: TRUE = accessconflict; FALSE = no accessconflict
**    All locks on same ANODE are checked. So a lock on a link can
**    be denied if the linked to file is locked exclusively.
**
** Because UpdateReference always updates all references to a dirblock,
** and the match object is valid, a flushed reference CANNOT point to
** the same dirblock. If it is a link, it CAN reference the same
** object
**
** Returns FALSE if there is an exclusive lock or if there is
** write access on a shared lock
**
*/
bool AccessConflict (listentry_t *entry)
{
  ULONG anodenr;
  listentry_t *fe;
  struct volumedata *volume;

	DB(Trace(1,"Accessconflict","entry %lx\n",entry));

	// -I- get anodenr
	anodenr =  entry->anodenr;
	volume  = entry->volume;

	// -II- zoek locks naar zelfde object
	for(fe = (listentry_t * )HeadOf(&volume->fileentries); fe->next; fe=fe->next)
	{
		if(fe->type.flags.type == ETF_VOLUME)
		{
			if(entry->type.flags.type == ETF_VOLUME &&
			   (!SHAREDLOCK(fe->type) || !SHAREDLOCK(entry->type)))
			{
				DB(Trace(1,"Accessconflict","on volume\n"));
				return(true);
			}
		}	
		else if(fe->anodenr == anodenr)
		{
			// on of the two wants or has an exclusive lock?
			if(!SHAREDLOCK(fe->type) || !SHAREDLOCK(entry->type))
			{
				DB(Trace(1,"Accessconflict","exclusive lock\n"));
				return(true);
			}

			// new & old shared lock, both write? 
			else if(fe->type.flags.access == ET_SHAREDWRITE &&
					entry->type.flags.access == ET_SHAREDWRITE)
			{
				DB(Trace(1,"Accessconflict","two write locks\n"));
				return(true);
			}
		}
	}
	
	return false;	// no conflicting locks
}

/* ScanLockList
 *
 * checks <list> for a lock on the file with anode anodenr
 * ONLY FOR LOCKS TO CURRENTVOLUME
 */
bool ScanLockList (listentry_t *list, ULONG anodenr)
{
	for (;list->next; list=list->next)
	{
		if (list->anodenr == anodenr)
			return true;
	}
	return false;
}
