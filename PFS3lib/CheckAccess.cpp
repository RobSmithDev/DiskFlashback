/* $Id: CheckAccess.c 1.1 1996/01/03 10:18:38 Michiel Exp Michiel $ */
/* $Log: CheckAccess.c $
 * Revision 1.1  1996/01/03  10:18:38  Michiel
 * Initial revision
 * */

// own includes

#include "blocks.h"
#include "struct.h"
#include "disk.h"
#include "allocation.h"
#include "volume.h"
#include "directory.h"
#include "anodes.h"
#include "update.h"
#include "checkaccess.h"

#include "kswrapper.h"

/* fileaccess that changes a file but doesn't write to it
 */
bool CheckChangeAccess(fileentry_t *file, SIPTR *error, globaldata *g)
{
#if DELDIR
	/* delfiles cannot be altered */
	if (IsDelFile (file->le.info))
	{
		*error = PFS_ERROR_WRITE_PROTECTED;
		return false;
	}
#endif

	/* test on type */
	if (!IsFile(file->le.info)) 
	{
		*error = PFS_ERROR_OBJECT_WRONG_TYPE;
		return false;
	}
	
	/* volume must be or become currentvolume */
	if (!CheckVolume(file->le.volume, 1, error, g))
		return false;

	/* check reserved area lock */
	if (ReservedAreaIsLocked)
	{
		*error = PFS_ERROR_DISK_FULL;
		return false;
	}

	return true;
}


/* fileaccess that writes to a file
 */
bool CheckWriteAccess(fileentry_t *file, SIPTR *error, globaldata *g)
{

	if (!CheckChangeAccess(file, error, g))
		return false;

#ifndef DISABLE_PROTECT
	if (file->le.info.file.direntry->protection & FIBF_WRITE)
	{
		*error = PFS_ERROR_WRITE_PROTECTED;
		return false;
	}
#endif
	return true;
}


/* fileaccess that reads from a file
 */
bool CheckReadAccess(fileentry_t *file, SIPTR *error, globaldata *g)
{
	*error = 0;

	/* Test on read-protection, type and volume */
#if DELDIR
	if (!IsDelFile(file->le.info))
	{
#endif
		if (!IsFile(file->le.info)) 
		{
			*error = PFS_ERROR_OBJECT_WRONG_TYPE;
			return false;
		}
#ifndef DISABLE_PROTECT

		if (file->le.info.file.direntry->protection & FIBF_READ)
		{
			*error = PFS_ERROR_READ_PROTECTED;
			return false;
		}
#endif
#if DELDIR
	}
#endif

	if (!CheckVolume(file->le.volume, 0, error, g))
		return false;

	return true;
}

/* check on operate access (like Seek)
 */
bool CheckOperateFile(fileentry_t *file, SIPTR *error, globaldata *g)
{
	*error = 0;

	/* test on type */
#if DELDIR
	if (!IsDelFile(file->le.info) && !IsFile(file->le.info))
#else
	if (!IsFile(file->le.info)) 
#endif
	{
		*error = PFS_ERROR_OBJECT_WRONG_TYPE;
		return false;
	}

	/* volume must be or become currentvolume */
	if (!CheckVolume(file->le.volume, 0, error, g))
		return false;

	return true;
}

