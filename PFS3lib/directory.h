#pragma once

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

bool dstricmp(DSTR , STRPTR );
bool ddstricmp(DSTR , DSTR );
UBYTE * BCPLtoCString(STRPTR , DSTR );

UBYTE * GetFullPath(union objectinfo * , STRPTR , union objectinfo * , SIPTR * , globaldata * );

bool GetRoot(union objectinfo * , globaldata * );

bool FindObject(union objectinfo * , STRPTR , union objectinfo * , SIPTR * , globaldata * );

bool GetParent(union objectinfo * , union objectinfo * , SIPTR * , globaldata * );

bool FetchObject(ULONG diranodenr, ULONG target, union objectinfo *result, globaldata *g);

bool ExamineFile(listentry_t * , struct FileInfoBlock * , SIPTR * , globaldata * );

bool ExamineNextFile(lockentry_t * , struct FileInfoBlock * , SIPTR * , globaldata * );

void GetNextEntry(lockentry_t * , globaldata * );

bool ExamineAll(lockentry_t * , UBYTE * , ULONG , LONG , struct ExAllControl * , SIPTR * , globaldata * );

ULONG NewFile(bool, union objectinfo * , STRPTR , union objectinfo * , globaldata * );

lockentry_t * NewDir(union objectinfo * , STRPTR , SIPTR * , globaldata * );

struct cdirblock * MakeDirBlock(ULONG , ULONG , ULONG , ULONG , globaldata * );

bool DeleteObject(union objectinfo * , SIPTR * , globaldata * );
bool KillEmpty (union objectinfo *parent, globaldata *g);
LONG forced_RemoveDirEntry (union objectinfo *info, SIPTR *error, globaldata *g);

bool RenameAndMove(union objectinfo *, union objectinfo *, union objectinfo *, STRPTR , SIPTR * , globaldata * );

bool AddComment(union objectinfo * , STRPTR , SIPTR * , globaldata * );

void FillFib(struct direntry*, struct FileInfoBlock*, globaldata*);

bool ProtectFile(struct fileinfo * , ULONG , SIPTR * , globaldata * );

bool SetOwnerID(struct fileinfo *file, ULONG owner, SIPTR *error, globaldata *g);

LONG ReadSoftLink(union objectinfo *linkfi, const char *prefix, char *buffer, ULONG size, SIPTR *error, globaldata *g);

bool CreateSoftLink(union objectinfo *linkdir, STRPTR linkname, STRPTR softlink,
	union objectinfo *newlink, SIPTR *error, globaldata *g);

bool CreateLink(union objectinfo *directory, STRPTR linkname, union objectinfo *object,
	union objectinfo *newlink, SIPTR *error, globaldata *g);

bool SetDate(union objectinfo * , struct DateStamp * , SIPTR * , globaldata * );

void Touch(struct fileinfo * , globaldata * );

bool CreateRollover(union objectinfo *dir, STRPTR rollname, ULONG size,
	union objectinfo *result, SIPTR *error, globaldata *g);
ULONG SetRollover(fileentry_t *rooi, struct rolloverinfo *roinfo, globaldata *g);

void ChangeDirEntry(struct fileinfo from, struct direntry *to, union objectinfo *destdir, struct fileinfo *result, globaldata *g);

struct cdirblock * LoadDirBlock(ULONG , globaldata * );


void GetExtraFields(struct direntry *direntry, struct extrafields *extrafields);
void AddExtraFields (struct direntry *direntry, struct extrafields *extra);
#if MULTIUSER
#if DELDIR
void GetExtraFieldsOI(union objectinfo *info, struct extrafields *extrafields, globaldata *g);
#endif
#endif

#if DELDIR
struct cdeldirblock *NewDeldirBlock(UWORD seqnr, globaldata *g);
struct deldirentry *GetDeldirEntryQuick(uint64_t ddnr, globaldata *g);
ULONG SetDeldir(int nbr, globaldata *g);
#endif
void UpdateLinks(struct direntry *object, globaldata *g);
void FreeAnodesInChain(ULONG anodenr, globaldata *g);

FSIZE GetDEFileSize(struct direntry *direntry, globaldata *g);
ULONG GetDEFileSize32(struct direntry *direntry, globaldata *g);
void SetDEFileSize(struct direntry *direntry, FSIZE size, globaldata *g);

#if DELDIR
FSIZE GetDDFileSize(struct deldirentry *dde, globaldata *g);
ULONG GetDDFileSize32(struct deldirentry *dde, globaldata *g);
void SetDDFileSize(struct deldirentry *dde, FSIZE size, globaldata *g);
#endif


#ifdef __cplusplus
}
#endif /* __cplusplus */