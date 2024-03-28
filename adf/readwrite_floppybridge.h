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
#define DISK_WRITE_TIMEOUT                  5000    // Allow 5 seconds to write the data
#define FORCE_FLUSH_AT_TRACKS               10      // How many tracks to have pending write before its forced (5 cylinders, both sides)

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
    bool m_diskChanged              = false;  // Monitor for disk change

    std::mutex m_motorTimerProtect;

    // Tracks that need committing to disk
    // NOTE: Using MAP not UNORDERED_MAP. This *should* make the disk head stepping fairly sequential and faster
    std::map<uint32_t, uint32_t> m_tracksToFlush; // mapping of track -> number of hits

    // Cache for previous tracks read
    DecodedTrack m_trackCache[MAX_TRACKS];

    // Flush any writing thats still pending
    void flushPendingWrites();

    // Checks for pending writes, if theres too many then flush them
    void checkFlushPendingWrites();

    // Actually read the track
    bool doTrackReading(const uint32_t track, const bool upperSurface);

    // Removes anything that failed from the cache so it has to be re-read from the disk
    void removeFailedWritesFromCache();


protected:
    virtual bool internalReadData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) override;
    virtual bool internalWriteData(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data) override;

    // Signal the motor is in use
    void motorInUse(bool upperSide);
    // Waits for the drive to be ready, and if it times out, returns false
    bool waitForMotor(bool upperSide);

public:
    SectorRW_FloppyBridge(const std::string& profile);
    ~SectorRW_FloppyBridge();

    // Fetch the size of the disk file
    virtual uint32_t getDiskDataSize() override;
    virtual bool available() override;

    virtual bool isDiskPresent() override;
    virtual bool isDiskWriteProtected() override;

    void motorMonitor();
};
