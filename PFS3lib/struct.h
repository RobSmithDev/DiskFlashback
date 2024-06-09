#pragma once

// taken from struct.h

#include <string.h>
#include <string>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <functional>
#include "exec_nodes.h"
#include "blocks.h"
#include "SmartSwap.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/****************************************************************************/
/* muFS related defines                                                     */
/****************************************************************************/

/* flags that allow SetProtect, SetOwner, SetDate, SetComment etc */
#define muRel_PROPERTY_ACCESS (muRelF_ROOT_UID|muRelF_UID_MATCH|muRelF_NO_OWNER)

/****************************************************************************/
/* CACHE related defines                                                    */
/****************************************************************************/

/* Locking. Dirblocks used during a operation have to be locked
 * with LOCK() (happens in LoadDirBlock() and UpdateLE())
 * UNLOCKALL() unlocks all blocks..
 */
#define LOCK(blk) ((blk)->used = g->locknr)
#define UNLOCKALL() (g->locknr++)
#define ISLOCKED(blk) ((blk)->used == g->locknr)

 /* Cache hashing table mask values for dir and anode */
#define HASHM_DIR 0x1f
#define HASHM_ANODE 0x7


/****************************************************************************/
/* general defines                                                          */
/****************************************************************************/

#define WITH(x) x;
typedef unsigned char* DSTR;      /* pascal string: length, than characters     */

/*****************************************************************************/
/* Rollover info structure                                                   */
/*****************************************************************************/

#pragma pack(2)
struct rolloverinfo
{
	bool set;           /* 0 -> read; 1 -> write */
	ULONG realsize;
	ULONG virtualsize;
	ULONG rollpointer;
};
#pragma pack()


/*****************************************************************************/
/* Allocation data                                                           */
/*****************************************************************************/

/*
 * The global allocation data
 *
 * the number of blocks really free can be found in rootblock->blocksfree
 * rootblock fields:
 *   ULONG blocksfree       // total blocks free
 *   ULONG alwaysfree       // blocks to be kept always free
 *   ULONG rovingptr        // roving 'normal' alloc pointer
 *   ULONG reserved_free    // number of free reserved blocks
 *
 * volumedata fields:
 *   ULONG numblocks        // total number of blocks
 *   struct MinList bmblks
 *   struct MinList bmindex
 *
 * andata field indexperblock is also used
 *
 * res_rovingptr: roving 'reserved' alloc pointer
 *
 * clean_blocksfree: directly available blocks (inc alwaysfree!). Updated by UpdateFreeList.
 *                  increased only by Update(), UpdateFreeList() and FlushBlock()
 *                  decreased by AllocateBlocks(), AllocReserved()
 *
 * alloc_available: number of eventually available blocks (exc alwaysfree!).
 *                 increased by FreeBlocks(), FreeReservedBlock()
 *                 decreased by AllocateBlocks()
 *
 * rootblock->blocksfree: only updated by Update(). Real number of blocks free (inc alwaysfree).
 *
 * reserved bitmap: behind rootblock. [0] = #free.
 */

#include "exec_nodes.h"

 /* cache grootte */
#define RTBF_CACHE_SIZE 512
#define TBF_CACHE_SIZE 256

/* update thresholds */
#define RTBF_THRESHOLD 256
#define RTBF_CHECK_TH  128
#define RTBF_POSTPONED_TH 48
#define TBF_THRESHOLD 252
#define RESFREE_THRESHOLD 10

/* indices in tobefreed array */
#define TBF_BLOCKNR 0
#define TBF_SIZE 1

#define COUNT UWORD
#define UCOUNT WORD

/* buffer for AllocReservedBlockSave */
#define RESERVED_BUFFER 10

/* check for reserved block allocation lock */
#define ReservedAreaIsLocked (alloc_data.res_alert)

/* checks if update is needed now */
#define IsUpdateNeeded(rtbf_threshold)                              \
	((alloc_data.rtbf_index > rtbf_threshold) ||                    \
	(g->rootblock->reserved_free < RESFREE_THRESHOLD + 5 + alloc_data.tbf_resneed))         \

/* keep or free anodes when freeing blocks */
enum freeblocktype { keepanodes, freeanodes };

struct allocation_data_s
{
	ULONG clean_blocksfree;             /* number of blocks directly allocatable            */
	ULONG alloc_available;              /* cleanblocksfree + blockstobefreed - alwaysfree   */
	ULONG longsperbmb;                  /* longwords per bitmapblock                        */
	ULONG no_bmb;                       /* number of bitmap blocks                          */
	ULONG bitmapstart;                  /* blocknr at which bitmap starts                   */
	ULONG tobefreed[TBF_CACHE_SIZE][2]; /* tobefreed array                                  */
	ULONG tobefreed_index;
	ULONG tbf_resneed;                  /* max reserved blks needed for tbf cache           */
	struct bitmapblock* res_bitmap;     /* reserved block bitmap pointer                    */
	ULONG res_roving;                   /* reserved roving pointer (0 at startup)           */
	UWORD rovingbit;                    /* bitnumber (within LW) of main roving pointer     */
	ULONG numreserved;                  /* total # reserved blocks (== lastreserved+1)      */
	ULONG* reservedtobefreed;           /* tbf cache for flush reserved blocks  */
	ULONG rtbf_size;                    /* size of the allocated cache */
	ULONG rtbf_index;                   /* current index in reserved tobefreed cache        */
	bool res_alert;                     /* TRUE if low on available reserved blocks         */
};

/*****************************************************************************/
/* anode data                                                                */
/*****************************************************************************/

/*
 * The global anode data
 *
 * Other used globaldata fields:
 *  (*)getanodeblock()
 *  (*)allocanode()
 */
struct anode_data_s
{
	UWORD curranseqnr;        /* current anode seqnr for anode allocation */
	UWORD indexperblock;      /* ALSO used by allocation (for bitmapindex blocks) */
	ULONG maxanodeseqnr;		/* max anode seqnr */
	UWORD anodesperblock;     /* number of anodes that fit in one block */
	UWORD reserved;           /* offset of first reserved anode within an anodeblock */
	ULONG* anblkbitmap;       /* anodeblock full-flag bitmap */
	ULONG anblkbitmapsize;    /* size of anblkbitmap */
	ULONG maxanseqnr;         /* current maximum anodeblock seqnr */
};


/*
 * anodecache structures
 */
struct anodechainnode
{
	struct anodechainnode* next;
	struct canode an;
};

struct anodechain
{
	struct anodechain* next;
	struct anodechain* prev;
	ULONG refcount;             /* will be discarded if refcount becomes 0 */
	struct anodechainnode head;
};

/* number of reserved anodes per anodeblock */
#define RESERVEDANODES 6

/*****************************************************************************/
/* LRU data                                                                  */
/*****************************************************************************/

/* the LRU global data */
struct lru_data_s
{
	struct MinList LRUqueue;
	struct MinList LRUpool;
	ULONG poolsize;
	struct lru_cachedblock** LRUarray;
	UWORD reserved_blksize;
};


/*****************************************************************************/
/* the diskcache                                                             */
/*****************************************************************************/

/* the cache is filled in a round robin manner, using 'roving' for
 * the roundrobin pointer. Cache checking is done in the same manner;
 * making the cache large will make it slow!
 */

struct diskcache
{
	struct reftable* ref;   /* reference table; one entry per slot */
	UBYTE* data;            /* the data (one slot per block) */
	UWORD size;             /* cache capacity in blocks (order of 2) */
	UWORD mask;             /* size expressed in a bitmask */
	UWORD roving;           /* round robin roving pointer */
};

struct reftable
{
	ULONG blocknr;          /* blocknr of cached block; 0 = empty slot */
	UBYTE dirty;            /* dirty flag (TRUE/FALSE) */
	UBYTE pad;
};

#define DATACACHELEN 32
#define DATACACHEMASK (DATACACHELEN - 1)

#define MarkDataDirty(i) (g->dc.ref[i].dirty = 1)

/*****************************************************************************/
/* globaldata structure                                                      */
/*****************************************************************************/

#define ACCESS_UNDETECTED 0
#define ACCESS_STD 1
#define ACCESS_DS 2
#define ACCESS_TD64 3
#define ACCESS_NSD 4

struct PartitionData {
	uint32_t 	de_SizeBlock;
	int32_t 	de_SectorPerBlock;
	int32_t 	de_BlocksPerTrack;
	int32_t 	de_LowCyl;
	int32_t 	de_HighCyl;
	int32_t 	de_Mask;
	int32_t		de_NumBuffers;
};

struct DriveGeom {
	int32_t dg_SectorSize;
	int32_t dg_Cylinders;
	int32_t dg_CylSectors;
	int32_t dg_TotalSectors;
	int32_t dg_Heads;
	int32_t dg_TrackSectors;
	int32_t dg_BufMemType;
	int32_t dg_DeviceType;
	int32_t dg_Flags;
};

/* ALL globals are defined here */
struct globaldata
{
	struct PartitionData dosenvec;
	struct DriveGeom geom;

	/* partition info (volume dependent) %7 */
	ULONG firstblock, firstblocknative; /* first and last block of partition    */
	ULONG lastblock, lastblocknative;
	ULONG maxtransfermax;
	UWORD infoblockshift;
	
	struct diskcache dc;                /* cache to make '196 byte mode' faster */

	/* LRU stuff */
	bool uip;                           /* update in progress flag              */
	UWORD locknr;                       /* prevents blocks from being flushed   */

	/* The DOS packet interpreter */
	//void (*DoCommand)(struct DosPacket*, struct globaldata*);

	/* Volume stuff */
	struct volumedata* currentvolume;
	/* disktype: ID_PFS_DISK/NO_DISK_PRESENT/UNREADABLE_DISK
	 * (only valid if currentvolume==NULL)
	 */
	ULONG disktype;

	/* state of currentvolume (ID_WRITE_PROTECTED/VALIDATED/VALIDATING) */
	ULONG diskstate;

	bool dieing;                        /* TRUE if ACTION_DIE called            */
	int8_t softprotect;                   /* 1 if 'ACTION_WRITE_PROTECTED'     	*/
										/* -1 if protection failed				*/
	bool dirty;                         /* Global dirty flag                    */
	LONG(*ErrorMsg)(CONST_STRPTR, APTR, ULONG, struct globaldata*);    /* The error message routine        */

	struct rootblock* rootblock;        /* shortcut of currentvolume->rootblk   */
	UBYTE harddiskmode;                 /* flag: harddisk mode?                 */
	UBYTE anodesplitmode;               /* flag: anodesplit mode?               */
	UBYTE dirextension;                 /* flag: dirextension?                  */
	UBYTE deldirenabled;                /* flag: deldir enabled?                */
	UBYTE sleepmode;                    /* flag: sleepmode?                     */
	UBYTE supermode;					/* flag: supermode? (104 bmi blocks)	*/
	UBYTE tdmode;						/* ACCESS_x mode					*/
	UBYTE largefile;					/* >4G file size support                */
	ULONG blocksize;                    /* logical blocksize                    */
	ULONG blocksize_phys;               /* g->dosenvec->de_SizeBlock << 2       */
	UWORD blockshift;                   /* 2 log van block size                 */
	UWORD fnsize;						/* filename size (18+)					*/
	UWORD blocklogshift;                /* blocksize_phys << blocklogshift == blocksize */
	ULONG directsize;                   /* number of blocks after which direct  */
										/* access is preferred (config)         */

	char* unparsed;                     /* rest of path after a softlinkdir     */
	bool protchecked;                   /* checked protection?                  */

	struct canodeblock* (*getanodeblock)(UWORD, struct globaldata*);

	struct anode_data_s glob_anodedata;
	struct lru_data_s glob_lrudata;
	struct allocation_data_s glob_allocdata;

	bool updateok;


	bool largeDiskSafeOS;

	std::function<bool(uint32_t logicalSector, void* data)> readSector;
	std::function<bool(uint32_t logicalSector, void* data)> writeSector;
	std::function<void(const std::string& message)> handleError;
};

typedef struct globaldata globaldata;

/*****************************************************************************/
/* defined function macros                                                   */
/*****************************************************************************/

#define GetAnodeBlock(a, b) (g->getanodeblock)(a,b)
#define AllocMemP(size,g) calloc(1,size)
#define FreeMemP(mem,g) free(mem)
#define AllocBufmem(size,g) calloc(1,size)
#define FreeBufmem(mem,g) free(mem)
#define AllocBufmemR(size, g) calloc(1,size)

/*****************************************************************************/
/* local globdata additions                                                  */
/*****************************************************************************/

#define alloc_data (g->glob_allocdata)
#define andata (g->glob_anodedata)
#define lru_data (g->glob_lrudata)


/*****************************************************************************/
/* volumedata                                                                */
/*****************************************************************************/

struct volumedata
{
	struct rootblock* rootblk;       /* the cached rootblock. Also in g.     */
#if VERSION23
	struct crootblockextension* rblkextension; /* extended rblk, NULL if disabled*/
#endif

	struct MinList fileentries;         /* all locks and open files             */
	struct MinList anblks[HASHM_ANODE + 1];   /* anode block hash table           */
	struct MinList dirblks[HASHM_DIR + 1];    /* dir block hash table             */
	struct MinList indexblks;               /* cached index blocks              */
	struct MinList bmblks;              /* cached bitmap blocks                1 */
	struct MinList superblks;			/* cached super blocks					*/
	struct MinList deldirblks;			/* cached deldirblocks					*/
	struct MinList bmindexblks;         /* cached bitmap index blocks           */
	struct MinList anodechainlist;      /* list of cached anodechains           */

	bool    rootblockchangeflag;        /* indicates if rootblock dirty         1*/
	WORD    numsofterrors;              /* number of soft errors on this disk   */
	WORD    diskstate;                  /* normally ID_VALIDATED                */
	ULONG   numblocks;                  /* total number of blocks               */
	UWORD   bytesperblock;              /* blok size (datablocks)               */
	UWORD   rescluster;                 /* reserved blocks cluster              1*/
};


/*****************************************************************************/
/* LRU macro functions                                                       */
/*****************************************************************************/

/* Cached blocks are in two lists. This gets the outer list (the lru chain) from
 * the inner list
 */
#define LRU_CHAIN(b) \
 ((struct lru_cachedblock *)(((UBYTE *)(b))-offsetof(struct lru_cachedblock, cblk)))
#define LRU_CANODEBLOCK(blk) ((struct lru_canodeblock *)((ULONG *)blk - 2))
#define LRU_CDIRBLOCK(blk) ((struct lru_cdirblock *)((ULONG *)blk - 2))
#define LRU_NODE(blk) ((struct MinNode *)((ULONG *)blk - 2))



 /* Make a block the most recently used one. The block
  * should already be in the chain!
  * Argument blk = struct cachedblock *
  */
#define MakeLRU(blk)                                    \
{                                                       \
	MinRemove(LRU_CHAIN(blk));                          \
	MinAddHead(&g->glob_lrudata.LRUqueue, LRU_CHAIN(blk));           \
}

  /* Free a block from the LRU chain and add it to
   * the pool
   * Argument blk = struct cachedblock *
   */
#define FreeLRU(blk)                                    \
{                                                       \
	MinRemove(LRU_CHAIN(blk));                          \
	memset(blk, 0, SIZEOF_CACHEDBLOCK);                 \
	MinAddHead(&g->glob_lrudata.LRUpool, LRU_CHAIN(blk));            \
}

   /*
	* Hashing macros
	*/
#define ReHash(blk, list, mask)                         \
{                                                       \
	MinRemove(blk);                                     \
	MinAddHead(&list[(blk->blocknr/2)&mask], blk);      \
}

#define Hash(blk, list, mask)                           \
	MinAddHead(&list[(blk->blocknr/2)&mask], blk)


/*****************************************************************************/
/* other macro definitions                                                   */
/*****************************************************************************/

#define IsSoftLink(oi) ((IPTR)(oi).file.direntry>2 && ((oi).file.direntry->type==ST_SOFTLINK))
#define IsRealDir(oi) ((IPTR)(oi).file.direntry>2 && ((oi).file.direntry->type==ST_USERDIR))
#define IsDir(oi) ((IPTR)(oi).file.direntry>2 && ((oi).file.direntry->type)>0)
#define IsFile(oi) ((IPTR)(oi).file.direntry>2 && ((oi).file.direntry->type)<=0)
#define IsVolume(oi) ((oi).volume.root==0)
#if DELDIR
#define IsDelDir(oi) ((oi).deldir.special==SPECIAL_DELDIR)
#define IsDelFile(oi) ((oi).deldir.special==SPECIAL_DELFILE)
#endif /* DELDIR */
#if ROLLOVER
#define IsRollover(oi) ((IPTR)(oi).file.direntry>2 && ((oi).file.direntry->type==ST_ROLLOVERFILE))
#endif /* ROLLOVER */
#define ISCURRENTVOLUME(v) (g->currentvolume && \
	dstricmp(g->currentvolume->rootblk->diskname, v) == 0)
#define IsSameOI(oi1, oi2) ((oi1.file.direntry == oi2.file.direntry) && \
	(oi1.file.dirblock == oi2.file.dirblock))

// CHK(x) voorkomt indirectie van null pointer
// IsRoot(fi) checked of *oi bij de rootdir hoort
// IsRootA(fi) checked of oi bij de rootdir hoort
#define N(x) ((x)?(&(x)):NULL)
#define IsRoot(oi) (((oi)==NULL) || ((oi)->volume.root == 0))
#define IsRootA(oi) ((oi).volume.root == 0)

// voor VolumeRequest:
#define VR_URGENT   0
#define VR_PLEASE   1

/**********************************************************************/
/*                        Lists                                       */
/**********************************************************************/
#define MinAddHead(list, node)  AddHead((struct List *)(list), (struct Node *)(node))
#define MinAddTail(list, node)  AddTail((struct List *)(list), (struct Node *)(node))
#define MinInsert(list, node, listnode) Insert((struct List *)list, (struct Node *)node, (struct Node *)listnode)
#define MinRemove(node) Remove((struct Node *)node)
#define HeadOf(list) ((void *)((list)->mlh_Head))
#define IsHead(node) (!((node)->prev->prev))
#define IsTail(node) (!((node)->next->next))
#define IsMinListEmpty(x) ( ((x)->mlh_TailPred) == (struct MinNode *)(x) )

/**********************************************************************/
/*                        File administration                         */
/**********************************************************************/

#if LARGE_FILE_SIZE
/* >4G file size support */
typedef signed long long QUAD;
typedef signed long long FSIZE;
typedef signed long long SFSIZE;
/* Limit to useful sane size, not real max for now */
#define MAX_FILE_SIZE 0x7fffffffff
#else
typedef ULONG FSIZE;
typedef LONG SFSIZE;
#define MAX_FILE_SIZE 0xffffffff
#endif

/* FileInfo
**
** Fileinfo wordt door FindFile opgeleverd. Bevat pointers die wijzen naar
** gecachede directoryblokken. Deze blokken mogen dus alleen uit de cache
** verwijderd worden als deze verwijzingen verwijderd zijn. Op 't ogenblik
** is het verwijderen van fileinfo's uit in gebruik zijnde fileentries
** niet toegestaan. Een fileinfo gevuld met {NULL, xxx} is een volumeinfo. Deze
** wordt in locks naar de rootdir gebruikt.
** Een *fileinfo van NULL verwijst naar de root van de current volume
*/
struct fileinfo
{
	struct direntry* direntry;      // pointer wijst naar direntry binnen gecached dirblock
	struct cdirblock* dirblock;     // pointer naar gecached dirblock
};

struct volumeinfo
{
	ULONG   root;                   // 0 =>it's a volumeinfo; <>0 => it's a fileinfo
	struct volumedata* volume;
};

#if DELDIR
struct deldirinfo
{
	ULONG special;                  // 0 => volumeinfo; 1 => deldirinfo; 2 => delfile; >2 => fileinfo
	struct volumedata* volume;
};

struct delfileinfo
{
	ULONG special;					// 2
	ULONG slotnr;					// het slotnr voor deze deldirentry
};

/* info id's: delfile, deldir and flushed reference */
#define SPECIAL_DELDIR 1
#define SPECIAL_DELFILE 2
#define SPECIAL_FLUSHED 3
#endif /* DELDIR */

union objectinfo
{
	struct fileinfo file;
	struct volumeinfo volume;
#if DELDIR
	struct deldirinfo deldir;
	struct delfileinfo delfile;
#endif
};

/**********************************************************************/
/*                    Fileentries/locks & volumes                     */
/**********************************************************************
**
** Drie structuren met zelfde basis, maar verschillende lengte.
** Allemaal gekoppeld via next; type geeft type entry aan.
** CurrentAnode en AnodeOffset zijn eigenlijk redundant, want afleidbaar van 'offset'.
** FileInfo 'info' moet ingevulde zijn; het betreffende directoryblock moet dus ge-
** cached zijn.
** Van 'lock' is fl_Key het directoryblocknr (redundant met info.dirblock->blocknr)
** fl_Task, fl_Volume en fl_Access dienen ingevuld te zijn.
*/

/* entrytype's
** NB: ETF_VOLUME en ETF_LOCK zijn ALLEBIJ LOCKS!! TEST ON BOTH
*/
#define ET_VOLUME           0x0004
#define ET_FILEENTRY        0x0008
#define ET_LOCK             0x000c
#define ETF_VOLUME          1
#define ETF_FILEENTRY       2
#define ETF_LOCK            3
#define ET_SHAREDREAD       0
#define ET_SHAREDWRITE      1
#define ET_EXCLREAD         2
#define ET_EXCLWRITE        3

#define IsVolumeEntry(e)    ((e)->type.flags.type == ETF_VOLUME)
#define IsFileEntry(e)      ((e)->type.flags.type == ETF_FILEENTRY)
#define IsLockEntry(e)      ((e)->type.flags.type == ETF_LOCK)

#define IsVolumeLock(le)    ((le)->type.flags.type == ETF_VOLUME)

#define SHAREDLOCK(t) ((t).flags.access <= 1)

// ANODENR voor fe's en le's; FIANODENR voor fileinfo's; NOT FOR VOLUMES!!
#define ANODENR(fe) ((fe)->anodenr)
#define FIANODENR(fi) ((fi)->direntry->anode)

union listtype
{
	struct
	{
#ifdef LITT_ENDIAN
		UWORD access : 2;  // 0 = read shared; 2 = read excl; 1,3 = write shared, excl
		UWORD type : 2;    // 0 = unknown; 3 = lock; 1 = volume; 2 = fileentry
		UWORD dir : 1;     // 0 = file; 1 = dir or volume
		UWORD pad : 11;
#else
		UWORD pad : 11;
		UWORD dir : 1;     // 0 = file; 1 = dir or volume
		UWORD type : 2;    // 0 = unknown; 3 = lock; 1 = volume; 2 = fileentry
		UWORD access : 2;  // 0 = read shared; 2 = read excl; 1,3 = write shared, excl
#endif
	} flags;

	UWORD value;
};

typedef union listtype listtype;

/* Listentry
**
**- Alle locks op een disk zijn geketend vanuit volume->firstfe via 'next'. Het einde
**  van de keten is 0. Uitbreiden van de lijst gaat dmv ADDHEAD en ADDTAIL
**- [volume] verwijst terug naar de volume
**- [info] is {NULL, don't care} als root.
**  FileInfo van file/dir moet ingevulde zijn; het betreffende directoryblock moet dus ge-
**  cached zijn.
**- [self] verwijst naar begin structuur
**- [lock] bevat verwijzing naar DLT_VOLUME DosList entry. MOET 4-aligned zijn !!
**- [currentanode] en [anodeoffset] zijn eigenlijk redundant, want afleidbaar van [offset].
**- Van [lock] is fl_Key het directoryblocknr (redundant met info.dirblock->blocknr)
**  fl_Task, fl_Volume en fl_Access dienen ingevuld te zijn.
**
**  Possible locks: root of volume; readlock; writelock; dir; file; readfe; writefe
*/

/* de algemene structure */
typedef struct listentry
{
	struct listentry* next;          /* for linkage                                      */
	struct listentry* prev;
	//struct FileLock     lock;           /* <4A> contains accesstype, dirblocknr (redundant) */
	listtype            type;
	ULONG               anodenr;        /* object anodenr. Always valid. Used by ACTION_SLEEP */
	ULONG               diranodenr;     /* anodenr of parent. Only valid during SLEEP_MODE. */
	union objectinfo    info;           /* refers to dir                                    */
	ULONG               dirblocknr;     /* set when block flushed and info is set to NULL   */
	ULONG               dirblockoffset;
	struct volumedata* volume;        /* pointer to volume                                */
} listentry_t;

/* de specifieke structuren */
typedef struct
{
	listentry_t le;

	struct anodechain* anodechain;      // the cached anodechain of this file
	struct anodechainnode* currnode;    // anode behorende bij offset in file
	ULONG   anodeoffset;        // blocknr binnen currentanode
	ULONG   blockoffset;        // byteoffset binnen huidig block
	FSIZE   offset;             // offset tov start of file
	FSIZE   originalsize;       // size of file at time of opening
	bool    checknotify;        // set if a notify is necessary at ACTION_END time > ALSO touch flag <
} fileentry_t;

typedef struct lockentry
{
	listentry_t le;

	ULONG               nextanode;          // anodenr of next entry (dir/vollock only)
	struct fileinfo     nextentry;          // for examine
	ULONG               nextdirblocknr;     // for flushed block only.. (dir/vollock only)
	ULONG               nextdirblockoffset;
} lockentry_t;

// *lock -> *fileentry
#define LOCKTOFILEENTRY(l) ((fileentry_t *)(((UBYTE*)l)-offsetof(fileentry_t, le.lock)))

// Maakt geen lock naar 'root of currentdir' aan!!
#define LockEntryFromLock(x) ((x) ? \
 (lockentry_t *)((UBYTE*)BADDR(x)-offsetof(listentry_t, lock)) : 0)

#define ListEntryFromLock(x) ((x) ? \
 (listentry_t *)((UBYTE*)BADDR(x)-offsetof(listentry_t, lock)) : 0)

#define MAXEXACTFIT 10

/**********************************************************************/
/*                        Disk administration                         */
/**********************************************************************/

#define InReservedArea(blocknr) \
	(((blocknr) >= g->currentvolume->rootblk->firstreserved) && \
	 ((blocknr) <= g->currentvolume->rootblk->lastreserved))

#define LastReserved    (g->currentvolume->rootblk->lastreserved)
#define FirstReserved   (g->currentvolume->rootblk->firstreserved)
#define InPartition(blk)  ((blk)>=g->firstblock && (blk)<=g->lastblock)
#define BLOCKSIZE (g->blocksize)
#define BLOCKSIZEMASK (g->blocksize - 1)
#define BLOCKSHIFT (g->blockshift)
#define DIRECTSIZE (g->directsize)
#define BLOCKNATIVESIZE (g->blocksize >> g->blocklogshift)
#define BLOCKNATIVESHIFT (g->blockshift - g->blocklogshift)


/*
 * TD64 support
 */
#ifndef TD_READ64
#define TD_READ64	24
#define TD_WRITE64	25
#define TD_SEEK64	26
#define TD_FORMAT64	27
#endif

 /* NSD support */
#ifndef NSCMD_DEVICEQUERY
#define NSCMD_DEVICEQUERY 0x4000
#define NSCMD_TD_READ64 0xc000
#define NSCMD_TD_WRITE64 0xc001
#define NSDEVTYPE_TRACKDISK 5
struct NSDeviceQueryResult
{
	ULONG   DevQueryFormat;
	ULONG   SizeAvailable;
	UWORD   DeviceType;
	UWORD   DeviceSubType;
	UWORD* SupportedCommands;
};
#endif

#define ACCESS_DETECT (TD64 + NSD + SCSIDIRECT > 1)



#ifdef __cplusplus
}
#endif /* __cplusplus */