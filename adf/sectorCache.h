#pragma once


#include <Windows.h>
#include <unordered_map>
#include <dokan/dokan.h>
#include <atomic>

class SectorCacheEngine {
private:
    struct SectorData {
        void* data;
        ULONGLONG lastUse;
    };

    uint32_t m_maxCacheEntries;
    const uint32_t m_cacheMaxMem;

    std::atomic<bool> m_isLocked = false;

    // Sector disk cache for speed
    std::unordered_map<uint32_t, SectorData*> m_cache;

    SectorData* getAndReleaseOldestSector();
    // Write data to the cache
    virtual void writeCache(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data);
    // Read data from the cache
    virtual bool readCache(const uint32_t sectorNumber, const uint32_t sectorSize, void* data);

protected:


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

    virtual void setActiveFileIO(PDOKAN_FILE_INFO dokanfileinfo) { UNREFERENCED_PARAMETER(dokanfileinfo); };

    // Flush changes to disk
    virtual bool flushWriteCache() { return true; };

    // Force writing only, so no read-by back first
    virtual void setWritingOnlyMode(bool only) {  };

    // Fetch the size of the disk file
    virtual uint32_t getDiskDataSize() = 0;

    // Return TRUE if this is actually a physical "REAL" drive
    virtual bool isPhysicalDisk() { return false; };

    // Return an ID to identify this with
    virtual uint32_t id() { return 0xFFFF; };

    virtual bool available() = 0;
};
