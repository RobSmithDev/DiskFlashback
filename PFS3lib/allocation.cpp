#include <string.h>
#include "debug.h"
#include <limits.h>
#include <math.h>

/*
 * Own includes
 */
#include "blocks.h"
#include "struct.h"
#include "allocation.h"
#include "update.h"
#include "anodes.h"
#include "disk.h"
#include "lru.h"
#include "volume.h"
#include "directory.h"
#include "messages.h"
#include "kswrapper.h"
#include "ass.h"
#include "SmartSwap.h"
/*
 * Contents
 */
static struct cindexblock *NewBitmapIndexBlock(UWORD , globaldata * );

/*
 * Get bitmapblock seqnr
 */

/*
 * InitAllocation
 *
 * currentvolume has to be ok.
 */
void InitAllocation (struct volumedata *volume, globaldata *g)
{
  ULONG t;
  rootblock_t *rootblock = volume->rootblk;

	if (g->harddiskmode)
	{
		alloc_data.clean_blocksfree = rootblock->blocksfree;
		alloc_data.alloc_available = rootblock->blocksfree - rootblock->alwaysfree;
		alloc_data.longsperbmb = LONGS_PER_BMB;

		t = (volume->numblocks - (rootblock->lastreserved + 1) + 31)/32;
		t = (t+LONGS_PER_BMB-1)/LONGS_PER_BMB;
		alloc_data.no_bmb = t;
		alloc_data.bitmapstart = rootblock->lastreserved + 1;
		memset (alloc_data.tobefreed, 0, TBF_CACHE_SIZE*2*sizeof(ULONG));
		alloc_data.tobefreed_index = 0;
		alloc_data.tbf_resneed = 0;
		alloc_data.res_bitmap = (bitmapblock_t *)(rootblock+1);   /* bitmap directly behind rootblock */

		if (volume->rblkextension)
		{
			if (!(rootblock->options & MODE_EXTROVING))
			{
				rootblock->options |= MODE_EXTROVING;
				g->dirty = true;
				volume->rblkextension->blk.reserved_roving *= 32;
			}
			alloc_data.res_roving = volume->rblkextension->blk.reserved_roving;
			alloc_data.rovingbit = volume->rblkextension->blk.rovingbit;
		} 
		else
		{
			alloc_data.res_roving = 0;
			alloc_data.rovingbit = 0;
		}

		alloc_data.numreserved = (rootblock->lastreserved - rootblock->firstreserved + 1)/(volume->rescluster);
		alloc_data.reservedtobefreed = NULL;
		alloc_data.rtbf_size = 0;
		alloc_data.rtbf_index = 0;
		alloc_data.res_alert = 0;
	}	
}


/*
 * AllocateBlocks
 */
bool AllocateBlocks (ULONG anodenr, ULONG size, globaldata *g)
{
  struct anodechain *ac;
  bool success;

	if (!(ac = GetAnodeChain (anodenr, g)))
		return false;

	success = AllocateBlocksAC (ac, size, NULL, g);
	DetachAnodeChain (ac, g);
	return success;
}

/*
 * AllocateBlocksAC
 * Allocate blocks to end of (cached) anodechain.
 * If ref != NULL then online directory update enabled.
 * Make sure the state is valid before you call this function!
 * Returns success
 * if FAIL, then a part of the needed blocks could already have been allocated
 */
bool AllocateBlocksAC (struct anodechain *achain, ULONG size, struct fileinfo *ref, globaldata *g)
{
  ULONG nr, field, i, j, blocknr, blocksdone = 0;
  ULONG extra;
  FSIZE oldfilesize = 0;
  ULONG bmseqnr;
  UWORD bmoffset, oldlocknr;
  cbitmapblock_t *bitmap;
  struct anodechainnode *chnode;
  struct volumedata *vol = g->currentvolume;
  bool extend = false, updateroving = true;


	ENTER("AllocateBlocksAC");

	/* Check if allocation possible */
	if(alloc_data.alloc_available < size)
		return false;
	
	/* check for sufficient clean freespace (freespace that doesn't overlap
	 * with the current state on disk)
	 */
	if (alloc_data.clean_blocksfree < size)
		UpdateDisk (g);

#if VERSION23
	/* remember filesize in order to be able to cancel */
	if (ref)
		oldfilesize = GetDEFileSize(ref->direntry, g);
#endif

	/* count number of fragments and decide on fileextend preallocation
	 * get anode to expand
	 */
	chnode = &achain->head;
	for (i=0; chnode->next; i++)
		chnode = chnode->next;

	extra = min (256, i*8);
	if (chnode->an.blocknr && (chnode->an.blocknr != -1))
	{
		i = chnode->an.blocknr + chnode->an.clustersize - alloc_data.bitmapstart;
		nr = i/32;
		i %= 32;
		j = 1<<(31-i);
		bmseqnr = nr/alloc_data.longsperbmb;
		bmoffset = nr%alloc_data.longsperbmb;
		bitmap = GetBitmapBlock (bmseqnr, g);
		field = bitmap->blk.bitmap[bmoffset];

		/* block directly behind file free ? */
		if (field & j)
		{
			extend = true;

			/* if the position we want to allocate does not corresponds to the
			 * rovingpointer, the rovingpointer should not be updated
			 */
			if (nr != g->rootblock->roving_ptr)
				updateroving = false;
		}
	}
	

	/* Get bitmap to allocate from */
	if (!extend)
	{
		nr = g->rootblock->roving_ptr;
		bmseqnr = nr/alloc_data.longsperbmb;
		bmoffset = nr%alloc_data.longsperbmb;
		i = alloc_data.rovingbit;
		j = 1<<(31-i);
	}

	/* Allocate */
	while (size)
	{
		/* scan all bitmapblocks */
		bitmap = GetBitmapBlock(bmseqnr, g);
		if (!bitmap) {
			return false;
		}
		oldlocknr = bitmap->used;

		/* find all empty fields */
		while (bmoffset < alloc_data.longsperbmb)
		{
			field = bitmap->blk.bitmap[bmoffset];
			if (field)
			{
				/* take all empty bits */
				for ( ; i<32; j>>=1, i++)
				{
					if (field & j)
					{
						/* block is available, calc blocknr */
						blocknr = (bmseqnr*alloc_data.longsperbmb + bmoffset)*32 + i + 
								alloc_data.bitmapstart;

						/* check in range */
						if (blocknr >= vol->numblocks)
						{
							bmoffset = alloc_data.longsperbmb;
							continue;
						}

						/* take block */
						else
						{
							/* uninitialized anode */
							if (chnode->an.blocknr == -1)
							{
								chnode->an.blocknr = blocknr;
								chnode->an.clustersize = 0;
								chnode->an.next = 0;
							}
								
							/* check blockconnect */
							else if (!(chnode->an.blocknr + chnode->an.clustersize == blocknr))
							{
							  ULONG anodenr;

								LOCK(bitmap);

								if (!(chnode->next = (struct anodechainnode*)AllocMemP (sizeof(struct anodechainnode), g)))
								{
#if VERSION23
									if (ref)
									{
										SetDEFileSize(ref->direntry, oldfilesize, g);
										MakeBlockDirty ((struct cachedblock *)ref->dirblock, g);
									}
#endif
									/* undo allocation so far */
									FreeBlocksAC (achain, blocksdone, freeanodes, g);
									return false;
								}

								anodenr = AllocAnode (chnode->an.nr, g);  /* should not go wrong! */
								chnode->an.next = anodenr; 
								SaveAnode (&chnode->an, chnode->an.nr, g);
								chnode = chnode->next;
								chnode->an.nr = anodenr;
								chnode->an.blocknr = blocknr;
								chnode->an.clustersize = 0;
								chnode->an.next = 0;
							}

							bitmap->blk.bitmap[bmoffset] &= ~j;   /* remove block from freelist */
							chnode->an.clustersize++;  	   /* to file  	  	  */
							MakeBlockDirty ((struct cachedblock *)bitmap, g);

							/* update counters */
							alloc_data.clean_blocksfree--;
							alloc_data.alloc_available--;
							blocksdone++;

							/* update reference */
							if (ref)
							{
								SetDEFileSize(ref->direntry, GetDEFileSize(ref->direntry, g) + BLOCKSIZE, g);
								if (IsUpdateNeeded(RTBF_POSTPONED_TH))
								{
									/* make state valid and update disk */
									MakeBlockDirty ((struct cachedblock *)ref->dirblock, g);
									SaveAnode (&chnode->an, chnode->an.nr, g);
									UpdateDisk (g);

									/* abort if running out of reserved blocks */
									if (g->rootblock->reserved_free <= RESFREE_THRESHOLD)
									{
#if VERSION23
										SetDEFileSize(ref->direntry, oldfilesize, g);
										MakeBlockDirty ((struct cachedblock *)ref->dirblock, g);
#endif
										FreeBlocksAC (achain, blocksdone, freeanodes, g);
										return false;
									}
								}
							}


							if (!--size)
								goto alloc_end;
						}
					}
				}

				i = 0;
				j = 1<<31;
			}
			bmoffset++;
		}

		bitmap->used = oldlocknr;

		/* get ready for next block */
		bmseqnr = (bmseqnr+1)%(alloc_data.no_bmb);
		bmoffset = 0;
	}
	
alloc_end:

	/* finish by saving anode and updating roving ptr */
	SaveAnode (&chnode->an, chnode->an.nr, g);

	/* add fileextension preallocation */
	if (updateroving)
	{
		if (extend)
		{
			i += extra;
			alloc_data.rovingbit = i%32;
			bmoffset += i/32;
			if (bmoffset >= alloc_data.longsperbmb)
			{
				bmoffset -= alloc_data.longsperbmb;
				bmseqnr = (bmseqnr+1)%(alloc_data.no_bmb);
			}
		}
		else
		{
			alloc_data.rovingbit = i;
		}

		g->rootblock->roving_ptr = bmseqnr*alloc_data.longsperbmb + bmoffset;
	}

	EXIT ("AllocateBlocksAC");
	return true;
}


/*
 * Free all blocks in an anodechain? Use FreeBlocksAC(achain,ULONG_MAX,freetype,g)
 */

/*
 * Frees blocks allocated with AllocateBlocks. Freed blocks are added
 * to tobefreed list, and are not actually freed until UpdateFreeList
 * is called. Frees from END of anodelist.
 *
 * 'freetype' specifies if the anodes involved should be freed (freeanodes) or
 * not (keepanodes). In keepanodes mode the whole file (size >= filesize) should
 * be deleted, since otherwise the anodes cannot consistently be kept (dual
 * definition). In freeanodes mode the leading anode is not freed.
 *
 * VERSION23: uses tobedone fields. Because freeing blocks is idem-potent, fully
 * repeating an interrupted operation after reboot is ok. The blocks should not
 * be added to the blocksfree counter twice, however (see DoPostoned())
 * 
 * -> all references that indicate the freed blocks must have been
 *  done (atomically).
 */
static void RestoreAnodeChain (struct anodechain *achain, bool empty, struct anodechainnode *tail, globaldata *g);
void FreeBlocksAC (struct anodechain *achain, ULONG size, enum freeblocktype freetype, globaldata *g)
{
  struct anodechainnode *chnode, *tail;
  ULONG freeing;
  UWORD i, t = 0;
  bool empty = false;
  struct crootblockextension *rext;
  ULONG blocksdone = 0;

	ENTER("FreeBlocksAC");

	i = alloc_data.tobefreed_index;
	tail = NULL;

	/* store operation tobedone */
	rext = g->currentvolume->rblkextension;
	if (rext)
	{
		if (freetype == keepanodes)
			rext->blk.tobedone.operation_id = PP_FREEBLOCKS_KEEP;
		else
			rext->blk.tobedone.operation_id = PP_FREEBLOCKS_FREE;

		rext->blk.tobedone.argument1 = achain->head.an.nr;
		rext->blk.tobedone.argument2 = size;
		rext->blk.tobedone.argument3 = 0;     /* blocks done (FREEBLOCKS_KEEP) */
		MakeBlockDirty ((struct cachedblock *)rext, g);
	}

	/* check if tobefreedcache is sufficiently large,
	 * otherwise updatedisk
	 */
	for (chnode = &achain->head; chnode->next; t++)
		chnode = chnode->next;

	if ((i>0) && (t+i >= TBF_CACHE_SIZE-1))
	{
		UpdateDisk (g);
		i = alloc_data.tobefreed_index;
	}

	if (size)
		goto l1;

	/* reverse order freeloop */
	while (size && !empty)
	{
		/* Get chainnode to free from */
		chnode = &achain->head;
		while (chnode->next != tail)
			chnode = chnode->next;

l1:   /* get blocks to free */
		if (chnode->an.clustersize <= size)
		{
			freeing = chnode->an.clustersize;
			chnode->an.clustersize = 0;

			tail = chnode;
			empty = tail == &achain->head;
		}
		else
		{
			/* anode is partially freed;
			 * should only be possible in freeanodes mode 
			 */
			freeing = size;
			chnode->an.clustersize -= size;
			chnode->an.next = 0;
			if (freetype == freeanodes)
				SaveAnode (&chnode->an, chnode->an.nr, g);
		}

		/* and put them in the tobefreed list */
		if (freeing)
		{
			alloc_data.tobefreed[i][TBF_BLOCKNR] = chnode->an.blocknr + chnode->an.clustersize;
			alloc_data.tobefreed[i++][TBF_SIZE] = freeing;
			alloc_data.tbf_resneed += 3 + freeing/(32*alloc_data.longsperbmb);
			alloc_data.alloc_available += freeing;
			size -= freeing;
			blocksdone += freeing;

			/* free anode if it is empty, we're supposed to and it is not the head */
			if (!empty && freetype == freeanodes && !chnode->an.clustersize)
			{
				FreeAnode (chnode->an.nr, g);
				FreeMemP (chnode, g);
			}
		}

		/* check if intermediate update is needed 
		 * (tobefreed cache full, low on reserved blocks etc)
		 */
		if (i>=TBF_CACHE_SIZE || IsUpdateNeeded(RTBF_POSTPONED_TH))
		{
			alloc_data.tobefreed_index = i;
			g->dirty = true;
			if (rext && freetype == freeanodes)
			{
				/* make anodechain consistent */
				RestoreAnodeChain (achain, empty, tail, g);
				tail = NULL;

				/* postponed op: finish operation later */
				rext->blk.tobedone.argument2 = size;
			}
			else
				/* postponed op: repeat operation later, but don't increase blocks free twice */
				rext->blk.tobedone.argument3 = blocksdone;

			MakeBlockDirty ((struct cachedblock *)rext, g);
			UpdateDisk (g);
			i = alloc_data.tobefreed_index;
		}
	}

	/* restore anode chain (both cached and on disk) */
	if (freetype == freeanodes)
		RestoreAnodeChain (achain, empty, tail, g);

	/* cancel posponed operation */
	if (rext)
	{
		rext->blk.tobedone.operation_id = 0;
		rext->blk.tobedone.argument1 = 0;
		rext->blk.tobedone.argument2 = 0;
		rext->blk.tobedone.argument3 = 0;
		MakeBlockDirty ((struct cachedblock *)rext, g);
	}

	/* update tobefreed index */
	g->dirty = true;
	alloc_data.tobefreed_index = i;

	EXIT ("FreeBlocksAC");
}


/* local function of FreeBlocksAC
 * restore anodechain (freeanode mode only)
 */
static void RestoreAnodeChain (struct anodechain *achain, bool empty,
	struct anodechainnode *tail, globaldata *g)
{
  struct anodechainnode *chnode;

	if (empty)
	{
		achain->head.next = NULL;
		achain->head.an.clustersize = 0;
		achain->head.an.blocknr = ~0L;
		achain->head.an.next = 0;
		SaveAnode (&achain->head.an, achain->head.an.nr, g);
	}
	else
	{
		chnode = &achain->head;
		while (chnode->next != tail)
			chnode = chnode->next;

		chnode->next = NULL;
		chnode->an.next = 0;
		SaveAnode (&chnode->an, chnode->an.nr, g);
	}
}

/*
 * Update bitmap
 */
void UpdateFreeList (globaldata *g)
{
  cbitmapblock_t *bitmap = 0;
  UWORD i;
  ULONG longnr, blocknr, bmseqnr, newbmseqnr, bmoffset, bitnr;

	/* sort the free list */
	// not done right now

	/* free all blocks in list */
	bmseqnr = ~0;
	for (i=0; i<alloc_data.tobefreed_index; i++)
	{
		for ( blocknr = alloc_data.tobefreed[i][TBF_BLOCKNR];
			  blocknr < alloc_data.tobefreed[i][TBF_SIZE] + alloc_data.tobefreed[i][TBF_BLOCKNR];
			  blocknr++ )
		{
			/* now free block blocknr */
			bitnr = blocknr - alloc_data.bitmapstart;
			longnr = bitnr/32;
			newbmseqnr = longnr/alloc_data.longsperbmb;
			bmoffset = longnr%alloc_data.longsperbmb;
			if(newbmseqnr != bmseqnr)
			{
				bmseqnr = newbmseqnr;
				bitmap = GetBitmapBlock (bmseqnr, g);
			}
			if (bitmap) {
				bitmap->blk.bitmap[bmoffset] |= (1 << (31 - (bitnr % 32)));
				MakeBlockDirty((struct cachedblock*)bitmap, g);
			}
		}

		alloc_data.clean_blocksfree += alloc_data.tobefreed[i][TBF_SIZE];
	}

	/* update global data */
	/* alloc_data.alloc_available should already be equal blocksfree - alwaysfree */
	alloc_data.tobefreed_index = 0;
	alloc_data.tbf_resneed = 0;
	g->rootblock->blocksfree = alloc_data.clean_blocksfree;
	g->currentvolume->rootblockchangeflag = true;
}

/*
 * AllocReservedBlock
 */

ULONG AllocReservedBlock (globaldata *g)
{
  struct volumedata *vol = g->currentvolume;
  ULONG *bitmap = alloc_data.res_bitmap->bitmap;
  ULONG *free = &g->rootblock->reserved_free;
  ULONG blocknr;
  LONG i, j;

  ENTER("AllocReservedBlock");

  /* Check if allocation possible 
   * (really necessary?)
   */
	if (*free == 0)
  	return 0;

  j = 31 - alloc_data.res_roving % 32;
  for (i = alloc_data.res_roving / 32; i < ((alloc_data.numreserved + 31)/32); i++, j=31)
  {
		if (bitmap[i] != 0)
		{
		  ULONG field = bitmap[i];
		  for ( ;j >= 0; j--)
		  {
				if (field & (1 << j))
				{
				  blocknr = g->rootblock->firstreserved + (i*32+(31-j))*vol->rescluster;
				  if (blocknr <= g->rootblock->lastreserved) 
				  {
						bitmap[i] &= ~(1 << j);
						g->currentvolume->rootblockchangeflag = true;
						g->dirty = true;
						(*free)--;
						alloc_data.res_roving = 32*i + (31 - j);
						DB(Trace(10,"AllocReservedBlock","allocated %ld\n", blocknr));
						return blocknr;
				  }
				}
		  }
		}
  }

  /* end of bitmap reached. Reset roving pointer and try again 
  */
  if (alloc_data.res_roving)
  {
		alloc_data.res_roving = 0;
		return AllocReservedBlock (g);
  }
  else
		return 0;

  EXIT("AllocReservedBlock");
}


#if 0
/*
 * Allocates a reserved block while keeping at least 
 * RESERVED_BUFFER reserved blocks free
 */
ULONG AllocReservedBlockSave (globaldata *g)
{
  ULONG free = g->rootblock->reserved_free;
  ULONG blocknr;

	/* dirty blocks claim two blocks, so */
	if (free + g->blocks_dirty < RESERVED_BUFFER)
		return NULL;

	if (!(blocknr = AllocReservedBlock(g)))
	{
		UpdateDisk(g);
		blocknr = AllocReservedBlock(g);
	}

	return blocknr;
}
#endif

/*
 * frees reserved block, or does nothing if blocknr = 0
 */
void FreeReservedBlock (ULONG blocknr, globaldata *g)
{
  ULONG *bitmap, t;

	if (blocknr && blocknr <= g->rootblock->lastreserved)
	{
		bitmap = alloc_data.res_bitmap->bitmap;
		t = (blocknr - g->rootblock->firstreserved)/g->currentvolume->rescluster;
		bitmap[t/32] |= (0x80000000UL >> (t%32));
		g->rootblock->reserved_free++;
		g->currentvolume->rootblockchangeflag = true;
	}
}



/* this routine is analogous GetAnodeBlock()
 * GetBitmapIndex is analogous GetIndexBlock()
 */
struct cbitmapblock *GetBitmapBlock(ULONG seqnr, globaldata *g)
{
  ULONG blocknr, temp;
  cbitmapblock_t *bmb;
  cindexblock_t *indexblock;
  struct volumedata *volume = g->currentvolume;

	/* check cache */
	for (bmb = (cbitmapblock_t * )HeadOf(&volume->bmblks); bmb->next; bmb=bmb->next)
	{
		if (bmb->blk.seqnr == seqnr)
		{
			MakeLRU (bmb);
			return bmb;
		}
	}

	/* not in cache, put it in */
	/* get the indexblock */
	temp = divide (seqnr, andata.indexperblock);
	if (!(indexblock = GetBitmapIndex(temp /* & 0xffff */, g)))
		return NULL;

	/* get blocknr */
	if (!(blocknr = indexblock->blk.index[temp>>16]) ||
		!(bmb = (cbitmapblock_t *)AllocLRU(g)))
		return NULL;

	DB(Trace(10,"GetBitmapBlock", "seqnr = %ld blocknr = %lx\n", seqnr, blocknr));

	/* read it */
	if (RawRead((UBYTE*)&bmb->blk, RESCLUSTER, blocknr, g) != 0)
	{
		FreeLRU((struct cachedblock *)bmb);
		return NULL;
	}
	SmartSwap(&bmb->blk, g->blocksize * RESCLUSTER);

	/* check it */
	if (bmb->blk.id != BMBLKID)
	{
		ULONG args[2];
		args[0] = bmb->blk.id;
		args[1] = blocknr;
		FreeLRU ((struct cachedblock *)bmb);
		ErrorMsg (AFS_ERROR_DNV_WRONG_BMID, args, g);
		return NULL;
	}
	
	/* initialize it */
	bmb->volume = volume;
	bmb->blocknr = blocknr;
	bmb->used = false;
	bmb->changeflag = false;
	MinAddHead(&volume->bmblks, bmb);

	return bmb;
}

cindexblock_t *GetBitmapIndex (UWORD nr, globaldata *g)
{
  ULONG blocknr;
  cindexblock_t *indexblk;
  struct volumedata *volume = g->currentvolume;

	/* check cache */
	for (indexblk = (cindexblock_t * )HeadOf(&volume->bmindexblks); indexblk->next; indexblk=indexblk->next)
	{
		if (indexblk->blk.seqnr == nr)
		{
			MakeLRU(indexblk);
			return indexblk;
		}
	}

	/* not in cache, put it in */
	if ((nr > (g->supermode ? MAXBITMAPINDEX : MAXSMALLBITMAPINDEX)) ||
		!(blocknr = volume->rootblk->idx.large.bitmapindex[nr]) ||
		!(indexblk = (struct cindexblock *)AllocLRU(g)) )
	return NULL;

	DB(Trace(10,"GetBitmapIndex", "seqnr = %ld blocknr = %lx\n", nr, blocknr));

	if (RawRead((UBYTE*)&indexblk->blk, RESCLUSTER, blocknr, g) != 0) {
		FreeLRU((struct cachedblock *)indexblk);
		return NULL;
	}
	SmartSwap(&indexblk->blk, g->blocksize * RESCLUSTER);

	if (indexblk->blk.id == BMIBLKID)
	{
		indexblk->volume   = volume;
		indexblk->blocknr  = blocknr;
		indexblk->used     = false;
		indexblk->changeflag = false;
		MinAddHead (&volume->bmindexblks, indexblk);
	}
	else
	{
		ULONG args[5];
		args[0] = indexblk->blk.id;
		args[1] = BMIBLKID;
		args[2] = blocknr;
		args[3] = nr;
		args[4] = 0;
		FreeLRU ((struct cachedblock *)indexblk);
		ErrorMsg (AFS_ERROR_DNV_WRONG_INDID, args, g);
		return NULL;
	}

	LOCK(indexblk);
	return indexblk;
}     

/*
 * the following routines (NewBitmapBlock & NewBitmapIndexBlock are
 * primarily (read only) used by Format
 */
struct cbitmapblock *NewBitmapBlock (ULONG seqnr, globaldata *g)
{
  struct cbitmapblock *blok;
  struct cindexblock *indexblock;
  struct volumedata *volume = g->currentvolume;
  ULONG indexblnr, blocknr, indexoffset;
  ULONG *bitmap, i;
  UWORD oldlock;

	/* get indexblock */
	indexblnr = seqnr/andata.indexperblock;
	indexoffset = seqnr%andata.indexperblock;
	if (!(indexblock = GetBitmapIndex(indexblnr, g)))
		if (!(indexblock = NewBitmapIndexBlock(indexblnr, g)))
			return NULL;

	oldlock = indexblock->used;
	LOCK(indexblock);
	if (!(blok = (struct cbitmapblock *)AllocLRU(g)) ||
		!(blocknr = AllocReservedBlock(g)) )
		return NULL;

	indexblock->blk.index[indexoffset] = blocknr;

	blok->volume = volume;
	blok->blocknr = blocknr;
	blok->used = false;
	blok->blk.id = BMBLKID;
	blok->blk.seqnr = seqnr;
	blok->changeflag = true;

	/* fill bitmap */
	bitmap = blok->blk.bitmap;
	for (i = 0; i<alloc_data.longsperbmb; i++)
		*bitmap++ = ~0;

	MinAddHead(&volume->bmblks, blok);
	MakeBlockDirty((struct cachedblock *)indexblock, g);
	indexblock->used = oldlock;  	   // unlock;

	return blok;
}

static struct cindexblock *NewBitmapIndexBlock (UWORD seqnr, globaldata *g)
{
  struct cindexblock *blok;
  struct volumedata *volume = g->currentvolume;

	if (seqnr > (g->supermode ? MAXBITMAPINDEX : MAXSMALLBITMAPINDEX) ||
		!(blok = (struct cindexblock *)AllocLRU(g)))
		return NULL;

	if (!(g->rootblock->idx.large.bitmapindex[seqnr] = AllocReservedBlock (g)))
	{
		FreeLRU((struct cachedblock *)blok);
		return NULL;
	}

	volume->rootblockchangeflag = true;

	blok->volume   = volume;
	blok->blocknr  = volume->rootblk->idx.large.bitmapindex[seqnr];
	blok->used     = false;
	blok->blk.id   = BMIBLKID;
	blok->blk.seqnr  = seqnr;
	blok->changeflag = true;
	MinAddHead(&volume->bmindexblks, blok);

	return blok;
}


/*
 * End allocation.c
 */
