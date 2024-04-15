#pragma once


#include <dokan/dokan.h>
#include <unordered_map>
#include <atomic>

// #define ATARTST_SUPPORTED

// Possible types of sector / file
enum class SectorType  {stAmiga, stIBM, 
#ifdef ATARTST_SUPPORTED
    stAtari, stHybrid, 
#endif
    stUnknown
};

class SectorCacheEngine {
private:
    struct SectorData {
        void* data;
        ULONGLONG lastUse;
        uint32_t sectorSize;
    };

    uint32_t m_maxCacheEntries;
    const uint32_t m_cacheMaxMem;

    std::atomic<bool> m_isLocked = false;

    // Sector disk cache for speed
    std::unordered_map<uint32_t, SectorData*> m_cache;

    SectorData* getAndReleaseOldestSector();

protected:
    // Write data to the cache
    void writeCache(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data);
    // Read data from the cache
    bool readCache(const uint32_t sectorNumber, const uint32_t sectorSize, void* data);


    // Override.  
    virtual bool internalReadData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) = 0;
    virtual bool internalWriteData(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data) = 0;

public:
    // Create cache engine, setting maxCacheMem to zero disables the cache
    SectorCacheEngine(const uint32_t maxCacheMem);
    virtual ~SectorCacheEngine();

    // Reset the cache
    virtual void resetCache();

    // Special lock flag that locks out Dokan while we're doing low-level stuff
    bool isAccessLocked() { return m_isLocked; };
    void setLocked(bool locked) { m_isLocked = locked; };

    bool readData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data);
    bool writeData(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data);

    virtual bool isDiskPresent() = 0;
    virtual bool isDiskWriteProtected() = 0;

    // Set the active file io
    virtual void setActiveFileIO(PDOKAN_FILE_INFO dokanfileinfo) { UNREFERENCED_PARAMETER(dokanfileinfo); };

    // Total number of tracks avalable
    virtual uint32_t totalNumTracks() = 0;

    // Flush changes to disk
    virtual bool flushWriteCache() { return true; };

    // Force writing only, so no read-by back first - useful for formatting disks
    virtual void setWritingOnlyMode(bool only) {  };

    // Fetch the size of the disk file
    virtual uint32_t getDiskDataSize() = 0;

    // Returns the name of the driver providing access
    virtual std::wstring getDriverName() = 0;

    // Fetch the sector size in bytes
    virtual uint32_t sectorSize() { return 512; };

    // Return TRUE if this is actually a physical "REAL" drive
    virtual bool isPhysicalDisk() { return false; };

    // Return TRUE if yu can export this to an image file
    virtual bool allowCopyToFile() { return false; };

    // Return the current number of sectors per track
    virtual uint32_t numSectorsPerTrack() = 0;

    // Get the type of file that is loaded
    virtual SectorType getSystemType() = 0;

    // Return an ID to identify this with
    virtual uint32_t id() { return 0xFFFF; };

    // Fetch the serial number of the disk
    virtual uint32_t serialNumber() = 0;

    // Is this working and available
    virtual bool available() = 0;
};
