#pragma once

#include "struct.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void InitAllocation (struct volumedata * , globaldata * );

bool AllocateBlocks (ULONG , ULONG , globaldata * );
bool AllocateBlocksAC (struct anodechain *achain, ULONG size, struct fileinfo *ref, globaldata *g);
void FreeBlocksAC (struct anodechain *achain, ULONG size, enum freeblocktype freetype, globaldata *g);
void UpdateFreeList(globaldata * );

ULONG AllocReservedBlock(globaldata * );
ULONG AllocReservedBlockSave(globaldata * );
void FreeReservedBlock(ULONG , globaldata * );

cindexblock_t * GetBitmapIndex(UWORD , globaldata * );
struct cbitmapblock * NewBitmapBlock(ULONG seqnr , globaldata * );
struct cbitmapblock *GetBitmapBlock (ULONG seqnr, globaldata *g);



#ifdef __cplusplus
}


#endif /* __cplusplus */