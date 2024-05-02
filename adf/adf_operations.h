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
#include "adflib/src/adflib.h"
#include "adflib/src/adf_blk.h"
#include <map>
#include <unordered_map>

// A class with all of the Dokan commands needed
class DokanFileSystemAmigaFS : public DokanFileSystemBase {
private:
    // Simple nasty class to auto release (so I don't forget) details about an active file i/o occuring. 
    // Everything is single threadded so this is OK
    class ActiveFileIO {
    private:
        DokanFileSystemAmigaFS* m_owner;
    public:
        ActiveFileIO(DokanFileSystemAmigaFS* owner);
        // Remove copy constructor
        ActiveFileIO(const ActiveFileIO&) = delete;
        // Add Move constructor
        ActiveFileIO(ActiveFileIO&& source) noexcept;
        ~ActiveFileIO();
    };
    friend class ActiveFileIO;

    bool m_autoRemapFileExtensions = false;  // remap mod.* to *.mod for example

    struct AdfVolume* m_volume;             // The volume

    // Reverse mapping for badly (non-windows) named files
    std::map<std::string, std::wstring> m_safeFilenameMap;
    std::map<std::wstring, std::string> m_specialRenameMap;

    // Files in use
    std::unordered_map<struct AdfFile*, int> m_inUse;

    // Convert Amiga file attributes to Windows file attributes - only a few actually match
    DWORD amigaToWindowsAttributes(const int32_t access, int32_t type);
    // Search for a file or folder, returns 0 if not found or the type of item (eg: ST_FILE)
    int32_t locatePath(const std::wstring& path, PDOKAN_FILE_INFO dokanfileinfo, std::string& filename);
    // Stub version of the above
    int32_t locatePath(const std::wstring& path, PDOKAN_FILE_INFO dokanfileinfo);


    // Sets a current file info block as active (or NULL for not) so DokanResetTimeout can be called if needed
    void setActiveFileIO(PDOKAN_FILE_INFO dokanfileinfo);
    void clearFileIO();
    // Let the system know I/O is currently happenning.  ActiveFileIO must be kepyt in scope until io is complete
    ActiveFileIO notifyIOInUse(PDOKAN_FILE_INFO dokanfileinfo);


    // Return TRUE if file is in use for the new requested mode
    bool isFileInUse(const char* const name, const AdfFileMode mode);
    void addTrackFileInUse(struct AdfFile* handle);
    void releaseFileInUse(struct AdfFile* handle);

    // Handles fixing filenames so they're amiga compatable - returns TRUE if the name changed
    void amigaFilenameToWindowsFilename(const std::wstring& windowsPath, const std::string& amigaFilename, std::wstring& windowsFilename);
    void windowsFilenameToAmigaFilename(const std::wstring& windowsFilename, std::string& amigaFilename);

    // Handle a note about the remap of file extension
    void handleRemap(const std::wstring& windowsPath, const std::string& amigaFilename, std::wstring& windowsFilename);

public:
    DokanFileSystemAmigaFS(DokanFileSystemManager* owner, bool autoRename);
    void changeAutoRename(bool autoRename);
    void resetFileSystem();
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
    void setCurrentVolume(AdfVolume* volume);
};

