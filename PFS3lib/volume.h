#pragma once

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

LONG ErrorMsg(CONST_STRPTR, APTR, globaldata*);
LONG _NormalErrorMsg(CONST_STRPTR, APTR, ULONG, globaldata*);
#define NormalErrorMsg(a,b,c) _NormalErrorMsg(a,b,c,g)

void DiskInsertSequence(struct rootblock* rootblock, globaldata* g);

bool GetCurrentRoot(struct rootblock** rootblock, globaldata* g);

bool SafeDiskRemoveSequence(globaldata * );

void FreeUnusedResources(struct volumedata * , globaldata * );

bool UpdateChangeCount(globaldata * );

void UpdateCurrentDisk(globaldata * );

bool CheckVolume(struct volumedata * , bool, SIPTR * , globaldata * );

void GetDriveGeometry(globaldata * );

bool CheckCurrentVolumeBack(globaldata *g);

void CalculateBlockSize(globaldata *g, ULONG, ULONG);

#ifdef __cplusplus
}
#endif /* __cplusplus */