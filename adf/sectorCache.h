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


#include <dokan/dokan.h>
#include <unordered_map>
#include <atomic>


// Possible types of sector / file
enum class SectorType  {stAmiga, stIBM, stAtari, stHybrid, stUnknown };

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
    virtual bool internalHybridReadData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) { return internalReadData(sectorNumber, sectorSize, data); };
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
    bool hybridReadData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) { return internalHybridReadData(sectorNumber, sectorSize, data); };

    virtual bool isDiskPresent() = 0;
    virtual bool isDiskWriteProtected() = 0;

    // Set the active file io
    virtual void setActiveFileIO(PDOKAN_FILE_INFO dokanfileinfo) { UNREFERENCED_PARAMETER(dokanfileinfo); };

    // Total number of tracks avalable
    virtual uint32_t totalNumTracks() = 0;
    virtual uint32_t hybridTotalNumTracks() { return totalNumTracks(); };

    // Flush changes to disk
    virtual bool flushWriteCache() { return true; };

    // Force writing only, so no read-by back first - useful for formatting disks
    virtual void setWritingOnlyMode(bool only) {  };

    // Fetch the size of the disk file
    virtual uint64_t getDiskDataSize() = 0;
    virtual uint64_t hybridGetDiskDataSize() { return getDiskDataSize(); };

    // Return the number of heads/sides
    virtual uint32_t getNumHeads() = 0;
    virtual uint32_t hybridGetNumHeads() { return getNumHeads(); };

    // Returns the name of the driver providing access
    virtual std::wstring getDriverName() = 0;

    // Fetch the sector size in bytes
    virtual uint32_t sectorSize() { return 512; };
    virtual uint32_t hybridSectorSize() { return sectorSize(); };

    // Return TRUE if this is actually a physical "REAL" drive
    virtual bool isPhysicalDisk() { return false; };

    // Return TRUE if yu can export this to an image file
    virtual bool allowCopyToFile() { return false; };

    // Return the current number of sectors per track
    virtual uint32_t numSectorsPerTrack() = 0;
    virtual uint32_t hybridNumSectorsPerTrack() { return numSectorsPerTrack(); };

    // Get the type of file that is loaded
    virtual SectorType getSystemType() = 0;

    // Return an ID to identify this with
    virtual uint32_t id() { return 0xFFFF; };

    // Fetch the serial number of the disk
    virtual uint32_t serialNumber() = 0;

    // Is this working and available
    virtual bool available() = 0;

    // Raid shutdown to release resource
    virtual void quickClose() = 0;
};
