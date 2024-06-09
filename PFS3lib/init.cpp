
#include <string.h>
#include "debug.h"

/* own includes */
#include "blocks.h"
#include "struct.h"
#include "init.h"
#include "volume.h"
#include "anodes.h"
#include "directory.h"
#include "lru.h"
#include "allocation.h"
#include "disk.h"

#include "kswrapper.h"


/* prototypes
 */
static bool TestRemovability(globaldata *);
#if VERSION23
static void DoPostponed (struct volumedata *volume, globaldata *g);
#endif


/* Reconfigure the filesystem from a rootblock
** GetDriveGeometry already called by GetCurrentRoot, which does
** g->firstblock and g->lastblock.
** rootblockextension must have been loaded
*/
void InitModules (struct volumedata *volume, bool formatting, globaldata *g)
{
  rootblock_t *rootblock = volume->rootblk;

	g->rootblock = rootblock;
	g->uip = 0;
	g->harddiskmode = (rootblock->options & MODE_HARDDISK) != 0;
	g->anodesplitmode = (rootblock->options & MODE_SPLITTED_ANODES) != 0;
	g->dirextension = (rootblock->options & MODE_DIR_EXTENSION) != 0;
#if DELDIR
	g->deldirenabled = (rootblock->options & MODE_DELDIR) && 
	g->dirextension && (volume->rblkextension->blk.deldirsize > 0);
#endif
	g->supermode = (rootblock->options & MODE_SUPERINDEX) != 0;
	g->fnsize = (volume->rblkextension) ? (volume->rblkextension->blk.fnsize) : 32;
	if (!g->fnsize) g->fnsize = 32;
	g->largefile = (rootblock->options & MODE_LARGEFILE) && g->dirextension && LARGE_FILE_SIZE;

	InitAnodes (volume, formatting, g);
	InitAllocation (volume, g);

#if VERSION23
	if (!formatting)
		DoPostponed (volume, g);
#endif
}


#if VERSION23
static void DoPostponed (struct volumedata *volume, globaldata *g)
{
  struct crootblockextension *rext;
  struct anodechain *achain;
  struct postponed_op *postponed;

	rext = volume->rblkextension;
	if (rext)
	{
		postponed = &rext->blk.tobedone;

		switch (postponed->operation_id)
		{
			case PP_FREEBLOCKS_FREE:

				/* we assume we have enough memory at startup.. */
				achain = GetAnodeChain (postponed->argument1, g);
				FreeBlocksAC (achain, postponed->argument2, freeanodes, g);
				break;

			case PP_FREEBLOCKS_KEEP:

				/* we assume we have enough memory at startup.. */
				achain = GetAnodeChain (postponed->argument1, g);
				alloc_data.clean_blocksfree -= postponed->argument3;
				alloc_data.alloc_available -= postponed->argument3;
				FreeBlocksAC (achain, postponed->argument2, keepanodes, g);
				break;

			case PP_FREEANODECHAIN:

				FreeAnodesInChain (postponed->argument1, g);
				break;
		}

		postponed->operation_id = 0;
		postponed->argument1 = 0;
		postponed->argument2 = 0;
		postponed->argument3 = 0;
	}
}
#endif

