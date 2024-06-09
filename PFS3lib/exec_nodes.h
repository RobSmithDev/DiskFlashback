#pragma once
/*  Some portions of this file are from lists.h
* 
**	$VER: lists.h 39.0 (15.10.1991)
**	Includes Release 44.1
**
**	Definitions and macros for use with Exec lists
**
**	(C) Copyright 1985-1999 Amiga, Inc.
**	    All Rights Reserved
*/

#include <stdbool.h>
#include <stdint.h>

#define SAVEDS

#define ULONG uint32_t
typedef uint16_t UWORD;
#define WORD int16_t
#define LONG int32_t
typedef int32_t SIPTR;
typedef uint64_t IPTR;
typedef uint8_t UBYTE;
typedef void* APTR;
typedef char* STRPTR;
typedef const char* CONST_STRPTR;

#define	ED_NAME		1
#define	ED_TYPE		2
#define ED_SIZE		3
#define ED_PROTECTION	4
#define ED_DATE		5
#define ED_COMMENT	6
// no idea if this is right
#define ED_OWNER    7

#define ID_NO_DISK_PRESENT	(-1)
#define ID_UNREADABLE_DISK	(0x42414400L)	/* 'BAD\0' */
#define ID_DOS_DISK		(0x444F5300L)	/* 'DOS\0' */
#define ID_FFS_DISK		(0x444F5301L)	/* 'DOS\1' */
#define ID_INTER_DOS_DISK	(0x444F5302L)	/* 'DOS\2' */
#define ID_INTER_FFS_DISK	(0x444F5303L)	/* 'DOS\3' */
#define ID_NOT_REALLY_DOS	(0x4E444F53L)	/* 'NDOS'  */
#define ID_KICKSTART_DISK	(0x4B49434BL)	/* 'KICK'  */
#define ID_MSDOS_DISK		(0x4d534400L)	/* 'MSD\0' */

#define PFS_ERROR_COMMENT_TOO_BIG		  220
#define PFS_ERROR_ACTION_NOT_KNOWN        209
#define PFS_ERROR_OBJECT_IN_USE		  202
#define PFS_ERROR_BAD_NUMBER		  115
#define PFS_ERROR_NO_FREE_STORE		  103
#define PFS_ERROR_DISK_FULL			  221
#define PFS_ERROR_INVALID_COMPONENT_NAME	  210
#define PFS_ERROR_SEEK_ERROR		  219
#define PFS_ERROR_OBJECT_EXISTS		  203
#define PFS_ERROR_DIRECTORY_NOT_EMPTY	  216
#define PFS_ERROR_WRITE_PROTECTED		  223
#define PFS_ERROR_DEVICE_NOT_MOUNTED	  218
#define PFS_ERROR_OBJECT_WRONG_TYPE		  212
#define PFS_ERROR_READ_PROTECTED		  224
#define PFS_ERROR_DISK_NOT_VALIDATED	  213
#define PFS_ERROR_DISK_WRITE_PROTECTED	  214
#define PFS_ERROR_DELETE_PROTECTED		  222
#define PFS_ERROR_OBJECT_NOT_FOUND		  205
#define PFS_ERROR_IS_SOFT_LINK		  233
#define PFS_ERROR_NO_MORE_ENTRIES		  232

#define MODE_NEWFILE	     1006   
#define MODE_READWRITE	     1004  

#define ST_ROOT		1
#define ST_USERDIR	2
#define ST_SOFTLINK	3	/* looks like dir, but may point to a file! */
#define ST_LINKDIR	4	/* hard link to dir */
#define ST_FILE		-3	/* must be negative for FIB! */
#define ST_LINKFILE	-4	/* hard link to file */
#define ST_PIPEFILE	-5	/* for pipes that support ExamineFH */

#define ID_WRITE_PROTECTED 80	 /* Disk is write protected */
#define ID_VALIDATING	   81	 /* Disk is currently being validated */
#define ID_VALIDATED	   82	 /* Disk is consistent and writeable */

#define FIBB_SCRIPT    6	/* program is a script (execute) file */
#define FIBB_PURE      5	/* program is reentrant and rexecutable */
#define FIBB_ARCHIVE   4	/* cleared whenever file is changed */
#define FIBB_READ      3	/* ignored by old filesystem */
#define FIBB_WRITE     2	/* ignored by old filesystem */
#define FIBB_EXECUTE   1	/* ignored by system, used by Shell */
#define FIBB_DELETE    0	/* prevent file from being deleted */
#define FIBF_SCRIPT    (1<<FIBB_SCRIPT)
#define FIBF_PURE      (1<<FIBB_PURE)
#define FIBF_ARCHIVE   (1<<FIBB_ARCHIVE)
#define FIBF_READ      (1<<FIBB_READ)
#define FIBF_WRITE     (1<<FIBB_WRITE)
#define FIBF_EXECUTE   (1<<FIBB_EXECUTE)
#define FIBF_DELETE    (1<<FIBB_DELETE)

#define OFFSET_BEGINNING    -1	    /* relative to Begining Of File */
#define OFFSET_CURRENT	     0	    /* relative to Current file position */
#define OFFSET_END	     1	    /* relative to End Of File	  */

#ifndef min
#define min(a,b) (a<b?a:b)
#endif
#ifndef max
#define max(a,b) (a>b?a:b)
#endif


void SmartSwap(uint16_t& p);
void SmartSwap(uint32_t& p);
void SmartSwap(int16_t& p);
void SmartSwap(int32_t & p);

struct DateStamp {
	LONG	 ds_Days;	      /* Number of days since Jan. 1, 1978 */
	LONG	 ds_Minute;	      /* Number of minutes past midnight */
	LONG	 ds_Tick;	      /* Number of ticks past minute */
}; /* DateStamp */

struct FileInfoBlock {
	uint64_t  fib_DiskKey;
	LONG	  fib_DirEntryType;  /* Type of Directory. If < 0, then a plain file.
				   * If > 0 a directory */
	char	  fib_FileName[108]; /* Null terminated. Max 30 chars used for now */
	LONG	  fib_Protection;    /* bit mask of protection, rwxd are 3-0.	   */
	LONG	  fib_EntryType;
	LONG	  fib_Size;	     /* Number of bytes in file */
	LONG	  fib_NumBlocks;     /* Number of blocks in file */
	struct DateStamp fib_Date;/* Date file last changed */
	char	  fib_Comment[80];  /* Null terminated comment associated with file */
	UWORD  fib_OwnerUID;		/* owner's UID */
	UWORD  fib_OwnerGID;		/* owner's GID */

	char	  fib_Reserved[32];
}; /* FileInfoBlock */

struct ExAllData {
	struct ExAllData* ed_Next;
	UBYTE* ed_Name;
	LONG	ed_Type;
	ULONG	ed_Size;
	ULONG	ed_Prot;
	ULONG	ed_Days;
	ULONG	ed_Mins;
	ULONG	ed_Ticks;
	UBYTE* ed_Comment;	/* strings will be after last used field */
	UWORD	ed_OwnerUID;	/* new for V39 */
	UWORD	ed_OwnerGID;
};

struct ExAllControl {
	ULONG	eac_Entries;	 /* number of entries returned in buffer      */
	uint64_t	eac_LastKey;	 /* Don't touch inbetween linked ExAll calls! */
	UBYTE* eac_MatchString; /* wildcard string for pattern match or NULL */
};

struct Node {
	struct  Node* ln_Succ;	/* Pointer to next (successor) */
	struct  Node* ln_Pred;	/* Pointer to previous (predecessor) */
	UBYTE   ln_Type;
	int8_t    ln_Pri;		/* Priority, for sorting */
	char* ln_Name;		/* ID string, null terminated */
};	/* Note: word aligned */

/* minimal node -- no type checking possible */
struct MinNode {
	struct MinNode* mln_Succ;
	struct MinNode* mln_Pred;
};

/*
 *  Full featured list header.
 */
struct List {
	struct  Node* lh_Head;
	struct  Node* lh_Tail;
	struct  Node* lh_TailPred;
	UBYTE   lh_Type;
	UBYTE   l_pad;
};	/* word aligned */

/*
 * Minimal List Header - no type checking
 */
struct MinList {
	struct  MinNode* mlh_Head;
	struct  MinNode* mlh_Tail;
	struct  MinNode* mlh_TailPred;
};	/* longword aligned */

#define IsListEmpty(x) ( ((x)->lh_TailPred) == (struct Node *)(x) )

void NewList(struct List* lh);

#define AddHead(l, n)     ((void)(\
	((struct Node*)n)->ln_Succ = ((struct List*)l)->lh_Head, \
	((struct Node*)n)->ln_Pred = (struct Node*)&((struct List*)l)->lh_Head, \
	((struct List*)l)->lh_Head->ln_Pred = ((struct Node*)n), \
	((struct List*)l)->lh_Head = ((struct Node*)n)))

#define AddTail(l,n)     ((void)(\
	((struct Node *)n)->ln_Succ              = (struct Node *)&((struct List *)l)->lh_Tail, \
	((struct Node *)n)->ln_Pred              = ((struct List *)l)->lh_TailPred, \
	((struct List *)l)->lh_TailPred->ln_Succ = ((struct Node *)n), \
	((struct List *)l)->lh_TailPred          = ((struct Node *)n) ))

#define Remove(n)        ((void)(\
	((struct Node *)n)->ln_Pred->ln_Succ = ((struct Node *)n)->ln_Succ,\
	((struct Node *)n)->ln_Succ->ln_Pred = ((struct Node *)n)->ln_Pred ))

void DateTime(struct DateStamp* r);

void PFS3_DateTime2DateStamp(const struct tm* inStamp, struct DateStamp* stamp);
void PFS3_DateStamp2DateTime(const struct DateStamp* stamp, struct tm* inStamp);