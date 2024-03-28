#pragma once


#include <Windows.h>
#include <unordered_map>


class SectorCacheEngine {
private:
    struct SectorData {
        void* data;
        ULONGLONG lastUse;
    };

    uint32_t m_maxCacheEntries;
    const uint32_t m_cacheMaxMem;

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

    bool readData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data);
    bool writeData(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data);

    virtual bool isDiskPresent() = 0;
    virtual bool isDiskWriteProtected() = 0;

    // Fetch the size of the disk file
    virtual uint32_t getDiskDataSize() = 0;

    virtual bool available() = 0;
};
