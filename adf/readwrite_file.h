#pragma once

// Handles reading and writing to a file, with sector cache for improved speed
#include <Windows.h>
#include <unordered_map>
#include "sectorCache.h"

class SectorRW_File : public SectorCacheEngine {
private:
    HANDLE m_file;
    uint32_t m_sectorsPerTrack;

protected:
    virtual bool internalReadData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) override;
    virtual bool internalWriteData(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data) override;

public:
    SectorRW_File(const uint32_t maxCacheMem, HANDLE fle);
    ~SectorRW_File();

    virtual bool isDiskPresent() override;
    virtual bool isDiskWriteProtected() override;

    // Returns the name of the driver providing access
    virtual std::wstring getDriverName() override { return L"ADF File"; };

    // Return the current number of sectors per track
    virtual uint32_t numSectorsPerTrack() override { return m_sectorsPerTrack; };

    // Fetch the size of the disk file
    virtual uint32_t getDiskDataSize() override;
    virtual bool available() override;
};
