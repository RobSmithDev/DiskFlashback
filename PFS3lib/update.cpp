
#include <ctype.h>
#include "debug.h"

#include "blocks.h"
#include "struct.h"
#include "update.h"
#include "volume.h"
#include "disk.h"
#include "allocation.h"
#include "anodes.h"
#include "lru.h"
#include "ass.h"
#include "init.h"
#include "messages.h"
#include "kswrapper.h"
#include "SmartSwap.h"
/*
 * prototypes
 */
 /* update bitmap and bitmap index blocks */


 // Writes to disk



static void RemoveEmptyABlocks(struct volumedata *volume, globaldata *g);
static void RemoveEmptyDBlocks(struct volumedata *volume, globaldata *g);
static void RemoveEmptyIBlocks(struct volumedata *volume, globaldata *g);
static void RemoveEmptySBlocks(struct volumedata *volume, globaldata *g);
static ULONG GetAnodeOfDBlk(struct cdirblock *blk, struct canode *anode, globaldata *g);
static bool IsFirstDBlk(struct cdirblock *blk, globaldata *g);
static bool IsEmptyABlk(struct canodeblock *ablk, globaldata *g);
static bool IsEmptyIBlk(struct cindexblock *blk, globaldata *g);
static bool UpdateList (struct cachedblock *blk, globaldata *g);
static void CommitReservedToBeFreed (globaldata *g);
static bool UpdateDirtyBlock (struct crootblockextension* blk, globaldata *g);

static void UpdateBlocknr(struct cachedblock *blk, ULONG newblocknr, globaldata *g);
static void UpdateABLK(struct cachedblock *, ULONG, globaldata *);
static void UpdateDBLK(struct cachedblock *, ULONG, globaldata *);
static void UpdateIBLK(struct cachedblock *, ULONG, globaldata *);
static void UpdateSBLK(struct cachedblock *, ULONG, globaldata *);
static void UpdateBMBLK(struct cachedblock *blk, ULONG newblocknr, globaldata *g);
static void UpdateBMIBLK(struct cachedblock *blk, ULONG newblocknr, globaldata *g);
#if VERSION23
static void UpdateRBlkExtension (struct cachedblock *blk, ULONG newblocknr, globaldata *g);
#endif
#if DELDIR
static void UpdateDELDIR (struct cachedblock *blk, ULONG newblocknr, globaldata *g);
#endif

/**********************************************************************/
/*                             UPDATEDISK                             */
/*                             UPDATEDISK                             */
/*                             UPDATEDISK                             */
/**********************************************************************/

#define IsFirstABlk(blk) (blk->blk.seqnr == 0)
#define IsFirstIBlk(blk) (blk->blk.seqnr == 0)
#define IsEmptyDBlk(blk) (FIRSTENTRY(blk)->next == 0)

/* indicates current state of update */
#define updateok g->updateok

/*
 * Snapshot to disk.
 * --> remove empty blocks
 * --> update freelist
 * --> save reserved blocks at new location, free the old
 *
 * Resultfalse/TRUE done nothing/volume updated
 */
bool UpdateDisk (globaldata *g)
{
  struct DateStamp time;
  struct volumedata *volume = g->currentvolume;
  bool success;
  ULONG i;

	ENTER("UpdateDisk");

	/*
	 * Do update
	 */
	if (volume && g->dirty && !g->softprotect)
	{
		g->uip = true;
		updateok = true;
		// OK - does not need any SWAPs
		UpdateDataCache (g);            /* flush DiskRead DiskWrite cache */

#if VERSION23
		/* make sure rootblockextension is reallocated */
		if (volume->rblkextension)
			MakeBlockDirty ((struct cachedblock *)volume->rblkextension, g);
#endif

		/* commit user space free list */
		UpdateFreeList(g);

		/* remove empty dir, anode, index and superblocks */
		RemoveEmptyDBlocks(volume, g);
		RemoveEmptyABlocks(volume, g);
		RemoveEmptyIBlocks(volume, g);
		RemoveEmptySBlocks(volume, g);

		// UpdateList's all write to disk
		/* update anode, dir, index and superblocks (not changed by UpdateFreeList) */
		for (i=0; i<=HASHM_DIR; i++)
			updateok &= UpdateList ((struct cachedblock *)HeadOf(&volume->dirblks[i]), g);
		for (i=0; i<=HASHM_ANODE; i++)
			updateok &= UpdateList ((struct cachedblock *)HeadOf(&volume->anblks[i]), g);
		updateok &= UpdateList ((struct cachedblock *)HeadOf(&volume->indexblks), g);
		updateok &= UpdateList ((struct cachedblock *)HeadOf(&volume->superblks), g);
#if DELDIR
		updateok &= UpdateList ((struct cachedblock *)HeadOf(&volume->deldirblks), g);
#endif

#if VERSION23
		if (volume->rblkextension)
		{
			struct crootblockextension *rext = volume->rblkextension;

			/* reserved roving and anode roving */
			rext->blk.reserved_roving = alloc_data.res_roving;
			rext->blk.rovingbit = alloc_data.rovingbit;
			rext->blk.curranseqnr = andata.curranseqnr;

			/* volume datestamp */
			DateTime(&time);
			rext->blk.volume_date[0] = (UWORD)time.ds_Days;
			rext->blk.volume_date[1] = (UWORD)time.ds_Minute;
			rext->blk.volume_date[2] = (UWORD)time.ds_Tick;
			rext->blk.datestamp = volume->rootblk->datestamp;

			// Writes to disk
			updateok &= UpdateDirtyBlock (rext, g);
		}
#endif


		/* commit reserved to be freed list */
		CommitReservedToBeFreed(g);

		/* update bitmap and bitmap index blocks */
		// Writes to disk
		updateok &= UpdateList ((struct cachedblock *)HeadOf(&volume->bmblks), g);
		updateok &= UpdateList ((struct cachedblock *)HeadOf(&volume->bmindexblks), g);

		/* update root (MUST be done last) */
		if (updateok)
		{
			const UWORD blk = volume->rootblk->rblkcluster;
			SmartSwap(volume->rootblk);
			RawWrite((UBYTE *)volume->rootblk, blk, ROOTBLOCK, g);
			SmartSwap(volume->rootblk);
			volume->rootblk->datestamp++;
			volume->rootblockchangeflag =false;

			success = true;
		}
		else
		{
			ErrorMsg (AFS_ERROR_UPDATE_FAIL, NULL, g);
			success =false;
		}

		g->uip =false;
	}
	else
	{
		if (volume && g->dirty && g->softprotect)
			ErrorMsg (AFS_ERROR_UPDATE_FAIL, NULL, g);

		success =false;
	}

	g->dirty =false;

	EXIT("UpdateDisk");
	return success;
}

/*
 * Empty Dirblocks 
 */
static void RemoveEmptyDBlocks(struct volumedata *volume, globaldata *g)
{
  struct cdirblock *blk, *next;
  struct canode anode;
  ULONG previous, i;

	for (i=0; i<=HASHM_DIR; i++)
	{
		for (blk = (cdirblock*)HeadOf(&volume->dirblks[i]); (next=blk->next); blk=next)
		{
			if (IsEmptyDBlk(blk) && !IsFirstDBlk(blk, g) && !ISLOCKED(blk) )
			{
				previous = GetAnodeOfDBlk(blk, &anode, g);
				RemoveFromAnodeChain(&anode, previous, blk->blk.anodenr, g);
				MinRemove(blk);
				FreeReservedBlock(blk->blocknr, g);
				ResToBeFreed(blk->oldblocknr, g);
				FreeLRU((struct cachedblock *)blk);
			}
		}
	}
}

static ULONG GetAnodeOfDBlk(struct cdirblock *blk, struct canode *anode, globaldata *g)
{
  ULONG prev = 0;

	GetAnode(anode, blk->blk.anodenr, g);
	while (anode->blocknr != blk->blocknr && anode->next)   //anode.next purely safety
	{
		prev = anode->nr;
		GetAnode(anode, anode->next, g);
	}

	return prev;
}

static bool IsFirstDBlk(struct cdirblock *blk, globaldata *g)
{
  bool first;
  struct canode anode;

	GetAnode(&anode, blk->blk.anodenr, g);
	first = (anode.blocknr == blk->blocknr);

	return first;
}


/*
 * Empty block check
 */
static void RemoveEmptyIBlocks(struct volumedata *volume, globaldata *g)
{
  struct cindexblock *blk, *next;

	for (blk = (cindexblock * )HeadOf(&volume->indexblks); (next=blk->next); blk=next)
	{
		if (blk->changeflag && !IsFirstIBlk(blk) && IsEmptyIBlk(blk,g) && !ISLOCKED(blk) )
		{
			UpdateIBLK((struct cachedblock *)blk, 0, g);
			MinRemove(blk);
			FreeReservedBlock(blk->blocknr, g);
			ResToBeFreed(blk->oldblocknr, g);
			FreeLRU((struct cachedblock *)blk);
		}
	}
}

static void RemoveEmptySBlocks(struct volumedata *volume, globaldata *g)
{
  struct cindexblock *blk, *next;

	for (blk = (cindexblock * )HeadOf(&volume->superblks); (next=blk->next); blk=next)
	{
		if (blk->changeflag && !IsFirstIBlk(blk) && IsEmptyIBlk(blk, g) && !ISLOCKED(blk) )
		{
			UpdateSBLK((struct cachedblock *)blk, 0, g);
			MinRemove(blk);
			FreeReservedBlock(blk->blocknr, g);
			ResToBeFreed(blk->oldblocknr, g);
			FreeLRU((struct cachedblock *)blk);
		}
	}
}

static void RemoveEmptyABlocks(struct volumedata *volume, globaldata *g)
{
  struct canodeblock *blk, *next;
  ULONG indexblknr, indexoffset, i;
  struct cindexblock *index;

	for (i=0; i<=HASHM_ANODE; i++)
	{
		for (blk = (canodeblock* )HeadOf(&volume->anblks[i]); (next=blk->next); blk=next)
		{
			if (blk->changeflag && !IsFirstABlk(blk) && IsEmptyABlk(blk, g) && !ISLOCKED(blk) )
			{
				indexblknr  = ((struct canodeblock *)blk)->blk.seqnr / andata.indexperblock;
				indexoffset = ((struct canodeblock *)blk)->blk.seqnr % andata.indexperblock;

				/* kill the block */
				MinRemove(blk);
				FreeReservedBlock(blk->blocknr, g);
				ResToBeFreed(blk->oldblocknr, g);
				FreeLRU((struct cachedblock *)blk);

				/* and remove the reference (this one should already be in the cache) */
				index = GetIndexBlock(indexblknr, g);
				DBERR(if (!index) ErrorTrace(5,"RemoveEmptyABlocks", "GetIndexBlock returned NULL!"));
				index->blk.index[indexoffset] = 0;
				index->changeflag = true;
			}
		}
	}
}

static bool IsEmptyABlk(struct canodeblock *ablk, globaldata *g)
{
  struct anode *anodes;
  ULONG j; 
  bool found = 0;

	/* zoek bezette anode */
	anodes = ablk->blk.nodes;
	for(j=0; j<andata.anodesperblock && !found; j++)
		found |= (anodes[j].blocknr != 0);

	found = !found;
	return found;       /* not found -> empty */
}

static bool IsEmptyIBlk(struct cindexblock *blk, globaldata *g)
{
  ULONG *index, i;
  bool found = 0;

	index = (ULONG*)blk->blk.index;
	for(i=0; i<andata.indexperblock; i++)
	{
		found = index[i] != 0;
		if (found)
			break;
	}

	found = !found;
	return found;
}

static bool UpdateList (struct cachedblock *blk, globaldata *g)
{
  ULONG error;
  struct cbitmapblock *blk2;

	if (!updateok)
		return false;

	while (blk->next)
	{
		if (blk->changeflag)
		{
			FreeReservedBlock (blk->oldblocknr, g);
			blk2 = (struct cbitmapblock *)blk;
			blk2->blk.datestamp = blk2->volume->rootblk->datestamp;
			blk->oldblocknr = 0;
			error = SmartRawWrite(blk, g);
			if (error)
				goto update_error;

			blk->changeflag =false;
		}

		blk=blk->next;
	}

	return true;

  update_error:
	ErrorMsg (AFS_ERROR_UPDATE_FAIL, NULL, g);
	return false;
}

static bool UpdateDirtyBlock (struct crootblockextension *blk, globaldata *g)
{
  ULONG error;

	if (!updateok)
		return false;

	if (blk->changeflag)
	{
		FreeReservedBlock (blk->oldblocknr, g);
		blk->oldblocknr = 0;
		// Swap it
		const ULONG blkNum = blk->blocknr;
		SmartSwap(&blk->blk);
		error = RawWrite ((UBYTE *)&blk->blk, RESCLUSTER, blkNum, g);
		// And swap it back
		SmartSwap(&blk->blk);
		if (error)
		{
			ErrorMsg (AFS_ERROR_UPDATE_FAIL, NULL, g);
			return false;
		}
	}

	blk->changeflag =false;
	return true;
}

static void CommitReservedToBeFreed (globaldata *g)
{
	int i;
	for (i=0;i<alloc_data.rtbf_index;i++)
	{
		if (alloc_data.reservedtobefreed[i])
		{
			FreeReservedBlock (alloc_data.reservedtobefreed[i], g);
			alloc_data.reservedtobefreed[i] = 0;
		}
	}

	alloc_data.rtbf_index = 0;
}

/* Check if an update is needed (see atomic.fw)
 * rtbf_threshold indicates how full the rtbf cache is allowed to be,
 * normally RTBF_THRESHOLD, RTBF_POSTPONED_TH for intermediate checks.
 */
void CheckUpdate (ULONG rtbf_threshold, globaldata *g)
{
	if (g->currentvolume &&
	   ((alloc_data.rtbf_index > rtbf_threshold) ||
	    (alloc_data.tobefreed_index > TBF_THRESHOLD) ||
		(g->rootblock->reserved_free < RESFREE_THRESHOLD + 5 + alloc_data.tbf_resneed)))
	{
		UpdateDisk (g);

		if (g->rootblock->reserved_free <= RESFREE_THRESHOLD)
			alloc_data.res_alert = true;
		else
			alloc_data.res_alert =false;
	}
}


/**********************************************************************/
/*                            MAKEBLOCKDIRTY                          */
/*                            MAKEBLOCKDIRTY                          */
/*                            MAKEBLOCKDIRTY                          */
/**********************************************************************/

/* --> part of update
 * marks a directory or anodeblock dirty. Nothing happens if it already
 * was dirty. If it wasn't, the block will be reallocated and marked dirty.
 * If the reallocation fails, an error is displayed.
 *
 * result: true = was clean;false: was already dirty
 * 
 * LOCKing the block until next packet proves to be too restrictive,
 * so unlock afterwards.
 */
bool MakeBlockDirty (struct cachedblock *blk, globaldata *g)
{
  ULONG blocknr;
  UWORD oldlock;

	if (!(blk->changeflag))
	{
		g->dirty = true;
		oldlock = blk->used;
		LOCK(blk);

		blocknr = AllocReservedBlock (g);
		if (blocknr)
		{
			blk->oldblocknr = blk->blocknr;
			blk->blocknr = blocknr;
			UpdateBlocknr (blk, blocknr, g);
		}
		else
		{
#ifdef BETAVERSION
			ErrorMsg(AFS_BETA_WARNING_2, NULL, g);
#endif
			blk->changeflag = true;
		}

		blk->used = oldlock;    // unlock block
		return true;
	}
	else
	{
		return false;
	}
}

static void UpdateBlocknr (struct cachedblock *blk, ULONG newblocknr, globaldata *g)
{
	switch (((UWORD *)blk->data)[0])
	{
		case DBLKID:    /* dirblock */
			UpdateDBLK (blk, newblocknr, g);
			break;

		case ABLKID:    /* anodeblock */
			UpdateABLK (blk, newblocknr, g);
			break;

		case IBLKID:    /* indexblock */
			UpdateIBLK (blk, newblocknr, g);
			break;

		case BMBLKID:   /* bitmapblock */
			UpdateBMBLK (blk, newblocknr, g);
			break;

		case BMIBLKID:  /* bitmapindexblock */
			UpdateBMIBLK (blk, newblocknr, g);
			break;

#if VERSION23
		case EXTENSIONID:   /* rootblockextension */
			UpdateRBlkExtension (blk, newblocknr, g);
			break;
#endif

#if DELDIR
		case DELDIRID:  /* deldir */
			UpdateDELDIR (blk, newblocknr, g);
			break;
#endif

		case SBLKID:	/* superblock */
			UpdateSBLK (blk, newblocknr, g);
			break;
	}
}


static void UpdateDBLK (struct cachedblock *blk, ULONG newblocknr, globaldata *g)
{
  struct cdirblock *dblk = (struct cdirblock *)blk;
  struct canode anode;
  ULONG oldblocknr = dblk->oldblocknr;

	LOCK(blk);

	/* get old anode (all 1-block anodes) */
	GetAnode (&anode, dblk->blk.anodenr, g);
	while ((anode.blocknr != oldblocknr) && anode.next) //anode.next purely safety
		GetAnode (&anode, anode.next, g);

	/* change it.. */
	if (anode.blocknr != oldblocknr)
	{
		DB(Trace(4, "UpdateDBLK", "anode.blocknr=%ld, dblk->blocknr=%ld\n",
			anode.blocknr, dblk->blocknr));
		ErrorMsg (AFS_ERROR_CACHE_INCONSISTENCY, NULL, g);
	}

	/* This must happen AFTER anode correction, because Update() could be called,
	 * causing trouble (invalid checkpoint: dirblock uptodate, anode not)
	 */
	blk->changeflag = true;
	anode.blocknr = newblocknr;
	SaveAnode(&anode, anode.nr, g);

	ReHash(blk, g->currentvolume->dirblks, HASHM_DIR);
}

static void UpdateABLK (struct cachedblock *blk, ULONG newblocknr, globaldata *g)
{
  struct cindexblock *index;
  ULONG indexblknr, indexoffset, temp;

	blk->changeflag = true;
	temp = ((struct canodeblock *)blk)->blk.seqnr;
	indexblknr  = temp / andata.indexperblock;
	indexoffset = temp % andata.indexperblock;

	/* this one should already be in the cache */
	index = GetIndexBlock(indexblknr, g);

	DBERR(if (!index) ErrorTrace(5,"UpdateABLK", "GetIndexBlock returned NULL!"));

	index->blk.index[indexoffset] = newblocknr;
	MakeBlockDirty ((struct cachedblock *)index, g);
	ReHash(blk, g->currentvolume->anblks, HASHM_ANODE);
}

static void UpdateIBLK(struct cachedblock *blk, ULONG newblocknr, globaldata *g)
{
  struct cindexblock *superblk;
  ULONG temp;

	blk->changeflag = true;
	if (g->supermode)
	{
		temp = divide (((struct cindexblock *)blk)->blk.seqnr, andata.indexperblock);
		superblk = GetSuperBlock (temp /* & 0xffff */, g);

		DBERR(if (!superblk) ErrorTrace(5,"UpdateIBLK", "GetSuperBlock returned NULL!"));

		superblk->blk.index[temp >> 16] = newblocknr;
		MakeBlockDirty ((struct cachedblock *)superblk, g);
	}
	else
	{
		blk->volume->rootblk->idx._small.indexblocks[((struct cindexblock *)blk)->blk.seqnr] = newblocknr;
		blk->volume->rootblockchangeflag = true;
	}
}

static void UpdateSBLK(struct cachedblock *blk, ULONG newblocknr, globaldata *g)
{
	blk->changeflag = true;
	blk->volume->rblkextension->changeflag = true;
	blk->volume->rblkextension->blk.superindex[((struct cindexblock *)blk)->blk.seqnr] = newblocknr;
}

static void UpdateBMBLK (struct cachedblock *blk, ULONG newblocknr, globaldata *g)
{
  struct cindexblock *indexblock;
  struct cbitmapblock *bmb = (struct cbitmapblock *)blk;
  ULONG temp;

	blk->changeflag = true;
	temp = divide (bmb->blk.seqnr, andata.indexperblock);
	indexblock = GetBitmapIndex (temp /* & 0xffff */, g);

	DBERR(if (!indexblock) ErrorTrace(5,"UpdateBMBLK", "GetBitmapIndex returned NULL!"));

	indexblock->blk.index[temp >> 16] = newblocknr;
	MakeBlockDirty ((struct cachedblock *)indexblock, g);   /* recursion !! */
}

/* validness of seqnr is checked when block is loaded */
static void UpdateBMIBLK (struct cachedblock *blk, ULONG newblocknr, globaldata *g)
{
	blk->changeflag = true;
	blk->volume->rootblk->idx.large.bitmapindex[((struct cindexblock *)blk)->blk.seqnr] = newblocknr;
	blk->volume->rootblockchangeflag = true;
}

#if VERSION23
static void UpdateRBlkExtension (struct cachedblock *blk, ULONG newblocknr, globaldata *g)
{
	blk->changeflag = true;
	blk->volume->rootblk->extension = newblocknr;
	blk->volume->rootblockchangeflag = true;
}
#endif

#if DELDIR
static void UpdateDELDIR (struct cachedblock *blk, ULONG newblocknr, globaldata *g)
{
	blk->changeflag = true;
	blk->volume->rblkextension->blk.deldir[((struct cdeldirblock *)blk)->blk.seqnr] = newblocknr;
	MakeBlockDirty((struct cachedblock *)blk->volume->rblkextension, g);
}
#endif

/* Update datestamp (copy from rootblock
 * Call before writing block (lru.c)
 */
void UpdateDatestamp (struct cachedblock *blk, globaldata *g)
{
  struct cdirblock *dblk = (struct cdirblock *)blk;
  struct crootblockextension *rext = (struct crootblockextension *)blk;

	switch (((UWORD *)blk->data)[0])
	{
		case DBLKID:    /* dirblock */
		case ABLKID:    /* anodeblock */
		case IBLKID:    /* indexblock */
		case BMBLKID:   /* bitmapblock */
		case BMIBLKID:  /* bitmapindexblock */
		case DELDIRID:  /* deldir */
		case SBLKID:	/* superblock */
			dblk->blk.datestamp = g->currentvolume->rootblk->datestamp;
			break;

		case EXTENSIONID:   /* rootblockextension */
			rext->blk.datestamp = g->currentvolume->rootblk->datestamp;
			break;
	}
}
