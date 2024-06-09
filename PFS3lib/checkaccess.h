#pragma once

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

bool CheckChangeAccess(fileentry_t *file, SIPTR *error, globaldata *g);
bool CheckWriteAccess(fileentry_t *file, SIPTR *error, globaldata *g);
bool CheckReadAccess(fileentry_t *file, SIPTR *error, globaldata *g);
bool CheckOperateFile(fileentry_t *file, SIPTR *error, globaldata *g);


#ifdef __cplusplus
}
#endif /* __cplusplus */