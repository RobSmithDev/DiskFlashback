/* DiskFlashback, Copyright (C) 2021-2024 Robert Smith (@RobSmithDev)
 * https://robsmithdev.co.uk/diskflashback
 *
 * This file is multi-licensed under the terms of the Mozilla Public
 * License Version 2.0 as published by Mozilla Corporation and the
 * GNU General Public License, version 2 or later, as published by the
 * Free Software Foundation.
 *
 * MPL2: https://www.mozilla.org/en-US/MPL/2.0/
 * GPL2: https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html
 *
 * This file is maintained at https://github.com/RobSmithDev/DiskFlashback
 */

#include <dokan/dokan.h>
#include "adf_operations.h"
#define BUILDING_WITH_CMAKE
#include "adflib/src/adflib.h"
#include <time.h>
#include <algorithm>
#include <Shlobj.h>

DokanFileSystemAmigaFS::DokanFileSystemAmigaFS(DokanFileSystemManager* owner, bool autoRename) : DokanFileSystemAmiga(owner, autoRename) {
}

bool DokanFileSystemAmigaFS::isFileSystemReady() {
    return m_volume != nullptr;
}

bool DokanFileSystemAmigaFS::isDiskInUse() {
    if (!m_volume) return false;
    return !m_inUse.empty();
}


// Return TRUE if file is in use
bool DokanFileSystemAmigaFS::isFileInUse(const char* const name, const AdfFileMode mode) {
    if (!m_volume) return false;

    // Horrible way to do this 
    AdfFile* fle = adfFileOpen(m_volume, name, AdfFileMode::ADF_FILE_MODE_READ);
    if (!fle) return false;
    ADF_SECTNUM fleKey = fle->fileHdr->headerKey;
    adfFileClose(fle);

    for (const auto& openFle : m_inUse)
        if (openFle.first->fileHdr->headerKey == fleKey) {
            // Match - if its read only and we're read only thats ok
            if ((mode & AdfFileMode::ADF_FILE_MODE_WRITE) || (openFle.first->modeWrite)) return true;
        }

    return false;
}
void DokanFileSystemAmigaFS::addTrackFileInUse(AdfFile* handle) {
    m_inUse.insert(std::make_pair(handle, 1));
}
void DokanFileSystemAmigaFS::releaseFileInUse(AdfFile* handle) {
    auto f = m_inUse.find(handle);
    if (f != m_inUse.end()) m_inUse.erase(f);
}

void DokanFileSystemAmigaFS::setCurrentVolume(AdfVolume* volume) { 
    m_inUse.clear();
    m_volume = volume; 
}

// Convert Amiga file attributes to Windows file attributes - only a few actually match
DWORD DokanFileSystemAmigaFS::amigaToWindowsAttributes(const int32_t access, int32_t type) {
    DWORD result = 0;
    if (type == ADF_ST_DIR) result |= FILE_ATTRIBUTE_DIRECTORY;
    if (type == ADF_ST_ROOT) result |= FILE_ATTRIBUTE_DIRECTORY;
    if (adfAccHasA(access)) result |= FILE_ATTRIBUTE_ARCHIVE;
    if (adfAccHasW(access)) result |= FILE_ATTRIBUTE_READONLY;  // no write, its read only    
    return result;
}

// Search for a file or folder, returns 0 if not found or the type of item (eg: ST_FILE)
int32_t DokanFileSystemAmigaFS::locatePath(const std::wstring& path, PDOKAN_FILE_INFO dokanfileinfo, std::string& filename) {
    // Strip off prefix of '\'
    std::wstring search = ((path.length()) && (path[0] == '\\')) ? path.substr(1) : path;
    if (!m_volume) return 0;
    adfToRootDir(m_volume);

    // Bypass for speed
    if (search.empty()) return ADF_ST_ROOT;

    filename.clear();

    size_t sepPos = search.find(L'\\');    
    size_t first = 0;
    while (sepPos != std::string::npos) {
        std::string amigaPath;
        windowsFilenameToAmigaFilename(search.substr(first, sepPos - first), amigaPath);
        if (adfChangeDir(m_volume, amigaPath.c_str()) != ADF_RETCODE::ADF_RC_OK)
            return 0;
        filename = amigaPath;

        first = sepPos + 1;
        sepPos = search.find('\\', first);
    }
 
    if (first < search.length()) {
        std::string amigaPath;
        windowsFilenameToAmigaFilename(search.substr(first), amigaPath);
        filename = amigaPath;

        // Change to last dir/file
        if (adfChangeDir(m_volume, amigaPath.c_str()) == ADF_RETCODE::ADF_RC_ERROR) return 0;
    }

    struct AdfEntryBlock parent;
    ADF_RETCODE rc = adfReadEntryBlock(m_volume, m_volume->curDirPtr, &parent);
    if (rc != ADF_RETCODE::ADF_RC_OK)  return 0;
    
    return parent.secType;
}
// Stub version of the above
int32_t DokanFileSystemAmigaFS::locatePath(const std::wstring& path, PDOKAN_FILE_INFO dokanfileinfo) {
    std::string filename;
    return locatePath(path, dokanfileinfo, filename);
}

NTSTATUS DokanFileSystemAmigaFS::fs_createfile(const std::wstring& filename, const PDOKAN_IO_SECURITY_CONTEXT security_context, const ACCESS_MASK generic_desiredaccess, const uint32_t file_attributes, const uint32_t shareaccess, const uint32_t creation_disposition, const bool fileSupersede, PDOKAN_FILE_INFO dokanfileinfo) {
    dokanfileinfo->Context = 0;
    uint32_t file_attributes_and_flags = file_attributes;
    if (!m_volume) return STATUS_UNRECOGNIZED_MEDIA;

    std::wstring windowsPath = filename;

    int32_t search = locatePath(windowsPath, dokanfileinfo);
    ADF_SECTNUM locatedSecNum = m_volume->curDirPtr;
   
    if ((search == ADF_ST_ROOT) || (search == ADF_ST_DIR)) dokanfileinfo->IsDirectory = true;

    if (dokanfileinfo->IsDirectory) {
        if (search == ADF_ST_FILE)  return STATUS_NOT_A_DIRECTORY;

        if (creation_disposition == CREATE_NEW || creation_disposition == OPEN_ALWAYS) {
            if (search != 0) 
                return STATUS_OBJECT_NAME_COLLISION;

            if (isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

            size_t parentFolder = windowsPath.rfind(L'\\');
            ADF_SECTNUM rootFolder = m_volume->rootBlock;
            std::string amigaName;

            if (parentFolder != std::wstring::npos) {
                windowsFilenameToAmigaFilename(windowsPath.substr(parentFolder + 1), amigaName);
                int32_t search = locatePath(windowsPath.substr(0, parentFolder), dokanfileinfo);
                if ((search == ADF_ST_ROOT) || (search == ADF_ST_DIR))
                    rootFolder = m_volume->curDirPtr;
                else 
                    return STATUS_OBJECT_NAME_NOT_FOUND;
            } else
                windowsFilenameToAmigaFilename(windowsPath, amigaName);

            if (amigaName.length() > ADF_MAX_NAME_LEN) return STATUS_OBJECT_NAME_INVALID;

            ActiveFileIO io = notifyIOInUse(dokanfileinfo);

            if (adfCountFreeBlocks(m_volume) < 1) 
                return STATUS_DISK_FULL;

            if (adfCreateDir(m_volume, rootFolder, amigaName.c_str()) != ADF_RETCODE::ADF_RC_OK)
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
            if (isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

        size_t parentFolder = windowsPath.rfind(L'\\');
        std::string amigaName;

        struct AdfEntryBlock parent;
        ActiveFileIO io = notifyIOInUse(dokanfileinfo);

        ADF_RETCODE rc = adfReadEntryBlock(m_volume, m_volume->curDirPtr, &parent);
        if ((rc == ADF_RETCODE::ADF_RC_OK) && (parent.secType == ADF_ST_FILE)) {
            // Cannot delete a file with readonly attributes.
            if (file_attributes_and_flags & FILE_FLAG_DELETE_ON_CLOSE) 
                if ((file_attributes_and_flags & FILE_ATTRIBUTE_READONLY) || (adfAccHasW(parent.access) || (isWriteProtected())))
                    return STATUS_CANNOT_DELETE;
            
            // Cannot open a readonly file for writing.
            if ((creation_disposition == OPEN_ALWAYS || creation_disposition == OPEN_EXISTING) && (adfAccHasW(parent.access)) && (generic_desiredaccess & GENERIC_WRITE))
                return STATUS_ACCESS_DENIED;

            // Cannot overwrite an existing read only file.
            if ((creation_disposition == CREATE_NEW || (creation_disposition == CREATE_ALWAYS && !fileSupersede) || creation_disposition == TRUNCATE_EXISTING) && (adfAccHasW(parent.access)))
                return STATUS_ACCESS_DENIED;

            // Attributes patch
            if (creation_disposition == CREATE_NEW || creation_disposition == CREATE_ALWAYS || creation_disposition == OPEN_ALWAYS || creation_disposition == TRUNCATE_EXISTING) {
                // Combines the file attributes and flags specified by
                file_attributes_and_flags |= FILE_ATTRIBUTE_ARCHIVE;
                // We merge the attributes with the existing file attributes
                if (!fileSupersede) file_attributes_and_flags |= amigaToWindowsAttributes(parent.access, ADF_ST_FILE);
                // Remove non specific attributes.
                file_attributes_and_flags &= ~FILE_ATTRIBUTE_STRICTLY_SEQUENTIAL;
                // FILE_ATTRIBUTE_NORMAL is override if any other attribute is set.
                file_attributes_and_flags &= ~FILE_ATTRIBUTE_NORMAL;
            }
        }

        // Make sure the current folder is correct
        if (parentFolder != std::wstring::npos) {
            windowsFilenameToAmigaFilename(windowsPath.substr(parentFolder + 1), amigaName);
            int32_t search = locatePath(windowsPath.substr(0, parentFolder), dokanfileinfo);
            if (search == 0) return STATUS_OBJECT_NAME_NOT_FOUND;
            if ((search != ADF_ST_ROOT) && (search != ADF_ST_DIR))
                return STATUS_ACCESS_DENIED;
        }
        else {
            adfToRootDir(m_volume);
            windowsFilenameToAmigaFilename(windowsPath, amigaName);
        }
        if (amigaName.length() > ADF_MAX_NAME_LEN) return STATUS_OBJECT_NAME_INVALID;

        // Some small patches to the access rights that you can't control from windows, but only if needed
       // if ((rc == RC_OK) && (parent.secType == ST_FILE)) {
       //     int32_t oldAccess = parent.access;
       //     if ((file_attributes_and_flags & FILE_FLAG_DELETE_ON_CLOSE) || (access)) oldAccess &= ~(ACCMASK_R | ACCMASK_D);
       //     if (oldAccess != parent.access) 
       //         if (!isWriteProtected())
       //             adfSetEntryAccess(m_volume, m_volume->curDirPtr, amigaName.c_str(), oldAccess);
       // }

        // Open fail?
        if (access == 0) {
            if (creation_disposition == OPEN_EXISTING) {
                if (search != ADF_ST_FILE) return STATUS_OBJECT_NAME_NOT_FOUND;
            }
            return STATUS_SUCCESS;
        }
      
        AdfFile* fle = nullptr;

        if (isFileInUse(amigaName.c_str(), (AdfFileMode)access)) return STATUS_SHARING_VIOLATION;
         
        switch (creation_disposition) {
            case CREATE_ALWAYS:          
                if (search != ADF_ST_FILE) 
                    if (adfCountFreeBlocks(m_volume) < 1)
                        return STATUS_DISK_FULL;

                // Its possible to create, with GENERIC_WRITE not specified! so this bypasses the problem
                if (access != AdfFileMode::ADF_FILE_MODE_WRITE) {
                    fle = adfFileOpen(m_volume, amigaName.c_str(), (AdfFileMode)AdfFileMode::ADF_FILE_MODE_WRITE);
                    if (!fle) return STATUS_ACCESS_DENIED;
                    adfFileTruncate(fle, 0);
                    adfFileClose(fle);
                }
                fle = adfFileOpen(m_volume, amigaName.c_str(), (AdfFileMode)access);
                if (!fle) return STATUS_ACCESS_DENIED;
                adfFileTruncate(fle, 0);
                dokanfileinfo->Context = (ULONG64)fle;
                addTrackFileInUse(fle);
                if (search == ADF_ST_FILE) return STATUS_OBJECT_NAME_COLLISION;
                break;

            case CREATE_NEW:
                // Fail if it already exists
                if (search == ADF_ST_FILE) return STATUS_OBJECT_NAME_COLLISION; 
                if (adfCountFreeBlocks(m_volume) < 1)
                    return STATUS_DISK_FULL;

                // Its possible to create, with GENERIC_WRITE not specified! so this bypasses the problem
                if (access != AdfFileMode::ADF_FILE_MODE_WRITE) {
                    fle = adfFileOpen(m_volume, amigaName.c_str(), (AdfFileMode)AdfFileMode::ADF_FILE_MODE_WRITE);
                    if (!fle) return STATUS_ACCESS_DENIED;
                    adfFileClose(fle);
                }
                fle = adfFileOpen(m_volume, amigaName.c_str(), (AdfFileMode)access);
                if (!fle) return STATUS_ACCESS_DENIED;
                dokanfileinfo->Context = (ULONG64)fle;
                addTrackFileInUse(fle);
                break;

            case OPEN_ALWAYS: 
                fle = adfFileOpen(m_volume, amigaName.c_str(), (AdfFileMode)access);
                if (!fle) return STATUS_ACCESS_DENIED;
                dokanfileinfo->Context = (ULONG64)fle;
                addTrackFileInUse(fle);
                break;

            case OPEN_EXISTING:
                if (search == 0) return STATUS_OBJECT_NAME_NOT_FOUND;

                fle = adfFileOpen(m_volume, amigaName.c_str(), (AdfFileMode)access);
                if (!fle) return STATUS_ACCESS_DENIED;
                dokanfileinfo->Context = (ULONG64)fle;
                addTrackFileInUse(fle);
                break;

            case TRUNCATE_EXISTING:
                if (search == 0)  return STATUS_OBJECT_NAME_NOT_FOUND;

                fle = adfFileOpen(m_volume, amigaName.c_str(), (AdfFileMode)access);
                if (!fle) return STATUS_ACCESS_DENIED;
                adfFileTruncate(fle, 0);
                dokanfileinfo->Context = (ULONG64)fle;
                addTrackFileInUse(fle);
                break;
            default:
                // Unknown
                return STATUS_ACCESS_DENIED;
        }

        return STATUS_SUCCESS;
    }
    return STATUS_ACCESS_DENIED;
}

void DokanFileSystemAmigaFS::fs_cleanup(const std::wstring& filename, PDOKAN_FILE_INFO dokanfileinfo) {
    if (!m_volume) return;

    UNREFERENCED_PARAMETER(filename);    
    if (dokanfileinfo->Context) {
        ActiveFileIO io = notifyIOInUse(dokanfileinfo);
        AdfFile* fle = (AdfFile*)dokanfileinfo->Context;
        adfFileClose(fle); 

        releaseFileInUse(fle);

        dokanfileinfo->Context = 0;
    }
    if (dokanfileinfo->DeleteOnClose) {
        // Delete happens during cleanup and not in close event.
        ActiveFileIO io = notifyIOInUse(dokanfileinfo);
        fs_deletefile(filename, dokanfileinfo);
    }
}

NTSTATUS DokanFileSystemAmigaFS::fs_readfile(const std::wstring& filename, void* buffer, const uint32_t bufferlength, uint32_t& actualReadLength, const int64_t offset, PDOKAN_FILE_INFO dokanfileinfo) {
    UNREFERENCED_PARAMETER(filename);

    if (dokanfileinfo->Context) {
        AdfFile* fle = (AdfFile*)dokanfileinfo->Context;

        ActiveFileIO io = notifyIOInUse(dokanfileinfo);

        if (adfFileGetPos(fle) != (uint32_t)offset)
            if (adfFileSeek(fle, (uint32_t)offset) != ADF_RETCODE::ADF_RC_OK)
                return STATUS_DATA_ERROR;

        DWORD lenRead = adfFileRead(fle, bufferlength, (uint8_t*)buffer);
        actualReadLength = lenRead;

        // This shouldn't ever happen
        if (fle->curDataPtr == 0) return STATUS_DATA_ERROR;
    }
    else {
        actualReadLength = 0;
    }

    return STATUS_SUCCESS;
}

NTSTATUS DokanFileSystemAmigaFS::fs_writefile(const std::wstring& filename, const void* buffer, const uint32_t bufferLength, uint32_t& actualWriteLength, const int64_t offset, PDOKAN_FILE_INFO dokanfileinfo) {
    if (dokanfileinfo->Context) {
        AdfFile* fle = (AdfFile*)dokanfileinfo->Context;

        ActiveFileIO io = notifyIOInUse(dokanfileinfo);

        uint64_t actualOffset;
        uint32_t number_of_bytes_to_write = bufferLength;

        if (offset == -1) {
            if (adfFileSeekEOF(fle) != ADF_RETCODE::ADF_RC_OK) return STATUS_DATA_ERROR;
            actualOffset = adfFileGetSize(fle);
        }
        else actualOffset = (uint64_t)offset;

        if (dokanfileinfo->PagingIo) {
            // PagingIo cannot extend file size.
            // We return STATUS_SUCCESS when offset is beyond fileSize
            // and write the maximum we are allowed to.
            if (actualOffset >= adfFileGetSize(fle)) {
                actualWriteLength = 0;
                return STATUS_SUCCESS;
            }

            if ((actualOffset + number_of_bytes_to_write) > adfFileGetSize(fle)) {
                // resize the write length to not go beyond file size.
                LONGLONG bytes = adfFileGetSize(fle) - actualOffset;
                if (bytes >> 32) {
                    number_of_bytes_to_write = static_cast<DWORD>(bytes & 0xFFFFFFFFUL);
                }
                else {
                    number_of_bytes_to_write = static_cast<DWORD>(bytes);
                }
            }
        }

        //, offset
        if (adfFileGetPos(fle) != (uint32_t)actualOffset)
            if (adfFileSeek(fle, (uint32_t)offset) != ADF_RETCODE::ADF_RC_OK)
                return STATUS_DATA_ERROR;
        
        DWORD written = adfFileWrite(fle,number_of_bytes_to_write, (uint8_t*)buffer);
        actualWriteLength = written;

        if (written < number_of_bytes_to_write) return STATUS_DISK_FULL;

        // This shouldn't ever happen
        if (fle->curDataPtr == 0) return STATUS_DATA_ERROR;

        return STATUS_SUCCESS;
    }
    else {
        return STATUS_ACCESS_DENIED;
    }         
}

NTSTATUS DokanFileSystemAmigaFS::fs_flushfilebuffers(const std::wstring& filename, PDOKAN_FILE_INFO dokanfileinfo) {
    UNREFERENCED_PARAMETER(filename);
        
    if (dokanfileinfo->Context) {
        AdfFile* fle = (AdfFile*)dokanfileinfo->Context;
        ActiveFileIO io = notifyIOInUse(dokanfileinfo);
        if (adfFileFlush(fle) == ADF_RETCODE::ADF_RC_OK) return STATUS_SUCCESS;
    }
    else return STATUS_OBJECT_NAME_NOT_FOUND;
    return STATUS_ACCESS_DENIED;
}

NTSTATUS DokanFileSystemAmigaFS::fs_setendoffile(const std::wstring& filename, const uint64_t ByteOffset, PDOKAN_FILE_INFO dokanfileinfo) {
    UNREFERENCED_PARAMETER(filename);
   
    if (dokanfileinfo->Context) {
        AdfFile* fle = (AdfFile*)dokanfileinfo->Context;
        ActiveFileIO io = notifyIOInUse(dokanfileinfo);
        if (adfFileTruncate(fle, (uint32_t)ByteOffset) == ADF_RETCODE::ADF_RC_OK) return STATUS_SUCCESS;
    } else return STATUS_OBJECT_NAME_NOT_FOUND;
    return STATUS_ACCESS_DENIED;
}

NTSTATUS DokanFileSystemAmigaFS::fs_setallocationsize(const std::wstring& filename, const uint64_t alloc_size, PDOKAN_FILE_INFO dokanfileinfo) {
    UNREFERENCED_PARAMETER(filename);   

    if (dokanfileinfo->Context) {
        AdfFile* fle = (AdfFile*)dokanfileinfo->Context;
        ActiveFileIO io = notifyIOInUse(dokanfileinfo);

        uint32_t siz = adfFileGetSize(fle);
        if (siz == alloc_size) {
            return STATUS_SUCCESS;
        }

        if (alloc_size < siz) {
            if (adfFileTruncate(fle, (uint32_t)alloc_size) == ADF_RETCODE::ADF_RC_OK) return STATUS_SUCCESS;
        }
        else {
            uint32_t pos = adfFileGetPos(fle);
            if (adfFileSeekEOF(fle) == ADF_RETCODE::ADF_RC_OK) {
                uint32_t toWrite = (uint32_t)alloc_size - siz;

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

NTSTATUS DokanFileSystemAmigaFS::fs_getfileInformation(const std::wstring& filename, LPBY_HANDLE_FILE_INFORMATION buffer, PDOKAN_FILE_INFO dokanfileinfo) {
    // This is queried for folders and volume id
    int32_t search = locatePath(filename, dokanfileinfo);
    if (search == 0) return STATUS_OBJECT_NAME_NOT_FOUND;

    struct AdfEntryBlock parent;
    ADF_RETCODE rc = adfReadEntryBlock(m_volume, m_volume->curDirPtr, &parent);
    if (rc != ADF_RETCODE::ADF_RC_OK)
        return STATUS_OBJECT_NAME_NOT_FOUND;
    if ((parent.secType != ADF_ST_FILE) && (parent.secType != ADF_ST_DIR) && (parent.secType != ADF_ST_ROOT)) return STATUS_OBJECT_NAME_NOT_FOUND;

    struct AdfEntry e = { 0 };
 
    e.access = parent.access;
    e.type = parent.secType;
    buffer->dwFileAttributes = amigaToWindowsAttributes(parent.access, search);
    buffer->nNumberOfLinks = 1;
    buffer->nFileIndexHigh = 0;
    buffer->nFileIndexLow = 0;
    buffer->nFileSizeHigh = 0;
    buffer->nFileSizeLow = parent.byteSize;
    buffer->dwVolumeSerialNumber = volumeSerialNumber();
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
    if (parent.secType == ADF_ST_FILE) {
        buffer->ftCreationTime = { 0, 0 };
        SystemTimeToFileTime(&tm, &buffer->ftLastWriteTime);
    } else {
        buffer->ftLastWriteTime = { 0, 0 };
        SystemTimeToFileTime(&tm, &buffer->ftCreationTime);
    }
    return STATUS_SUCCESS;
}

NTSTATUS DokanFileSystemAmigaFS::fs_findfiles(const std::wstring& filename, PFillFindData fill_finddata, PDOKAN_FILE_INFO dokanfileinfo) {     
    WIN32_FIND_DATAW findData;
    ZeroMemory(&findData, sizeof(WIN32_FIND_DATAW));

    int32_t search = locatePath(filename, dokanfileinfo);
    if ((search != ADF_ST_DIR) && (search != ADF_ST_ROOT)) return STATUS_OBJECT_NAME_NOT_FOUND;
   
    struct AdfList* list = adfGetDirEnt(m_volume, m_volume->curDirPtr);
    for (struct AdfList* node = list; node; node = node->next) {
        struct AdfEntry* e = (struct AdfEntry* ) node->content;
        if (e->type != ADF_ST_FILE && e->type != ADF_ST_DIR) continue;

        std::wstring winFilename;
        amigaFilenameToWindowsFilename(filename, e->name, winFilename);
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

NTSTATUS DokanFileSystemAmigaFS::fs_setfileattributes(const std::wstring& filename, const uint32_t fileattributes, PDOKAN_FILE_INFO dokanfileinfo) {
    std::string amigafilename;
    int32_t search = locatePath(filename, dokanfileinfo, amigafilename);
    if (search != ADF_ST_FILE) return STATUS_OBJECT_NAME_NOT_FOUND;

    struct AdfEntryBlock parent;
    ADF_RETCODE rc = adfReadEntryBlock(m_volume, m_volume->curDirPtr, &parent);
    if (rc != ADF_RETCODE::ADF_RC_OK) return false;
    
    if (parent.secType != ADF_ST_FILE) return STATUS_OBJECT_NAME_NOT_FOUND;
    int32_t access = parent.access;   // force it to be readible and deletible
   
    if (fileattributes & FILE_ATTRIBUTE_ARCHIVE)
        access |= ADF_ACCMASK_A; else access &= ~ADF_ACCMASK_A;
    if (fileattributes & FILE_ATTRIBUTE_READONLY)
        access |= ADF_ACCMASK_W; else  access &= ~ADF_ACCMASK_W;

    // Nothing changed? 
    access &= ~(ADF_ACCMASK_R | ADF_ACCMASK_D);
    if (access == parent.access) return STATUS_SUCCESS;

    adfParentDir(m_volume);

    if (adfSetEntryAccess(m_volume, m_volume->curDirPtr, amigafilename.c_str(), access) == ADF_RETCODE::ADF_RC_OK) return STATUS_SUCCESS;
    return STATUS_DATA_ERROR;
}

NTSTATUS DokanFileSystemAmigaFS::fs_setfiletime(const std::wstring& filename, CONST FILETIME* creationtime, CONST FILETIME* lastaccesstime, CONST FILETIME* lastwritetime, PDOKAN_FILE_INFO dokanfileinfo) {
    int32_t search = locatePath(filename, dokanfileinfo);
    if (search != ADF_ST_FILE) return STATUS_OBJECT_NAME_NOT_FOUND;

    // We only support last write time
    if (!lastwritetime) return STATUS_SUCCESS;

    struct AdfEntryBlock parent;
    ADF_RETCODE rc = adfReadEntryBlock(m_volume, m_volume->curDirPtr, &parent);
    if (rc != ADF_RETCODE::ADF_RC_OK) return false;

    if (parent.secType != ADF_ST_FILE) return STATUS_OBJECT_NAME_NOT_FOUND;

    DateTime tm;

    SYSTEMTIME sys;
    FileTimeToSystemTime(lastwritetime, &sys);
    tm.day = sys.wDay;
    tm.hour = sys.wHour;
    tm.min = sys.wMinute;
    tm.sec = sys.wSecond;
    tm.year = max(0, sys.wYear - 1900);
    tm.mon = sys.wMonth;    

    int32_t days;
    int32_t mins;
    int32_t ticks;

    adfTime2AmigaTime(tm, &days, &mins, &ticks);

    // Block changes if nothing happened
    if ((days == parent.days) && (mins == parent.mins) && (ticks == parent.ticks)) return STATUS_SUCCESS;
    parent.days = days;
    parent.mins = mins;
    parent.ticks = ticks;

    if (adfWriteEntryBlock(m_volume, m_volume->curDirPtr, &parent) == ADF_RETCODE::ADF_RC_OK) return STATUS_SUCCESS;
    return STATUS_DATA_ERROR;
}

NTSTATUS DokanFileSystemAmigaFS::fs_deletefile(const std::wstring& filename, PDOKAN_FILE_INFO dokanfileinfo) {   
    std::string amigaName;
    int32_t search = locatePath(filename, dokanfileinfo, amigaName);
    if (search == 0) return STATUS_OBJECT_NAME_NOT_FOUND;
    if (search != ADF_ST_FILE) return STATUS_ACCESS_DENIED;

    if (isFileInUse(amigaName.c_str(), AdfFileMode::ADF_FILE_MODE_WRITE))
        return STATUS_SHARING_VIOLATION;

    struct AdfEntryBlock parent;
    if (adfReadEntryBlock(m_volume, m_volume->curDirPtr, &parent) == ADF_RETCODE::ADF_RC_OK) {
        if ((parent.secType == ADF_ST_FILE) && (adfAccHasW(parent.access)))
            return STATUS_CANNOT_DELETE;
    }

    adfParentDir(m_volume);

    if (adfRemoveEntry(m_volume, m_volume->curDirPtr, amigaName.c_str()) == ADF_RETCODE::ADF_RC_OK) return STATUS_SUCCESS;
    return STATUS_DATA_ERROR;
}

NTSTATUS DokanFileSystemAmigaFS::fs_deletedirectory(const std::wstring& filename, PDOKAN_FILE_INFO dokanfileinfo) {
    std::string amigaName;
    int32_t search = locatePath(filename, dokanfileinfo, amigaName);
    if (search == 0) return STATUS_OBJECT_NAME_NOT_FOUND;
    if (search != ADF_ST_DIR) 
        return STATUS_ACCESS_DENIED;

    struct AdfDirBlock dirBlock;
    if (adfReadEntryBlock(m_volume, m_volume->curDirPtr, (struct AdfEntryBlock*)&dirBlock) != ADF_RETCODE::ADF_RC_OK) return STATUS_DATA_ERROR;

    if (!isDirEmpty(&dirBlock)) return STATUS_DIRECTORY_NOT_EMPTY;

    adfParentDir(m_volume);

    if (adfRemoveEntry(m_volume, m_volume->curDirPtr, amigaName.c_str()) == ADF_RETCODE::ADF_RC_OK) return STATUS_SUCCESS;
    return STATUS_DATA_ERROR;
}

NTSTATUS DokanFileSystemAmigaFS::fs_movefile(const std::wstring& filename, const std::wstring& new_filename, const bool replaceExisting, PDOKAN_FILE_INFO dokanfileinfo) {
        
    std::string amigaName;
    int32_t srcFileFolder = locatePath(filename, dokanfileinfo, amigaName);
    if (srcFileFolder == 0) return STATUS_OBJECT_NAME_NOT_FOUND;

    int32_t search = locatePath(filename, dokanfileinfo, amigaName);
    if (srcFileFolder == ADF_ST_FILE) {
        if (isFileInUse(amigaName.c_str(), AdfFileMode::ADF_FILE_MODE_WRITE))
            return STATUS_SHARING_VIOLATION;
    }

    adfParentDir(m_volume);
    ADF_SECTNUM srcSector = m_volume->curDirPtr;
    ADF_SECTNUM dstSector = srcSector;

    std::wstring newName = new_filename;
    if (newName.length() && (newName[0] == '\\')) newName.erase(newName.begin());

    std::string amigaTargetName;

    std::string targetNameOutput;
    int32_t target = locatePath(new_filename, dokanfileinfo, targetNameOutput);
    if ((!replaceExisting) && (target != 0)) return STATUS_OBJECT_NAME_EXISTS;
    ADF_SECTNUM targetNameSec = m_volume->curDirPtr;

    size_t i = newName.rfind('\\');
    if (i != std::wstring::npos) {
        // Locate target path
        int32_t dstFileFolder = locatePath(newName.substr(0, i), dokanfileinfo);
        dstSector = m_volume->curDirPtr;
        if (dstFileFolder == 0) return STATUS_OBJECT_NAME_NOT_FOUND;
       
        windowsFilenameToAmigaFilename(newName.substr(i+1), amigaTargetName);       
    }
    else {
        dstSector = m_volume->rootBlock;
        windowsFilenameToAmigaFilename(newName, amigaTargetName);
    }

    // Try to remove the target first
    if (target != 0) {
        if (adfRemoveEntry(m_volume, targetNameSec, targetNameOutput.c_str()) != ADF_RETCODE::ADF_RC_OK)
            return STATUS_ACCESS_DENIED;
    }

    if (amigaTargetName.length() > ADF_MAX_NAME_LEN) return STATUS_OBJECT_NAME_INVALID;

    if (adfRenameEntry(m_volume, srcSector, amigaName.c_str(), dstSector, amigaTargetName.c_str()) == ADF_RETCODE::ADF_RC_OK) return STATUS_SUCCESS;

    return STATUS_ACCESS_DENIED;
}

NTSTATUS DokanFileSystemAmigaFS::fs_getdiskfreespace(uint64_t& freeBytesAvailable, uint64_t& totalNumBytes, uint64_t& totalNumFreeBytes, PDOKAN_FILE_INFO dokanfileinfo) {   
    uint32_t numBlocks = (m_volume->lastBlock - m_volume->firstBlock) - 1;
    uint32_t blocksFree = adfCountFreeBlocks(m_volume);
    freeBytesAvailable = blocksFree * m_volume->datablockSize;
    totalNumBytes = numBlocks * m_volume->datablockSize;
    totalNumFreeBytes = blocksFree * m_volume->datablockSize;
    return STATUS_SUCCESS;
}

NTSTATUS DokanFileSystemAmigaFS::fs_getvolumeinformation(std::wstring& volumeName, uint32_t& volumeSerialNumber, uint32_t& maxComponentLength, uint32_t& filesystemFlags, std::wstring& filesystemName, PDOKAN_FILE_INFO dokanfileinfo) {
    volumeSerialNumber = DokanFileSystemAmigaFS::volumeSerialNumber();
    maxComponentLength = ADF_MAX_NAME_LEN;
    filesystemFlags = FILE_CASE_PRESERVED_NAMES;

    if (m_volume) {
        if (m_volume->volName) ansiToWide(m_volume->volName, volumeName);

        filesystemName = L"Amiga ";
        switch (m_volume->dev->devType) {
        case ADF_DEVTYPE_FLOPDD: filesystemName += L"DD Floppy "; break;
        case ADF_DEVTYPE_FLOPHD: filesystemName += L"HD Floppy "; break;
        case ADF_DEVTYPE_HARDDISK: filesystemName += L"HD Partition "; break;
        case ADF_DEVTYPE_HARDFILE: filesystemName += L"HD File "; break;
        }

        filesystemName += adfDosFsIsFFS(m_volume->fs.type) ? L"FFS" : L"OFS";
        if (adfDosFsHasINTL(m_volume->fs.type)) filesystemName += L" Intl";
        if (adfDosFsHasDIRCACHE(m_volume->fs.type)) filesystemName += L" Cache";
    }
    else {
        volumeName = L"Unknown";
        filesystemName = L"???";
    }

    return STATUS_SUCCESS;
}
