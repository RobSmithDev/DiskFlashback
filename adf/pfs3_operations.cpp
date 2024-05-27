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
#include "pfs3_operations.h"
#include <time.h>
#include <algorithm>
#include <Shlobj.h>


DokanFileSystemAmigaPFS3::DokanFileSystemAmigaPFS3(DokanFileSystemManager* owner, bool autoRename) : DokanFileSystemAmiga(owner, autoRename) {
}

std::string upperString(const std::string& str) {
    std::string ret = str;
    for (char& c : ret) c = toupper(c);
    return ret;
}

bool DokanFileSystemAmigaPFS3::isFileSystemReady() {
    return (m_volume != nullptr) && (m_volume->available());
}

bool DokanFileSystemAmigaPFS3::isDiskInUse() {
    if (!m_volume) return false;
    return !m_inUse.empty();
}


// Return TRUE if file is in use
bool DokanFileSystemAmigaPFS3::isFileInUse(const std::string& name, const bool readOnly) {
    if (!m_volume) return false;

    auto f = m_inUse.find(upperString(name));
    if (f == m_inUse.end()) return false;
    if (f->second != readOnly) return true;
   
    return false;
}
void DokanFileSystemAmigaPFS3::addTrackFileInUse(const std::string amigaFilename, bool readOnly) {
    m_inUse.insert(std::make_pair(upperString(amigaFilename), readOnly));
}
void DokanFileSystemAmigaPFS3::releaseFileInUse(const std::string amigaFilename) {
    auto f = m_inUse.find(upperString(amigaFilename));
    if (f != m_inUse.end()) m_inUse.erase(f);
}

void DokanFileSystemAmigaPFS3::setCurrentVolume(IPFS3* volume) {
    m_inUse.clear();
    m_volume = volume; 
}

NTSTATUS DokanFileSystemAmigaPFS3::pfs3Error2SysError(const IPFS3::Error err) {
    switch (err) {
        case IPFS3::Error::eOK:               return STATUS_SUCCESS;
        case IPFS3::Error::eWriteProtected:   return STATUS_MEDIA_WRITE_PROTECTED;
        case IPFS3::Error::eAlreadyExists:    return STATUS_OBJECT_NAME_COLLISION; 
        case IPFS3::Error::eNotFound:         return STATUS_OBJECT_NAME_NOT_FOUND;
        case IPFS3::Error::eNotDirectory:     return STATUS_NOT_A_DIRECTORY;
        case IPFS3::Error::eNotFile:          return STATUS_FILE_IS_A_DIRECTORY;
        case IPFS3::Error::eSeekError:        return STATUS_DATA_ERROR;
        case IPFS3::Error::eCantOpenLink:     return STATUS_STOPPED_ON_SYMLINK;
        case IPFS3::Error::eDiskFull:         return STATUS_DISK_FULL;
        case IPFS3::Error::eBadName:          
            return STATUS_OBJECT_NAME_INVALID;
        case IPFS3::Error::eNotEmpty:         return STATUS_DIRECTORY_NOT_EMPTY;
        case IPFS3::Error::eInUse:            return STATUS_SHARING_VIOLATION;
        case IPFS3::Error::eAccessDenied:     return STATUS_ACCESS_DENIED;
        case IPFS3::Error::eOutOfMem:         return STATUS_NO_MEMORY;
        default:                             return STATUS_DATA_ERROR;
    }
}

DWORD DokanFileSystemAmigaPFS3::amigaToWindowsAttributes(const IPFS3::FileInformation& fileInfo) {
    DWORD ret = fileInfo.isDirectory ? FILE_ATTRIBUTE_DIRECTORY : 0;
    if (IPFS3::IsFileWriteProtected(fileInfo.protectBits)) ret |= FILE_ATTRIBUTE_READONLY;
    if (IPFS3::IsFileArchve(fileInfo.protectBits)) ret |= FILE_ATTRIBUTE_ARCHIVE;
    return ret;
}

NTSTATUS DokanFileSystemAmigaPFS3::fs_createfile(const std::wstring& filename, const PDOKAN_IO_SECURITY_CONTEXT security_context, const ACCESS_MASK generic_desiredaccess, const uint32_t file_attributes, const uint32_t shareaccess, const uint32_t creation_disposition, const bool fileSupersede, PDOKAN_FILE_INFO dokanfileinfo) {
    dokanfileinfo->Context = 0;
    uint32_t file_attributes_and_flags = file_attributes;
    if (!m_volume) return STATUS_UNRECOGNIZED_MEDIA;

    std::wstring windowsPath = filename;
    std::wstring amigaPath = windowsPathToAmigaPath(filename);
    std::string amigaPathA; wideToAnsi(amigaPath, amigaPathA);

    IPFS3::FileInformation fileInfo;
    bool fileExists;
   
    if (amigaPath.empty()) {
        fileInfo.isDirectory = true;
        fileInfo.fileSize = 0;
        fileExists = true;
    }
    else {
        fileExists = m_volume->GetFileInformation(amigaPathA, fileInfo) == IPFS3::Error::eOK;
    }

    if (fileExists && fileInfo.isDirectory) dokanfileinfo->IsDirectory = true;


    if (dokanfileinfo->IsDirectory) {

        if (!fileInfo.isDirectory) 
            return STATUS_NOT_A_DIRECTORY;

        if (creation_disposition == CREATE_NEW || creation_disposition == OPEN_ALWAYS) {
            if (fileExists) return STATUS_OBJECT_NAME_COLLISION;
            if (isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

            ActiveFileIO io = notifyIOInUse(dokanfileinfo);
            return pfs3Error2SysError(m_volume->MkDir(amigaPathA));
        }

        if (!fileExists) return STATUS_OBJECT_NAME_NOT_FOUND;
        return STATUS_SUCCESS;
    }
    else {
        bool readOnly = true;
        if (generic_desiredaccess & GENERIC_WRITE) readOnly = false;
        if (generic_desiredaccess & GENERIC_ALL) readOnly = false;

        if (!readOnly)
            if (isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

        if (fileExists) {
            // Cannot delete a file with readonly attributes.
            if (file_attributes_and_flags & FILE_FLAG_DELETE_ON_CLOSE) 
                if ((file_attributes_and_flags & FILE_ATTRIBUTE_READONLY) || (IPFS3::IsFileWriteProtected(fileInfo.protectBits) || (isWriteProtected())))
                    return STATUS_CANNOT_DELETE;
            
            // Cannot open a readonly file for writing.
            if ((creation_disposition == OPEN_ALWAYS || creation_disposition == OPEN_EXISTING) && (IPFS3::IsFileWriteProtected(fileInfo.protectBits)) && (generic_desiredaccess & GENERIC_WRITE))
                return STATUS_ACCESS_DENIED;

            // Cannot overwrite an existing read only file.
            if ((creation_disposition == CREATE_NEW || (creation_disposition == CREATE_ALWAYS && !fileSupersede) || creation_disposition == TRUNCATE_EXISTING) && (IPFS3::IsFileWriteProtected(fileInfo.protectBits)))
                return STATUS_ACCESS_DENIED;

            // Attributes patch
            if (creation_disposition == CREATE_NEW || creation_disposition == CREATE_ALWAYS || creation_disposition == OPEN_ALWAYS || creation_disposition == TRUNCATE_EXISTING) {
                // Combines the file attributes and flags specified by
                file_attributes_and_flags |= FILE_ATTRIBUTE_ARCHIVE;
                // We merge the attributes with the existing file attributes
                if (!fileSupersede) file_attributes_and_flags |= amigaToWindowsAttributes(fileInfo);
                // Remove non specific attributes.
                file_attributes_and_flags &= ~FILE_ATTRIBUTE_STRICTLY_SEQUENTIAL;
                // FILE_ATTRIBUTE_NORMAL is override if any other attribute is set.
                file_attributes_and_flags &= ~FILE_ATTRIBUTE_NORMAL;
            }
        }


        if (((generic_desiredaccess & GENERIC_WRITE) | (generic_desiredaccess & GENERIC_ALL) | (generic_desiredaccess & GENERIC_READ)) == 0) {
            if (creation_disposition == OPEN_EXISTING) {
                if (!fileExists) 
                    return STATUS_OBJECT_NAME_NOT_FOUND;
            }
            return STATUS_SUCCESS;
        }
      
        IPFS3::PFS3File fle = nullptr;

        if (isFileInUse(amigaPathA, readOnly)) return STATUS_SHARING_VIOLATION;
        IPFS3::PFSVolInfo volInfo;
        IPFS3::Error e;
        m_volume->GetVolInformation(volInfo);
         
        switch (creation_disposition) {
            case CREATE_ALWAYS:          
                if ((!fileExists) && (volInfo.blocksUsed >= volInfo.totalBlocks)) return STATUS_DISK_FULL;
                e = m_volume->Createfile(amigaPathA, IPFS3::OpenMode::omCreateAlways, readOnly, fle);
                if (e == IPFS3::Error::eOK) {
                    dokanfileinfo->Context = (ULONG64)fle;
                    addTrackFileInUse(amigaPathA, readOnly);
                }
                return pfs3Error2SysError(e);
                break;

            case CREATE_NEW:
                // Fail if it already exists
                if (fileExists) return STATUS_OBJECT_NAME_COLLISION; 
                if (volInfo.blocksUsed >= volInfo.totalBlocks) return STATUS_DISK_FULL;
                e = m_volume->Createfile(amigaPathA, IPFS3::OpenMode::omCreate, readOnly, fle);
                if (e == IPFS3::Error::eOK) {
                    dokanfileinfo->Context = (ULONG64)fle;
                    addTrackFileInUse(amigaPathA, readOnly);
                }
                return pfs3Error2SysError(e);
                break;

            case OPEN_ALWAYS: 
                e = m_volume->Createfile(amigaPathA, IPFS3::OpenMode::omOpenAlways, readOnly, fle);
                if (e == IPFS3::Error::eOK) {
                    dokanfileinfo->Context = (ULONG64)fle;
                    addTrackFileInUse(amigaPathA, readOnly);
                }
                return pfs3Error2SysError(e);
                break;

            case OPEN_EXISTING:
                if (!fileExists) return STATUS_OBJECT_NAME_NOT_FOUND;
                e = m_volume->Createfile(amigaPathA, IPFS3::OpenMode::omOpenExisting, readOnly, fle);
                if (e == IPFS3::Error::eOK) {
                    dokanfileinfo->Context = (ULONG64)fle;
                    addTrackFileInUse(amigaPathA, readOnly);
                }
                return pfs3Error2SysError(e);
                break;

            case TRUNCATE_EXISTING:
                if (!fileExists) return STATUS_OBJECT_NAME_NOT_FOUND;
                e = m_volume->Createfile(amigaPathA, IPFS3::OpenMode::omTruncateExisting, readOnly, fle);
                if (e == IPFS3::Error::eOK) {
                    dokanfileinfo->Context = (ULONG64)fle;
                    addTrackFileInUse(amigaPathA, readOnly);
                }
                return pfs3Error2SysError(e);
                break;
            default:
                // Unknown
                return STATUS_ACCESS_DENIED;
        }

        return STATUS_SUCCESS;
    }
    return STATUS_ACCESS_DENIED;
}

void DokanFileSystemAmigaPFS3::fs_cleanup(const std::wstring& filename, PDOKAN_FILE_INFO dokanfileinfo) {
    if (!m_volume) return;

    UNREFERENCED_PARAMETER(filename);    
    if (dokanfileinfo->Context) {
        ActiveFileIO io = notifyIOInUse(dokanfileinfo);
        IPFS3::PFS3File fle = (IPFS3::PFS3File)dokanfileinfo->Context;
        m_volume->Closefile(fle); 
        dokanfileinfo->Context = 0;
        
        std::wstring amigaPath = windowsPathToAmigaPath(filename);
        std::string amigaPathA; wideToAnsi(amigaPath, amigaPathA);
        releaseFileInUse(amigaPathA);
    }
    if (dokanfileinfo->DeleteOnClose) {
        // Delete happens during cleanup and not in close event.
        ActiveFileIO io = notifyIOInUse(dokanfileinfo);
        fs_deletefile(filename, dokanfileinfo);
    }
}

NTSTATUS DokanFileSystemAmigaPFS3::fs_readfile(const std::wstring& filename, void* buffer, const uint32_t bufferlength, uint32_t& actualReadLength, const int64_t offset, PDOKAN_FILE_INFO dokanfileinfo) {
    UNREFERENCED_PARAMETER(filename);

    if (dokanfileinfo->Context) {
        IPFS3::PFS3File fle = (IPFS3::PFS3File)dokanfileinfo->Context;

        ActiveFileIO io = notifyIOInUse(dokanfileinfo);
        uint64_t currentPos;
        if (m_volume->GetfilePos(fle, currentPos) != IPFS3::Error::eOK) return STATUS_DATA_ERROR;
        if (offset != currentPos) {
            IPFS3::Error e = m_volume->Seekfile(fle, offset, IPFS3::SeekMode::smFromBeginning);
            if (e != IPFS3::Error::eOK) return pfs3Error2SysError(e);
        }
        IPFS3::Error e = m_volume->Readfile(fle, buffer, bufferlength, actualReadLength);
        return pfs3Error2SysError(e);
    }
    else {
        actualReadLength = 0;
        return STATUS_SUCCESS;
    }
}

NTSTATUS DokanFileSystemAmigaPFS3::fs_writefile(const std::wstring& filename, const void* buffer, const uint32_t bufferLength, uint32_t& actualWriteLength, const int64_t offset, PDOKAN_FILE_INFO dokanfileinfo) {
    if (dokanfileinfo->Context) {
        IPFS3::PFS3File fle = (IPFS3::PFS3File)dokanfileinfo->Context;
        IPFS3::Error e;
        ActiveFileIO io = notifyIOInUse(dokanfileinfo);

        uint64_t actualOffset;
        uint32_t number_of_bytes_to_write = bufferLength;

        if (offset == -1) {
            e = m_volume->Seekfile(fle, 0, IPFS3::SeekMode::smFromEnd);
            if (e != IPFS3::Error::eOK) return pfs3Error2SysError(e);
            e = m_volume->GetfileSize(fle, actualOffset);
            if (e != IPFS3::Error::eOK) return pfs3Error2SysError(e);
        }
        else actualOffset = (uint64_t)offset;

        if (dokanfileinfo->PagingIo) {
            uint64_t fSize;
            e = m_volume->GetfileSize(fle, fSize);
            // PagingIo cannot extend file size.
            // We return STATUS_SUCCESS when offset is beyond fileSize
            // and write the maximum we are allowed to.
            if (actualOffset >= fSize) {
                actualWriteLength = 0;
                return STATUS_SUCCESS;
            }

            if ((actualOffset + number_of_bytes_to_write) > fSize) {
                // resize the write length to not go beyond file size.
                LONGLONG bytes = fSize - actualOffset;
                if (bytes >> 32) {
                    number_of_bytes_to_write = static_cast<DWORD>(bytes & 0xFFFFFFFFUL);
                }
                else {
                    number_of_bytes_to_write = static_cast<DWORD>(bytes);
                }
            }
        }

        uint64_t currentPos;
        if (m_volume->GetfilePos(fle, currentPos) != IPFS3::Error::eOK) return STATUS_DATA_ERROR;
        if (actualOffset != currentPos) {
            e = m_volume->Seekfile(fle, actualOffset, IPFS3::SeekMode::smFromBeginning);
            if (e != IPFS3::Error::eOK) return pfs3Error2SysError(e);
        }
        
        e = m_volume->Writefile(fle, buffer, number_of_bytes_to_write, actualWriteLength);
        if (e != IPFS3::Error::eOK) return pfs3Error2SysError(e);
        if (actualWriteLength < number_of_bytes_to_write) return STATUS_DISK_FULL;

        return STATUS_SUCCESS;
    }
    else {
        return STATUS_ACCESS_DENIED;
    }         
}

NTSTATUS DokanFileSystemAmigaPFS3::fs_flushfilebuffers(const std::wstring& filename, PDOKAN_FILE_INFO dokanfileinfo) {
    UNREFERENCED_PARAMETER(filename);
        
    if (dokanfileinfo->Context) {
        IPFS3::PFS3File fle = (IPFS3::PFS3File)dokanfileinfo->Context;
        ActiveFileIO io = notifyIOInUse(dokanfileinfo);
        return pfs3Error2SysError(m_volume->Flushfile(fle));
    }
    else return STATUS_OBJECT_NAME_NOT_FOUND;
    return STATUS_ACCESS_DENIED;
}

NTSTATUS DokanFileSystemAmigaPFS3::fs_setendoffile(const std::wstring& filename, const uint64_t ByteOffset, PDOKAN_FILE_INFO dokanfileinfo) {
    UNREFERENCED_PARAMETER(filename);
   
    if (dokanfileinfo->Context) {
        IPFS3::PFS3File fle = (IPFS3::PFS3File)dokanfileinfo->Context;
        ActiveFileIO io = notifyIOInUse(dokanfileinfo);

        uint64_t currentPos;
        if (m_volume->GetfilePos(fle, currentPos) != IPFS3::Error::eOK) return STATUS_DATA_ERROR;
        if (ByteOffset != currentPos) {
            IPFS3::Error e = m_volume->Seekfile(fle, ByteOffset, IPFS3::SeekMode::smFromBeginning);
            if (e != IPFS3::Error::eOK) return pfs3Error2SysError(e);
        }

        return pfs3Error2SysError(m_volume->Truncatefile(fle));
    } else return STATUS_OBJECT_NAME_NOT_FOUND;
    return STATUS_ACCESS_DENIED;
}

NTSTATUS DokanFileSystemAmigaPFS3::fs_setallocationsize(const std::wstring& filename, const uint64_t alloc_size, PDOKAN_FILE_INFO dokanfileinfo) {
    UNREFERENCED_PARAMETER(filename);   

    if (dokanfileinfo->Context) {
        IPFS3::PFS3File fle = (IPFS3::PFS3File)dokanfileinfo->Context;
        ActiveFileIO io = notifyIOInUse(dokanfileinfo);
        IPFS3::Error e;

        uint64_t siz;
        e = m_volume->GetfileSize(fle, siz);
        if (e != IPFS3::Error::eOK) return pfs3Error2SysError(e);
        if (siz == alloc_size) return STATUS_SUCCESS;

        if (alloc_size < siz) {
            IPFS3::Error e = m_volume->Seekfile(fle, alloc_size, IPFS3::SeekMode::smFromBeginning);
            if (e != IPFS3::Error::eOK) return pfs3Error2SysError(e);
            return pfs3Error2SysError(m_volume->Truncatefile(fle));
        }
        else {
            return pfs3Error2SysError(m_volume->ChangefileSize(fle, alloc_size));
        }
    }
    else return STATUS_OBJECT_NAME_NOT_FOUND;
}

NTSTATUS DokanFileSystemAmigaPFS3::fs_getfileInformation(const std::wstring& filename, LPBY_HANDLE_FILE_INFORMATION buffer, PDOKAN_FILE_INFO dokanfileinfo) {
    std::wstring amigaPath = windowsPathToAmigaPath(filename);
    std::string amigaPathA; wideToAnsi(amigaPath, amigaPathA);

    if (amigaPathA.empty()) {
        memset(buffer, 0, sizeof(_BY_HANDLE_FILE_INFORMATION));
        buffer->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        return STATUS_SUCCESS;
    }

    IPFS3::FileInformation info;
    IPFS3::Error e = m_volume->GetFileInformation(amigaPathA, info);
    if (e != IPFS3::Error::eOK) return pfs3Error2SysError(e);

    buffer->dwFileAttributes = amigaToWindowsAttributes(info);
    buffer->nNumberOfLinks = 1;
    buffer->nFileIndexHigh = 0;
    buffer->nFileIndexLow = 0;
    buffer->nFileSizeHigh = 0;
    buffer->nFileSizeLow = info.fileSize;
    buffer->dwVolumeSerialNumber = volumeSerialNumber();
    SYSTEMTIME tm;
    tm.wHour = info.modified.tm_hour;
    tm.wMinute = info.modified.tm_min;
    tm.wSecond = info.modified.tm_sec;
    tm.wYear = info.modified.tm_year + 1900;
    tm.wMonth = info.modified.tm_mon;
    tm.wDay = info.modified.tm_mday;
    tm.wMilliseconds = 0;
    tm.wDayOfWeek = 0;

    buffer->ftLastAccessTime = { 0, 0 };    
    if (info.isDirectory) {
        buffer->ftLastWriteTime = { 0, 0 };
        SystemTimeToFileTime(&tm, &buffer->ftCreationTime);
    } else {
        buffer->ftCreationTime = { 0, 0 };
        SystemTimeToFileTime(&tm, &buffer->ftLastWriteTime);
    }
    return STATUS_SUCCESS;
}

NTSTATUS DokanFileSystemAmigaPFS3::fs_findfiles(const std::wstring& filename, PFillFindData fill_finddata, PDOKAN_FILE_INFO dokanfileinfo) {     
    WIN32_FIND_DATAW findData;
    ZeroMemory(&findData, sizeof(WIN32_FIND_DATAW));

    std::wstring amigaPath = windowsPathToAmigaPath(filename);
    std::string amigaPathA; wideToAnsi(amigaPath, amigaPathA);

    IPFS3::Error e = m_volume->Dir(amigaPathA, [this, &findData , &dokanfileinfo, &fill_finddata](const IPFS3::FileInformation& info) {
        findData.dwFileAttributes = amigaToWindowsAttributes(info);
        findData.nFileSizeHigh = 0;
        findData.nFileSizeLow = info.fileSize;

        SYSTEMTIME tm;
        tm.wHour = info.modified.tm_hour;
        tm.wMinute = info.modified.tm_min;
        tm.wSecond = info.modified.tm_sec;
        tm.wYear = info.modified.tm_year + 1900;
        tm.wMonth = info.modified.tm_mon;
        tm.wDay = info.modified.tm_mday;
        tm.wMilliseconds = 0;
        tm.wDayOfWeek = 0;
        SystemTimeToFileTime(&tm, &findData.ftLastWriteTime);

        std::wstring wideStr;
        ansiToWide(info.filename, wideStr);
        wcscpy_s(findData.cFileName, wideStr.c_str());

        fill_finddata(&findData, dokanfileinfo);
    });

    return pfs3Error2SysError(e);
}

NTSTATUS DokanFileSystemAmigaPFS3::fs_setfileattributes(const std::wstring& filename, const uint32_t fileattributes, PDOKAN_FILE_INFO dokanfileinfo) {
    std::wstring amigaPath = windowsPathToAmigaPath(filename);
    std::string amigaPathA; wideToAnsi(amigaPath, amigaPathA);

    IPFS3::FileInformation info;
    IPFS3::Error e = m_volume->GetFileInformation(amigaPathA, info);
    if (e != IPFS3::Error::eOK) return pfs3Error2SysError(e);
   
    uint32_t originalAccess = info.protectBits;
    if (fileattributes & FILE_ATTRIBUTE_ARCHIVE)
        info.protectBits |= IPFS3::Protect_Archive; else info.protectBits &= ~IPFS3::Protect_Archive;
    if (fileattributes & FILE_ATTRIBUTE_READONLY)
        info.protectBits |= IPFS3::Protect_Write; else info.protectBits &= ~IPFS3::Protect_Write;

    // Nothing changed? 
    if (originalAccess == info.protectBits) return STATUS_SUCCESS;

    e = m_volume->SetfileAttributes(amigaPathA, info.protectBits);
    return pfs3Error2SysError(e);
}

NTSTATUS DokanFileSystemAmigaPFS3::fs_setfiletime(const std::wstring& filename, CONST FILETIME* creationtime, CONST FILETIME* lastaccesstime, CONST FILETIME* lastwritetime, PDOKAN_FILE_INFO dokanfileinfo) {
    std::wstring amigaPath = windowsPathToAmigaPath(filename);
    std::string amigaPathA; wideToAnsi(amigaPath, amigaPathA);

    // We only support last write time
    if (!lastwritetime) return STATUS_SUCCESS;

    struct tm t;
    SYSTEMTIME sys;
    FileTimeToSystemTime(lastwritetime, &sys);
    t.tm_mday = sys.wDay;
    t.tm_hour = sys.wHour;
    t.tm_min = sys.wMinute;
    t.tm_sec = sys.wSecond;
    t.tm_year = max(0, sys.wYear - 1900);
    t.tm_mon = sys.wMonth;    

    return pfs3Error2SysError(m_volume->SetfileDate(amigaPathA, t));
}

NTSTATUS DokanFileSystemAmigaPFS3::fs_deletefile(const std::wstring& filename, PDOKAN_FILE_INFO dokanfileinfo) { 
    std::wstring amigaPath = windowsPathToAmigaPath(filename);
    std::string amigaPathA; wideToAnsi(amigaPath, amigaPathA);

    if (isFileInUse(amigaPathA, false)) return STATUS_SHARING_VIOLATION;

    return pfs3Error2SysError(m_volume->Deletefile(amigaPathA));
}

NTSTATUS DokanFileSystemAmigaPFS3::fs_deletedirectory(const std::wstring& filename, PDOKAN_FILE_INFO dokanfileinfo) {
    std::wstring amigaPath = windowsPathToAmigaPath(filename);
    std::string amigaPathA; wideToAnsi(amigaPath, amigaPathA);

    return pfs3Error2SysError(m_volume->RmDir(amigaPathA));
}

NTSTATUS DokanFileSystemAmigaPFS3::fs_movefile(const std::wstring& filename, const std::wstring& new_filename, const bool replaceExisting, PDOKAN_FILE_INFO dokanfileinfo) {
    std::wstring amigaPath = windowsPathToAmigaPath(filename);
    std::string amigaPathA; wideToAnsi(amigaPath, amigaPathA);

    std::wstring newAmigaPath = windowsPathToAmigaPath(new_filename);
    std::string newAmigaPathA; wideToAnsi(newAmigaPath, newAmigaPathA);

    return pfs3Error2SysError(m_volume->Movefile(amigaPathA, newAmigaPathA, replaceExisting));
}

NTSTATUS DokanFileSystemAmigaPFS3::fs_getdiskfreespace(uint64_t& freeBytesAvailable, uint64_t& totalNumBytes, uint64_t& totalNumFreeBytes, PDOKAN_FILE_INFO dokanfileinfo) {   
    IPFS3::PFSVolInfo info;
    if (!m_volume->GetVolInformation(info)) return STATUS_ACCESS_DENIED;
    
    uint32_t blocksFree = info.totalBlocks - info.blocksUsed;
        
    freeBytesAvailable = blocksFree * info.bytesPerBlock;
    totalNumBytes = info.totalBlocks * info.bytesPerBlock;
    totalNumFreeBytes = blocksFree * info.bytesPerBlock;
    return STATUS_SUCCESS;
}

NTSTATUS DokanFileSystemAmigaPFS3::fs_getvolumeinformation(std::wstring& volumeName, uint32_t& volumeSerialNumber, uint32_t& maxComponentLength, uint32_t& filesystemFlags, std::wstring& filesystemName, PDOKAN_FILE_INFO dokanfileinfo) {
    IPFS3::PFSVolInfo info;
    if (!m_volume->GetVolInformation(info)) return STATUS_ACCESS_DENIED;
    
    ansiToWide(info.volumeLabel, volumeName);
    maxComponentLength = m_volume->MaxNameLength();
    volumeSerialNumber = DokanFileSystemAmigaPFS3::volumeSerialNumber();
    filesystemFlags = FILE_CASE_PRESERVED_NAMES;

    filesystemName = L"Amiga ";

    switch (info.volType) {
    case IPFS3::DiskType::dt_beta: filesystemName += L"Beta PFS"; break;
    case IPFS3::DiskType::dt_pfs1: filesystemName += L"PFS"; break;
    case IPFS3::DiskType::dt_busy: filesystemName += L"Busy"; break;
    case IPFS3::DiskType::dt_muAF: filesystemName += L"muAF"; break;
    case IPFS3::DiskType::dt_muPFS: filesystemName += L"muPFS"; break;
    case IPFS3::DiskType::dt_afs1: filesystemName += L"AFS1"; break;
    case IPFS3::DiskType::dt_pfs2: filesystemName += L"PFS"; break;
    case IPFS3::DiskType::dt_pfs3: filesystemName += L"PFS"; break;
    case IPFS3::DiskType::dt_AFSU: filesystemName += L"AFSU"; break;
    default:  break;
    }
    
    return STATUS_SUCCESS;
}
