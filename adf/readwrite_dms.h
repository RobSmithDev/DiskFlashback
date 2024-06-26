/* DiskFlashback, Copyright (C) 2021-2024 Robert Smith (@RobSmithDev)
 * https://robsmithdev.co.uk/diskflashback
 *
 * This file is multi-licensed under the terms of the Mozilla Public
 * License Version 2.0 as published by Mozilla Corporation and the
 * GNU General Public License, version 2 or later, as published by the
 * Free Software Foundation.
 *
 * MPL2: https://www.mozilla.org/en-US/MPL/2.0/
 * GPL2: https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html
 *
 * This file is maintained at https://github.com/RobSmithDev/DiskFlashback
 */

#pragma once

// Handles reading and writing to a file, with sector cache for improved speed
#include <dokan/dokan.h>
#include <unordered_map>
#include "sectorCache.h"

class SectorRW_DMS : public SectorCacheEngine {
private:
    uint64_t m_diskSize = 0;
    uint16_t m_diskType = 0; // type of archive
    uint16_t m_geninfo = 0; // flags
    uint16_t m_sectorsPerTrack = 0;   
    uint32_t m_totalTracks = 0;
    bool m_validFile = false;
    
    bool parseDMSHeader(HANDLE fle, uint8_t* b1, uint8_t* b2, uint8_t* text);
    bool unpackCylinders(HANDLE fle, uint8_t* b1, uint8_t* b2, uint8_t* text);
    bool decompressTrack(uint8_t* b1, uint8_t* b2, uint8_t* text, uint16_t pklen2, uint16_t unpklen, uint16_t cmode, uint16_t flags);
    
protected:
    virtual bool internalReadData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) override;
    virtual bool internalWriteData(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data) override { return false; };

public:
    SectorRW_DMS(HANDLE fle);
    ~SectorRW_DMS();

    virtual bool isDiskPresent() override;
    virtual bool isDiskWriteProtected() override { return true; };

    // Returns the name of the driver providing access
    virtual std::wstring getDriverName() override { return L"DMS File"; };

    // Return TRUE if yu can export this to a file
    virtual bool allowCopyToFile() override { return true; };

    // Return the current number of sectors per track 
    virtual uint32_t numSectorsPerTrack() override { return m_sectorsPerTrack; };

    // Total number of tracks avalable
    virtual uint32_t totalNumTracks() override { return m_totalTracks; };

    // Return the number of heads/sides
    virtual uint32_t getNumHeads() override { return 2; };

    // Fetch the sector size in bytes
    virtual uint32_t sectorSize() override;

    // Get the type of file that is loaded
    virtual SectorType getSystemType() override { return SectorType::stAmiga; };

    // Fetch the serial number of the disk
    virtual uint32_t serialNumber() override { return 0x444D5330; };

    // Fetch the size of the disk file
    virtual uint64_t getDiskDataSize() override { return m_diskSize; };
    virtual bool available() override;

    // Raid shutdown to release resource
    virtual void quickClose() override {};

};
