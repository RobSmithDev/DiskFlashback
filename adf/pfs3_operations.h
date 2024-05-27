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

#include <dokan/dokan.h>
#include <string>
#include "dokaninterface.h"
#include "amiga_operations.h"
#include "PFS3Lib/pfs3.h"
#include <map>
#include <unordered_map>

// A class with all of the Dokan commands needed
class DokanFileSystemAmigaPFS3 : public DokanFileSystemAmiga {
private:
    IPFS3* m_volume;             // The volume

    // Files in use
    std::unordered_map<std::string, bool> m_inUse;

    // Return TRUE if file is in use for the new requested mode
    bool isFileInUse(const std::string& name, const bool readOnly);
    void addTrackFileInUse(const std::string amigaFilename, bool readOnly);
    void releaseFileInUse(const std::string amigaFilename);
    NTSTATUS pfs3Error2SysError(const IPFS3::Error err);
    DWORD amigaToWindowsAttributes(const IPFS3::FileInformation& fileInfo);
public:
    DokanFileSystemAmigaPFS3(DokanFileSystemManager* owner, bool autoRename);
    virtual NTSTATUS fs_createfile(const std::wstring& filename, const PDOKAN_IO_SECURITY_CONTEXT security_context, const ACCESS_MASK generic_desiredaccess, const uint32_t file_attributes, const uint32_t shareaccess, const uint32_t creation_disposition, const bool fileSupersede, PDOKAN_FILE_INFO dokanfileinfo) override;
    virtual void fs_cleanup(const std::wstring& filename, PDOKAN_FILE_INFO dokanfileinfo) override;
    virtual NTSTATUS fs_readfile(const std::wstring& filename, void* buffer, const uint32_t bufferlength, uint32_t& actualReadLength, const int64_t offset, PDOKAN_FILE_INFO dokanfileinfo) override;
    virtual NTSTATUS fs_writefile(const std::wstring& filename, const void* buffer, const uint32_t bufferLength, uint32_t& actualWriteLength, const int64_t offset, PDOKAN_FILE_INFO dokanfileinfo) override;
    virtual NTSTATUS fs_flushfilebuffers(const std::wstring& filename, PDOKAN_FILE_INFO dokanfileinfo) override;
    virtual NTSTATUS fs_setendoffile(const std::wstring& filename, const uint64_t ByteOffset, PDOKAN_FILE_INFO dokanfileinfo) override;
    virtual NTSTATUS fs_setallocationsize(const std::wstring& filename, const uint64_t alloc_size, PDOKAN_FILE_INFO dokanfileinfo) override;
    virtual NTSTATUS fs_getfileInformation(const std::wstring& filename, LPBY_HANDLE_FILE_INFORMATION buffer, PDOKAN_FILE_INFO dokanfileinfo) override;
    virtual NTSTATUS fs_findfiles(const std::wstring& filename, PFillFindData fill_finddata, PDOKAN_FILE_INFO dokanfileinfo) override;
    virtual NTSTATUS fs_setfileattributes(const std::wstring& filename, const uint32_t fileattributes, PDOKAN_FILE_INFO dokanfileinfo) override;
    virtual NTSTATUS fs_setfiletime(const std::wstring& filename, const FILETIME* creationtime, const FILETIME* lastaccesstime, const FILETIME* lastwritetime, PDOKAN_FILE_INFO dokanfileinfo) override;
    virtual NTSTATUS fs_deletefile(const std::wstring& filename, PDOKAN_FILE_INFO dokanfileinfo) override;
    virtual NTSTATUS fs_deletedirectory(const std::wstring& filename, PDOKAN_FILE_INFO dokanfileinfo) override;
    virtual NTSTATUS fs_movefile(const std::wstring& filename, const std::wstring& newFilename, const bool replaceExisting, PDOKAN_FILE_INFO dokanfileinfo) override;
    virtual NTSTATUS fs_getdiskfreespace(uint64_t& freeBytesAvailable, uint64_t& totalNumBytes, uint64_t& totalNumFreeBytes, PDOKAN_FILE_INFO dokanfileinfo) override;
    virtual NTSTATUS fs_getvolumeinformation(std::wstring& volumeName, uint32_t& volumeSerialNumber, uint32_t& maxComponentLength, uint32_t& filesystemFlags, std::wstring& filesystemName, PDOKAN_FILE_INFO dokanfileinfo) override;
    virtual bool isFileSystemReady() override;
    virtual bool isDiskInUse() override;
    void setCurrentVolume(IPFS3* volume);
};

