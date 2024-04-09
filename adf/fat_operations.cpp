#include "fat_operations.h"

#define SECTOR_SIZE 512

// They're (not suprisingly) identical
DWORD mapAttributes(BYTE att) {
    DWORD ret = 0;
    if (att & AM_DIR) ret |= FILE_ATTRIBUTE_DIRECTORY;
    if (att & AM_RDO) ret |= FILE_ATTRIBUTE_READONLY;
    if (att & AM_HID) ret |= FILE_ATTRIBUTE_HIDDEN;
    if (att & AM_SYS) ret |= FILE_ATTRIBUTE_SYSTEM;
    if (att & AM_ARC) ret |= FILE_ATTRIBUTE_ARCHIVE;
    return ret;
}

DokanFileSystemFATFS::DokanFileSystemFATFS(DokanFileSystemManager* owner) : DokanFileSystemBase(owner) {
}
DokanFileSystemFATFS::ActiveFileIO::ActiveFileIO(DokanFileSystemFATFS* owner) : m_owner(owner) {};
// Add Move constructor
DokanFileSystemFATFS::ActiveFileIO::ActiveFileIO(DokanFileSystemFATFS::ActiveFileIO&& source) noexcept {
    this->m_owner = source.m_owner;
    source.m_owner = nullptr;
}
DokanFileSystemFATFS::ActiveFileIO::~ActiveFileIO() {
    if (m_owner) m_owner->clearFileIO();
}

// Sets a current file info block as active (or NULL for not) so DokanResetTimeout can be called if needed
void DokanFileSystemFATFS::setActiveFileIO(PDOKAN_FILE_INFO dokanfileinfo) {
    owner()->getBlockDevice()->setActiveFileIO(dokanfileinfo);
}

// Let the system know I/O is currently happenning.  ActiveFileIO must be kepyt in scope until io is complete
DokanFileSystemFATFS::ActiveFileIO DokanFileSystemFATFS::notifyIOInUse(PDOKAN_FILE_INFO dokanfileinfo) {
    setActiveFileIO(dokanfileinfo);
    return ActiveFileIO(this);
}

void DokanFileSystemFATFS::clearFileIO() {
    setActiveFileIO(nullptr);
}

bool DokanFileSystemFATFS::isFileSystemReady() {
    return m_volume != nullptr;
}

bool DokanFileSystemFATFS::isDiskInUse() {
    if (!m_volume) return false;
    return !m_inUse.empty();
}

// Return TRUE if file is in use
bool DokanFileSystemFATFS::isFileInUse(const std::wstring& fullPath, bool isWrite) {
    if (!m_volume) return false;

    std::wstring fle = fullPath;
    for (WCHAR& c : fle) c = towupper(c);

    auto f = m_inUse.find(fle);

    // Something exists?
    if (f != m_inUse.end()) {
        if (isWrite) return true;
        if (f->second) return true;
    }

    return false;
}
void DokanFileSystemFATFS::addTrackFileInUse(const std::wstring& fullPath, bool isWrite) {
    std::wstring fle = fullPath;
    for (WCHAR& c : fle) c = towupper(c);
    m_inUse.insert(std::make_pair(fle, isWrite));
}
void DokanFileSystemFATFS::releaseFileInUse(const std::wstring& fullPath) {
    std::wstring fle = fullPath;
    for (WCHAR& c : fle) c = towupper(c);
    auto f = m_inUse.find(fle);
    if (f != m_inUse.end()) m_inUse.erase(f);
}

void DokanFileSystemFATFS::setCurrentVolume(FATFS* volume) { 
    m_inUse.clear();
    m_volume = volume; 
}

NTSTATUS makeFileOpenStatus(FRESULT status) {
    switch (status) {
        case FR_OK: return STATUS_SUCCESS;
        case FR_NO_PATH: return STATUS_OBJECT_PATH_NOT_FOUND;
        case FR_NO_FILE: return STATUS_OBJECT_NAME_NOT_FOUND;
        case FR_INVALID_NAME: return STATUS_OBJECT_NAME_INVALID;
        case FR_DENIED: return STATUS_DISK_FULL;
        case FR_EXIST: return STATUS_OBJECT_NAME_COLLISION;
        case FR_WRITE_PROTECTED: return STATUS_MEDIA_WRITE_PROTECTED;
        default: return STATUS_DATA_ERROR;
    }
}


NTSTATUS makeFileUseStatus(FRESULT status) {
    switch (status) {
    case FR_OK: return STATUS_SUCCESS;
    case FR_NO_PATH: return STATUS_OBJECT_PATH_NOT_FOUND;
    case FR_NO_FILE: return STATUS_OBJECT_NAME_NOT_FOUND;
    case FR_INVALID_NAME: return STATUS_OBJECT_NAME_INVALID;
    case FR_DENIED: return STATUS_ACCESS_DENIED;
    case FR_EXIST: return STATUS_OBJECT_NAME_COLLISION;
    case FR_WRITE_PROTECTED: return STATUS_MEDIA_WRITE_PROTECTED;
    default: return STATUS_DATA_ERROR;
    }
}

NTSTATUS DokanFileSystemFATFS::fs_createfile(const std::wstring& filename, const PDOKAN_IO_SECURITY_CONTEXT security_context, const ACCESS_MASK generic_desiredaccess, const uint32_t file_attributes, const uint32_t shareaccess, const uint32_t creation_disposition, const bool fileSupersede, PDOKAN_FILE_INFO dokanfileinfo) {
    dokanfileinfo->Context = 0;
    uint32_t file_attributes_and_flags = file_attributes;

    // Lookup whats requested
    FILINFO info;
    FRESULT res;
    if (filename.size() < 3) {
        res = FR_OK;
        info.fattrib = AM_DIR;
    } else res = f_stat(filename.c_str(), &info);

    if (res == FR_OK) {
        if (info.fattrib & AM_DIR) {
            dokanfileinfo->IsDirectory = true;
        }
        else {
            if (dokanfileinfo->IsDirectory) return STATUS_NOT_A_DIRECTORY;
        }
    }
    else
        if (res == FR_NO_FILESYSTEM) return STATUS_UNRECOGNIZED_MEDIA; else
        if ((res != FR_NO_PATH) && (res != FR_NO_FILE)) return STATUS_DATA_ERROR;
    
    if (dokanfileinfo->IsDirectory) {
        if (creation_disposition == CREATE_NEW || creation_disposition == OPEN_ALWAYS) {
            if (res == FR_OK) return STATUS_OBJECT_NAME_COLLISION;
            if (isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;
            ActiveFileIO io = notifyIOInUse(dokanfileinfo);
            return makeFileOpenStatus(f_mkdir(filename.c_str()));
        }
        if (res != FR_OK) return STATUS_OBJECT_NAME_NOT_FOUND;
        return STATUS_SUCCESS;
    }
    else {
        if (generic_desiredaccess & GENERIC_WRITE)
            if (isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

        ActiveFileIO io = notifyIOInUse(dokanfileinfo);

        FILINFO info;
        res = f_stat(filename.c_str(), &info);
        bool alreadyExists = false;

        if ((res == FR_OK) && ((info.fattrib & AM_DIR) == 0)) {
            alreadyExists = true;

            // Cannot delete a file with readonly attributes.
            if (file_attributes_and_flags & FILE_FLAG_DELETE_ON_CLOSE)
                if ((file_attributes_and_flags & FILE_ATTRIBUTE_READONLY) || (info.fattrib & AM_RDO) || (isWriteProtected()))
                    return STATUS_CANNOT_DELETE;

            // Cannot open a readonly file for writing.
            if ((creation_disposition == OPEN_ALWAYS || creation_disposition == OPEN_EXISTING) && (info.fattrib & AM_RDO) && (generic_desiredaccess & GENERIC_WRITE))
                return STATUS_ACCESS_DENIED;

            // Cannot overwrite an existing read only file.
            if ((creation_disposition == CREATE_NEW || (creation_disposition == CREATE_ALWAYS && !fileSupersede) || creation_disposition == TRUNCATE_EXISTING) && (info.fattrib & AM_RDO))
                return STATUS_ACCESS_DENIED;

            // Attributes patch
            if (creation_disposition == CREATE_NEW || creation_disposition == CREATE_ALWAYS || creation_disposition == OPEN_ALWAYS || creation_disposition == TRUNCATE_EXISTING) {
                // Combines the file attributes and flags specified by
                file_attributes_and_flags |= FILE_ATTRIBUTE_ARCHIVE;
                // We merge the attributes with the existing file attributes
                if (!fileSupersede) file_attributes_and_flags |= mapAttributes(info.fattrib);
                // Remove non specific attributes.
                file_attributes_and_flags &= ~FILE_ATTRIBUTE_STRICTLY_SEQUENTIAL;
                // FILE_ATTRIBUTE_NORMAL is override if any other attribute is set.
                file_attributes_and_flags &= ~FILE_ATTRIBUTE_NORMAL;
            }
        }

        // See if the parent folder exists - this shoudl NEVER fail
        size_t pos = filename.rfind(L"\\");
        if (pos == std::wstring::npos) return STATUS_ACCESS_DENIED;
        std::wstring path = filename.substr(0, pos);
        if (path.length() > 2) {
            DIR dir;
            if (f_opendir(&dir, path.c_str()) == FR_OK) f_closedir(&dir); else 
                return STATUS_OBJECT_PATH_NOT_FOUND;
        }

        // No access requested?
        if ((generic_desiredaccess & (GENERIC_READ | GENERIC_WRITE | GENERIC_ALL)) == 0) 
            return (res == FR_OK) ? STATUS_SUCCESS : STATUS_OBJECT_NAME_NOT_FOUND;

        bool isWrite = (generic_desiredaccess & (GENERIC_WRITE | GENERIC_ALL)) != 0;
        if (isFileInUse(filename, isWrite)) return STATUS_SHARING_VIOLATION;

        FIL* f = (FIL*)malloc(sizeof(FIL)); if (!f) return STATUS_NO_MEMORY;

        DWORD access = 0;
        if (generic_desiredaccess & (GENERIC_READ | GENERIC_ALL)) access |= FA_READ;
        if (generic_desiredaccess & (GENERIC_WRITE| GENERIC_ALL)) access |= FA_WRITE;

        switch (creation_disposition) {
        case CREATE_ALWAYS:
            if ((!isWrite) && (alreadyExists)) {
                free(f);
                return STATUS_ACCESS_DENIED;
            }
            res = f_open(f, filename.c_str(), FA_CREATE_ALWAYS | access);
            break;
        case CREATE_NEW:
            res = f_open(f, filename.c_str(), FA_CREATE_NEW | access);
            break;
        case OPEN_ALWAYS:
            res = f_open(f, filename.c_str(), FA_OPEN_ALWAYS | access);
            break;
        case OPEN_EXISTING:
            res = f_open(f, filename.c_str(), FA_OPEN_EXISTING | access);
            break;
        case TRUNCATE_EXISTING:
            res = f_open(f, filename.c_str(), FA_OPEN_EXISTING | access);
            if (res == FR_OK) f_truncate(f);
            break;
        default:
            free(f);
            return STATUS_ACCESS_DENIED;
        }

        if (res == FR_OK) {
            dokanfileinfo->Context = (ULONG64)f;
            addTrackFileInUse(filename, isWrite);
            return STATUS_SUCCESS;
        }
        else {
            free(f);
            return makeFileOpenStatus(res);
        }
    }    
    return STATUS_ACCESS_DENIED;
}

void DokanFileSystemFATFS::fs_cleanup(const std::wstring& filename, PDOKAN_FILE_INFO dokanfileinfo) {
    if (!m_volume) return;

    if (dokanfileinfo->Context) {
        ActiveFileIO io = notifyIOInUse(dokanfileinfo);
        FIL* fle = (FIL*)dokanfileinfo->Context;
        f_close(fle);
        free(fle);

        releaseFileInUse(filename);

        dokanfileinfo->Context = 0;
    }
    if (dokanfileinfo->DeleteOnClose) {
        // Delete happens during cleanup and not in close event.
        ActiveFileIO io = notifyIOInUse(dokanfileinfo);
        f_unlink(filename.c_str());
    }
}

NTSTATUS DokanFileSystemFATFS::fs_readfile(const std::wstring& filename, void* buffer, const uint32_t bufferlength, uint32_t& actualReadLength, const int64_t offset, PDOKAN_FILE_INFO dokanfileinfo) {
    UNREFERENCED_PARAMETER(filename);
    if (dokanfileinfo->Context) {
        FIL* fle = (FIL*)dokanfileinfo->Context;

        ActiveFileIO io = notifyIOInUse(dokanfileinfo);

        if (f_tell(fle) != offset) {
            FRESULT res = f_lseek(fle, offset);
            if (res != FR_OK) return makeFileUseStatus(res);
        }
        UINT actuallyRead;
        FRESULT res = f_read(fle, buffer, bufferlength, &actuallyRead);
        if (res != FR_OK) return makeFileUseStatus(res);
        actualReadLength = actuallyRead;
    }
    else {
        actualReadLength = 0;
    }
    return STATUS_SUCCESS;
}

NTSTATUS DokanFileSystemFATFS::fs_writefile(const std::wstring& filename, const void* buffer, const uint32_t bufferLength, uint32_t& actualWriteLength, const int64_t offset, PDOKAN_FILE_INFO dokanfileinfo) {
    if (dokanfileinfo->Context) {
        FIL* fle = (FIL*)dokanfileinfo->Context;

        ActiveFileIO io = notifyIOInUse(dokanfileinfo);
        uint64_t actualOffset;
        uint32_t number_of_bytes_to_write = bufferLength;

        if (offset == -1) {
            FRESULT res = f_lseek(fle, offset);
            actualOffset = f_size(fle);
            if (res != FR_OK) return makeFileUseStatus(res);
        }
        else actualOffset = (uint64_t)offset;

        if (dokanfileinfo->PagingIo) {
            // PagingIo cannot extend file size.
            // We return STATUS_SUCCESS when offset is beyond fileSize
            // and write the maximum we are allowed to.
            if (actualOffset >= f_size(fle)) {
                actualWriteLength = 0;
                return STATUS_SUCCESS;
            }

            if ((actualOffset + number_of_bytes_to_write) > f_size(fle)) {
                // resize the write length to not go beyond file size.
                LONGLONG bytes = f_size(fle) - actualOffset;
                if (bytes >> 32) {
                    number_of_bytes_to_write = static_cast<DWORD>(bytes & 0xFFFFFFFFUL);
                }
                else {
                    number_of_bytes_to_write = static_cast<DWORD>(bytes);
                }
            }
        }

        if (f_tell(fle) != offset) {
            FRESULT res = f_lseek(fle, offset);
            if (res != FR_OK) return makeFileUseStatus(res);
        }

        UINT written;
        FRESULT res = f_write(fle, buffer, number_of_bytes_to_write, &written);
        if (res != FR_OK) return makeFileUseStatus(res);
        actualWriteLength = written;

        if (written < number_of_bytes_to_write) return STATUS_DISK_FULL;

        return STATUS_SUCCESS;
    }
    else {
        return STATUS_ACCESS_DENIED;
    }   
}

NTSTATUS DokanFileSystemFATFS::fs_flushfilebuffers(const std::wstring& filename, PDOKAN_FILE_INFO dokanfileinfo) {
    UNREFERENCED_PARAMETER(filename);
        
    if (dokanfileinfo->Context) {
        FIL* fle = (FIL*)dokanfileinfo->Context;
        ActiveFileIO io = notifyIOInUse(dokanfileinfo);
        return makeFileUseStatus(f_sync(fle));
    }
    else return STATUS_OBJECT_NAME_NOT_FOUND;

    return STATUS_ACCESS_DENIED;
}

NTSTATUS DokanFileSystemFATFS::fs_setendoffile(const std::wstring& filename, const uint64_t ByteOffset, PDOKAN_FILE_INFO dokanfileinfo) {
    UNREFERENCED_PARAMETER(filename);
   
    if (dokanfileinfo->Context) {
        FIL* fle = (FIL*)dokanfileinfo->Context;
        FRESULT res = f_lseek(fle, ByteOffset); if (res != FR_OK) return makeFileUseStatus(res);
        return makeFileUseStatus(f_truncate(fle));
    } else return STATUS_OBJECT_NAME_NOT_FOUND;
}

NTSTATUS DokanFileSystemFATFS::fs_setallocationsize(const std::wstring& filename, const uint64_t alloc_size, PDOKAN_FILE_INFO dokanfileinfo) {
    UNREFERENCED_PARAMETER(filename);   
    
    if (dokanfileinfo->Context) {
        FIL* fle = (FIL*)dokanfileinfo->Context;
        ActiveFileIO io = notifyIOInUse(dokanfileinfo);

        FSIZE_t siz = f_size(fle);

        if (alloc_size < siz) {
            FRESULT res = f_lseek(fle, alloc_size); if (res != FR_OK) return makeFileUseStatus(res);
            return makeFileUseStatus(f_truncate(fle));
        }
        else {
            return makeFileUseStatus(f_expand(fle, alloc_size, 1));
        }
    }
    else return STATUS_OBJECT_NAME_NOT_FOUND;
    
    return STATUS_DATA_ERROR;
}

NTSTATUS DokanFileSystemFATFS::fs_getfileInformation(const std::wstring& filename, LPBY_HANDLE_FILE_INFORMATION buffer, PDOKAN_FILE_INFO dokanfileinfo) {
    FILINFO info;
    if (filename == L"\\") {
        buffer->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        buffer->nNumberOfLinks = 1;
        buffer->nFileIndexHigh = 0;
        buffer->nFileIndexLow = 0;
        buffer->nFileSizeHigh = 0;
        buffer->nFileSizeLow = 0;
        buffer->dwVolumeSerialNumber = volumeSerialNumber();
        buffer->ftLastAccessTime = { 0, 0 };
        buffer->ftLastWriteTime = { 0, 0 };
        buffer->ftCreationTime = { 0, 0 };
        return STATUS_SUCCESS;
    }

    if (f_stat(filename.c_str(), &info) == FR_OK) {
        buffer->dwFileAttributes = mapAttributes(info.fattrib);
        buffer->nNumberOfLinks = 1;
        buffer->nFileIndexHigh = 0;
        buffer->nFileIndexLow = 0;
        buffer->nFileSizeHigh = 0;
        buffer->nFileSizeLow = info.fsize;
        buffer->dwVolumeSerialNumber = volumeSerialNumber();

        SYSTEMTIME tm;
        tm.wYear = (info.fdate >> 9) + 1980;
        tm.wMonth = (info.fdate >> 5) & 0xF;
        tm.wDay = info.fdate & 0x1F;
        tm.wHour = info.ftime >> 11;
        tm.wMinute = (info.ftime >> 5) & 0x3F;
        tm.wSecond = (info.ftime & 31) * 2;
        tm.wMilliseconds = 0;
        tm.wDayOfWeek = 0;

        buffer->ftLastAccessTime = { 0, 0 };
        if (info.fattrib & AM_DIR) {
            buffer->ftLastWriteTime = { 0, 0 };
            SystemTimeToFileTime(&tm, &buffer->ftCreationTime);
        }
        else {
            buffer->ftCreationTime = { 0, 0 };
            SystemTimeToFileTime(&tm, &buffer->ftLastWriteTime);
        }
        return STATUS_SUCCESS;
    }
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

NTSTATUS DokanFileSystemFATFS::fs_findfiles(const std::wstring& filename, PFillFindData fill_finddata, PDOKAN_FILE_INFO dokanfileinfo) {     
    WIN32_FIND_DATAW findData;
    ZeroMemory(&findData, sizeof(WIN32_FIND_DATAW));

    DIR dir;
    if (f_opendir(&dir, filename.c_str()) == FR_OK) {
        FILINFO info;

        for (;;) {
            FRESULT res = f_readdir(&dir, &info);
            if (res != FR_OK || info.fname[0] == 0) break;

            wcscpy_s(findData.cFileName, info.fname);
            findData.nFileSizeHigh = 0;
            findData.nFileSizeLow = info.fsize;
            findData.dwFileAttributes = mapAttributes(info.fattrib);

            SYSTEMTIME tm;
            tm.wYear = (info.fdate >> 9) + 1980;
            tm.wMonth = (info.fdate >> 5) & 0xF;
            tm.wDay = info.fdate & 0x1F;
            tm.wHour = info.ftime >> 11;
            tm.wMinute = (info.ftime >> 5) & 0x3F;
            tm.wSecond = (info.ftime & 31) * 2;
            tm.wMilliseconds = 0;
            tm.wDayOfWeek = 0;
            SystemTimeToFileTime(&tm, &findData.ftLastWriteTime);
            
            fill_finddata(&findData, dokanfileinfo);
        }

        f_closedir(&dir);
        return STATUS_SUCCESS;
    }
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

NTSTATUS DokanFileSystemFATFS::fs_setfileattributes(const std::wstring& filename, const uint32_t fileattributes, PDOKAN_FILE_INFO dokanfileinfo) {
    BYTE newAttr = 0;
    if (fileattributes& FILE_ATTRIBUTE_READONLY   ) newAttr |= AM_RDO;
    if (fileattributes& FILE_ATTRIBUTE_HIDDEN     ) newAttr |= AM_HID;
    if (fileattributes& FILE_ATTRIBUTE_SYSTEM     ) newAttr |= AM_SYS;
    if (fileattributes& FILE_ATTRIBUTE_ARCHIVE    ) newAttr |= AM_ARC;

    return makeFileUseStatus(f_chmod(filename.c_str(), newAttr, AM_RDO | AM_HID | AM_SYS | AM_ARC));
}

NTSTATUS DokanFileSystemFATFS::fs_setfiletime(const std::wstring& filename, CONST FILETIME* creationtime, CONST FILETIME* lastaccesstime, CONST FILETIME* lastwritetime, PDOKAN_FILE_INFO dokanfileinfo) {
    if (!lastwritetime) return STATUS_SUCCESS;

    FILINFO inf;
    SYSTEMTIME sys;
    FileTimeToSystemTime(lastwritetime, &sys);
    inf.fdate = ((sys.wYear - 1980) << 9) | (sys.wMonth << 5) | (sys.wDay);
    inf.ftime = (sys.wHour << 11) | (sys.wMinute << 5) | (sys.wSecond >> 1);

    return makeFileUseStatus(f_utime(filename.c_str(), &inf));
}

NTSTATUS DokanFileSystemFATFS::fs_deletefile(const std::wstring& filename, PDOKAN_FILE_INFO dokanfileinfo) {   
    /*
    std::string amigaName;
    int32_t search = locatePath(filename, dokanfileinfo, amigaName);
    if (search == 0) return STATUS_OBJECT_NAME_NOT_FOUND;
    if (search != ST_FILE) return STATUS_ACCESS_DENIED;

    if (isFileInUse(amigaName.c_str(), AdfFileMode::ADF_FILE_MODE_WRITE))
        return STATUS_SHARING_VIOLATION;

    struct bEntryBlock parent;
    if (adfReadEntryBlock(m_volume, m_volume->curDirPtr, &parent) == RC_OK) {
        if ((parent.secType == ST_FILE) && (hasW(parent.access)))
            return STATUS_CANNOT_DELETE;
    }

    adfParentDir(m_volume);

    if (adfRemoveEntry(m_volume, m_volume->curDirPtr, amigaName.c_str()) == RC_OK) return STATUS_SUCCESS;
    */
    return STATUS_DATA_ERROR;
}

NTSTATUS DokanFileSystemFATFS::fs_deletedirectory(const std::wstring& filename, PDOKAN_FILE_INFO dokanfileinfo) {
    /*
    std::string amigaName;
    int32_t search = locatePath(filename, dokanfileinfo, amigaName);
    if (search == 0) return STATUS_OBJECT_NAME_NOT_FOUND;
    if (search != ST_DIR) 
        return STATUS_ACCESS_DENIED;

    struct bDirBlock dirBlock;
    if (adfReadEntryBlock(m_volume, m_volume->curDirPtr, (struct bEntryBlock*)&dirBlock) != RC_OK) return STATUS_DATA_ERROR;

    if (!isDirEmpty(&dirBlock)) return STATUS_DIRECTORY_NOT_EMPTY;

    adfParentDir(m_volume);

    if (adfRemoveEntry(m_volume, m_volume->curDirPtr, amigaName.c_str()) == RC_OK) return STATUS_SUCCESS;
    */
    return STATUS_DATA_ERROR;
}

NTSTATUS DokanFileSystemFATFS::fs_movefile(const std::wstring& filename, const std::wstring& new_filename, const bool replaceExisting, PDOKAN_FILE_INFO dokanfileinfo) {
    /*
    std::string amigaName;
    int32_t srcFileFolder = locatePath(filename, dokanfileinfo, amigaName);
    if (srcFileFolder == 0) return STATUS_OBJECT_NAME_NOT_FOUND;

    adfParentDir(m_volume);
    SECTNUM srcSector = m_volume->curDirPtr;
    SECTNUM dstSector = srcSector;

    std::wstring newName = new_filename;
    if (newName.length() && (newName[0] == '\\')) newName.erase(newName.begin());

    std::string amigaTargetName;

    std::string targetNameOutput;
    int32_t target = locatePath(new_filename, dokanfileinfo, targetNameOutput);
    if ((!replaceExisting) && (target != 0)) return STATUS_OBJECT_NAME_EXISTS;
    SECTNUM targetNameSec = m_volume->curDirPtr;

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
        if (adfRemoveEntry(m_volume, targetNameSec, targetNameOutput.c_str()) != RC_OK)
            return STATUS_ACCESS_DENIED;
    }

    if (amigaTargetName.length() > MAXNAMELEN) return STATUS_OBJECT_NAME_INVALID;

    if (adfRenameEntry(m_volume, srcSector, amigaName.c_str(), dstSector, amigaTargetName.c_str()) == RC_OK) return STATUS_SUCCESS;
    */
    return STATUS_ACCESS_DENIED;
}

NTSTATUS DokanFileSystemFATFS::fs_getdiskfreespace(uint64_t& freeBytesAvailable, uint64_t& totalNumBytes, uint64_t& totalNumFreeBytes, PDOKAN_FILE_INFO dokanfileinfo) {   
    DWORD fre_clust;
    FATFS* fs;
    if (f_getfree(L"\\", &fre_clust, &fs) == FR_OK) {
        DWORD tot_sect = (fs->n_fatent - 2) * fs->csize;
        DWORD fre_sect = fre_clust * fs->csize;

        freeBytesAvailable = fre_clust * fs->csize * SECTOR_SIZE;
        totalNumBytes = tot_sect * SECTOR_SIZE;
        totalNumFreeBytes = fre_clust * fs->csize * SECTOR_SIZE;

        return STATUS_SUCCESS;
    }    
    return STATUS_DATA_ERROR;
}

NTSTATUS DokanFileSystemFATFS::fs_getvolumeinformation(std::wstring& volumeName, uint32_t& volumeSerialNumber, uint32_t& maxComponentLength, uint32_t& filesystemFlags, std::wstring& filesystemName, PDOKAN_FILE_INFO dokanfileinfo) {
    WCHAR label[36] = { 0 };
    DWORD serial;
    if (f_getlabel(L"\\", (TCHAR*)label, &serial) == FR_OK) {
        volumeSerialNumber = serial;
        volumeName = label;
        
#ifdef FF_USE_LFN
        maxComponentLength = 255;
        filesystemFlags = FILE_CASE_PRESERVED_NAMES | FILE_UNICODE_ON_DISK;
#else
        maxComponentLength = 11;
        filesystemFlags = 0;
#endif
        switch (m_volume->fs_type) {
        case FS_FAT12: filesystemName = L"FAT12"; break;
        case FS_FAT16: filesystemName = L"FAT16"; break;
        case FS_FAT32: filesystemName = L"FAT32"; break;
        case FS_EXFAT: filesystemName = L"exFAT"; break;
        default: filesystemName = L"Unknown"; break;
        }
    }
    else {
        volumeName = L"Unknown";
        filesystemName = L"???";
    }

    return STATUS_SUCCESS;
}
