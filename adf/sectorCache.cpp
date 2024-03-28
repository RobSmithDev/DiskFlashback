
#include "sectorCache.h"


// Get oldest sector we've cached and remove it, but don't free it!
SectorCacheEngine::SectorData* SectorCacheEngine::getAndReleaseOldestSector() {
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
void SectorCacheEngine::writeCache(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data) {
    if (!m_cacheMaxMem) return;

    auto f = m_cache.find(sectorNumber);
    SectorData* secData;

    if (f == m_cache.end()) {
        if (m_maxCacheEntries == 0) m_maxCacheEntries = m_cacheMaxMem / sectorSize;

        // If cache size is ZERO it means cache everything
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
bool SectorCacheEngine::readCache(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) {
    if (!m_cacheMaxMem) return false;

    auto f = m_cache.find(sectorNumber);
    if (f == m_cache.end()) return false;
    if (m_maxCacheEntries == 0) m_maxCacheEntries = m_cacheMaxMem / sectorSize;

    memcpy_s(data, sectorSize, f->second->data, sectorSize);
    f->second->lastUse = GetTickCount64();
    return true;
}

SectorCacheEngine::SectorCacheEngine(const uint32_t maxCacheMem) : m_maxCacheEntries(0), m_cacheMaxMem(maxCacheMem) {

}

SectorCacheEngine::~SectorCacheEngine() {
    for (auto it : m_cache) {
        free(it.second->data);
        delete it.second;
    }
}

bool SectorCacheEngine::readData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) {
    if (readCache(sectorNumber, sectorSize, data)) return true;

    if (internalReadData(sectorNumber, sectorSize, data)) {
        writeCache(sectorNumber, sectorSize, data);
        return true;
    }

    return false;
}

bool SectorCacheEngine::writeData(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data) {
    if (internalWriteData(sectorNumber, sectorSize, data)) {
        writeCache(sectorNumber, sectorSize, data);
        return true;
    } 
    return false;
}

