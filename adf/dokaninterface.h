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

#pragma once


#define APPLICATION_NAME "DiskFlashback"
#define APPLICATION_NAME_L L"DiskFlashback"

#include <dokan/dokan.h>
#include <string>
#include <stdint.h>
#include "sectorCache.h"
#include "SignalWnd.h"

class DokanFileSystemManager;

// A class with all of the Dokan commands needed for a specific type of file system
class DokanFileSystemBase {
private: 
    DokanFileSystemManager* m_owner;
    CMessageWindow* m_messageWindow = nullptr;
protected:
    inline DokanFileSystemManager* owner() { return m_owner; };
public:
    DokanFileSystemBase(DokanFileSystemManager* owner);
    virtual NTSTATUS fs_createfile(const std::wstring& filename, const PDOKAN_IO_SECURITY_CONTEXT security_context, const ACCESS_MASK generic_desiredaccess, const uint32_t file_attributes, const uint32_t shareaccess, const uint32_t creation_disposition, const bool fileSupersede, PDOKAN_FILE_INFO dokanfileinfo) = 0;
    virtual void fs_cleanup(const std::wstring& filename, PDOKAN_FILE_INFO dokanfileinfo) = 0;
    virtual void fs_closeFile(const std::wstring& filename, PDOKAN_FILE_INFO dokanfileinfo);
    virtual NTSTATUS fs_readfile(const std::wstring& filename, void* buffer, const uint32_t bufferlength, uint32_t& actualReadLength, const int64_t offset, PDOKAN_FILE_INFO dokanfileinfo) = 0;
    virtual NTSTATUS fs_writefile(const std::wstring& filename, const void* buffer, const uint32_t bufferLength, uint32_t& actualWriteLength, const int64_t offset, PDOKAN_FILE_INFO dokanfileinfo) = 0;
    virtual NTSTATUS fs_flushfilebuffers(const std::wstring& filename, PDOKAN_FILE_INFO dokanfileinfo) = 0;
    virtual NTSTATUS fs_setendoffile(const std::wstring& filename, const uint64_t ByteOffset, PDOKAN_FILE_INFO dokanfileinfo) = 0;
    virtual NTSTATUS fs_setallocationsize(const std::wstring& filename, const uint64_t alloc_size, PDOKAN_FILE_INFO dokanfileinfo) = 0;
    virtual NTSTATUS fs_getfileInformation(const std::wstring& filename, LPBY_HANDLE_FILE_INFORMATION buffer, PDOKAN_FILE_INFO dokanfileinfo) = 0;
    virtual NTSTATUS fs_findfiles(const std::wstring& filename, PFillFindData fill_finddata, PDOKAN_FILE_INFO dokanfileinfo) = 0;
    virtual NTSTATUS fs_setfileattributes(const std::wstring& filename, const uint32_t fileattributes, PDOKAN_FILE_INFO dokanfileinfo) = 0;
    virtual NTSTATUS fs_setfiletime(const std::wstring& filename, const FILETIME* creationtime, const FILETIME* lastaccesstime, const FILETIME* lastwritetime, PDOKAN_FILE_INFO dokanfileinfo) = 0;
    virtual NTSTATUS fs_deletefile(const std::wstring& filename, PDOKAN_FILE_INFO dokanfileinfo) = 0;
    virtual NTSTATUS fs_deletedirectory(const std::wstring& filename, PDOKAN_FILE_INFO dokanfileinfo) = 0;
    virtual NTSTATUS fs_movefile(const std::wstring& filename, const std::wstring& newFilename, const bool replaceExisting, PDOKAN_FILE_INFO dokanfileinfo) = 0;
    virtual NTSTATUS fs_getdiskfreespace(uint64_t& freeBytesAvailable, uint64_t& totalNumBytes, uint64_t& totalNumFreeBytes, PDOKAN_FILE_INFO dokanfileinfo) = 0;       
    virtual NTSTATUS fs_getvolumeinformation(std::wstring& volumeName, uint32_t& volumeSerialNumber, uint32_t& maxComponentLength, uint32_t& filesystemFlags, std::wstring& filesystemName, PDOKAN_FILE_INFO dokanfileinfo) = 0;
    virtual bool isWriteProtected();
    virtual uint32_t volumeSerialNumber();
    virtual const std::wstring getDriverName();
    virtual bool isFileSystemReady() = 0;
    virtual bool isDiskInUse() = 0;

};

// A class that handles the redirection of everything dokan 
class DokanFileSystemManager {
private:
    DokanFileSystemBase* m_activeFileSystem = nullptr;
    std::wstring m_mountPoint;
    std::wstring m_mainExe;
    WCHAR m_initialLetter;
    bool m_forceWriteProtect;
    bool m_driveLocked = false;
    DOKAN_HANDLE m_dokanInstance = 0;
    bool m_isNonDOS = false;
    DOKAN_OPTIONS dokan_options;   // stupidly these are always needed in scope!
protected:
    void setActiveFileSystem(DokanFileSystemBase* fileSystem) { m_activeFileSystem = fileSystem; };
    virtual bool isForcedWriteProtect() { return m_forceWriteProtect; };
    void setIsNONDos(bool isNonDOS) { m_isNonDOS = isNonDOS; };
public:
    DokanFileSystemManager(WCHAR initialLetter, bool forceWriteProtect, const std::wstring &mainExe);

    // Fetch the active dokan file system
    DokanFileSystemBase* getActiveSystem() { return m_activeFileSystem; };
    virtual bool isDriveLocked() { return m_driveLocked; };
    DOKAN_HANDLE getDonakInstance() { return m_dokanInstance; };
    virtual bool isDiskInDrive() = 0;
    virtual bool isDriveRecognised();
    virtual bool isWriteProtected() = 0;
    virtual uint32_t volumeSerial() = 0;
    virtual const std::wstring getDriverName() = 0;
    virtual SectorCacheEngine* getBlockDevice() = 0;
    virtual bool isDriveInUse();
    virtual bool isPhysicalDevice() = 0;
    virtual uint32_t getTotalTracks() = 0;

    virtual bool start();
    virtual void stop();

    virtual bool setLocked(bool enableLock) = 0;

    // Returns TRUE if the DOKAN system is up and running
    virtual bool isRunning() const;

    // Shut down the file system
    virtual void shutdownFS() = 0;

    // Return TRUE if this is a NON-DOS disk, but the disk type was recognised
    virtual bool isNonDOS() { return m_isNonDOS; };

    // Notifications of the file system being mounted
    virtual void onMounted(const std::wstring& mountPoint, PDOKAN_FILE_INFO dokanfileinfo);
    virtual void onUnmounted(PDOKAN_FILE_INFO dokanfileinfo);

    const std::wstring& getMountPoint() const { return m_mountPoint; };
    const std::wstring& getMainEXEName() const { return m_mainExe; }
};

extern void wideToAnsi(const std::wstring& wstr, std::string& str);
extern void ansiToWide(const std::string& wstr, std::wstring& str);



