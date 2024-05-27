#include "messages.h"

CONST_STRPTR AFS_WARNING_MEMORY_MASK         = "WARNING:\nAllocated memory doesn't match memorymask";
CONST_STRPTR AFS_WARNING_EXPERIMENTAL_DISK   = "WARNING:\nExperimental >104G partition / block size support enabled.\n(PBS=%lu,LBS=%lu,RBS=%lu)";
#if LARGE_FILE_SIZE
CONST_STRPTR AFS_WARNING_EXPERIMENTAL_FILE   = "WARNING:\nExperimental >4G file size support enabled";
#endif

CONST_STRPTR AFS_ERROR_DNV_ALLOC_INFO        = "ALERT:\nAllocation info not found";
CONST_STRPTR AFS_ERROR_DNV_ALLOC_BLOCK       = "ALERT:\nAllocation block not found";
CONST_STRPTR AFS_ERROR_DNV_WRONG_ANID        = "ALERT:\nWrong ablock id %08lX block %lu";
CONST_STRPTR AFS_ERROR_DNV_WRONG_DIRID       = "ALERT:\nWrong dirblock id %08lX block %lu";
CONST_STRPTR AFS_ERROR_DNV_LOAD_DIRBLOCK     = "ALERT:\nCould not read directoryblock";
CONST_STRPTR AFS_ERROR_DNV_WRONG_BMID        = "ALERT:\nWrong bitmap block id %08lX block %lu";
CONST_STRPTR AFS_ERROR_DNV_WRONG_INDID       = "ALERT:\nWrong index block id %08lX, expected %08lX, block %lu, %ld, %ld";
CONST_STRPTR AFS_ERROR_CACHE_INCONSISTENCY   = "Cache inconsistency detected\nFinish all disk activity";
CONST_STRPTR AFS_ERROR_OUT_OF_BUFFERS        = "Out of buffers";
CONST_STRPTR AFS_ERROR_MEMORY_POOL           = "Couldn't allocate memorypool";
CONST_STRPTR AFS_ERROR_PLEASE_FREE_MEM       = "Please free some memory";
CONST_STRPTR AFS_ERROR_LIBRARY_PROBLEM       = "Couldn't open library!";
CONST_STRPTR AFS_ERROR_INIT_FAILED           = "Initialization failure";
CONST_STRPTR AFS_ERROR_READ_OUTSIDE          = "Read %ld attempt outside partition! %lu + %lu (%ld - %lu)";
CONST_STRPTR AFS_ERROR_WRITE_OUTSIDE         = "Write %ld attempt outside partition! %lu + %lu (%ld - %lu)";
//CONST_STRPTR AFS_ERROR_READ_ERROR            = "Read %ld Error %lu on block %lu + %lu%s";
//CONST_STRPTR AFS_ERROR_WRITE_ERROR           = "Write %ld Error %lu on block %lu + %lu%s";
CONST_STRPTR AFS_ERROR_READ_DELDIR           = "Could not read deldir";
CONST_STRPTR AFS_ERROR_DELDIR_INVALID        = "Deldir invalid";
CONST_STRPTR AFS_ERROR_EXNEXT_FAIL           = "ExamineNext failed";
CONST_STRPTR AFS_ERROR_DOSLIST_ADD           = "DosList add error.\nPlease remove volume";
CONST_STRPTR AFS_ERROR_EX_NEXT_FAIL          = "ExamineNext failed";
CONST_STRPTR AFS_ERROR_NEWDIR_ADDLISTENTRY   = "Newdir addlistentry failure";
CONST_STRPTR AFS_ERROR_LOAD_DIRBLOCK_FAIL    = "Couldn't load dirblock!!";
CONST_STRPTR AFS_ERROR_LRU_UPDATE_FAIL       = "LRU update failed block %lu, err %ld";
CONST_STRPTR AFS_ERROR_UPDATE_FAIL           = "Disk update failed";
CONST_STRPTR AFS_ERROR_UNSLEEP               = "Unsleep error";
CONST_STRPTR AFS_ERROR_DISK_TOO_LARGE		= "Disk too large for this version of PFS3.\nPlease install TD64 or direct-scsi version";
CONST_STRPTR AFS_ERROR_ANODE_ERROR			= "Anode index invalid";
CONST_STRPTR AFS_ERROR_ANODE_INIT            = "Anode initialisation failure";
//CONST_STRPTR AFS_ERROR_32BIT_ACCESS_ERROR    = "TD32 and Direct SCSI access modes failed!\nCan't read block %lu (<4G)\n%s:%ld";

#if VERSION23
CONST_STRPTR AFS_ERROR_READ_EXTENSION        = "Could not read rootblock extension";
CONST_STRPTR AFS_ERROR_EXTENSION_INVALID     = "Rootblock extension invalid";
#endif

#ifdef BETAVERSION
/* beta messages */
CONST_STRPTR AFS_BETA_WARNING_1              = "BETA WARNING NR 1";
CONST_STRPTR AFS_BETA_WARNING_2              = "BETA WARNING NR 2";
#endif

/*
 * Message shown when formatting a disk
 */

CONST_STRPTR FORMAT_MESSAGE =
                "   Professional File System 3   \n"
                "      Open Source Version       \n"
                "          based on PFS3         \n"
                "            written by          \n"
                "           Michiel Pelt         \n"
                "      All-In-One version by     \n"
                "            Toni Wilen          \n"
                "     Press mouse to continue    ";

#if DELDIR
CONST_STRPTR deldirname = "\007.DELDIR";
#endif

