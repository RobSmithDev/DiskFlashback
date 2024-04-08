#pragma once


#define APPLICATION_NAME "AMount"

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
protected:
    void setActiveFileSystem(DokanFileSystemBase* fileSystem) { m_activeFileSystem = fileSystem; };
    bool isForcedWriteProtect() const { return m_forceWriteProtect; };
public:
    DokanFileSystemManager(WCHAR initialLetter, bool forceWriteProtect, const std::wstring &mainExe);

    // Fetch the active dokan file system
    DokanFileSystemBase* getActiveSystem() { return m_activeFileSystem; };
    virtual bool isDriveLocked() { return m_driveLocked; };

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

    virtual void temporaryUnmountDrive() = 0;
    virtual void restoreUnmountedDrive() = 0;

    virtual bool setLocked(bool enableLock) = 0;

    // Returns TRUE if the DOKAN system is up and running
    virtual bool isRunning() const;

    // Notifications of the file system being mounted
    virtual void onMounted(const std::wstring& mountPoint, PDOKAN_FILE_INFO dokanfileinfo);
    virtual void onUnmounted(PDOKAN_FILE_INFO dokanfileinfo);

    const std::wstring& getMountPoint() const { return m_mountPoint; };
    const std::wstring& getMainEXEName() const { return m_mainExe; }
};

extern void wideToAnsi(const std::wstring& wstr, std::string& str);
extern void ansiToWide(const std::string& wstr, std::wstring& str);



