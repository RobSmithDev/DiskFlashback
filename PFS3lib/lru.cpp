#include "debug.h"
#include <math.h>

#include "exec_nodes.h"
#include "blocks.h"
#include "struct.h"
#include "volume.h"
#include "lru.h"
#include "directory.h"
#include "update.h"
#include "disk.h"
#include "allocation.h"
#include "messages.h"
#include "kswrapper.h"

/*
 * prototypes
 */

#define MIN_BUFFERS 10
#define MAX_BUFFERS 600
#define NEW_LRU_ENTRIES 5

/* Allocate LRU queue
*/
bool InitLRU (globaldata *g, UWORD reserved_blksize)
{
  int i, j;
  bool warned = false;
  UBYTE *array;

	ENTER("InitLRU");

	if (g->glob_lrudata.LRUarray && g->glob_lrudata.reserved_blksize == reserved_blksize)
		return true;

	DeallocLRU(g);

	g->glob_lrudata.reserved_blksize = reserved_blksize;

	NewList((struct List *)&g->glob_lrudata.LRUqueue);
	NewList((struct List *)&g->glob_lrudata.LRUpool);

	i = g->dosenvec.de_NumBuffers;

	/* sanity checks. If HDToolbox default of 30, then 150,
	 * otherwise round in range 70 -- 600
	 */
	if (i==30) i=150;
	if (i<MIN_BUFFERS) i = MIN_BUFFERS;
	if (i>MAX_BUFFERS) i = MAX_BUFFERS;
	g->dosenvec.de_NumBuffers = g->glob_lrudata.poolsize = i;
	g->uip = false;
	g->locknr = 1;

	g->glob_lrudata.LRUarray = (struct lru_cachedblock** )AllocVec(sizeof(struct lru_cachedblock*) * g->glob_lrudata.poolsize);
	if (!g->glob_lrudata.LRUarray)
		return false;
	for(j = 0; j < g->glob_lrudata.poolsize; j++) {
		g->glob_lrudata.LRUarray[j] = (struct lru_cachedblock* )AllocVec((sizeof(struct lru_cachedblock) + reserved_blksize));
		if (!g->glob_lrudata.LRUarray[j]) {
			DeallocLRU(g);
			return false;
		}		
	}

	array = (UBYTE *)g->glob_lrudata.LRUarray;
	for(i=0;i<g->glob_lrudata.poolsize;i++) {
		MinAddHead(&g->glob_lrudata.LRUpool, g->glob_lrudata.LRUarray[i]);
	}

	return true;
}

void DeallocLRU(globaldata *g)
{
	int j;
	if (g->glob_lrudata.LRUarray) {
		for(j = 0; j < g->glob_lrudata.poolsize; j++)
			FreeVec(g->glob_lrudata.LRUarray[j]);
	}
	FreeVec (g->glob_lrudata.LRUarray);
	g->glob_lrudata.LRUarray = NULL;
}


/* Allocate a block from the LRU chain and make
** it current LRU.
** Returns NULL if none available
*/
struct cachedblock *AllocLRU (globaldata *g)
{
  struct lru_cachedblock *lrunode;
  struct lru_cachedblock **nlru;
  ULONG error;
  int retries = 0;
  int j;

	ENTER("AllocLRU");

	if (g->glob_lrudata.LRUarray == NULL)
		return NULL;

	/* Use free block from pool or flush lru unused
	** block (there MUST be one!)
	*/
retry:
	if (IsMinListEmpty(&g->glob_lrudata.LRUpool))
	{
		for (lrunode = (struct lru_cachedblock *)g->glob_lrudata.LRUqueue.mlh_TailPred; lrunode->prev; lrunode = lrunode->prev)
		{
			/* skip locked blocks */
			if (ISLOCKED(&lrunode->cblk))
				continue;

			if (lrunode->cblk.changeflag)
			{
				DB(Trace(1,"AllocLRU","ResToBeFreed %lx\n",&lrunode->cblk));
				ResToBeFreed(lrunode->cblk.oldblocknr, g);
				UpdateDatestamp(&lrunode->cblk, g);

				error = SmartRawWrite(&lrunode->cblk, g);
				
				if (error) {
					ULONG args[2];
					args[0] = lrunode->cblk.blocknr;
					args[1] = error;
					ErrorMsg (AFS_ERROR_LRU_UPDATE_FAIL, args, g);
				}
			}

			FlushBlock(&lrunode->cblk, g);
			goto ready;
		}
	}
	else
	{
		lrunode = (lru_cachedblock*)HeadOf(&g->glob_lrudata.LRUpool);
		goto ready;
	}

	/* Attempt to allocate new entries */
	nlru = (lru_cachedblock * *)AllocVec(sizeof(struct lru_cachedblock*) * (g->glob_lrudata.poolsize + NEW_LRU_ENTRIES));
	for (j = 0; j < NEW_LRU_ENTRIES; j++) {
		if (!nlru)
			break;
		nlru[j + g->glob_lrudata.poolsize] = (lru_cachedblock *)AllocVec((sizeof(struct lru_cachedblock) + SIZEOF_RESBLOCK));
		if (!nlru[j + g->glob_lrudata.poolsize]) {
			while (j >= 0) {
				FreeVec(nlru[j + g->glob_lrudata.poolsize]);
				j--;
			}
			FreeVec(nlru);
			nlru = NULL;
		}
	}
	if (!nlru) {
		/* No suitable block found -> we are in trouble */
		NormalErrorMsg (AFS_ERROR_OUT_OF_BUFFERS, NULL, 1);
		retries++;
		if (retries > 3)
			return NULL;
		goto retry;
	}
	CopyMem(g->glob_lrudata.LRUarray, nlru, sizeof(struct lru_cachedblock*) * g->glob_lrudata.poolsize);
	FreeVec(g->glob_lrudata.LRUarray);
	g->glob_lrudata.LRUarray = nlru;
	for (j = 0; j < NEW_LRU_ENTRIES; j++, g->glob_lrudata.poolsize++) {
		MinAddHead(&g->glob_lrudata.LRUpool, g->glob_lrudata.LRUarray[g->glob_lrudata.poolsize]);
	}
	g->dosenvec.de_NumBuffers = g->glob_lrudata.poolsize;
	goto retry;

ready:
	MinRemove(lrunode);
	MinAddHead(&g->glob_lrudata.LRUqueue, lrunode);

	DB(Trace(1,"AllocLRU","Allocated block %lx\n", &lrunode->cblk));

	//  LOCK(&lrunode->cblk);
	return &lrunode->cblk;
}


/* Adds a block to the ReservedToBeFreedCache
 */
void ResToBeFreed(ULONG blocknr, globaldata *g)
{
	/* bug 00116, 13 June 1998 */
	if (blocknr)
	{
		/* check if cache has space left */
		if (alloc_data.rtbf_index < alloc_data.rtbf_size)
		{
			alloc_data.reservedtobefreed[alloc_data.rtbf_index++] = blocknr;
		}
		else
		{
			/* reallocate cache */
			ULONG newsize = alloc_data.rtbf_size ? alloc_data.rtbf_size * 2 : RTBF_CACHE_SIZE;
			ULONG *newbuffer = (ULONG*)malloc(sizeof(*newbuffer) * newsize);
			if (newbuffer)
			{
				if (alloc_data.reservedtobefreed)
				{
					CopyMem(alloc_data.reservedtobefreed, newbuffer, sizeof(*newbuffer) * alloc_data.rtbf_index);
					free(alloc_data.reservedtobefreed);
				}
				alloc_data.reservedtobefreed = newbuffer;
				alloc_data.rtbf_size = newsize;
				alloc_data.reservedtobefreed[alloc_data.rtbf_index++] = blocknr;
				return;
			}

			/* this should never happen */
			DB(Trace(10,"ResToBeFreed","reserved to be freed cache full\n"));
#ifdef BETAVERSION
			ErrorMsg (AFS_BETA_WARNING_1, NULL, g);
#endif
			/* hope nobody allocates this block before the disk has been
			 * updated
			 */
			FreeReservedBlock (blocknr, g);
		}
	}
}


/* Makes a cached block ready for reuse:
** - Remove from queue
** - (dirblock) Decouple all references to the block
** - wipe memory
** NOTE: NOT REMOVED FROM LRU!
*/
void FlushBlock (struct cachedblock *block, globaldata *g)
{
    lockentry_t *le;

	DB(Trace(10,"FlushBlock","Flushing block %lx\n", block->blocknr));

	/* remove block from blockqueue */
	MinRemove(block);

	/* decouple references */
	if (IsDirBlock(block))
	{
		/* check fileinfo references */
		for (le = (lockentry_t *)HeadOf(&block->volume->fileentries); le->le.next; le = (lockentry_t *)le->le.next)
		{
			/* only dirs and files have fileinfos that need to be updated,
			** but the volume * pointer of volumeinfos never points to
			** a cached block, so the type != ETF_VOLUME check is not
			** necessary. Just check the dirblockpointer
			*/
			if (le->le.info.file.dirblock == (struct cdirblock *)block)
			{
				le->le.dirblocknr = block->blocknr;
				le->le.dirblockoffset = (UBYTE *)le->le.info.file.direntry - (UBYTE *)block;
#if DELDIR
				le->le.info.deldir.special = SPECIAL_FLUSHED;  /* flushed reference */
#else
				le->le.info.direntry = NULL;
#endif
				le->le.info.file.dirblock = NULL;
			}

			/* exnext references */
			if (le->le.type.flags.dir && le->nextentry.dirblock == (struct cdirblock *)block)
			{
				le->nextdirblocknr = block->blocknr;
				le->nextdirblockoffset = (UBYTE *)le->nextentry.direntry - (UBYTE *)block;
#if DELDIR
				le->nextentry.direntry = (struct direntry *)SPECIAL_FLUSHED;
#else
				le->nextentry.direntry = NULL;
#endif
				le->nextentry.dirblock = NULL;
			}
		}
	}

	/* wipe memory */
	memset(block, 0, SIZEOF_CACHEDBLOCK);
}

/* updates references of listentries to dirblock
*/
void UpdateReference (ULONG blocknr, struct cdirblock *blk, globaldata *g)
{
  lockentry_t *le;

	DB(Trace(1,"UpdateReference","block %lx\n", blocknr));

	for (le = (lockentry_t *)HeadOf(&blk->volume->fileentries); le->le.next; le = (lockentry_t *)le->le.next)
	{
		/* ignoring the fact that not all objectinfos are fileinfos, but the
		** 'volumeinfo.volume' and 'deldirinfo.deldir' fields never are NULL anyway, so ...
		** maybe better to check for SPECIAL_FLUSHED
		*/
		if (le->le.info.file.dirblock == NULL && le->le.dirblocknr == blocknr)
		{
			le->le.info.file.dirblock = blk;
			le->le.info.file.direntry = (struct direntry *)((UBYTE *)blk + le->le.dirblockoffset);
			le->le.dirblocknr =
			le->le.dirblockoffset = 0;
		}

		/* exnext references */
		if (le->le.type.flags.dir && le->nextdirblocknr == blocknr)
		{
			le->nextentry.dirblock = blk;
			le->nextentry.direntry = (struct direntry *)((UBYTE *)blk + le->nextdirblockoffset);
			le->nextdirblocknr =
			le->nextdirblockoffset = 0;
		}
	}
}

/* Updates objectinfo of a listentry (if necessary)
 * This function only reloads the flushed directory block referred to. The
 * load directory block routine will actually restore the reference.
 */
void UpdateLE (listentry_t *le, globaldata *g)
{
	//DB(Trace(1,"UpdateLE","Listentry %lx\n", le));

	/* don't update volumeentries or deldirs!! */
#if DELDIR
	if (!le || le->info.deldir.special <= SPECIAL_DELFILE)
#else
	if (!le || IsVolumeEntry(le))
#endif
		return;

	if (le->dirblocknr)
		LoadDirBlock (le->dirblocknr, g);

	MakeLRU (le->info.file.dirblock);
	LOCK(le->info.file.dirblock);
}

void UpdateLE_exa (lockentry_t *le, globaldata *g)
{
	//DB(Trace(1,"UpdateLE_exa","LE %lx\n", le));

	if (!le) return;

	if (le->le.type.flags.dir)
	{
#if DELDIR
		if (IsDelDir(le->le.info))
			return;
#endif

		if (le->nextdirblocknr)
			LoadDirBlock (le->nextdirblocknr, g);

		if (le->nextentry.dirblock)
		{
			MakeLRU (le->nextentry.dirblock);
			LOCK(le->nextentry.dirblock);
		}
	}
}

/*
 * Cache check ..
 * The 'mask' is used as a fast modulo operator for the hash table size.
 */

struct cachedblock *CheckCache (struct MinList *list, UWORD mask, ULONG blocknr, globaldata *g)
{
  struct cachedblock *block;

	for (block = (cachedblock * )HeadOf(&list[(blocknr/2)&mask]); block->next; block=block->next)
	{
		if (block->blocknr == blocknr)
		{
			MakeLRU(block);
			return block;
		}
	}

	return NULL;
}

