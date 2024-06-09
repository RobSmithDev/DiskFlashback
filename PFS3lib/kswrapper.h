#pragma once


#define KSWRAPPER_DEBUG 0

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define ErrorReport(code, type, arg1, device) W_ErrorReport(code, type, arg1, device, g)
#define FilePart(path) W_FilePart(path, g)
#define PathPart(path) W_PathPart(path, g)

STRPTR W_FilePart(STRPTR path, struct globaldata *g);
STRPTR W_PathPart(STRPTR path, struct globaldata *g);
LONG W_ErrorReport(LONG code, LONG type, ULONG arg1, struct MsgPort *device, struct globaldata *g);

#define MEMF_ANY 0
#define MEMF_CLEAR 1
#define AllocVec(size) calloc(1,size)
#define FreeVec(mem) free(mem)
#define CopyMem(s,d,n) memcpy(d,s,n)  

#ifdef __cplusplus
}
#endif /* __cplusplus */