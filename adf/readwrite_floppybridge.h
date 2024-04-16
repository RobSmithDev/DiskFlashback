#pragma once

// Handles reading and writing from real disks, with *hopefully* reliable detection of the type of disk inserted
#include <dokan/dokan.h>
#include <map>
#include "sectorCache.h"
#include "floppybridge_lib.h"
#include "sectorCommon.h"
#include <mutex>

#define MAX_TRACKS                          168
#define MOTOR_TIMEOUT_TIME                  2500    // Timeout to wait for the motor to spin up
#define TRACK_READ_TIMEOUT                  1000    // Should be enough to read it 5 times!
#define MAX_RETRIES                         10      // Attempts to re-read a sector to get a better one
#define MOTOR_IDLE_TIMEOUT                  2000    // How long after access to switch off the motor and flush changes to disk
#define DISK_WRITE_TIMEOUT                  1000    // Allow 1.5 second to write and read-back the data
#define FORCE_FLUSH_AT_TRACKS               10      // How many tracks to have pending write before its forced (5 cylinders, both sides)
#define DOKAN_EXTRATIME                     10000   // How much extra time to add to the timeout for dokan file operations

class SectorRW_FloppyBridge : public SectorCacheEngine {
private:
    SectorType m_diskType           = SectorType::stUnknown;
    FloppyBridgeAPI* m_bridge       = nullptr;
    ULONGLONG m_motorTurnOnTime     = 0;
    void* m_mfmBuffer               = nullptr;
    bool m_ignoreErrors             = false;   // should get reset when motor goes off
    HANDLE m_timerQueue;
    HANDLE m_timer                  = 0;
    bool m_blockWriting             = false;  // used if errors occur
    bool m_diskInDrive              = false;  // Monitor for disk change
    PDOKAN_FILE_INFO m_dokanfileinfo = nullptr; // active file i/o
    std::mutex m_motorTimerProtect;
    bool m_writeOnly                = false;
    bool m_lockedOut                = false;
    std::function<void(bool diskInserted, SectorType diskFormat)> m_diskChangeCallback;

    uint32_t m_sectorsPerTrack[2] = { 0,0 };
    uint32_t m_bytesPerSector[2] = { 512, 512 };
    uint32_t m_totalCylinders[2] = { 0, 0 };
    uint32_t m_serialNumber[2] = { 0x554E4B4E, 0 };
    uint32_t m_numHeads[2] = { 2, 2 };

    // Tracks that need committing to disk
    // NOTE: Using MAP not UNORDERED_MAP. This *should* make the disk head stepping fairly sequential and faster
    std::map<uint32_t, uint32_t> m_tracksToFlush; // mapping of track -> number of hits

    // Cache for previous tracks read
    DecodedTrack m_trackCache[2][MAX_TRACKS];

    // Flush any writing thats still pending
    bool flushPendingWrites();

    // Checks for pending writes, if theres too many then flush them
    void checkFlushPendingWrites();

    // Actually read the track
    bool doTrackReading(const uint32_t fileSystem, const uint32_t track, bool retryMode);

    // Removes anything that failed from the cache so it has to be re-read from the disk
    void removeFailedWritesFromCache();

    // Show disk removed warning - returns TRUE if disk was re-inserted
    bool diskRemovedWarning();

    // Reads some data to see what kind of disk it is
    void identifyFileSystem();

    // Read all sector data regarding of the mode
    bool readDataAllFS(const uint32_t fileSystem, const uint32_t sectorNumber, const uint32_t sectorSize, void* data);

protected:
    virtual bool internalReadData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) override;
    virtual bool internalWriteData(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data) override;
    virtual bool internalHybridReadData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) override;

    // Signal the motor is in use
    void motorInUse(bool upperSide);
    // Waits for the drive to be ready, and if it times out, returns false
    bool waitForMotor(bool upperSide);

public:
    SectorRW_FloppyBridge(const std::string& profile, std::function<void(bool diskInserted, SectorType diskFormat)> diskChangeCallback);
    ~SectorRW_FloppyBridge();

    // Fetch the size of the disk file
    virtual uint32_t getDiskDataSize() override;
    virtual bool available() override;

    // Return TRUE if theres a disk in the drive
    virtual bool isDiskPresent() override;

    // Return TRUE if the disk is write protected
    virtual bool isDiskWriteProtected() override;

    // Used to set what file is currently being accessed - to help dokan know we're busy
    virtual void setActiveFileIO(PDOKAN_FILE_INFO dokanfileinfo) override;

    // Return TRUE if this is actually a physical "REAL" drive
    virtual bool isPhysicalDisk() override { return true; };

    // Total number of tracks avalable - 0 is not specified
    virtual uint32_t totalNumTracks() override { return m_totalCylinders[0] * m_numHeads[0]; };

    // Flush changes to disk
    virtual bool flushWriteCache() override;

    // Reset the cache
    virtual void resetCache() override;

    // Force writing only, so no read-by back first
    virtual void setWritingOnlyMode(bool only) override { m_writeOnly = only; };

    // Change the denity more of the bridge
    bool setForceDensityMode(FloppyBridge::BridgeDensityMode mode);

    // Return an ID to identify this with
    virtual uint32_t id() override;

    // Restore and release for remote usage
    void releaseDrive();
    bool restoreDrive();

    // Return the number of heads/sides
    virtual uint32_t getNumHeads() override { return m_numHeads[0]; };

    // Returns TRUE if the inserted disk is HD
    bool isHD();

    // Pre-populate with blank sectors
    void createBlankSectors();

    // trigger new disk detection
    void triggerNewDiskMount();

    // Override sector infomration - this also wipes the cache and resets everything
    void overwriteSectorSettings(const SectorType systemType, const uint32_t totalCylinders, const uint32_t totalHeads, const uint32_t sectorsPerTrack, const uint32_t sectorSize);

    // Return TRUE if you can export this to disk image
    virtual bool allowCopyToFile() override;

    // Get the type of file that is loaded
    virtual SectorType getSystemType() override { return m_diskType; };

    // Return the current number of sectors per track
    virtual uint32_t numSectorsPerTrack() override { return m_sectorsPerTrack[0]; }

    // Fetch the sector size in bytes
    virtual uint32_t sectorSize() override { return m_bytesPerSector[0]; };

    // Fetch the serial number of the disk
    virtual uint32_t serialNumber() override { return m_serialNumber[0]; };

    // Returns the name of the driver providing access
    virtual std::wstring getDriverName() override;

    // Monitoring keeping the motor spinning or not when not in use
    void motorMonitor();
};
