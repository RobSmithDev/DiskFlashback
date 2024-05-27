#pragma once

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

bool UpdateDisk(globaldata * );

bool MakeBlockDirty(struct cachedblock * , globaldata * );


void CheckUpdate (ULONG rtbf_threshold, globaldata *);

void UpdateDatestamp (struct cachedblock *blk, globaldata *g);


#ifdef __cplusplus
}
#endif /* __cplusplus */