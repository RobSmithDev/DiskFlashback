#pragma once

#include "struct.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

SFSIZE ChangeFileSize(fileentry_t * , SFSIZE , LONG , SIPTR * , globaldata * );
ULONG ReadFromObject(fileentry_t * , UBYTE * , ULONG , SIPTR * , globaldata * );
ULONG WriteToObject(fileentry_t * , UBYTE * , ULONG , SIPTR * , globaldata * );
SFSIZE SeekInObject(fileentry_t * , SFSIZE , LONG , SIPTR * , globaldata * );
SFSIZE ChangeObjectSize(fileentry_t * , SFSIZE , LONG , SIPTR * , globaldata * );
SFSIZE SeekInFile(fileentry_t *file, SFSIZE offset, LONG mode, SIPTR *error, globaldata *g);

ULONG DiskRead(UBYTE * , ULONG , ULONG , globaldata * );

ULONG DiskWrite(UBYTE * , ULONG , ULONG , globaldata * );

ULONG RawRead(UBYTE * , ULONG , ULONG , globaldata * );

ULONG RawWrite(UBYTE * , ULONG , ULONG , globaldata * );

void FlushDataCache (globaldata *g);
void UpdateDataCache (globaldata *g);

void FreeDataCache(globaldata *g);
bool InitDataCache(globaldata *g);


#ifdef __cplusplus
}
#endif /* __cplusplus */