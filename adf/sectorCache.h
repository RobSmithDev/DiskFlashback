#pragma once

// Handles reading and writing to a file, with sector cache for improved speed
#include <Windows.h>
#include <unordered_map>


class NativeDeviceCached {
private:
    struct SectorData {
        void* data;
        ULONGLONG lastUse;
    };

    HANDLE m_file;
    uint32_t m_maxCacheEntries;
    const uint32_t m_cacheMaxMem;

    // Sector disk cache for speed
    std::unordered_map<uint32_t, SectorData*> m_cache;

    SectorData* getAndReleaseOldestSector();
    // Write data to the cache
    void writeCache(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data);
    // Read data from the cache
    bool readCache(const uint32_t sectorNumber, const uint32_t sectorSize, void* data);

public:
    NativeDeviceCached(const uint32_t maxCacheMem, HANDLE fle);
    ~NativeDeviceCached();

    bool readData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data);
    bool writeData(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data);
};
