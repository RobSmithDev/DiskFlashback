#pragma once
#include "struct.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct listentry * MakeListEntry(union objectinfo * , listtype , SIPTR *error, globaldata * );

bool _AddListEntry(listentry_t *, globaldata * );
#define AddListEntry(a) _AddListEntry(a,g)

void RemoveListEntry(listentry_t * , globaldata * );

void FreeListEntry(listentry_t * , globaldata * );

bool _ChangeAccessMode(listentry_t * , LONG , SIPTR *, globaldata * );
#define ChangeAccessMode(a,b,c) _ChangeAccessMode(a,b,c,g)

bool AccessConflict(listentry_t * );

bool ScanLockList(listentry_t * , ULONG );


#ifdef __cplusplus
}
#endif /* __cplusplus */