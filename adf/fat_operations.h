#pragma once

#include <dokan/dokan.h>
#include <string>
#include "dokaninterface.h"
#include "fatfs/source/ff.h"
#include <map>
#include <unordered_map>

// A class with all of the Dokan commands needed
class DokanFileSystemFATFS : public DokanFileSystemBase {
private:
    // Simple nasty class to auto release (so I don't forget) details about an active file i/o occuring. 
    // Everything is single threadded so this is OK
    class ActiveFileIO {
    private:
        DokanFileSystemFATFS* m_owner;
    public:
        ActiveFileIO(DokanFileSystemFATFS* owner);
        // Remove copy constructor
        ActiveFileIO(const ActiveFileIO&) = delete;
        // Add Move constructor
        ActiveFileIO(ActiveFileIO&& source) noexcept;
        ~ActiveFileIO();
    };
    friend class ActiveFileIO;

    FATFS* m_volume;             // The volume

    // Files in use
    std::unordered_map<std::wstring, bool> m_inUse;

    // Sets a current file info block as active (or NULL for not) so DokanResetTimeout can be called if needed
    void setActiveFileIO(PDOKAN_FILE_INFO dokanfileinfo);
    void clearFileIO();
    // Let the system know I/O is currently happenning.  ActiveFileIO must be kepyt in scope until io is complete
    ActiveFileIO notifyIOInUse(PDOKAN_FILE_INFO dokanfileinfo);


    // Return TRUE if file is in use for the new requested mode
    bool isFileInUse(const std::wstring& fullPath, bool isWrite);
    void addTrackFileInUse(const std::wstring& fullPath, bool isWrite);
    void releaseFileInUse(const std::wstring& fullPath);
public:
    DokanFileSystemFATFS(DokanFileSystemManager* owner);
    virtual NTSTATUS fs_createfile(const std::wstring& filename, const PDOKAN_IO_SECURITY_CONTEXT security_context, const ACCESS_MASK generic_desiredaccess, const uint32_t file_attributes, const uint32_t shareaccess, const uint32_t creation_disposition, const bool fileSupersede, PDOKAN_FILE_INFO dokanfileinfo) override;
    virtual void fs_closeFile(const std::wstring& filename, PDOKAN_FILE_INFO dokanfileinfo) override;
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
    void setCurrentVolume(FATFS* volume);
};

