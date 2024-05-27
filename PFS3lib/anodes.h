#pragma once
#include "struct.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct anodechain *GetAnodeChain (ULONG anodenr, globaldata *g);
void DetachAnodeChain (struct anodechain *chain, globaldata *g);
bool NextBlockAC (struct anodechainnode **acnode, ULONG *anodeoffset, globaldata *g);
bool CorrectAnodeAC (struct anodechainnode **acnode, ULONG *anodeoffset, globaldata *g);

struct cindexblock *GetSuperBlock (UWORD nr, globaldata *g);
bool NextBlock(struct canode * , ULONG * , globaldata * );
bool CorrectAnode(struct canode * , ULONG * , globaldata * );
void GetAnode(struct canode * , ULONG , globaldata * );
void SaveAnode(struct canode * , ULONG , globaldata * );
ULONG AllocAnode(ULONG connect, globaldata *g);
void FreeAnode(ULONG , globaldata * );

struct cindexblock * GetIndexBlock(UWORD , globaldata * );
void RemoveFromAnodeChain(struct canode const * , ULONG , ULONG , globaldata * );
void InitAnodes(struct volumedata * , bool, globaldata * );

#ifdef __cplusplus
}
#endif /* __cplusplus */