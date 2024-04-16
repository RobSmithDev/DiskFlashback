#pragma once

// Handles reading and writing to a file.  The ADF, IMG and ST formats are all basically the same thing!
#include <dokan/dokan.h>
#include <unordered_map>
#include "sectorCache.h"


class SectorRW_File : public SectorCacheEngine {
private:
    HANDLE m_file;
    uint32_t m_sectorsPerTrack;
    SectorType m_fileType;
    uint32_t m_serialNumber;
    uint32_t m_bytesPerSector;
    uint32_t m_totalTracks;
    uint32_t m_numHeads;
protected:
    virtual bool internalReadData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) override;
    virtual bool internalWriteData(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data) override;

public:
    SectorRW_File(const std::wstring& filename, HANDLE fle);
    ~SectorRW_File();

    virtual bool isDiskPresent() override;
    virtual bool isDiskWriteProtected() override;

    // Returns the name of the driver providing access
    virtual std::wstring getDriverName() override { return L"Disk Image"; };

    // Return the current number of sectors per track
    virtual uint32_t numSectorsPerTrack() override { return m_sectorsPerTrack; };

    // Fetch the sector size in bytes
    virtual uint32_t sectorSize() override { return m_bytesPerSector; };

    // Total number of tracks avalable
    virtual uint32_t totalNumTracks() override { return m_totalTracks; };

    // Return the number of heads/sides
    virtual uint32_t getNumHeads() override { return m_numHeads; };

    // Fetch the size of the disk file
    virtual uint32_t getDiskDataSize() override;
    virtual bool available() override;

    // Get the type of file that is loaded
    virtual SectorType getSystemType() override { return m_fileType; };

    // Attempts to guess the number of sectors per track based on the supplied image size
    static uint32_t GuessSectorsPerTrackFromImageSize(const uint32_t imageSize, const uint32_t sectorSize = 512);

    // Fetch the serial number of the disk
    virtual uint32_t serialNumber() override { return m_serialNumber; };

};
