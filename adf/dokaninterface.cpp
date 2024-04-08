
#include "dokaninterface.h"

extern DOKAN_OPERATIONS fs_operations;

#pragma region DokanFileSystemBase
DokanFileSystemBase::DokanFileSystemBase(DokanFileSystemManager* owner) : m_owner(owner) {

}

bool DokanFileSystemBase::isWriteProtected() { 
    return m_owner->isWriteProtected(); 
};

uint32_t DokanFileSystemBase::volumeSerialNumber() { 
    return m_owner->volumeSerial(); 
};

const std::wstring DokanFileSystemBase::getDriverName() { 
    return m_owner->getDriverName(); 
};

// close file default handler
void DokanFileSystemBase::fs_closeFile(const std::wstring& filename, PDOKAN_FILE_INFO dokanfileinfo) { 
    UNREFERENCED_PARAMETER(filename);
    UNREFERENCED_PARAMETER(dokanfileinfo);
};
 
#pragma endregion DokanFileSystemBase

#pragma region DokanFileSystemManager
bool DokanFileSystemManager::isDriveRecognised() {
    if (!m_activeFileSystem) return false;
    return m_activeFileSystem->isFileSystemReady();
}

bool DokanFileSystemManager::isRunning() const {
    return m_dokanInstance && DokanIsFileSystemRunning(m_dokanInstance);
}

// Start the file system
bool DokanFileSystemManager::start() {
    if (m_dokanInstance) return true;

    DOKAN_OPTIONS dokan_options;
    ZeroMemory(&dokan_options, sizeof(DOKAN_OPTIONS));
    dokan_options.Version = DOKAN_VERSION;
    dokan_options.Options = DOKAN_OPTION_CURRENT_SESSION | DOKAN_OPTION_MOUNT_MANAGER;
    std::wstring d = m_mountPoint;
    if (d.length() < 3) d += L":\\";
    dokan_options.MountPoint = d.c_str();
    dokan_options.SingleThread = true;
    dokan_options.GlobalContext = reinterpret_cast<ULONG64>(this);
    dokan_options.SectorSize =512;
    dokan_options.AllocationUnitSize = 512;
    dokan_options.Timeout = 5 * 60000; // 5 minutes

    if (m_forceWriteProtect) dokan_options.Options |= DOKAN_OPTION_WRITE_PROTECT;

    NTSTATUS status = DokanCreateFileSystem(&dokan_options, &fs_operations, &m_dokanInstance);
    return status == DOKAN_SUCCESS;
    /*
    switch (status) {
    case DOKAN_SUCCESS:
        break;
    case DOKAN_ERROR:
        throw std::runtime_error("Error");
    case DOKAN_DRIVE_LETTER_ERROR:
        throw std::runtime_error("Bad Drive letter");
    case DOKAN_DRIVER_INSTALL_ERROR:
        throw std::runtime_error("Can't install driver");
    case DOKAN_START_ERROR:
        throw std::runtime_error("Driver something wrong");
    case DOKAN_MOUNT_ERROR:
        throw std::runtime_error("Can't assign a drive letter");
    case DOKAN_MOUNT_POINT_ERROR:
        throw std::runtime_error("Mount point error");
    case DOKAN_VERSION_ERROR:
        throw std::runtime_error("Version error");
    default:
        throw std::runtime_error("Unknown error");
    }
    */
}

// Close the file system
void DokanFileSystemManager::stop() {
    if (m_dokanInstance) {
        DokanCloseHandle(m_dokanInstance);
        m_dokanInstance = 0;
        m_mountPoint[0] = L'?';
    }
}

bool DokanFileSystemManager::isDriveInUse() {
    if (m_activeFileSystem) return m_activeFileSystem->isDiskInUse();
    return false;
}

DokanFileSystemManager::DokanFileSystemManager(WCHAR initialLetter, bool forceWriteProtect, const std::wstring& mainExe) : 
    m_initialLetter(initialLetter), m_forceWriteProtect(forceWriteProtect), m_mainExe(mainExe) {
    m_mountPoint.resize(3);
    m_mountPoint[0] = initialLetter;
    m_mountPoint[1] = L':';
    m_mountPoint[2] = L'\\';
}

// Notifications of the file system being mounted
void DokanFileSystemManager::onMounted(const std::wstring& mountPoint, PDOKAN_FILE_INFO dokanfileinfo) {
    if (mountPoint.empty()) return;
    m_mountPoint[0] = towupper(mountPoint[0]);
}

// Unmount notification
void DokanFileSystemManager::onUnmounted(PDOKAN_FILE_INFO dokanfileinfo) {
    m_mountPoint[0] = m_initialLetter;
}
#pragma endregion DokanFileSystemManager

#pragma region MiscFuncs
void wideToAnsi(const std::wstring& wstr, std::string& str) {

    int size = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    if (size) {
        str.resize(size);
        WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, &str[0], size, NULL, NULL);
        str.resize(size - 1);
    }
}

void ansiToWide(const std::string& wstr, std::wstring& str) {
    int size = MultiByteToWideChar(CP_ACP, 0, wstr.c_str(), -1, NULL, 0);
    if (size) {
        str.resize(size * 2);
        MultiByteToWideChar(CP_ACP, 0, wstr.c_str(), -1, &str[0], size * 2);
    }
}
#pragma endregion MiscFuncs

#pragma region DokanInterface
NTSTATUS fs_checkVolume(const std::wstring& fname, DokanFileSystemManager* manager) {
    // Allow queries to \\ only
    if (manager->isDriveLocked()) {
        if (fname.length() <= 2) return STATUS_SUCCESS;
        return STATUS_DEVICE_BUSY;
    }
    if (!manager->isDiskInDrive()) {
        if (fname.length() <= 2) return STATUS_SUCCESS;
        return STATUS_NO_MEDIA_IN_DEVICE;
    }
    if (!manager->isDriveRecognised()) {
        if (fname.length() <= 2) return STATUS_SUCCESS;
        return STATUS_UNRECOGNIZED_MEDIA;
    }
    return STATUS_SUCCESS;
}

NTSTATUS fs_checkVolume(DokanFileSystemManager* manager) {
    return fs_checkVolume(L"\\\\", manager);
}

static NTSTATUS DOKAN_CALLBACK fs_createfile(LPCWSTR filename, PDOKAN_IO_SECURITY_CONTEXT security_context, ACCESS_MASK desiredaccess, ULONG fileattributes, ULONG shareaccess, ULONG createdisposition, ULONG createoptions, PDOKAN_FILE_INFO dokanfileinfo) {
    const std::wstring fname = filename;
    if (fname.substr(0, 26) == L"\\System Volume Information" || fname.substr(0, 13) == L"\\$RECYCLE.BIN") return STATUS_NO_SUCH_FILE;

    DokanFileSystemManager* manager = reinterpret_cast<DokanFileSystemManager*> (dokanfileinfo->DokanOptions->GlobalContext);
    if (!manager) return STATUS_ACCESS_DENIED;

    // Some basic filtering
    NTSTATUS status = fs_checkVolume(fname, manager);
    if (status != STATUS_SUCCESS) return status;

    DokanFileSystemBase* fs = manager->getActiveSystem();
    if (fs) {
        DWORD file_attributes_and_flags;
        ACCESS_MASK generic_desiredaccess;
        DWORD creation_disposition;

        DokanMapKernelToUserCreateFileFlags(desiredaccess, fileattributes, createoptions, createdisposition, &generic_desiredaccess, &file_attributes_and_flags, &creation_disposition);
        return fs->fs_createfile(fname, security_context, generic_desiredaccess, file_attributes_and_flags, shareaccess, creation_disposition, createdisposition == FILE_SUPERSEDE, dokanfileinfo);
    }
    else {
        if (fname == L"\\") {
            dokanfileinfo->IsDirectory = true;
            return STATUS_SUCCESS;
        }
    }

    return STATUS_ACCESS_DENIED;
}

static void DOKAN_CALLBACK fs_cleanup(LPCWSTR filename, PDOKAN_FILE_INFO dokanfileinfo) {
    DokanFileSystemManager* manager = reinterpret_cast<DokanFileSystemManager*> (dokanfileinfo->DokanOptions->GlobalContext);
    if (!manager) return;

    DokanFileSystemBase* fs = manager->getActiveSystem();
    if (fs) fs->fs_cleanup(filename, dokanfileinfo);
}

static void DOKAN_CALLBACK fs_closeFile(LPCWSTR filename, PDOKAN_FILE_INFO dokanfileinfo) {
    DokanFileSystemManager* manager = reinterpret_cast<DokanFileSystemManager*> (dokanfileinfo->DokanOptions->GlobalContext);
    if (!manager) STATUS_ACCESS_DENIED;

    DokanFileSystemBase* fs = manager->getActiveSystem();
    if (fs) fs->fs_closeFile(filename, dokanfileinfo);
}

static NTSTATUS DOKAN_CALLBACK fs_readfile(LPCWSTR filename, LPVOID buffer, DWORD bufferlength, LPDWORD readlength, LONGLONG offset, PDOKAN_FILE_INFO dokanfileinfo) {
    DokanFileSystemManager* manager = reinterpret_cast<DokanFileSystemManager*> (dokanfileinfo->DokanOptions->GlobalContext);
    if (!manager) return STATUS_ACCESS_DENIED;

    // Some basic filtering
    NTSTATUS status = fs_checkVolume(filename, manager);
    if (status != STATUS_SUCCESS) return status;

    DokanFileSystemBase* fs = manager->getActiveSystem();
    if (fs) {
        uint32_t l;
        NTSTATUS t = fs->fs_readfile(filename, buffer, bufferlength, l, offset, dokanfileinfo);
        if (readlength) *readlength = l;
        return t;
    }

    return STATUS_ACCESS_DENIED;
}

static NTSTATUS DOKAN_CALLBACK fs_writefile(LPCWSTR filename, LPCVOID buffer, DWORD number_of_bytes_to_write, LPDWORD number_of_bytes_written, LONGLONG offset, PDOKAN_FILE_INFO dokanfileinfo) {
    DokanFileSystemManager* manager = reinterpret_cast<DokanFileSystemManager*> (dokanfileinfo->DokanOptions->GlobalContext);
    if (!manager) return STATUS_ACCESS_DENIED;

    // Some basic filtering
    NTSTATUS status = fs_checkVolume(filename, manager);
    if (status != STATUS_SUCCESS) return status;
    if (manager->isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

    DokanFileSystemBase* fs = manager->getActiveSystem();
    if (fs) {
        uint32_t dataWritten = 0;

        NTSTATUS l = fs->fs_writefile(filename, buffer, number_of_bytes_to_write, dataWritten, offset, dokanfileinfo);

        if (number_of_bytes_written) *number_of_bytes_written = dataWritten;

        return l;
    }

    return STATUS_ACCESS_DENIED;
}

static NTSTATUS DOKAN_CALLBACK fs_flushfilebuffers(LPCWSTR filename, PDOKAN_FILE_INFO dokanfileinfo) {
    DokanFileSystemManager* manager = reinterpret_cast<DokanFileSystemManager*> (dokanfileinfo->DokanOptions->GlobalContext);
    if (!manager) return STATUS_ACCESS_DENIED;

    // Some basic filtering
    NTSTATUS status = fs_checkVolume(filename, manager);
    if (status != STATUS_SUCCESS) return status;
    if (manager->isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

    DokanFileSystemBase* fs = manager->getActiveSystem();
    if (fs) return fs->fs_flushfilebuffers(filename, dokanfileinfo);

    return STATUS_ACCESS_DENIED;
}

static NTSTATUS DOKAN_CALLBACK fs_setendoffile(LPCWSTR filename, LONGLONG ByteOffset, PDOKAN_FILE_INFO dokanfileinfo) {
    DokanFileSystemManager* manager = reinterpret_cast<DokanFileSystemManager*> (dokanfileinfo->DokanOptions->GlobalContext);
    if (!manager) return STATUS_ACCESS_DENIED;

    // Some basic filtering
    NTSTATUS status = fs_checkVolume(filename, manager);
    if (status != STATUS_SUCCESS) return status;
    if (manager->isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

    DokanFileSystemBase* fs = manager->getActiveSystem();
    if (fs) return fs->fs_setendoffile(filename, ByteOffset, dokanfileinfo);

    return STATUS_ACCESS_DENIED;
}

static NTSTATUS DOKAN_CALLBACK fs_setallocationsize(LPCWSTR filename, LONGLONG alloc_size, PDOKAN_FILE_INFO dokanfileinfo) {
    DokanFileSystemManager* manager = reinterpret_cast<DokanFileSystemManager*> (dokanfileinfo->DokanOptions->GlobalContext);
    if (!manager) return STATUS_ACCESS_DENIED;

    // Some basic filtering
    NTSTATUS status = fs_checkVolume(filename, manager);
    if (status != STATUS_SUCCESS) return status;
    if (manager->isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

    DokanFileSystemBase* fs = manager->getActiveSystem();
    if (fs) return fs->fs_setallocationsize(filename, alloc_size, dokanfileinfo);

    return STATUS_ACCESS_DENIED;
}

static NTSTATUS DOKAN_CALLBACK fs_getfileInformation(LPCWSTR filename, LPBY_HANDLE_FILE_INFORMATION buffer, PDOKAN_FILE_INFO dokanfileinfo) {
    DokanFileSystemManager* manager = reinterpret_cast<DokanFileSystemManager*> (dokanfileinfo->DokanOptions->GlobalContext);
    if (!manager) return STATUS_ACCESS_DENIED;

    // Some basic filtering
    NTSTATUS status = fs_checkVolume(filename, manager);
    if (status != STATUS_SUCCESS) return status;

    DokanFileSystemBase* fs = manager->getActiveSystem();
    if (fs) return fs->fs_getfileInformation(filename, buffer, dokanfileinfo);

    return STATUS_ACCESS_DENIED;
}

static NTSTATUS DOKAN_CALLBACK fs_findfiles(LPCWSTR filename, PFillFindData fill_finddata, PDOKAN_FILE_INFO dokanfileinfo) {
    DokanFileSystemManager* manager = reinterpret_cast<DokanFileSystemManager*> (dokanfileinfo->DokanOptions->GlobalContext);
    if (!manager) return STATUS_NO_MEDIA_IN_DEVICE;

    // Some basic filtering
    NTSTATUS status = fs_checkVolume(filename, manager);
    if (status != STATUS_SUCCESS) return status;

    DokanFileSystemBase* fs = manager->getActiveSystem();
    if (fs) return fs->fs_findfiles(filename, fill_finddata, dokanfileinfo);

    return STATUS_ACCESS_DENIED;
}

static NTSTATUS DOKAN_CALLBACK fs_setfileattributes(LPCWSTR filename, DWORD fileattributes, PDOKAN_FILE_INFO dokanfileinfo) {
    DokanFileSystemManager* manager = reinterpret_cast<DokanFileSystemManager*> (dokanfileinfo->DokanOptions->GlobalContext);
    if (!manager) return STATUS_ACCESS_DENIED;

    // Some basic filtering
    NTSTATUS status = fs_checkVolume(filename, manager);
    if (status != STATUS_SUCCESS) return status;
    if (manager->isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

    DokanFileSystemBase* fs = manager->getActiveSystem();
    if (fs) return fs->fs_setfileattributes(filename, fileattributes, dokanfileinfo);

    return STATUS_ACCESS_DENIED;
}

static NTSTATUS DOKAN_CALLBACK fs_setfiletime(LPCWSTR filename, CONST FILETIME* creationtime, CONST FILETIME* lastaccesstime, CONST FILETIME* lastwritetime, PDOKAN_FILE_INFO dokanfileinfo) {
    DokanFileSystemManager* manager = reinterpret_cast<DokanFileSystemManager*> (dokanfileinfo->DokanOptions->GlobalContext);
    if (!manager) return STATUS_ACCESS_DENIED;

    // Some basic filtering
    NTSTATUS status = fs_checkVolume(filename, manager);
    if (status != STATUS_SUCCESS) return status;
    if (manager->isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

    DokanFileSystemBase* fs = manager->getActiveSystem();
    if (fs) return fs->fs_setfiletime(filename, creationtime, lastaccesstime, lastwritetime, dokanfileinfo);

    return STATUS_ACCESS_DENIED;
}

static NTSTATUS DOKAN_CALLBACK fs_deletefile(LPCWSTR filename, PDOKAN_FILE_INFO dokanfileinfo) {
    DokanFileSystemManager* manager = reinterpret_cast<DokanFileSystemManager*> (dokanfileinfo->DokanOptions->GlobalContext);
    if (!manager) return STATUS_ACCESS_DENIED;

    // Some basic filtering
    NTSTATUS status = fs_checkVolume(filename, manager);
    if (status != STATUS_SUCCESS) return status;
    if (manager->isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

    DokanFileSystemBase* fs = manager->getActiveSystem();
    if (fs) return fs->fs_deletefile(filename, dokanfileinfo);

    return STATUS_ACCESS_DENIED;
}

static NTSTATUS DOKAN_CALLBACK fs_deletedirectory(LPCWSTR filename, PDOKAN_FILE_INFO dokanfileinfo) {
    DokanFileSystemManager* manager = reinterpret_cast<DokanFileSystemManager*> (dokanfileinfo->DokanOptions->GlobalContext);
    if (!manager) return STATUS_ACCESS_DENIED;

    // Some basic filtering
    NTSTATUS status = fs_checkVolume(filename, manager);
    if (status != STATUS_SUCCESS) return status;
    if (manager->isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

    DokanFileSystemBase* fs = manager->getActiveSystem();
    if (fs) return fs->fs_deletedirectory(filename, dokanfileinfo);

    return STATUS_ACCESS_DENIED;
}

static NTSTATUS DOKAN_CALLBACK fs_movefile(LPCWSTR filename, LPCWSTR new_filename, BOOL replace_if_existing, PDOKAN_FILE_INFO dokanfileinfo) {
    DokanFileSystemManager* manager = reinterpret_cast<DokanFileSystemManager*> (dokanfileinfo->DokanOptions->GlobalContext);
    if (!manager) return STATUS_ACCESS_DENIED;

    // Some basic filtering
    NTSTATUS status = fs_checkVolume(filename, manager);
    if (status != STATUS_SUCCESS) return status;
    if (manager->isWriteProtected()) return STATUS_MEDIA_WRITE_PROTECTED;

    DokanFileSystemBase* fs = manager->getActiveSystem();
    if (fs) return fs->fs_movefile(filename, new_filename, replace_if_existing, dokanfileinfo);

    return STATUS_ACCESS_DENIED;
}

static NTSTATUS DOKAN_CALLBACK fs_getdiskfreespace(PULONGLONG free_bytes_available, PULONGLONG total_number_of_bytes, PULONGLONG total_number_of_free_bytes, PDOKAN_FILE_INFO dokanfileinfo) {
    DokanFileSystemManager* manager = reinterpret_cast<DokanFileSystemManager*> (dokanfileinfo->DokanOptions->GlobalContext);
    if (!manager) return STATUS_ACCESS_DENIED;

    // Some basic filtering
    NTSTATUS status = fs_checkVolume(manager);
    if (status != STATUS_SUCCESS) return status;

    DokanFileSystemBase* fs = manager->getActiveSystem();
    if (fs) {
        uint64_t freeBytes = 0;
        uint64_t numBytes = 0;
        uint64_t totalBytes = 0;
        NTSTATUS t = fs->fs_getdiskfreespace(freeBytes, numBytes, totalBytes, dokanfileinfo);

        if (free_bytes_available) *free_bytes_available = freeBytes;
        if (total_number_of_bytes) *total_number_of_bytes = numBytes;
        if (total_number_of_free_bytes) *total_number_of_free_bytes = totalBytes;

        return t;
    }

    return STATUS_ACCESS_DENIED;
}

static NTSTATUS DOKAN_CALLBACK fs_getvolumeinformation(LPWSTR volumename_buffer, DWORD volumename_size, LPDWORD volume_serialnumber, LPDWORD maximum_component_length, LPDWORD filesystem_flags, LPWSTR filesystem_name_buffer, DWORD filesystem_name_size, PDOKAN_FILE_INFO dokanfileinfo) {
    DokanFileSystemManager* manager = reinterpret_cast<DokanFileSystemManager*> (dokanfileinfo->DokanOptions->GlobalContext);
    if (!manager) {
        wcscpy_s(volumename_buffer, volumename_size, L"Unknown");
        wcscpy_s(filesystem_name_buffer, filesystem_name_size, L"Unknown");
        *filesystem_flags = FILE_READ_ONLY_VOLUME;
        return STATUS_SUCCESS;
    }


    if (!manager->isDriveRecognised()) {
        wcscpy_s(volumename_buffer, volumename_size, manager->getDriverName().c_str());
        wcscpy_s(filesystem_name_buffer, filesystem_name_size, L"Unknown");
        *filesystem_flags = FILE_READ_ONLY_VOLUME;
        return STATUS_SUCCESS;
    }
    // Allow queries to \\ only
    if (manager->isDriveLocked()) {
        wcscpy_s(volumename_buffer, volumename_size, manager->getDriverName().c_str());
        wcscpy_s(filesystem_name_buffer, filesystem_name_size, L"Busy");
        *filesystem_flags = FILE_READ_ONLY_VOLUME;
        return STATUS_SUCCESS;
    }
    if (!manager->isDiskInDrive()) {
        wcscpy_s(volumename_buffer, volumename_size, manager->getDriverName().c_str());
        wcscpy_s(filesystem_name_buffer, filesystem_name_size, L"No Disk");
        *filesystem_flags = FILE_READ_ONLY_VOLUME;
        return STATUS_SUCCESS;
    } 

    DokanFileSystemBase* fs = manager->getActiveSystem();
    if (fs) {
        std::wstring volName;
        std::wstring filesysName;
        uint32_t serialNumber = 0;
        uint32_t maxCompLength = 0;
        uint32_t sysFlags = 0;

        NTSTATUS t = fs->fs_getvolumeinformation(volName, serialNumber, maxCompLength, sysFlags, filesysName, dokanfileinfo);
        if (volName.empty()) volName = L"Unnamed";
        if (filesysName.empty()) filesysName = L"Unknown";
        if (volume_serialnumber) *volume_serialnumber = serialNumber;
        if (maximum_component_length) *maximum_component_length = maxCompLength;
        if (filesystem_flags) {
            *filesystem_flags = sysFlags;
            if (fs->isWriteProtected()) *filesystem_flags |= FILE_READ_ONLY_VOLUME;
        }
        if (volumename_buffer && volumename_size) {
            wcscpy_s(volumename_buffer, volumename_size, volName.c_str());
        }
        if (filesystem_name_buffer && filesystem_name_size) {
            wcscpy_s(filesystem_name_buffer, filesystem_name_size, filesysName.c_str());
        }

        return t;
    }
    else {
        wcscpy_s(volumename_buffer, volumename_size, manager->getDriverName().c_str());
        wcscpy_s(filesystem_name_buffer, filesystem_name_size, L"Unknown");
        *filesystem_flags = FILE_READ_ONLY_VOLUME;
        return STATUS_SUCCESS;
    }

    return STATUS_ACCESS_DENIED;
}

static NTSTATUS DOKAN_CALLBACK fs_mounted(LPCWSTR MountPoint, PDOKAN_FILE_INFO dokanfileinfo) {
    DokanFileSystemManager* manager = reinterpret_cast<DokanFileSystemManager*> (dokanfileinfo->DokanOptions->GlobalContext);
    if (!manager) return STATUS_ACCESS_DENIED;
    manager->onMounted(MountPoint, dokanfileinfo);

    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK fs_unmounted(PDOKAN_FILE_INFO dokanfileinfo) {
    DokanFileSystemManager* manager = reinterpret_cast<DokanFileSystemManager*> (dokanfileinfo->DokanOptions->GlobalContext);
    if (!manager) return STATUS_ACCESS_DENIED;
    manager->onUnmounted(dokanfileinfo);

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


#pragma endregion DokanInterface
