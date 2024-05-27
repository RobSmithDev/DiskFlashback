

#include <stdlib.h>
#include "debug.h"

#include "blocks.h"
#include "struct.h"
#include "anodes.h"
#include "allocation.h"
#include "disk.h"
#include "lru.h"
#include "update.h"
#include "ass.h"
#include "volume.h"
#include "messages.h"
#include "kswrapper.h"
#include "SmartSwap.h"

/*
 * Contents (local functions)
 */
static struct anodechain *MakeAnodeChain (ULONG anodenr, globaldata *g);
static struct anodechain *FindAnodeChain (ULONG anodenr, globaldata *g);
static void FreeAnodeChain (struct anodechain *chain, globaldata *g);
static struct canodeblock *big_GetAnodeBlock(UWORD seqnr, globaldata *g);
static struct canodeblock *big_NewAnodeBlock(UWORD , globaldata * );
static struct cindexblock *NewIndexBlock(UWORD , globaldata * );
static struct cindexblock *NewSuperBlock (UWORD seqnr, globaldata *g);
static void MakeAnodeBitmap (bool formatting, globaldata *g);
static void ReallocAnodeBitmap (ULONG newseqnr, globaldata *g);


/*********************************************************************/
/* Anode cache functions                                             */
/*********************************************************************/

/*
 * Get anodechain of anodenr, making it if necessary.
 * Returns chain or NULL for failure
 */
struct anodechain *GetAnodeChain(ULONG anodenr, globaldata *g)
{
	struct anodechain *ac;

	if (!(ac = FindAnodeChain(anodenr, g)))
		ac = MakeAnodeChain(anodenr, g);
	if (ac)
		ac->refcount++;

	return ac;
}

/*
 * Called when a reference to an anodechain ceases to exist
 */
void DetachAnodeChain(struct anodechain *chain, globaldata *g)
{
	chain->refcount--;
	if (!chain->refcount)
		FreeAnodeChain(chain, g);
}


/*
 * makes anodechain
 */
static struct anodechain *MakeAnodeChain(ULONG anodenr, globaldata *g)
{
	struct anodechain *ac;
	struct anodechainnode *node, *newnode;

	ENTER("MakeAnodeChain");
	if (!(ac = (anodechain * )AllocMemP(sizeof(struct anodechain), g)))
		return NULL;

	ac->refcount = 0;
	node = &ac->head;
	GetAnode(&node->an, anodenr, g);
	while (node->an.next)
	{
		if (!(newnode = (anodechainnode*)AllocMemP(sizeof(struct anodechainnode), g)))
			goto failure;
		node->next = newnode;
		GetAnode(&newnode->an, node->an.next, g);
		node = newnode;
	}

	MinAddHead(&g->currentvolume->anodechainlist, ac);
	return ac;
	
failure:
	FreeAnodeChain(ac, g);  
	return NULL;
}


/*
 * search anodechain. 
 * Return anodechain found, or 0 if not found
 */
static struct anodechain *FindAnodeChain (ULONG anodenr, globaldata *g)
{
  struct anodechain *chain;

	ENTER("FindAnodeChain");
	for (chain = (anodechain * )HeadOf(&g->currentvolume->anodechainlist); chain->next; chain=chain->next)
	{
		if (chain->head.an.nr == anodenr)
			return chain;
	}

	return NULL;
}

/*
 * Free an anodechain. Anodechain will be removed from list if it is
 * in the list.
 */
static void FreeAnodeChain (struct anodechain *chain, globaldata *g)
{
	struct anodechainnode *node, *nextnode;

	ENTER("FreeAnodeChain");
	for (node=chain->head.next; node; node=nextnode)
	{
		nextnode = node->next;
		FreeMemP (node, g);
	}

	if (chain->next)
		MinRemove (chain);

	FreeMemP (chain, g);
}

/*
 * Tries to fetch the block that follows after anodeoffset. Returns success,
 * updates anodechainpointer and anodeoffset. If failed, acnode will point to
 * the tail of the anodechain
 */
bool NextBlockAC (struct anodechainnode **acnode, ULONG *anodeoffset, globaldata *g)
{
	(*anodeoffset)++;
	return CorrectAnodeAC (acnode, anodeoffset, g);
}

/* 
 * Correct anodeoffset overflow. Corrects anodechainnode pointer pointed to by acnode.
 * Returns success. If correction was not possible, acnode will be the tail of the
 * anodechain. Anodeoffset is updated to point to a block within the current
 * anodechainnode.
 */
bool CorrectAnodeAC (struct anodechainnode **acnode, ULONG *anodeoffset, globaldata *g)
{
	while (*anodeoffset >= (*acnode)->an.clustersize)
	{
		if (!(*acnode)->next)
			return false;

		*anodeoffset -= (*acnode)->an.clustersize;
		*acnode = (*acnode)->next;
	}

	return true;
}


/*********************************************************************/
/* Main functions                                                    */
/*********************************************************************/

/*
 * Tries to fetch the block that follows after anodeoffset. Returns
 * success and anodeoffset is updated.
 */
bool NextBlock (struct canode *anode, ULONG *anodeoffset, globaldata *g)
{
	(*anodeoffset)++;
	return CorrectAnode(anode, anodeoffset, g);
}

/* 
 * Correct anodeoffset overflow
 */
bool CorrectAnode (struct canode *anode, ULONG *anodeoffset, globaldata *g)
{
	while(*anodeoffset >= anode->clustersize)
	{
		if(!anode->next)
			return false;

		*anodeoffset -= anode->clustersize;
		GetAnode(anode, anode->next, g);
	}

	return true;
}

/*
 * Retrieve an anode from disk
 */
void GetAnode (struct canode *anode, ULONG anodenr, globaldata *g)
{
  ULONG temp;
  UWORD seqnr, anodeoffset;
  struct canodeblock *ablock;

	if(g->anodesplitmode)
	{
		anodenr_t *split = (anodenr_t *)&anodenr;		
		seqnr = split->seqnr;
		anodeoffset = split->offset;
	}
	else
	{
		temp		 = divide(anodenr, andata.anodesperblock);
		seqnr        = temp;				// 1e block = 0
		anodeoffset  = temp >> 16;
	}
	
	ablock = GetAnodeBlock(seqnr, g);
	if(ablock)
	{
		anode->clustersize = ablock->blk.nodes[anodeoffset].clustersize;
		anode->blocknr     = ablock->blk.nodes[anodeoffset].blocknr;
		anode->next        = ablock->blk.nodes[anodeoffset].next;
		anode->nr          = anodenr;
	}
	else
	{
		anode->clustersize = anode->next = 0;
		anode->blocknr     = ~0UL;
		// ErrorMsg (AFS_ERROR_DNV_ALLOC_INFO, NULL);
		DBERR(ErrorTrace(5,"GetAnode","ERR: anode = 0x%lx\n",anodenr));
	}
}


/* saves and anode..
*/
void SaveAnode (struct canode *anode, ULONG anodenr, globaldata *g)
{
  ULONG temp;
  UWORD seqnr, anodeoffset;
  struct canodeblock *ablock;

	if (g->anodesplitmode)
	{
		anodenr_t *split = (anodenr_t *)&anodenr;
		seqnr = split->seqnr;
		anodeoffset = split->offset;
	}
	else
	{
		temp		= divide(anodenr,andata.anodesperblock);
		seqnr       = temp;      // 1e block = 0
		anodeoffset = temp >> 16;
	}

	anode->nr   = anodenr;

	/* Save Anode */
	ablock = GetAnodeBlock (seqnr, g);
	if (ablock)
	{
		ablock->blk.nodes[anodeoffset].clustersize = anode->clustersize;
		ablock->blk.nodes[anodeoffset].blocknr     = anode->blocknr;
		ablock->blk.nodes[anodeoffset].next        = anode->next;
		MakeBlockDirty ((struct cachedblock *)ablock, g);
	}
	else
	{
		DBERR(ErrorTrace(5,"SaveAnode","ERR: anode = 0x%lx\n",anodenr));
		// ErrorMsg (AFS_ERROR_DNV_ALLOC_BLOCK, NULL);
	}
}


/* allocates an anode and marks it as reserved
 * connect is anodenr to connect to (0 = no connection)
 */
ULONG AllocAnode (ULONG connect, globaldata *g)
{
	WORD i, j, k;
	struct canodeblock *ablock = nullptr;
	struct anode* anodes = nullptr;
	bool found = 0;
	ULONG seqnr = 0, field;

	if (connect && g->anodesplitmode)
	{
		/* try to place new anode in same block */
		ablock = big_GetAnodeBlock (seqnr = connect>>16, g);
		if (ablock)
		{
			anodes = ablock->blk.nodes;
			for (k = andata.anodesperblock-1; k > -1 && !found; k--)
				found = (anodes[k].clustersize == 0 &&
						 anodes[k].blocknr == 0 &&
						 anodes[k].next == 0);
		}
	}
	else
	{
		for (i = andata.curranseqnr/32; i < andata.maxanseqnr/32 + 1; i++)
		{
			
			DBERR(if (i >= andata.anblkbitmapsize / 4 || i < 0)
				ErrorTrace(5, "AllocAnode","ERR: anblkbitmap out of bounds %lu >= %lu\n", i, andata.anblkbitmapsize / 4));
			
			field = andata.anblkbitmap[i];
			if (field)
			{
				for (j = 31; j >= 0; j--)
				{
					if (field & (1 << j))
					{
						seqnr = i*32 + 31-j;
						ablock = big_GetAnodeBlock(seqnr, g);
						if (ablock)
						{
							anodes = ablock->blk.nodes;
							for (k=0; k<andata.reserved && !found; k++)
								found = (anodes[k].clustersize == 0 &&
										 anodes[k].blocknr == 0 &&
										 anodes[k].next == 0);

							if (found)
								goto found_it;
							else
								/* mark anodeblock as full */
								andata.anblkbitmap[i] &= ~(1 << j);
						}
						/* anodeblock does not exist */
						else goto found_it;
					}
				}
			}
		}
		
		seqnr = andata.maxanseqnr + 1;
	}

  found_it:

	if (!found)
	{
		/* give up connect mode and try again */
		if (connect)
			return AllocAnode (0, g);

		/* start over if not started from start of list;
		 * else make new block
		 */
		if (andata.curranseqnr)
		{
			andata.curranseqnr = 0;
			return AllocAnode (0, g);
		}
		else
		{
			if (!(ablock = big_NewAnodeBlock (seqnr, g)))
				return 0;
			anodes = ablock->blk.nodes;
			k = 0;
		}
	}
	else
	{
		if (connect)
			k++;
		else
			k--;
	}

	anodes[k].clustersize = 0;
	anodes[k].blocknr     = 0xffffffff;
	anodes[k].next        = 0;

	MakeBlockDirty((struct cachedblock *)ablock, g);
	andata.curranseqnr = seqnr;

	if(g->anodesplitmode)   
		return (ULONG)(seqnr<<16 | k);
	else
		return (ULONG)(seqnr*andata.anodesperblock + k);
}


/*
 * frees an anode for later reuse
 * universal version
 */
void FreeAnode (ULONG anodenr, globaldata *g)
{
  struct canode anode = {0};

	/* don't kill reserved anodes */
	if (anodenr < ANODE_USERFIRST) 
	{
		anode.blocknr = (ULONG)~0L;
	}

	SaveAnode (&anode, anodenr, g);
	andata.anblkbitmap[(anodenr>>16)/32] |= 1 << (31 - ((anodenr>>16)%32));
}


/***********************************************************************/
/* LOCAL functions                                                     */
/***********************************************************************/



/* MODE_BIG has indexblocks, and negative blocknrs indicating freenode
** blocks instead of anodeblocks
*/
static struct canodeblock *big_GetAnodeBlock (UWORD seqnr, globaldata *g)
{
  ULONG blocknr;
  ULONG temp;
  struct canodeblock *ablock;
  struct cindexblock *indexblock;
  struct volumedata *volume = g->currentvolume;

	temp = divide (seqnr, andata.indexperblock);

	/* not in cache, put it in */
	/* get the indexblock */
	if (!(indexblock = GetIndexBlock (temp /*& 0xffff*/, g)))
	{
		DBERR(ErrorTrace(5, "GetAnodeBlock","ERR: index not found. %lu %lu %08lx\n", seqnr, andata.indexperblock, temp));
		return NULL;
	}

	/* get blocknr */
	if (!(blocknr = indexblock->blk.index[temp >> 16]))
	{
		DBERR(ErrorTrace(5,"GetAnodeBlock","ERR: index zero %lu %lu %08lx\n", seqnr, andata.indexperblock, temp));
		return NULL;
	}

	/* check cache */
	ablock = (struct canodeblock *)CheckCache (volume->anblks, HASHM_ANODE, blocknr, g);
	if (ablock)
		return ablock;

	if (!(ablock = (struct canodeblock *)AllocLRU(g)))
	{
		DBERR(ErrorTrace(5,"GetAnodeBlock","ERR: alloclru failed\n"));
		return NULL;
	}

	DBERR(ErrorTrace(10,"GetAnodeBlock", "seqnr = %lu blocknr = %lu\n", seqnr, blocknr));

	/* read it */
	if (RawRead ((UBYTE*)&ablock->blk, RESCLUSTER, blocknr, g) != 0)
	{
		DB(Trace(5,"GetAnodeBlock","Read ERR: seqnr = %lu blocknr = %lx\n", seqnr, blocknr));
		FreeLRU ((struct cachedblock *)ablock);
		return NULL;
	}
	SmartSwap(&ablock->blk, RESCLUSTER * g->blocksize);

	/* check it */
	if (ablock->blk.id != ABLKID)
	{
		ULONG args[2];
		args[0] = ablock->blk.id;
		args[1] = blocknr;
		FreeLRU ((struct cachedblock *)ablock);
		ErrorMsg (AFS_ERROR_DNV_WRONG_ANID, args, g);
		return NULL;
	}

	/* initialize it */
	ablock->volume     = volume;
	ablock->blocknr    = blocknr;
	ablock->used       = false;
	ablock->changeflag = false;
	Hash (ablock, volume->anblks, HASHM_ANODE);

	return ablock;
}


/* MODE_BIG has difference between anodeblocks and fnodeblocks
*/
static struct canodeblock *big_NewAnodeBlock (UWORD seqnr, globaldata *g)
{
  struct canodeblock *blok;
  struct volumedata *volume = g->currentvolume;
  struct cindexblock *indexblock;
  ULONG indexblnr;
  LONG blocknr;
  UWORD indexoffset, oldlock;

	/* get indexblock */
	indexblnr = seqnr/andata.indexperblock;
	indexoffset = seqnr%andata.indexperblock;
	if (!(indexblock = GetIndexBlock(indexblnr, g))) {
		if (!(indexblock = NewIndexBlock(indexblnr, g))) {
			DBERR(ErrorTrace(10,"big_NewAnodeBlock","ERR: NewIndexBlock %lu %lu %lu %lu\n", seqnr, indexblnr, indexoffset, andata.indexperblock));
			return NULL;
		}
	}

	oldlock = indexblock->used;
	LOCK(indexblock);
	if (!(blok = (struct canodeblock *)AllocLRU(g)) || !(blocknr = AllocReservedBlock(g)) ) {
		DBERR(ErrorTrace(10,"big_NewAnodeBlock","ERR: AllocLRU/AllocReservedBlock %lu %lu %lu\n", seqnr, indexblnr, indexoffset));
		indexblock->used = oldlock;         // unlock block
		return NULL;
	}

	DBERR(ErrorTrace(10,"big_NewAnodeBlock", "seqnr = %lu block = %lu\n", seqnr, blocknr));

	indexblock->blk.index[indexoffset] = blocknr;

	blok->volume     = volume;
	blok->blocknr    = blocknr;
	blok->used       = false;
	blok->blk.id     = ABLKID;
	blok->blk.seqnr  = seqnr;
	blok->changeflag = true;
	Hash(blok, volume->anblks, HASHM_ANODE);
	MakeBlockDirty((struct cachedblock *)indexblock, g);
	indexblock->used = oldlock;         // unlock block

	ReallocAnodeBitmap (seqnr, g);
	return blok;
}



/**********************************************************************/
/* indexblocks                                                        */
/**********************************************************************/

/*
 * get indexblock nr
 * returns NULL if failure
 */
struct cindexblock *GetIndexBlock (UWORD nr, globaldata *g)
{
  ULONG blocknr, temp;
  struct cindexblock *indexblk;
  struct cindexblock *superblk;
  struct volumedata *volume = g->currentvolume;

	/* check cache (can be empty) */
	for (indexblk = (struct cindexblock* )HeadOf(&volume->indexblks); indexblk->next; indexblk=indexblk->next)
	{
		if (indexblk->blk.seqnr == nr)
		{
			MakeLRU (indexblk);
			return indexblk;
		}
	}

	/* not in cache, put it in
	 * first, get blocknr
	 */
	if (g->supermode) 
	{
		/* temp is chopped by auto cast */
		temp = divide(nr, andata.indexperblock);
		if (!(superblk = GetSuperBlock (temp, g)))
		{
			DBERR(ErrorTrace(5, "GetIndexBlock", "ERR: superblock not found. %lu %lu %08lx\n", nr, andata.indexperblock, temp));
			return NULL;
		}

		if (!(blocknr = superblk->blk.index[temp>>16]))
		{
			DBERR(ErrorTrace(5, "GetIndexBlock", "ERR: super zero. %lu %lu %08lx\n", nr, andata.indexperblock, temp));
			return NULL;
		}
	}
	else
	{
		if ((nr>MAXSMALLINDEXNR) || !(blocknr = volume->rootblk->idx._small.indexblocks[nr])) 
			return NULL;
	}

	/* allocate space from cache */
	if (!(indexblk = (struct cindexblock *)AllocLRU(g))) {
		DBERR(ErrorTrace(5, "GetIndexBlock", "ERR: AllocLRU. %lu %lu %08lx %lu\n", nr, andata.indexperblock, temp, blocknr));
		return NULL;
	}

	DBERR(ErrorTrace(10,"GetIndexBlock","seqnr = %lu blocknr = %lu\n", nr, blocknr));

	if (RawRead ((UBYTE*)&indexblk->blk, RESCLUSTER, blocknr, g) != 0) {
		FreeLRU ((struct cachedblock *)indexblk);
		return NULL;
	}
	SmartSwap(&indexblk->blk, RESCLUSTER * g->blocksize);
		
	if (indexblk->blk.id == IBLKID)
	{
		indexblk->volume     = volume;
		indexblk->blocknr    = blocknr;
		indexblk->used       = false;
		indexblk->changeflag = false;
		MinAddHead (&volume->indexblks, indexblk);
	}
	else
	{
		ULONG args[5];
		args[0] = indexblk->blk.id;
		args[1] = IBLKID;
		args[2] = blocknr;
		args[3] = nr;
		args[4] = andata.indexperblock;
		FreeLRU ((struct cachedblock *)indexblk);
		ErrorMsg (AFS_ERROR_DNV_WRONG_INDID, args, g);
		return NULL;
	}

	return indexblk;
}
		

static struct cindexblock *NewIndexBlock (UWORD seqnr, globaldata *g)
{
  struct cindexblock *blok;
  struct cindexblock *superblok = NULL;
  struct volumedata *volume = g->currentvolume;
  ULONG superblnr = 0;
  LONG blocknr;
  UWORD superoffset = 0;

	if (g->supermode)
	{
		superblnr = seqnr/andata.indexperblock;
		superoffset = seqnr%andata.indexperblock;
		if (!(superblok = GetSuperBlock (superblnr, g)))
			if (!(superblok = NewSuperBlock (superblnr, g))) {
				DBERR(ErrorTrace(1, "NewIndexBlock", "ERR: Super not found. %lu %lu %lu %lu\n", seqnr, andata.indexperblock, superblnr, superoffset));
				return NULL;
			} else {
				DBERR(ErrorTrace(1, "NewIndexBlock", "OK. %lu %lu %lu %lu\n", seqnr, andata.indexperblock, superblnr, superoffset));		
			}

		LOCK(superblok);
	}
	else if (seqnr > MAXSMALLINDEXNR) {
		return NULL;
	}

	if (!(blok = (struct cindexblock *)AllocLRU(g)) ||
		!(blocknr = AllocReservedBlock(g)) )
	{
		DBERR(ErrorTrace(1, "NewIndexBlock", "ERR: AllocLRU/AllocReservedBlock. %lu %lu %lu %lu\n", seqnr, blocknr, superblnr, superoffset));
		if (blok)
			FreeLRU((struct cachedblock *)blok);
		return NULL;
	}

	DBERR(ErrorTrace(10,"NewIndexBlock", "seqnr = %lu block = %lu\n", seqnr, blocknr));

	if (g->supermode) {
		superblok->blk.index[superoffset] = blocknr;
		MakeBlockDirty((struct cachedblock *)superblok, g);
	} else {
		volume->rootblk->idx._small.indexblocks[seqnr] = blocknr;
		volume->rootblockchangeflag = true;
	}

	blok->volume     = volume;
	blok->blocknr    = blocknr;
	blok->used       = false;
	blok->blk.id     = IBLKID;
	blok->blk.seqnr  = seqnr;
	blok->changeflag = true;
	MinAddHead(&volume->indexblks, blok);

	return blok;
}

struct cindexblock *GetSuperBlock (UWORD nr, globaldata *g)
{
  ULONG blocknr;
  struct cindexblock *superblk;
  struct volumedata *volume = g->currentvolume;

	DBERR(blocknr = 0xffdddddd);

	/* check supermode */
	if (!g->supermode) {
		DBERR(ErrorTrace(1, "GetSuperBlock", "ERR: Illegally entered\n"));
		return NULL;
	}

	/* check cache (can be empty) */
	for (superblk = (struct cindexblock* )HeadOf(&volume->superblks); superblk->next; superblk=superblk->next)
	{
		if (superblk->blk.seqnr == nr)
		{
			MakeLRU (superblk);
			return superblk;
		}
	}

	/* not in cache, put it in
	 * first, get blocknr
	 */
	if ((nr>MAXSUPER) || !(blocknr = volume->rblkextension->blk.superindex[nr])) {
		DBERR(ErrorTrace(1, "GetSuperBlock", "ERR: out of bounds. %lu %lu\n", nr, blocknr));
		return NULL;
	}

	/* allocate space from cache */
	if (!(superblk = (struct cindexblock *)AllocLRU(g))) {
		DBERR(ErrorTrace(1, "GetSuperBlock", "ERR: AllocLRU error. %lu %lu\n", nr, blocknr));
		return NULL;
	}

	DBERR(ErrorTrace(10,"GetSuperBlock","seqnr = %lu blocknr = %lu\n", nr, blocknr));

	if (RawRead ((UBYTE*)&superblk->blk, RESCLUSTER, blocknr, g) != 0) {
		DBERR(ErrorTrace(1, "GetSuperBlock", "ERR: read error. %lu %lu\n", nr, blocknr));
		FreeLRU ((struct cachedblock *)superblk);
		return NULL;
	}
	SmartSwap(&superblk->blk, RESCLUSTER * g->blocksize);
		
	if (superblk->blk.id == SBLKID)
	{
		superblk->volume     = volume;
		superblk->blocknr    = blocknr;
		superblk->used       = false;
		superblk->changeflag = false;
		MinAddHead (&volume->superblks, superblk);
	}
	else
	{
		ULONG args[5];
		args[0] = superblk->blk.id;
		args[1] = SBLKID;
		args[2] = blocknr;
		args[3] = nr;
		args[4] = 0;
		FreeLRU ((struct cachedblock *)superblk);
		ErrorMsg (AFS_ERROR_DNV_WRONG_INDID, args, g);
		return NULL;
	}

	return superblk;
}

static struct cindexblock *NewSuperBlock (UWORD seqnr, globaldata *g)
{
  struct cindexblock *blok;
  struct volumedata *volume = g->currentvolume;

	DBERR(blok = NULL;)

	if ((seqnr > MAXSUPER) ||
		!(blok = (struct cindexblock *)AllocLRU(g)) ) {
		DBERR(ErrorTrace(1, "NewSuperBlock", "ERR: out of bounds or LRU error. %lu %p\n", seqnr, blok));
		return NULL;
	}

	if (!(volume->rblkextension->blk.superindex[seqnr] = AllocReservedBlock(g)))
	{
		DBERR(ErrorTrace(1, "NewSuperBlock", "ERR: AllocReservedBlock. %lu %p\n", seqnr, blok));
		FreeLRU((struct cachedblock *)blok);
		return NULL;
	}
 
	DBERR(ErrorTrace(10,"NewSuperBlock", "seqnr = %lu block = %lu\n", seqnr, volume->rblkextension->blk.superindex[seqnr]));

	volume->rblkextension->changeflag = true;

	blok->volume     = volume;
	blok->blocknr    = volume->rblkextension->blk.superindex[seqnr];
	blok->used       = false;
	blok->blk.id     = SBLKID;
	blok->blk.seqnr  = seqnr;
	blok->changeflag = true;
	MinAddHead(&volume->superblks, blok);

	return blok;
}

/* Remove anode from anodechain
 * If previous==0, anode->next becomes head.
 * Otherwise previous->next becomes anode->next.
 * Anode is freed.
 *
 * Arguments:
 * anode = anode to be removed
 * previous = previous in chain; or 0 if anode is head
 * head = anodenr of head of list
 */
void RemoveFromAnodeChain (const struct canode *anode, ULONG previous, ULONG head, globaldata *g)
{
  struct canode sparenode;

	if(previous)
	{
		GetAnode(&sparenode, previous, g);
		sparenode.next = anode->next;
		SaveAnode(&sparenode, sparenode.nr, g);
		FreeAnode(anode->nr, g);
	}
	else
	{
		/* anode is head of list (check both tails here) */
		if (anode->next)
		{
			/* There is a next entry -> becomes head */
			GetAnode(&sparenode, anode->next, g);
			SaveAnode(&sparenode, head, g); // overwrites [anode]
			FreeAnode(anode->next, g);  
		}
		else
		{
			/* No anode->next: Free list. */
			FreeAnode(head, g);
		}
	}
}


/*********************************************************************/
/* Initialization                                                    */
/*********************************************************************/

void InitAnodes (struct volumedata *volume, bool formatting, globaldata *g)
{
	g->getanodeblock = big_GetAnodeBlock;

	andata.curranseqnr = volume->rblkextension ? volume->rblkextension->blk.curranseqnr : 0;
	andata.anodesperblock = (volume->rootblk->reserved_blksize - sizeof(anodeblock_t)) / sizeof(anode_t);
	andata.indexperblock = (volume->rootblk->reserved_blksize - sizeof(indexblock_t)) / sizeof(LONG);
	andata.maxanodeseqnr = g->supermode ?
			((MAXSUPER+1) * andata.indexperblock * andata.indexperblock * andata.anodesperblock - 1) :
			(MAXSMALLINDEXNR * andata.indexperblock - 1);
	andata.reserved = andata.anodesperblock - RESERVEDANODES;
	MakeAnodeBitmap (formatting, g);
}


/* Find out how large the anblkbitmap must be, allocate it and
 * initialise it. Free any preexisting anblkbitmap
 *
 * The anode bitmap is used for allocating anodes. It has the
 * following properties:
 * - It is  maintained in memory only (not on disk). 
 * - Intialization is lazy: all anodes are marked as available
 * - When allocation anodes (see AllocAnode), this bitmap is used
 *   to find available anodes. It then checks with the actual
 *   anode (which should be 0,0,0 if available). If it isn't really
 *   available, the anodebitmap is updated, otherwise the anode is
 *   taken.
 */
static void MakeAnodeBitmap (bool formatting, globaldata *g)
{
	struct cindexblock *iblk;
	struct cindexblock *sblk;
	int i, j, s;
	ULONG size;

	if (andata.anblkbitmap)
		FreeMemP (andata.anblkbitmap, g);

	/* count number of anodeblocks and allocate bitmap */
	if (formatting)
	{
		i = 0; s = 0; j = 1;
	}
	else
	{
		if (g->supermode)
		{
			for (s=MAXSUPER; s >= 0 && !g->currentvolume->rblkextension->blk.superindex[s]; s--);
			if (s < 0)
				goto error;
				
			sblk = GetSuperBlock (s, g);

			DBERR(if (!sblk) ErrorTrace(1, "MakeAnodeBitmap", "ERR: GetSuperBlock returned NULL!. %ld\n", s));

			for (i=andata.indexperblock - 1; i >= 0 && !sblk->blk.index[i]; i--);
		}
		else
		{
			for (s=0, i=MAXSMALLINDEXNR; i >= 0 && !g->rootblock->idx._small.indexblocks[i]; i--);
		}

		if (i < 0)
			goto error;
		iblk = GetIndexBlock (s * andata.indexperblock + i, g);

		DBERR(if (!iblk) ErrorTrace(1, "MakeAnodeBitmap", "ERR: GetIndexBlock returned NULL!. %ld %ld\n", s, i));

		for (j=andata.indexperblock - 1; j >= 0 && !iblk->blk.index[j]; j--);
	}

	if (g->supermode)
	{
		andata.maxanseqnr = s * andata.indexperblock * andata.indexperblock + i * andata.indexperblock + j;
		size = ((s * andata.indexperblock + i + 1) * andata.indexperblock + 7) / 8;
	}
	else
	{
		andata.maxanseqnr = i * andata.indexperblock + j;
		size = ((i+1) * andata.indexperblock + 7) / 8;
	}
	andata.anblkbitmapsize = (size + 3) & ~3;
	andata.anblkbitmap = (ULONG*)AllocMemP (andata.anblkbitmapsize, g);
	if (andata.anblkbitmap)  memset(andata.anblkbitmap, 0xFF, andata.anblkbitmapsize);

	return;

 error:
	ErrorMsg(AFS_ERROR_ANODE_ERROR, NULL, g);
}


/* test if new anodeseqnr causes change in anblkbitmap */
static void ReallocAnodeBitmap (ULONG newseqnr, globaldata *g)
{
	ULONG *newbitmap, newsize;

	if (newseqnr > andata.maxanseqnr)
	{
		andata.maxanseqnr = newseqnr;
		newsize = ((newseqnr/andata.indexperblock + 1) * andata.indexperblock + 7) / 8;
		if (newsize > andata.anblkbitmapsize)
		{
			newsize = (newsize + 3) & ~3;   /* longwords */
			newbitmap = (ULONG*)AllocMemP (newsize, g);
			if (newbitmap) {
				memset(newbitmap, 0xFF, newsize);
				memcpy(newbitmap, andata.anblkbitmap, andata.anblkbitmapsize);
			}
			FreeMemP (andata.anblkbitmap, g);
			andata.anblkbitmap = newbitmap;
			andata.anblkbitmapsize = newsize;
		}
	}
}
