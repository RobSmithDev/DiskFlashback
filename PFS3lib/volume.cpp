#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

#include "debug.h"

// own includes
#include "blocks.h"
#include "struct.h"
#include "exec_nodes.h"
#include "directory.h"
#include "volume.h"
#include "disk.h"
#include "allocation.h"
#include "anodes.h"
#include "update.h"
#include "lru.h"
#include "ass.h"
#include "init.h"
#include "messages.h"
#include "SmartSwap.h"

#include "kswrapper.h"


#define DebugOn
#define DebugOff
#define DebugMsg(m)
#define DebugMsgNum(msg,num)
#define DebugMsgName(msg, name)


/*
 * ErrorMsg wrapper function
 */
LONG ErrorMsg(CONST_STRPTR msg, APTR arg, globaldata* g)
{
	LONG rv;
	rv = (g->ErrorMsg)(msg, arg, 1, g);
	if (!g->softprotect) {
		g->softprotect = 1;
	}
	if (g->currentvolume)
		g->currentvolume->numsofterrors++;

	return rv;
	return 0;
}

void FreeUnusedResources(struct volumedata *volume, globaldata *g)
{
  struct MinList *list;
  struct MinNode *node, *next;

	ENTER("FreeUnusedResources");

	/* check if volume passed */
	if (!volume)
		return;

	/* start with anblks!, fileentries are to be kept! */
	for (list = volume->anblks; list<=&volume->bmindexblks; list++)
	{
		node = (struct MinNode *)HeadOf(list);
		while ((next = node->mln_Succ))
		{
			FlushBlock((struct cachedblock *)node, g);
			FreeLRU((struct cachedblock *)node);
			node = next;
		}
	}
}


/**********************************************************************/
/*                             NEWVOLUME                              */
/*                             NEWVOLUME                              */
/*                             NEWVOLUME                              */
/**********************************************************************/


/* make and fill in volume structure
 * uses g->geom!
 * returns 0 is fails
 */
struct volumedata* MakeVolumeData(struct rootblock* rootblock, globaldata* g)
{
	struct volumedata* volume;
	struct MinList* list;

	ENTER("MakeVolumeData");

	volume = (struct volumedata* )malloc(sizeof(struct volumedata));

	volume->rootblk = rootblock;
	volume->rootblockchangeflag = false;

	/* lijsten initieren */
	for (list = &volume->fileentries; list <= &volume->anodechainlist; list++)
		NewList((struct List*)list);

	/* andere gegevens invullen */
	volume->numsofterrors = 0;
	volume->diskstate = ID_VALIDATED;

	/* these could be put in rootblock @@ see also HD version */
	volume->numblocks = g->geom.dg_TotalSectors >> g->blocklogshift;
	volume->bytesperblock = BLOCKSIZE;
	volume->rescluster = rootblock->reserved_blksize / volume->bytesperblock;

	/* Calculate minimum fake block size that keeps total block count less than 16M.
	 * Workaround for programs (including WB) that calculate free space using
	 * "in use * 100 / total" formula that overflows if in use is block count is larger
	 * than 16M blocks with 512 block size. Used only in ACTION_INFO.
	 */
	g->infoblockshift = 0;
	if (g->largeDiskSafeOS) {
		UWORD blockshift = 0;
		ULONG bpb = volume->bytesperblock;
		while (bpb > 512) {
			blockshift++;
			bpb >>= 1;
		}
		// Calculate smallest safe fake block size, up to max 32k. (512=0,1024=1,..32768=6)
		while ((volume->numblocks >> blockshift) >= 0x02000000 && g->infoblockshift < 6) {
			g->infoblockshift++;
			blockshift++;
		}
	}

	/* load rootblock extension (if it is present) */
	if (rootblock->extension && (rootblock->options & MODE_EXTENSION))
	{
		struct crootblockextension* rext;

		rext = (struct crootblockextension* )AllocBufmemR(sizeof(struct cachedblock) + rootblock->reserved_blksize, g);
		memset(rext, 0, sizeof(struct cachedblock) + rootblock->reserved_blksize);
		if (RawRead((UBYTE*)&rext->blk, volume->rescluster, rootblock->extension, g) != 0)
		{			
			ErrorMsg(AFS_ERROR_READ_EXTENSION, NULL, g);
			FreeBufmem(rext, g);
			rootblock->options ^= MODE_EXTENSION;
		}
		else
		{
			SmartSwap(&rext->blk);
			if (rext->blk.id == EXTENSIONID)
			{
				volume->rblkextension = rext;
				rext->volume = volume;
				rext->blocknr = rootblock->extension;
			}
			else
			{
				ErrorMsg(AFS_ERROR_EXTENSION_INVALID, NULL, g);
				FreeBufmem(rext, g);
				rootblock->options ^= MODE_EXTENSION;
			}
		}
	}
	else
	{
		volume->rblkextension = NULL;
	}

	return volume;
}

void DiskInsertSequence(struct rootblock* rootblock, globaldata* g)
{
	/* make new volumestructure for inserted volume */
	g->currentvolume = MakeVolumeData(rootblock, g);

	/* Reconfigure modules to new volume */
	InitModules(g->currentvolume, false, g);

	/* create rootblockextension if its not there yet */
	if (!g->currentvolume->rblkextension &&
		g->diskstate != ID_WRITE_PROTECTED)
	{
		//MakeRBlkExtension(g);
	}

#if DELDIR
	/* upgrade deldir */
	if (rootblock->deldir)
	{
		struct cdeldirblock* ddblk;
		int i, nr;

		/* kill current deldir */
		ddblk = (struct cdeldirblock*)AllocLRU(g);
		if (ddblk)
		{
			if (RawRead((UBYTE*)&ddblk->blk, RESCLUSTER, rootblock->deldir, g) == 0)
			{
				SmartSwap(&ddblk->blk, RESCLUSTER * g->blocksize);
				if (ddblk->blk.id == DELDIRID)
				{
					for (i = 0; i < 31; i++)
					{
						nr = ddblk->blk.entries[i].anodenr;
						if (nr)
							FreeAnodesInChain(nr, g);
					}
				}
			}
			FreeLRU((struct cachedblock*)ddblk);
		}

		/* create new deldir */
		SetDeldir(1, g);
		ResToBeFreed(rootblock->deldir, g);
		rootblock->deldir = 0;
		rootblock->options |= MODE_SUPERDELDIR;
	}
#endif

	/* update datestamp and enable */
	rootblock->options |= MODE_DATESTAMP;
	rootblock->datestamp++;
	g->dirty = true;

	EXIT("DiskInsertSequence");
}


/**********************************************************************/
/*                        OTHER VOLUMEROUTINES                        */
/*                        OTHER VOLUMEROUTINES                        */
/*                        OTHER VOLUMEROUTINES                        */
/**********************************************************************/

/* checks if disk is changed. If so calls NewVolume()
** NB: new volume might be NOVOLUME or NOTAFDSDISK
*/
void UpdateCurrentDisk(globaldata *g)
{
	
}

/* CheckVolume checks if a volume (ve lock) is (still) present.
** If volume==NULL (no disk present) then FALSE is returned (@XLII).
** result: requested volume present/not present TRUE/FALSE
*/
bool CheckVolume(struct volumedata *volume, bool write, SIPTR *error, globaldata *g)
{
	if(!volume || !g->currentvolume)
	{

		*error = PFS_ERROR_DEVICE_NOT_MOUNTED;
		return(false);
	}
	else if(g->currentvolume == volume)
	{
		switch(g->diskstate)
		{
			case ID_WRITE_PROTECTED:
				if(write)
				{
					*error = PFS_ERROR_DISK_WRITE_PROTECTED;
					return(false);
				}

			case ID_VALIDATING:
				if(write)
				{
					*error = PFS_ERROR_DISK_NOT_VALIDATED;
					return(false);
				}

			case ID_VALIDATED:
				if(write && g->softprotect)
				{
					*error = PFS_ERROR_DISK_WRITE_PROTECTED;
					return(false);
				}

			default:
				return(true);
		}
	}
	else
	{
		*error = PFS_ERROR_DEVICE_NOT_MOUNTED;
		return(false);
	}
}




/***********************************************************************/
/*                              LOWLEVEL                               */
/*                              LOWLEVEL                               */
/*                              LOWLEVEL                               */
/***********************************************************************/

/* Load the rootblock of the volume that is currently in the drive.
 * Returns FLASE if no disk or no PFS disk and TRU if it is a PFS disk.
 * Sets disktype in globaldata->disktype
 * Should NOT change globaldata->currentvolume
 *
 * Allocates space in 'rootblock' which is freed if an error occurs.
 */


static WORD IsRBBlock(UBYTE *rbpt)
{
	struct rootblock *rootblock = (struct rootblock*)rbpt;
	if (rootblock->disktype != ID_PFS_DISK && rootblock->disktype != ID_PFS2_DISK)
		return 0;
	if (rootblock->options == 0 || rootblock->reserved_blksize == 0)
		return -1; // boot block
	return 1; // root block
}


/* Don't show up a error requester. Used by GetCurrentRoot */
static LONG NoErrorMsg(CONST_STRPTR melding, APTR arg, ULONG dummy, globaldata* g)
{
	return 0;
}

bool GetCurrentRoot(struct rootblock** rootblock, globaldata* g)
{
	ULONG error;
	int rblsize;
	struct rootblock* rbp;
	UBYTE* rbpt;

	/* get drive geometry table (V4.3) */
	GetDriveGeometry(g);

	g->diskstate = ID_VALIDATED;

	/* check if disk is PFS disk */
	*rootblock = (struct rootblock*) AllocBufmemR(BLOCKSIZE * 2, g);
	rbp = *rootblock;
	rbpt = (UBYTE*)rbp;
	g->ErrorMsg = NoErrorMsg;   // prevent readerrormsg

#if ACCESS_DETECT
	/* Detect best access mode, TD32, TD64, NSD or DirectSCSI */
	if (!detectaccessmode((UBYTE*)*rootblock, g))
		goto nrd_error;
#endif

	// check if pre-v20 partition has >512 block size,
	// if detected, set block size to 512.
	WORD bsfix = 0;
	for (UWORD rbcnt = 0; rbcnt < 2; rbcnt++)
	{
		if (rbcnt == 1)
		{
			bsfix = -1;
		}
		// Read boot block
		error = RawRead(rbpt + BLOCKSIZE, 1, BOOTBLOCK1, g);
		if (error)
		{
			goto end_error;
		}
		SmartSwap(*((uint32_t*)(rbpt + BLOCKSIZE)));

		// Read root block
		error = RawRead(rbpt, 1, ROOTBLOCK, g);
		SmartSwap((struct rootblock*)rbpt);
		if (error)
		{
			goto end_error;
		}
		if (IsRBBlock(rbpt) > 0 && IsRBBlock(rbpt + BLOCKSIZE))
		{
			if (rbcnt == 1)
			{
				bsfix = 1;
			}
			break;
		}
		// Recheck with 512 byte block size if blocksize >512
		if (g->dosenvec.de_SectorPerBlock <= 1)
		{
			goto nrd_error;
		}
		CalculateBlockSize(g, 1, 0);
	}
	if (bsfix < 0) {
		CalculateBlockSize(g, 1, 0);
		goto nrd_error;
	}
	else if (bsfix > 0) {
		if (!InitDataCache(g))
		{
			goto nrd_error;
		}
	}

	g->ErrorMsg = _NormalErrorMsg;
	g->disktype = ID_PFS_DISK;

	/* check size and read all rootblock blocks */
	// 17.10: with 1024 byte blocks rblsize can be 1!
	rblsize = rbp->rblkcluster;
	if (rblsize < 1 || rblsize > 521)
	{
		goto nrd_error;
	}

	// original PFS_DISK with PFS2_DISK features -> don't mount
	if (rbp->disktype == ID_PFS_DISK && ((rbp->options & MODE_LARGEFILE) || (rbp->reserved_blksize > 1024)))
	{
		goto nrd_error;
	}

	if (!InitLRU(g, rbp->reserved_blksize))
	{
		goto nrd_error;
	}

	FreeBufmem(rbpt, g);
	*rootblock = (struct rootblock* )AllocBufmemR(rblsize << BLOCKSHIFT, g);
	rbp = *rootblock;
	rbpt = (UBYTE*)rbp;

	error = RawRead(rbpt, rblsize, ROOTBLOCK, g);
	if (error)
	{
		goto nrd_error;
	}
	SmartSwap((struct rootblock*)rbpt);

	return true;

nrd_error:
	g->ErrorMsg = _NormalErrorMsg;
	g->disktype = ID_NOT_REALLY_DOS;
	FreeBufmem(rbpt, g);
	*rootblock = NULL;
	return false;

end_error:
	if (g->diskstate != ID_WRITE_PROTECTED) g->diskstate = ID_VALIDATING;
	g->disktype = ID_UNREADABLE_DISK;
	FreeBufmem(rbpt, g);
	*rootblock = NULL;
	return false;
}

static void SetPartitionLimits(globaldata *g)
{
	g->firstblocknative = g->dosenvec.de_LowCyl * g->geom.dg_CylSectors;
	g->lastblocknative = (g->dosenvec.de_HighCyl + 1) * g->geom.dg_CylSectors;
	g->firstblock = g->firstblocknative >> g->blocklogshift;
	g->lastblock = g->lastblocknative >> g->blocklogshift;
	g->lastblocknative -= 1 << g->blocklogshift;
	g->lastblock--;
	
	g->maxtransfermax = 0x7ffffffe;
#if LIMIT_MAXTRANSFER
	if (g->scsidevice)
	{
		struct Library *d;
		Forbid();
		d = (struct Library*)FindName(&SysBase->DeviceList, "scsi.device");
		if (d && d->lib_Version >= 36 && d->lib_Version < OS_VERSION_SAFE_LARGE_DISK) 
		{
			/* A600/A1200/A4000 ROM scsi.device ATA spec max transfer bug workaround */
			g->maxtransfermax = LIMIT_MAXTRANSFER;
		}
		Permit();
	}
#endif
}

void CalculateBlockSize(globaldata *g, ULONG spb, ULONG blocksize)
{
	ULONG t;
	WORD i;
	ULONG bs;

	if (!spb)
	{
		spb = g->dosenvec.de_SectorPerBlock;
		if (!spb)
		{
			spb = 1;
		}
	}
	bs = g->dosenvec.de_SizeBlock << 2;
	if (!blocksize)
	{
		blocksize = bs * spb;
		if (blocksize >= 4096)
		{
			blocksize = 4096;
		} else if (blocksize >= 2048)
		{
			blocksize = 2048;
		}
		if (blocksize < bs)
		{
			blocksize = bs;
		}
	}	
    g->blocksize_phys = bs;
    g->blocksize = blocksize;
    g->blocklogshift = 0;
    while (bs < g->blocksize)
   	{
    	bs <<= 1;
    	g->blocklogshift++;
    }
    
	t = BLOCKSIZE;
	for (i=-1; t; i++)
	{
		t >>= 1;
	}
	g->blockshift = i;
	g->directsize = 16*1024>>i;

	SetPartitionLimits(g);
}

/* Get drivegeometry from diskdevice.
** If TD_GETGEOMETRY fails the DOSENVEC values are taken
** Dosenvec is taken into consideration
*/
void GetDriveGeometry(globaldata *g)
{
	SetPartitionLimits(g);
}


bool CheckCurrentVolumeBack (globaldata *g)
{
	bool ready = false;
  struct rootblock *rootblock;
  struct volumedata *volume = g->currentvolume;

	ready = GetCurrentRoot(&rootblock, g);
	if (rootblock)
		FreeBufmem (rootblock, g);
	return ready;
}
