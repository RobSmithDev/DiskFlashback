#pragma once

// Handles reading and writing to a file, with sector cache for improved speed
#include <Windows.h>
#include <map>
#include "sectorCache.h"
#include "floppybridge_lib.h"
#include "amiga_sectors.h"
#include <mutex>


#define MFM_BUFFER_MAX_TRACK_LENGTH			(0x3A00 * 2)    // 29,696 bytes
#define MAX_TRACKS                          164
#define MOTOR_TIMEOUT_TIME                  2500    // Timeout to wait for the motor to spin up
#define TRACK_READ_TIMEOUT                  1000    // Should be enough to read it 5 times!
#define MAX_RETRIES                         6       // Attempts to re-read a sector to get a better one
#define MOTOR_IDLE_TIMEOUT                  2000    // How long after access to switch off the motor and flush changes to disk
#define DISK_WRITE_TIMEOUT                  1000    // Allow 1.5 second to write and read-back the data
#define FORCE_FLUSH_AT_TRACKS               10      // How many tracks to have pending write before its forced (5 cylinders, both sides)
#define DOKAN_EXTRATIME                     10000   // How much extra time to add to the timeout for dokan file operations

class SectorRW_FloppyBridge : public SectorCacheEngine {
private:
    FloppyBridgeAPI* m_bridge       = nullptr;
    int m_sectorsPerTrack;
    ULONGLONG m_motorTurnOnTime     = 0;
    void* m_mfmBuffer               = nullptr;
    bool m_ignoreErrors             = false;   // should get reset when motor goes off
    HANDLE m_timerQueue;
    HANDLE m_timer                  = 0;
    bool m_blockWriting             = false;  // used if errors occur
    bool m_diskInDrive              = false;  // Monitor for disk change
    PDOKAN_FILE_INFO m_dokanfileinfo = nullptr; // active file i/o
    std::mutex m_motorTimerProtect;
    bool m_writeOnly = false;
    bool m_lockedOut = false;
    std::function<void(bool diskInserted)> m_diskChangeCallback;

    // Tracks that need committing to disk
    // NOTE: Using MAP not UNORDERED_MAP. This *should* make the disk head stepping fairly sequential and faster
    std::map<uint32_t, uint32_t> m_tracksToFlush; // mapping of track -> number of hits

    // Cache for previous tracks read
    DecodedTrack m_trackCache[MAX_TRACKS];

    // Flush any writing thats still pending
    bool flushPendingWrites();

    // Checks for pending writes, if theres too many then flush them
    void checkFlushPendingWrites();

    // Actually read the track
    bool doTrackReading(const uint32_t track, const bool upperSurface);

    // Removes anything that failed from the cache so it has to be re-read from the disk
    void removeFailedWritesFromCache();

    // Show disk removed warning - returns TRUE if disk was re-inserted
    bool diskRemovedWarning();
protected:
    virtual bool internalReadData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) override;
    virtual bool internalWriteData(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data) override;

    // Signal the motor is in use
    void motorInUse(bool upperSide);
    // Waits for the drive to be ready, and if it times out, returns false
    bool waitForMotor(bool upperSide);

public:
    SectorRW_FloppyBridge(const std::string& profile, std::function<void(bool diskInserted)> diskChangeCallback);
    ~SectorRW_FloppyBridge();

    // Fetch the size of the disk file
    virtual uint32_t getDiskDataSize() override;
    virtual bool available() override;

    virtual bool isDiskPresent() override;
    virtual bool isDiskWriteProtected() override;

    virtual void setActiveFileIO(PDOKAN_FILE_INFO dokanfileinfo) override;

    // Return TRUE if this is actually a physical "REAL" drive
    virtual bool isPhysicalDisk() override { return true; };

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
    virtual void releaseDrive() override;
    virtual bool restoreDrive() override;

    // Return the current number of sectors per track
    virtual uint32_t numSectorsPerTrack() { return m_sectorsPerTrack; }

    // Returns the name of the driver providing access
    virtual std::wstring getDriverName() override;

    void motorMonitor();
};
