#include "adf_operations.h"

#include "adflib/src/adf_blk.h"

#include <sddl.h>
#include <spdlog/spdlog.h>
#include <iostream>
#include <mutex>
#include <sstream>
#include <unordered_map>

std::wstring mainEXE;

static NTSTATUS DOKAN_CALLBACK fs_deletefile(LPCWSTR filename, PDOKAN_FILE_INFO dokanfileinfo);

// Convert Amiga file attributes to Windows file attributes - only a few actually match
DWORD amigaToWindowsAttributes(const int32_t access, int32_t type) {
    DWORD result = 0;
    if (type == ST_DIR) result |= FILE_ATTRIBUTE_DIRECTORY;
    if (type == ST_ROOT) result |= FILE_ATTRIBUTE_DIRECTORY;
    if (hasA(access)) result |= FILE_ATTRIBUTE_ARCHIVE;
    if (hasW(access)) result |= FILE_ATTRIBUTE_READONLY;  // no write, its read only    
    if (result == 0) result = FILE_ATTRIBUTE_NORMAL;
    return result;
}
// Search for a file or folder, returns 0 if not found or the type of item (eg: ST_FILE)
int32_t locatePath(const std::wstring& path, PDOKAN_FILE_INFO dokanfileinfo, std::string& filename) {
    struct AdfVolume* v = reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->volume();
    fs* f = reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext);

    // Strip off prefix of '\'
    std::wstring search = ((path.length()) && (path[0] == '\\')) ? path.substr(1) : path;
    adfToRootDir(v);

    // Bypass for speed
    if (search.empty()) return ST_ROOT;

    filename.clear();

    size_t sepPos = search.find(L'\\');    
    size_t first = 0;
    while (sepPos != std::string::npos) {
        std::string amigaPath;
        f->windowsFilenameToAmigaFilename(search.substr(first, sepPos - first), amigaPath);
        if (adfChangeDir(v, amigaPath.c_str()) != RC_OK)
            return 0;
        filename = amigaPath;

        first = sepPos + 1;
        sepPos = search.find('\\', first);
    }
 
    if (first < search.length()) {
        std::string amigaPath;
        f->windowsFilenameToAmigaFilename(search.substr(first), amigaPath);
        filename = amigaPath;

        // Change to last dir/file
        if (adfChangeDir(v, amigaPath.c_str()) == RC_ERROR) return 0;
    }

    struct bEntryBlock parent;
    RETCODE rc = adfReadEntryBlock(v, v->curDirPtr, &parent);
    if (rc != RC_OK) 
        return 0; 
    
    return parent.secType;
}
// Stub version of the above
int32_t locatePath(const std::wstring& path, PDOKAN_FILE_INFO dokanfileinfo) {
    std::string filename;
    return locatePath(path, dokanfileinfo, filename);
}

static NTSTATUS DOKAN_CALLBACK fs_createfile(LPCWSTR filename, PDOKAN_IO_SECURITY_CONTEXT security_context, ACCESS_MASK desiredaccess, ULONG fileattributes, ULONG shareaccess, ULONG createdisposition, ULONG createoptions, PDOKAN_FILE_INFO dokanfileinfo) {
    
    ACCESS_MASK generic_desiredaccess;
    DWORD creation_disposition;
    DWORD file_attributes_and_flags;
    dokanfileinfo->Context = 0;

    struct AdfVolume* v = reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->volume();
    fs* f = reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext);

    DokanMapKernelToUserCreateFileFlags( desiredaccess, fileattributes, createoptions, createdisposition, &generic_desiredaccess, &file_attributes_and_flags, &creation_disposition);

    std::wstring windowsPath = filename;
    if (windowsPath.length() && (windowsPath[0] == '\\')) windowsPath.erase(windowsPath.begin());
    if (windowsPath.substr(0, 25) == L"System Volume Information" || windowsPath.substr(0,12) == L"$RECYCLE.BIN") return STATUS_NO_SUCH_FILE;

    int32_t search = locatePath(windowsPath, dokanfileinfo);
    SECTNUM locatedSecNum = v->curDirPtr;

    if ((search == ST_ROOT) || (search == ST_DIR)) dokanfileinfo->IsDirectory = true;

    if (dokanfileinfo->IsDirectory) {
        if (search == ST_FILE) 
            return STATUS_NOT_A_DIRECTORY;

        if (creation_disposition == CREATE_NEW || creation_disposition == OPEN_ALWAYS) {
            if (search != 0) 
                return STATUS_OBJECT_NAME_COLLISION;

            if (reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

            size_t parentFolder = windowsPath.rfind(L'\\');
            SECTNUM rootFolder = v->rootBlock;
            std::string amigaName;

            if (parentFolder != std::wstring::npos) {
                f->windowsFilenameToAmigaFilename(windowsPath.substr(parentFolder + 1), amigaName);
                int32_t search = locatePath(windowsPath.substr(0, parentFolder), dokanfileinfo);
                if ((search == ST_ROOT) || (search == ST_DIR))
                    rootFolder = v->curDirPtr;
                else 
                    return STATUS_OBJECT_NAME_NOT_FOUND;
            } else
                f->windowsFilenameToAmigaFilename(windowsPath, amigaName);

            if (amigaName.length() > MAXNAMELEN) return STATUS_OBJECT_NAME_INVALID;

            if (adfCountFreeBlocks(v) < 1) 
                return STATUS_DISK_FULL;

            if (adfCreateDir(v, rootFolder, amigaName.c_str()) != RC_OK) 
                return STATUS_DATA_ERROR;

            return STATUS_SUCCESS;
        }

        if (search == 0) 
            return STATUS_OBJECT_NAME_NOT_FOUND;
        return STATUS_SUCCESS;
    }
    else {
        int access = 0;
        if (generic_desiredaccess & GENERIC_READ) access |= AdfFileMode::ADF_FILE_MODE_READ;
        if (generic_desiredaccess & GENERIC_WRITE) access |= AdfFileMode::ADF_FILE_MODE_WRITE;
        if (generic_desiredaccess & GENERIC_ALL) access |= AdfFileMode::ADF_FILE_MODE_READ | AdfFileMode::ADF_FILE_MODE_WRITE;

        if (access & AdfFileMode::ADF_FILE_MODE_WRITE)
            if (reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

        size_t parentFolder = windowsPath.rfind(L'\\');
        std::string amigaName;

        struct bEntryBlock parent;
        RETCODE rc = adfReadEntryBlock(v, v->curDirPtr, &parent);
        if ((rc == RC_OK) && (parent.secType == ST_FILE)) {
            // Cannot delete a file with readonly attributes.
            if (file_attributes_and_flags & FILE_FLAG_DELETE_ON_CLOSE) 
                if ((file_attributes_and_flags & FILE_ATTRIBUTE_READONLY) || (hasW(parent.access) || (reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->isWriteProtected())))
                    return STATUS_CANNOT_DELETE;
            
            // Cannot open a readonly file for writing.
            if ((creation_disposition == OPEN_ALWAYS || creation_disposition == OPEN_EXISTING) && (hasW(parent.access)) && (desiredaccess & FILE_WRITE_DATA))
                return STATUS_ACCESS_DENIED;

            // Cannot overwrite an existing read only file.
            if ((creation_disposition == CREATE_NEW || (creation_disposition == CREATE_ALWAYS && createdisposition != FILE_SUPERSEDE) || creation_disposition == TRUNCATE_EXISTING) && (hasW(parent.access)))
                return STATUS_ACCESS_DENIED;

            // Attributes patch
            if (creation_disposition == CREATE_NEW || creation_disposition == CREATE_ALWAYS || creation_disposition == OPEN_ALWAYS || creation_disposition == TRUNCATE_EXISTING) {
                // Combines the file attributes and flags specified by
                file_attributes_and_flags |= FILE_ATTRIBUTE_ARCHIVE;
                // We merge the attributes with the existing file attributes
                if (f && createdisposition != FILE_SUPERSEDE) file_attributes_and_flags |= amigaToWindowsAttributes(parent.access, ST_FILE);
                // Remove non specific attributes.
                file_attributes_and_flags &= ~FILE_ATTRIBUTE_STRICTLY_SEQUENTIAL;
                // FILE_ATTRIBUTE_NORMAL is override if any other attribute is set.
                file_attributes_and_flags &= ~FILE_ATTRIBUTE_NORMAL;
            }
        }

        // Make sure the current folder is correct
        if (parentFolder != std::wstring::npos) {
            f->windowsFilenameToAmigaFilename(windowsPath.substr(parentFolder + 1), amigaName);
            int32_t search = locatePath(windowsPath.substr(0, parentFolder), dokanfileinfo);
            if (search == 0) return STATUS_OBJECT_NAME_NOT_FOUND;
            if ((search != ST_ROOT) && (search != ST_DIR))
                return STATUS_ACCESS_DENIED;
        }
        else {
            adfToRootDir(v);
            f->windowsFilenameToAmigaFilename(windowsPath, amigaName);
        }
        if (amigaName.length() > MAXNAMELEN) return STATUS_OBJECT_NAME_INVALID;

        // Some small patches to the access rights that you can't control from windows, but only if needed
        if ((rc == RC_OK) && (parent.secType == ST_FILE)) {
            int32_t oldAccess = parent.access;
            if ((file_attributes_and_flags & FILE_FLAG_DELETE_ON_CLOSE) || (access)) oldAccess &= ~(ACCMASK_R | ACCMASK_D);
            if (oldAccess != parent.access) 
                if (!(reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->isWriteProtected()))
                    adfSetEntryAccess(v, v->curDirPtr, amigaName.c_str(), oldAccess);
        }

        // Open fail?
        if (access == 0) {
            if (search != ST_FILE) return STATUS_OBJECT_NAME_NOT_FOUND;
            return STATUS_SUCCESS;
        }

        AdfFile* fle = nullptr;
         
        switch (creation_disposition) {
            case CREATE_ALWAYS:          
                if (search != ST_FILE) 
                    if (adfCountFreeBlocks(v) < 1) 
                        return STATUS_DISK_FULL;

                // Its possible to create, with GENERIC_WRITE not specified! so this bypasses the problem
                if (access != AdfFileMode::ADF_FILE_MODE_WRITE) {
                    fle = adfFileOpen(v, amigaName.c_str(), (AdfFileMode)AdfFileMode::ADF_FILE_MODE_WRITE);
                    if (!fle) return STATUS_ACCESS_DENIED;
                    adfFileTruncate(fle, 0);
                    adfFileClose(fle);
                }
                fle = adfFileOpen(v, amigaName.c_str(), (AdfFileMode)access);
                if (!fle) return STATUS_ACCESS_DENIED;
                adfFileTruncate(fle, 0);
                dokanfileinfo->Context = (ULONG64)fle;
                if (search == ST_FILE) return STATUS_OBJECT_NAME_COLLISION;
                break;

            case CREATE_NEW:
                // Fail if it already exists
                if (search == ST_FILE) return STATUS_OBJECT_NAME_COLLISION; 
                if (adfCountFreeBlocks(v) < 1) 
                    return STATUS_DISK_FULL;

                // Its possible to create, with GENERIC_WRITE not specified! so this bypasses the problem
                if (access != AdfFileMode::ADF_FILE_MODE_WRITE) {
                    fle = adfFileOpen(v, amigaName.c_str(), (AdfFileMode)AdfFileMode::ADF_FILE_MODE_WRITE);
                    if (!fle) return STATUS_ACCESS_DENIED;
                    adfFileClose(fle);
                }
                fle = adfFileOpen(v, amigaName.c_str(), (AdfFileMode)access);
                if (!fle) return STATUS_ACCESS_DENIED;
                dokanfileinfo->Context = (ULONG64)fle;
                break;

            case OPEN_ALWAYS: 
                fle = adfFileOpen(v, amigaName.c_str(), (AdfFileMode)access);
                if (!fle) return STATUS_ACCESS_DENIED;
                dokanfileinfo->Context = (ULONG64)fle;
                break;

            case OPEN_EXISTING:
                if (search == 0) return STATUS_OBJECT_NAME_NOT_FOUND;

                fle = adfFileOpen(v, amigaName.c_str(), (AdfFileMode)access);
                if (!fle) return STATUS_ACCESS_DENIED;
                dokanfileinfo->Context = (ULONG64)fle;
                break;

            case TRUNCATE_EXISTING:
                if (search == 0)  return STATUS_OBJECT_NAME_NOT_FOUND;

                fle = adfFileOpen(v, amigaName.c_str(), (AdfFileMode)access);
                if (fle) adfFileTruncate(fle, 0);
                if (!fle) return STATUS_ACCESS_DENIED;
                dokanfileinfo->Context = (ULONG64)fle;
                break;
            default:
                // Unknown
                return STATUS_ACCESS_DENIED;
        }

        return STATUS_SUCCESS;
    }
    return STATUS_ACCESS_DENIED;
}

static void DOKAN_CALLBACK fs_cleanup(LPCWSTR filename, PDOKAN_FILE_INFO dokanfileinfo) {
    UNREFERENCED_PARAMETER(filename);
    if (dokanfileinfo->Context) {
        AdfFile* fle = (AdfFile*)dokanfileinfo->Context;
        adfFileClose(fle); 
        dokanfileinfo->Context = 0;
    }
    if (dokanfileinfo->DeleteOnClose) {
        // Delete happens during cleanup and not in close event.
        fs_deletefile(filename, dokanfileinfo);
    }
}

static void DOKAN_CALLBACK fs_closeFile(LPCWSTR filename, PDOKAN_FILE_INFO dokanfileinfo) {
    UNREFERENCED_PARAMETER(filename);
    UNREFERENCED_PARAMETER(dokanfileinfo);
}

static NTSTATUS DOKAN_CALLBACK fs_readfile(LPCWSTR filename, LPVOID buffer, DWORD bufferlength, LPDWORD readlength, LONGLONG offset, PDOKAN_FILE_INFO dokanfileinfo) {
    UNREFERENCED_PARAMETER(filename);

    if (dokanfileinfo->Context) {
        AdfFile* fle = (AdfFile*)dokanfileinfo->Context;

        if (adfFileGetPos(fle) != offset)
            if (adfFileSeek(fle, offset) != RC_OK)
                return STATUS_DATA_ERROR;

        DWORD lenRead = adfFileRead(fle, bufferlength, (uint8_t*)buffer);
        if (readlength) *readlength = lenRead;

        // This shouldn't ever happen
        if (fle->curDataPtr == 0) return STATUS_DATA_ERROR;
}
    else *readlength = 0;
    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK fs_writefile(LPCWSTR filename, LPCVOID buffer, DWORD number_of_bytes_to_write, LPDWORD number_of_bytes_written, LONGLONG offset, PDOKAN_FILE_INFO dokanfileinfo) {
    if (reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

    if (dokanfileinfo->Context) {
        AdfFile* fle = (AdfFile*)dokanfileinfo->Context;

        if (offset == -1) {
            if (adfFileSeekEOF(fle) != RC_OK) return STATUS_DATA_ERROR;
            offset = adfFileGetSize(fle);
        }

        if (dokanfileinfo->PagingIo) {
            // PagingIo cannot extend file size.
            // We return STATUS_SUCCESS when offset is beyond fileSize
            // and write the maximum we are allowed to.
            if (offset >= adfFileGetSize(fle)) {
                if (number_of_bytes_written) *number_of_bytes_written = 0;
                return STATUS_SUCCESS;
            }

            if ((offset + number_of_bytes_to_write) > adfFileGetSize(fle)) {
                // resize the write length to not go beyond file size.
                LONGLONG bytes = adfFileGetSize(fle) - offset;
                if (bytes >> 32) {
                    number_of_bytes_to_write = static_cast<DWORD>(bytes & 0xFFFFFFFFUL);
                }
                else {
                    number_of_bytes_to_write = static_cast<DWORD>(bytes);
                }
            }
        }

        //, offset
        if (adfFileGetPos(fle) != offset)
            if (adfFileSeek(fle, offset) != RC_OK)
                return STATUS_DATA_ERROR;

        DWORD written = adfFileWrite(fle,number_of_bytes_to_write, (uint8_t*)buffer);
        if (number_of_bytes_written) *number_of_bytes_written = written;

        if (written < number_of_bytes_to_write)  return STATUS_DISK_FULL;

        // This shouldn't ever happen
        if (fle->curDataPtr == 0) return STATUS_DATA_ERROR;
    }
      
    return STATUS_SUCCESS;        
}

static NTSTATUS DOKAN_CALLBACK fs_flushfilebuffers(LPCWSTR filename, PDOKAN_FILE_INFO dokanfileinfo) {
    UNREFERENCED_PARAMETER(filename);
    if (reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

    if (dokanfileinfo->Context) {
        AdfFile* fle = (AdfFile*)dokanfileinfo->Context;
        if (adfFileFlush(fle) == RC_OK) return STATUS_SUCCESS;
    }
    else return STATUS_OBJECT_NAME_NOT_FOUND;
    return STATUS_ACCESS_DENIED;
}

static NTSTATUS DOKAN_CALLBACK fs_setendoffile(LPCWSTR filename, LONGLONG ByteOffset, PDOKAN_FILE_INFO dokanfileinfo) {
    UNREFERENCED_PARAMETER(filename);
    if (reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

    if (dokanfileinfo->Context) {
        AdfFile* fle = (AdfFile*)dokanfileinfo->Context;
        if (adfFileTruncate(fle, ByteOffset) == RC_OK) return STATUS_SUCCESS;
    } else return STATUS_OBJECT_NAME_NOT_FOUND;
    return STATUS_ACCESS_DENIED;
}

static NTSTATUS DOKAN_CALLBACK fs_setallocationsize(LPCWSTR filename, LONGLONG alloc_size, PDOKAN_FILE_INFO dokanfileinfo) {
    UNREFERENCED_PARAMETER(filename);
    if (reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

    if (dokanfileinfo->Context) {
        AdfFile* fle = (AdfFile*)dokanfileinfo->Context;
        uint32_t siz = adfFileGetSize(fle);
        if (siz == alloc_size) return STATUS_SUCCESS;

        if (alloc_size < siz) {
            if (adfFileTruncate(fle, alloc_size) == RC_OK) return STATUS_SUCCESS;
        }
        else {
            uint32_t pos = adfFileGetPos(fle);
            if (adfFileSeekEOF(fle) == RC_OK) {
                uint32_t toWrite = alloc_size - siz;
                uint32_t written = adfFileWriteFilled(fle, 0, toWrite);
                adfFileSeek(fle, pos);

                if (toWrite > written) 
                    return STATUS_DISK_FULL;
                return STATUS_SUCCESS;
            }
        }
    }
    else return STATUS_OBJECT_NAME_NOT_FOUND;
    return STATUS_DATA_ERROR;
}

static NTSTATUS DOKAN_CALLBACK fs_getfileInformation(LPCWSTR filename, LPBY_HANDLE_FILE_INFORMATION buffer, PDOKAN_FILE_INFO dokanfileinfo) {
    struct AdfVolume* v = reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->volume();
    fs* f = reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext);

    // This is queried for folders and volume id
    int32_t search = locatePath(filename, dokanfileinfo);
    if (search == 0) return STATUS_OBJECT_NAME_NOT_FOUND;

    struct bEntryBlock parent;
    RETCODE rc = adfReadEntryBlock(v, v->curDirPtr, &parent);
    if (rc != RC_OK) 
        return STATUS_OBJECT_NAME_NOT_FOUND;
    if ((parent.secType != ST_FILE) && (parent.secType != ST_DIR) && (parent.secType != ST_ROOT)) return STATUS_OBJECT_NAME_NOT_FOUND;

    struct AdfEntry e;
 
    e.access = parent.access;
    e.type = parent.secType;
    buffer->dwFileAttributes =amigaToWindowsAttributes(parent.access, search);
    buffer->nNumberOfLinks = 1;
    buffer->nFileIndexHigh = 0;
    buffer->nFileIndexLow = 0;
    buffer->nFileSizeHigh = 0;
    buffer->nFileSizeLow = parent.byteSize;
    buffer->dwVolumeSerialNumber = f->volumeSerial();
    SYSTEMTIME tm;

    int year, month, days;
    adfDays2Date(parent.days, &year, &month, &days);
    tm.wHour = parent.mins / 60;
    tm.wMinute = parent.mins % 60;
    tm.wSecond = parent.ticks / 50;
    tm.wYear = year;
    tm.wMonth = month;
    tm.wDay = days;
    tm.wMilliseconds = 0;
    tm.wDayOfWeek = 0;
    buffer->ftLastAccessTime = { 0, 0 };    
    if (parent.secType == ST_FILE) {
        buffer->ftCreationTime = { 0, 0 };
        SystemTimeToFileTime(&tm, &buffer->ftLastWriteTime);
    } else {
        buffer->ftLastWriteTime = { 0, 0 };
        SystemTimeToFileTime(&tm, &buffer->ftCreationTime);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK fs_findfiles(LPCWSTR filename, PFillFindData fill_finddata, PDOKAN_FILE_INFO dokanfileinfo) {
    struct AdfVolume* v = reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->volume();
    fs* f = reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext);

    WIN32_FIND_DATAW findData;
    ZeroMemory(&findData, sizeof(WIN32_FIND_DATAW));

    int32_t search = locatePath(filename, dokanfileinfo);
    if ((search != ST_DIR) && (search != ST_ROOT)) return STATUS_OBJECT_NAME_NOT_FOUND;

    struct AdfList* list = adfGetDirEnt(v, v->curDirPtr);
    for (struct AdfList* node = list; node; node = node->next) {
        struct AdfEntry* e = (struct AdfEntry* ) node->content;
        if (e->type != ST_FILE && e->type != ST_DIR) continue;

        std::wstring winFilename;
        f->amigaFilenameToWindowsFilename(e->name, winFilename);

        wcscpy_s(findData.cFileName, winFilename.c_str());
        findData.dwFileAttributes = amigaToWindowsAttributes(e->access, e->type);
        findData.nFileSizeHigh = 0;
        findData.nFileSizeLow = e->size;
        SYSTEMTIME tm;
        tm.wYear = e->year;
        tm.wMonth = e->month;
        tm.wDay = e->days;
        tm.wHour = e->hour;
        tm.wMinute = e->mins;
        tm.wSecond = e->secs;
        tm.wMilliseconds = 0;
        tm.wDayOfWeek = 0;
        SystemTimeToFileTime(&tm, &findData.ftLastWriteTime);
        
        fill_finddata(&findData, dokanfileinfo);
    }
    adfFreeDirList(list);

    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK fs_setfileattributes(LPCWSTR filename, DWORD fileattributes, PDOKAN_FILE_INFO dokanfileinfo) {
    struct AdfVolume* v = reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->volume();
    if (reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

    std::wstring windowsPath = filename;
    if (windowsPath.length() && (windowsPath[0] == '\\')) windowsPath.erase(windowsPath.begin());

    std::string amigafilename;
    int32_t search = locatePath(windowsPath, dokanfileinfo, amigafilename);
    if (search != ST_FILE) return STATUS_OBJECT_NAME_NOT_FOUND;

    struct bEntryBlock parent;
    RETCODE rc = adfReadEntryBlock(v, v->curDirPtr, &parent);
    if (rc != RC_OK) return false;
    
    if (parent.secType != ST_FILE) return STATUS_OBJECT_NAME_NOT_FOUND;
    int32_t access = parent.access | ACCMASK_R | ACCMASK_D;   // force it to be readible and deletible
   
    if (fileattributes & FILE_ATTRIBUTE_ARCHIVE)
        access |= ACCMASK_A; else access &= ~ACCMASK_A;
    if (fileattributes & FILE_ATTRIBUTE_READONLY)
        access |= ACCMASK_W; else  access &= ~ACCMASK_W; 

    adfParentDir(v);

    if (adfSetEntryAccess(v, v->curDirPtr, amigafilename.c_str(), access) == RC_OK) return STATUS_SUCCESS;
    return STATUS_DATA_ERROR;
}

static NTSTATUS DOKAN_CALLBACK fs_setfiletime(LPCWSTR filename, CONST FILETIME* creationtime, CONST FILETIME* lastaccesstime, CONST FILETIME* lastwritetime, PDOKAN_FILE_INFO dokanfileinfo) {
    struct AdfVolume* v = reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->volume();
    if (reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

    std::wstring windowsPath = filename;
    if (windowsPath.length() && (windowsPath[0] == '\\')) windowsPath.erase(windowsPath.begin());

    int32_t search = locatePath(windowsPath, dokanfileinfo);
    if (search != ST_FILE) return STATUS_OBJECT_NAME_NOT_FOUND;

    // We only support last write time
    if (!lastwritetime) return STATUS_SUCCESS;

    struct bEntryBlock parent;
    RETCODE rc = adfReadEntryBlock(v, v->curDirPtr, &parent);
    if (rc != RC_OK) return false;

    if (parent.secType != ST_FILE) return STATUS_OBJECT_NAME_NOT_FOUND;

    DateTime tm;

    SYSTEMTIME sys;
    FileTimeToSystemTime(lastwritetime, &sys);
    tm.day = sys.wDay;
    tm.hour = sys.wHour;
    tm.min = sys.wMinute;
    tm.sec = sys.wSecond;
    tm.year = max(0, sys.wYear - 1900);
    tm.mon = sys.wMonth;    

    adfTime2AmigaTime(tm, &parent.days, &parent.mins, &parent.ticks);

    if (adfWriteEntryBlock(v, v->curDirPtr, &parent) == RC_OK) return STATUS_SUCCESS;
    return STATUS_DATA_ERROR;
}

static NTSTATUS DOKAN_CALLBACK fs_deletefile(LPCWSTR filename, PDOKAN_FILE_INFO dokanfileinfo) {
    struct AdfVolume* v = reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->volume();
    if (reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

    std::wstring windowsPath = filename;
    if (windowsPath.length() && (windowsPath[0] == '\\')) windowsPath.erase(windowsPath.begin());

    std::string amigaName;
    int32_t search = locatePath(windowsPath, dokanfileinfo, amigaName);
    if (search == 0) return STATUS_OBJECT_NAME_NOT_FOUND;
    if (search != ST_FILE) return STATUS_ACCESS_DENIED;

    if (reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->isFileInUse(v->curDirPtr, amigaName))
        return STATUS_ACCESS_DENIED;

    struct bEntryBlock parent;
    if (adfReadEntryBlock(v, v->curDirPtr, &parent) == RC_OK) {
        if ((parent.secType == ST_FILE) && (hasW(parent.access)))
            return STATUS_CANNOT_DELETE;
    }

    adfParentDir(v);

    if (adfRemoveEntry(v, v->curDirPtr, amigaName.c_str()) == RC_OK) return STATUS_SUCCESS;   
    return STATUS_DATA_ERROR;
}

static NTSTATUS DOKAN_CALLBACK fs_deletedirectory(LPCWSTR filename, PDOKAN_FILE_INFO dokanfileinfo) {
    struct AdfVolume* v = reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->volume();
    if (reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

    std::wstring windowsPath = filename;
    if (windowsPath.length() && (windowsPath[0] == '\\')) windowsPath.erase(windowsPath.begin());

    std::string amigaName;
    int32_t search = locatePath(windowsPath, dokanfileinfo, amigaName);
    if (search == 0) return STATUS_OBJECT_NAME_NOT_FOUND;
    if (search != ST_DIR) 
        return STATUS_ACCESS_DENIED;

    struct bDirBlock dirBlock;
    if (adfReadEntryBlock(v, v->curDirPtr, (struct bEntryBlock*)&dirBlock) != RC_OK) return STATUS_DATA_ERROR;

    if (!isDirEmpty(&dirBlock)) return STATUS_DIRECTORY_NOT_EMPTY;

    adfParentDir(v);

    if (adfRemoveEntry(v, v->curDirPtr, amigaName.c_str()) == RC_OK) return STATUS_SUCCESS;
    return STATUS_DATA_ERROR;
}

static NTSTATUS DOKAN_CALLBACK fs_movefile(LPCWSTR filename, LPCWSTR new_filename, BOOL replace_if_existing, PDOKAN_FILE_INFO dokanfileinfo) {
    struct AdfVolume* v = reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->volume();
    if (reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

    std::wstring windowsPath = filename;
    if (windowsPath.length() && (windowsPath[0] == '\\')) windowsPath.erase(windowsPath.begin());

    std::string amigaName;
    int32_t srcFileFolder = locatePath(windowsPath, dokanfileinfo, amigaName);
    if (srcFileFolder == 0) return STATUS_OBJECT_NAME_NOT_FOUND;

    adfParentDir(v);
    SECTNUM srcSector = v->curDirPtr;
    SECTNUM dstSector = srcSector;


    std::wstring newName = new_filename;
    if (newName.length() && (newName[0] == '\\')) newName.erase(newName.begin());

    std::string amigaTargetName;

    std::string targetNameOutput;
    int32_t target = locatePath(new_filename, dokanfileinfo, targetNameOutput);
    if ((!replace_if_existing) && (target != 0)) return STATUS_OBJECT_NAME_EXISTS;
    SECTNUM targetNameSec = v->curDirPtr;

    size_t i = newName.rfind('\\');
    if (i != std::wstring::npos) {
        // Locate target path
        int32_t dstFileFolder = locatePath(newName.substr(0, i), dokanfileinfo);
        dstSector = v->curDirPtr;
        if (dstFileFolder == 0) return STATUS_OBJECT_NAME_NOT_FOUND;
       
        reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->windowsFilenameToAmigaFilename(newName.substr(i+1), amigaTargetName);       
    }
    else {
        dstSector = v->rootBlock;
        reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->windowsFilenameToAmigaFilename(newName, amigaTargetName);
    }

    // Try to remove the target first
    if (target != 0) {
        if (adfRemoveEntry(v, targetNameSec, targetNameOutput.c_str()) != RC_OK) 
            return STATUS_ACCESS_DENIED;
    }

    if (amigaTargetName.length() > MAXNAMELEN) return STATUS_OBJECT_NAME_INVALID;

    if (adfRenameEntry(v, srcSector, amigaName.c_str(), dstSector, amigaTargetName.c_str()) == RC_OK) return STATUS_SUCCESS;

    return STATUS_ACCESS_DENIED;
}

static NTSTATUS DOKAN_CALLBACK fs_getdiskfreespace(PULONGLONG free_bytes_available, PULONGLONG total_number_of_bytes, PULONGLONG total_number_of_free_bytes, PDOKAN_FILE_INFO dokanfileinfo) {
    struct AdfVolume* v = reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->volume();
    uint32_t numBlocks = (v->lastBlock - v->firstBlock) - 1;  
    uint32_t blocksFree = adfCountFreeBlocks(v);
    *total_number_of_bytes = numBlocks * v->datablockSize;
    *total_number_of_free_bytes = blocksFree * v->datablockSize;    
    *free_bytes_available = blocksFree * v->datablockSize;
    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK fs_getvolumeinformation( LPWSTR volumename_buffer, DWORD volumename_size, LPDWORD volume_serialnumber, LPDWORD maximum_component_length, LPDWORD filesystem_flags, LPWSTR filesystem_name_buffer, DWORD filesystem_name_size, PDOKAN_FILE_INFO dokanfileinfo) {
    
    struct AdfVolume* v = reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->volume();

    std::wstring volName; if (v->volName) ansiToWide(v->volName, volName);
    wcscpy_s(volumename_buffer, volumename_size, volName.c_str());
    *volume_serialnumber = reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->volumeSerial(); // make one up
    *maximum_component_length = MAXNAMELEN;
    *filesystem_flags = FILE_CASE_PRESERVED_NAMES | FILE_SUPPORTS_REMOTE_STORAGE;

    std::wstring volType = L"Amiga ";

    switch (v->dev->devType) {
    case DEVTYPE_FLOPDD: volType += L"DD Floppy "; break;
    case DEVTYPE_FLOPHD: volType += L"HD Floppy "; break;
    case DEVTYPE_HARDDISK: volType += L"HD Partition "; break;
    case DEVTYPE_HARDFILE: volType += L"HD File "; break;
    }

    volType += isFFS(v->dosType) ? L"FFS" : L"OFS";
    if (isINTL(v->dosType)) volType += L" Intl";
    if (isDIRCACHE(v->dosType)) volType += L" Dir Cache";
    wcscpy_s(filesystem_name_buffer, filesystem_name_size, volType.c_str());

    if (reinterpret_cast<fs*>(dokanfileinfo->DokanOptions->GlobalContext)->isWriteProtected()) *filesystem_flags |= FILE_READ_ONLY_VOLUME;

    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK fs_mounted(LPCWSTR MountPoint, PDOKAN_FILE_INFO dokanfileinfo) {
    WCHAR path[128];
    WCHAR c = towupper(MountPoint[0]);    
    swprintf_s(path, L"Software\\Classes\\Applications\\Explorer.exe\\Drives\\%c\\DefaultIcon", c);
    RegSetValue(HKEY_CURRENT_USER, path, REG_SZ, mainEXE.c_str(), 1);     
    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK fs_unmounted(PDOKAN_FILE_INFO dokanfileinfo) {
    UNREFERENCED_PARAMETER(dokanfileinfo);
    return STATUS_SUCCESS;
}

DOKAN_OPERATIONS    fs_operations = { fs_createfile,
                                        fs_cleanup,
                                        fs_closeFile,
                                        fs_readfile,
                                        fs_writefile,
                                        fs_flushfilebuffers,
                                        fs_getfileInformation,
                                        fs_findfiles,
                                        nullptr,  // FindFilesWithPattern
                                        fs_setfileattributes,
                                        fs_setfiletime,
                                        fs_deletefile,
                                        fs_deletedirectory,
                                        fs_movefile,
                                        fs_setendoffile,
                                        fs_setallocationsize,
                                        nullptr, // fs_lockfile,
                                        nullptr, // fs_unlockfile,
                                        fs_getdiskfreespace,
                                        fs_getvolumeinformation,
                                        fs_mounted,
                                        fs_unmounted,
                                        nullptr,
                                        nullptr,
                                        nullptr // FindStreams;
};
