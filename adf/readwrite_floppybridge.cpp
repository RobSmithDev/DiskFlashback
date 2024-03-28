
#include "readwrite_floppybridge.h"
#include <stdio.h>

VOID CALLBACK MotorMonitor(_In_ PVOID   lpParameter, _In_ BOOLEAN TimerOrWaitFired) {
    SectorRW_FloppyBridge* callback = (SectorRW_FloppyBridge*)lpParameter;
    callback->motorMonitor();
}

SectorRW_FloppyBridge::SectorRW_FloppyBridge(const std::string& profile) : SectorCacheEngine(0), m_sectorsPerTrack(0), m_motorTurnOnTime(0), m_timer(0) {
    m_timerQueue = CreateTimerQueue();

    // Use profile eventually, for now we cheat
      // Hard code for now, later will come via profile
    m_bridge = FloppyBridgeAPI::createDriver(0);
    m_bridge->setBridgeMode(FloppyBridge::BridgeMode::bmStalling);
    m_bridge->setComPortAutoDetect(true);

    if (!m_bridge->initialise()) {
        delete m_bridge;
        m_bridge = nullptr;
        return;
    }

    if (!m_bridge->resetDrive(0)) {
        delete m_bridge;
        m_bridge = nullptr;
        return;
    }

    m_mfmBuffer = malloc(MFM_BUFFER_MAX_TRACK_LENGTH);
    if (!m_mfmBuffer) {
        delete m_bridge;
        m_bridge = nullptr;
        return;
    }    

    // Force m_sectorsPerTrack to be populated
    std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);
    motorInUse(false);
    getDiskDataSize();
    CreateTimerQueueTimer(&m_timer, m_timerQueue, MotorMonitor, this, 500, 200, WT_EXECUTEDEFAULT | WT_EXECUTELONGFUNCTION);
}

bool SectorRW_FloppyBridge::isDiskPresent() {
    std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);
    return m_bridge->isDiskInDrive();
}

bool SectorRW_FloppyBridge::isDiskWriteProtected() {
    std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);
    return m_bridge->isWriteProtected();
}

// The motor usage has timed out
void SectorRW_FloppyBridge::motorMonitor() {
    std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);
    if ((m_motorTurnOnTime) && (GetTickCount64() - m_motorTurnOnTime > MOTOR_IDLE_TIMEOUT)) {
        flushPendingWrites();
        m_bridge->setMotorStatus(false, false);
        m_ignoreErrors = false;
        m_blockWriting = false;
        m_motorTurnOnTime = 0;
    }

    if (!m_bridge->isDiskInDrive()) {
        if (!m_diskChanged) {
            m_bridge->gotoCylinder(0, false);
            m_bridge->setMotorStatus(false, false);
            // cache really needs to be cleared!
            if (m_tracksToFlush.size() < 1) {
                for (uint32_t trk = 0; trk < MAX_TRACKS; trk++)
                    m_trackCache[trk].sectors.clear();
            }
            m_diskChanged = true;
        }
    }
}

// Signal the motor is in use.  Returns if its ok
void SectorRW_FloppyBridge::motorInUse(bool upperSide) {
    if (!m_motorTurnOnTime) m_bridge->setMotorStatus(upperSide, true);
    m_motorTurnOnTime = GetTickCount64();
}

// Waits for the drive to be ready, and if it times out, returns false
bool SectorRW_FloppyBridge::waitForMotor(bool upperSide) {
    motorInUse(upperSide);
    while (!m_bridge->isReady()) {
        Sleep(100);
        if (GetTickCount64() - m_motorTurnOnTime > MOTOR_TIMEOUT_TIME) return false;
    }
    return true;
}

bool SectorRW_FloppyBridge::available() {
    return m_bridge && m_bridge->isAvailable();
}

SectorRW_FloppyBridge::~SectorRW_FloppyBridge() {
    // FLUSH
    std::lock_guard<std::mutex> guard(m_motorTimerProtect);
    if (m_timer) {
        // Disable the motor timer
        DeleteTimerQueueTimer(m_timerQueue, m_timer, 0);
        m_timer = 0;
    }
    if (DeleteTimerQueueEx(m_timerQueue, NULL)) m_timerQueue = 0;

    if (m_bridge) {
        m_bridge->shutdown();
        delete m_bridge;
        m_bridge = nullptr;
    }

    if (m_mfmBuffer) free(m_mfmBuffer);
}

uint32_t SectorRW_FloppyBridge::getDiskDataSize() {
    if (!m_bridge) return 0;

    switch (m_bridge->getDriveTypeID()) {
        case FloppyDiskBridge::DriveTypeID::dti35DD: m_sectorsPerTrack = NUM_SECTORS_PER_TRACK_DD; break;
        case FloppyDiskBridge::DriveTypeID::dti35HD: m_sectorsPerTrack = NUM_SECTORS_PER_TRACK_HD; break;
        default: m_sectorsPerTrack = 0;
    }
    return SECTOR_BYTES * m_sectorsPerTrack * 2 * 80;
}

// Do reading
bool SectorRW_FloppyBridge::internalReadData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) {
    if (sectorSize != SECTOR_BYTES) return false;

    const int track = sectorNumber / m_sectorsPerTrack;
    const int trackBlock = sectorNumber % m_sectorsPerTrack;
    const bool upperSurface = track & 1;
    const int cylinder = track / 2;
    if (track >= MAX_TRACKS) return false;

    std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);

    checkFlushPendingWrites();

    // Retry several times
    uint32_t retries = 0;
    for (;;) {
        // First, see if we have a perfect sector already
        auto it = m_trackCache[track].sectors.find(trackBlock);

        if (it != m_trackCache[track].sectors.end()) {
            // No errors? (or are we skipping them?)
            if ((it->second.numErrors == 0) || (m_ignoreErrors)) {
                memcpy_s(data, sectorSize, it->second.data, SECTOR_BYTES);
                return true;
            }
        }

        // Retry monitor
        if (retries > MAX_RETRIES) {
            if (m_ignoreErrors) return false;
            retries = 0;

            switch (MessageBox(GetDesktopWindow(), L"There have been some errors reading data from the disk.\nWhat would you like to do?", L"Disk Errors Detected", MB_SETFOREGROUND | MB_SYSTEMMODAL | MB_ICONQUESTION | MB_ABORTRETRYIGNORE)) {
            case IDRETRY:
            case IDTRYAGAIN: break;
            case IDIGNORE:
                m_ignoreErrors = true;
                break;
            default:
                return false;
            }
        }

        // If this hits, then do a re-seek.  Sometimes it helps
        if (retries == MAX_RETRIES / 2) {
            motorInUse(upperSurface);

            if (cylinder < 40)
                m_bridge->gotoCylinder(79, upperSurface);
            else
                m_bridge->gotoCylinder(0, upperSurface);

            // Wait for the seek, or it will get removed! 
            Sleep(300);
        }

        // If we get here then this sector isn't in the cache (or has errors), so we'll read and update ALL sectors for this cylinder
        motorInUse(upperSurface);
        m_bridge->gotoCylinder(cylinder, upperSurface);

        // Wait for the motor to spin up properly
        if (!waitForMotor(upperSurface)) return false;

        // Actually do the read
        if (!doTrackReading(track, upperSurface)) return false;

        retries++;
    }

    return true;
}

// Internal single attempt to read a track
bool SectorRW_FloppyBridge::doTrackReading(const uint32_t track, const bool upperSurface) {
    // Read some track data, with some delay for a retry
    ULONGLONG start = GetTickCount64();
    uint32_t bitsReceived;
    do {
        motorInUse(upperSurface);
        bitsReceived = m_bridge->getMFMTrack(MFM_BUFFER_MAX_TRACK_LENGTH, m_mfmBuffer);

        if (!bitsReceived) {
            if (GetTickCount64() - start > TRACK_READ_TIMEOUT) return false;
            else Sleep(50);
        }
    } while (!bitsReceived);

    // Extract sectors
    findSectors((const unsigned char*)m_mfmBuffer, bitsReceived, m_sectorsPerTrack == NUM_SECTORS_PER_TRACK_HD, track, m_trackCache[track]);

    // Switch the buffer so next time we get a different one, maybe with less errors
    m_bridge->mfmSwitchBuffer(upperSurface);
    return true;
}

// Do writing
bool SectorRW_FloppyBridge::internalWriteData(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data) {
    if (sectorSize != SECTOR_BYTES) return false;
    if (m_blockWriting) return false;

    const int track = sectorNumber / m_sectorsPerTrack;
    if (track >= MAX_TRACKS) return false;
    const int trackBlock = sectorNumber % m_sectorsPerTrack;
    const bool upperSurface = track & 1;
    const int cylinder = track / 2;

    char b[100];
    sprintf_s(b, "WRITE SECTOR %i\n", sectorNumber);
    OutputDebugStringA(b);

    std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);

    motorInUse(upperSurface);
   
    // Now replace the sector we're overwriting, just in memory at this point
    auto it = m_trackCache[track].sectors.find(trackBlock);
    if (it != m_trackCache[track].sectors.end()) {
        if (memcmp(it->second.data, data, sectorSize) == 0) {
            OutputDebugStringA("SECTOR WRITE SKIPPED\n");
            it->second.numErrors = 0;
            return true;
        }
        else {
            // No errors? (or are we skipping them?)
            memcpy_s(it->second.data, SECTOR_BYTES, data, sectorSize);
            it->second.numErrors = 0;
        }
    }
    else {
        // Add the track
        DecodedSector sector;
        memcpy_s(sector.data, SECTOR_BYTES, data, sectorSize);
        sector.numErrors = 0;
        m_trackCache[track].sectors.insert(std::make_pair(trackBlock, sector));
    }

    auto i = m_tracksToFlush.find(track);
    if (i == m_tracksToFlush.end())
        m_tracksToFlush.insert(std::make_pair(track, 1));
    else i->second++;

    checkFlushPendingWrites();

    return true;
}

// Checks for pending writes, if theres too many then flush them
void SectorRW_FloppyBridge::checkFlushPendingWrites() {
    if (m_tracksToFlush.size() < FORCE_FLUSH_AT_TRACKS) return;
    flushPendingWrites();
}

// Removes anything that failed from the cache so it has to be re-read from the disk
void SectorRW_FloppyBridge::removeFailedWritesFromCache() {
    for (auto& trk : m_tracksToFlush) 
        if (trk.second) 
            m_trackCache[trk.first].sectors.clear();
    m_tracksToFlush.clear();
}

// Flush any writing thats still pending - lock must already be obtained
void SectorRW_FloppyBridge::flushPendingWrites() {
    if (m_blockWriting) return;
    
    for (auto& trk : m_tracksToFlush) {
        const uint32_t track = trk.first;
        const bool upperSurface = track & 1;
        const uint32_t cylinder = track / 2;

        char b[100];
        sprintf_s(b, "Writing to track %i\n", track);
        OutputDebugStringA(b);

        // Motor shouldn't stop here
        motorInUse(upperSurface);
        m_bridge->gotoCylinder(cylinder, upperSurface);
        if (!waitForMotor(upperSurface)) {
            m_tracksToFlush.clear();
            return;
        }

        // Assemble and commit an entire track.  First see if any data is missing
        bool fillData = m_trackCache[track].sectors.size() < m_sectorsPerTrack;
        if (!fillData)
            for (const auto& it : m_trackCache[track].sectors)
                if (it.second.numErrors) {
                    fillData = true;
                    break;
                }

        // Theres some missing data. We we'll request the track again and fill in the gaps
        if (fillData) {
            // 1. Take a copy
            const std::unordered_map<int, DecodedSector> backup = m_trackCache[track].sectors;
            // 2. *try* to read the track (but dont care if it fails)
            doTrackReading(track, upperSurface);
            // 3. Replace any tracks now read with any we have in our backup that have errors = 0
            for (const auto& sec : backup) {
                if (sec.second.numErrors == 0) {
                    auto it = m_trackCache[track].sectors.find(sec.first);
                    if (it != m_trackCache[track].sectors.end())
                        it->second = sec.second;
                }
            }
        }

        // We will now have a complete track worth of sectors so we can now finally commit this to disk (hopefully) plus we will verify it
        uint32_t numBytes = encodeSectorsIntoMFM(m_sectorsPerTrack == NUM_SECTORS_PER_TRACK_HD, m_trackCache[track], track, MFM_BUFFER_MAX_TRACK_LENGTH, m_mfmBuffer);
        if (numBytes < 0) {
            // this *should* never happen
            removeFailedWritesFromCache();
            OutputDebugStringA("encodeSectorsIntoMFM Error\n");
            return;
        }

        // Write retries
        uint32_t retries = 0;
        for (;;) {
            // Handle a re-seek - might clean the head
            if (retries == MAX_RETRIES / 2) {
                motorInUse(upperSurface);

                if (cylinder < 40)
                    m_bridge->gotoCylinder(79, upperSurface);
                else
                    m_bridge->gotoCylinder(0, upperSurface);

                // Wait for the seek, or it will get removed! 
                Sleep(300);
                m_bridge->gotoCylinder(cylinder, upperSurface);
                OutputDebugStringA("WRITE Re-Seek\n");
            }

            // Commit to disk
            motorInUse(upperSurface);
            OutputDebugStringA("...writing\n");
            if (m_bridge->writeMFMTrackToBuffer(upperSurface, cylinder, false, numBytes, m_mfmBuffer)) {
                // Now wait until it completes - approx 400-500ms as this will also read it back to verify it
                ULONGLONG start = GetTickCount64();
                while (!m_bridge->isWriteComplete()) {
                    if (GetTickCount64() - start > DISK_WRITE_TIMEOUT) {
                        switch (MessageBox(GetDesktopWindow(), L"Disk writing is taking too long.\nWhat would you like to do?", L"Disk Writing Timeout", MB_ABORTRETRYIGNORE | MB_ICONQUESTION)) {
                        case IDABORT:
                            m_blockWriting = true;
                            removeFailedWritesFromCache();
                            return;
                        case IDRETRY:
                            start = GetTickCount64();
                            break;
                        default:
                            removeFailedWritesFromCache();
                            return;
                        }
                    }
                }
                OutputDebugStringA("...verifying\n");

                // Writing succeeded. Now to do a verify!
                const std::unordered_map<int, DecodedSector> backup = m_trackCache[track].sectors;
                for (;;) {
                    if (!doTrackReading(track, upperSurface)) {
                        if (MessageBox(GetDesktopWindow(), L"Disk verifying is taking too long.\nWhat would you like to do?", L"Disk Verifying Timeout", MB_ABORTRETRYIGNORE | MB_ICONQUESTION) != IDRETRY) {
                            removeFailedWritesFromCache();
                            return;
                        }
                        // Wait and try again
                        Sleep(100);
                    }
                    else break;
                }

                // Check what was read back matches what we wrote
                bool errors = false;
                for (const auto& sec : backup) {
                    auto search = m_trackCache[track].sectors.find(sec.first);
                    // Sector no longer exists.  ERROR!
                    if (search == m_trackCache[track].sectors.end()) {
                        errors = true;
                        break;
                    }
                    else {
                        // Did it reac back with errors!?
                        if (search->second.numErrors) {
                            errors = true;
                            break;
                        }
                        else {
                            // Finally, compare the data and see if its identical
                            if (memcmp(search->second.data, sec.second.data, sizeof(RawDecodedSector)) != 0) {
                                // BAD read back even though there were no errors
                                errors = true;
                                break;
                            }
                        }
                    }
                }

                if (errors) {
                    OutputDebugStringA("...VERIFY FAILED\n");
                }
                else
                    break;
            }
            else {
                // this shouldnt ever happen
                removeFailedWritesFromCache();
                OutputDebugStringA("...shouldnt happen\n");
                return;
            }

            retries++;
        }

        // Mark that its done!
        trk.second = 0;
    }

    removeFailedWritesFromCache();
}