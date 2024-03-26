
#include "sectorCache.h"


// Get oldest sector we've cached and remove it, but don't free it!
NativeDeviceCached::SectorData* NativeDeviceCached::getAndReleaseOldestSector() {
    SectorData* result = nullptr;
    uint32_t sectorNumber = 0;

    for (auto cache : m_cache)
        if ((!result) || (cache.second->lastUse < result->lastUse)) {
            result = cache.second;
            sectorNumber = cache.first;
        }
    if (!result) return nullptr;

    m_cache.erase(sectorNumber);
    return result;
}


// Write data to the cache
void NativeDeviceCached::writeCache(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data) {
    auto f = m_cache.find(sectorNumber);
    SectorData* secData;

    if (f == m_cache.end()) {
        if (m_maxCacheEntries == 0) m_maxCacheEntries = m_cacheMaxMem / sectorSize;

        if (m_cache.size() > m_maxCacheEntries) {
            secData = getAndReleaseOldestSector();
            if (!secData) return;
        }
        else {
            secData = new SectorData();
            if (!secData) return;
            secData->data = malloc(sectorSize);
            if (!secData->data) {
                delete secData;
                return;
            }
        }
        m_cache.insert(std::make_pair(sectorNumber, secData));
    }
    else {
        secData = f->second;
    }

    // Make a copy
    memcpy_s(secData->data, sectorSize, data, sectorSize);
    secData->lastUse = GetTickCount64();
}

// Read data from the cache
bool NativeDeviceCached::readCache(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) {
    auto f = m_cache.find(sectorNumber);
    if (f == m_cache.end()) return false;
    if (m_maxCacheEntries == 0) m_maxCacheEntries = m_cacheMaxMem / sectorSize;

    memcpy_s(data, sectorSize, f->second->data, sectorSize);
    f->second->lastUse = GetTickCount64();
    return true;
}

NativeDeviceCached::NativeDeviceCached(const uint32_t maxCacheMem, HANDLE fle) : m_file(fle), m_maxCacheEntries(0), m_cacheMaxMem(maxCacheMem) {

}

NativeDeviceCached::~NativeDeviceCached() {
    for (auto it : m_cache) {
        free(it.second->data);
        delete it.second;
    }
    CloseHandle(m_file);
}

bool NativeDeviceCached::readData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) {
    if (readCache(sectorNumber, sectorSize, data)) return true;

    if (SetFilePointer(m_file, sectorNumber * sectorSize, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
        return false;

    DWORD read = 0;
    if (!ReadFile(m_file, data, sectorSize, &read, NULL))
        return false;

    if (read != sectorSize) return false;

    writeCache(sectorNumber, sectorSize, data);
    return true;
}

bool NativeDeviceCached::writeData(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data) {
    if (SetFilePointer(m_file, sectorNumber * sectorSize, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
        return false;

    DWORD write = 0;
    if (!WriteFile(m_file, data, sectorSize, &write, NULL))
        return false;

    if (write != sectorSize) return false;

    writeCache(sectorNumber, sectorSize, data);
    return true;
}

