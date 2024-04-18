
#include "mfminterface.h"
#include "amiga_sectors.h"
#include "ibm_sectors.h"
#include <stdio.h>

// Jump back in!
VOID CALLBACK MotorMonitor(_In_ PVOID   lpParameter, _In_ BOOLEAN TimerOrWaitFired) {
    SectorCacheMFM* callback = (SectorCacheMFM*)lpParameter;
    callback->motorMonitor();
}

void SectorCacheMFM::releaseDrive() {
    if (m_diskInDrive) {
        m_diskInDrive = false;
        if (m_diskChangeCallback) m_diskChangeCallback(false, SectorType::stUnknown);
        m_diskType = SectorType::stUnknown;
    }
}

// init the drive
bool SectorCacheMFM::initDrive() {
    std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);
    m_diskType = SectorType::stUnknown;
    m_motorTurnOnTime = 0;
    m_diskInDrive = false;

    if (!restoreDrive()) 
        return false;

    return true;
}

// Reset the cache
void SectorCacheMFM::resetCache() {
    SectorCacheEngine::resetCache();

    std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);
    m_tracksToFlush.clear();
    for (uint32_t systems = 0; systems < 2; systems++)
        for (DecodedTrack& trk : m_trackCache[systems]) trk.sectors.clear();
}

// Flush changes to disk
bool SectorCacheMFM::flushWriteCache() {
    std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);
    return flushPendingWrites();
}

// Constructor
SectorCacheMFM::SectorCacheMFM(std::function<void(bool diskInserted, SectorType diskFormat)> diskChangeCallback) :
    SectorCacheEngine(0), m_motorTurnOnTime(0), m_timer(0), m_diskChangeCallback(diskChangeCallback) {
    m_timerQueue = CreateTimerQueue();

    m_mfmBuffer = malloc(MAX_TRACK_SIZE);
    if (!m_mfmBuffer) return;
}

void SectorCacheMFM::setReady() {
    initDrive();

    if (isDiskInDrive()) identifyFileSystem();

    // Force m_sectorsPerTrack to be populated
    {
        std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);
        CreateTimerQueueTimer(&m_timer, m_timerQueue, MotorMonitor, this, 1000, 200, WT_EXECUTEDEFAULT | WT_EXECUTELONGFUNCTION);
    }

}

// Reads some data to see what kind of disk it is
void SectorCacheMFM::identifyFileSystem() {
    for (uint32_t i = 0; i < 2; i++) {
        m_totalCylinders[i] = 0;
        m_numHeads[i] = 2;
    }
    std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);
    m_diskType = SectorType::stUnknown;
    cylinderSeek(0, false);
    motorInUse(true);
    if (waitForMotor(false)) {
        for (uint32_t retries = 0; retries < 5; retries++) {
            if (doTrackReading(0, 0, false))
                if (m_diskType != SectorType::stUnknown)
                    break;
        }
    }
}

// Used to set what file is currently being accessed - to help dokan know we're busy
void SectorCacheMFM::setActiveFileIO(PDOKAN_FILE_INFO dokanfileinfo) {
    std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);
    m_dokanfileinfo = dokanfileinfo;
};

// Return TRUE if theres a disk in the drive
bool SectorCacheMFM::isDiskPresent() {
    std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);
    return m_diskInDrive;
}

// Return TRUE if the disk is write protected
bool SectorCacheMFM::isDiskWriteProtected() {
    std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);
    return isDriveWriteProtected();
}

// Show disk removed warning - returns TRUE if disk was re-inserted
bool SectorCacheMFM::diskRemovedWarning() {
    if (isDiskInDrive()) return true;

    ULONGLONG t = GetTickCount64() - 1000;
    while (!isDiskInDrive()) {
        if (GetTickCount64() - t > 1000) {
            if (MessageBox(GetDesktopWindow(), L"WARNING: Not all data has been written to disk!\nYou MUST re-insert the disk into drive and press retry.", L"FLOPPY DISK REMOVED", MB_ICONSTOP | MB_RETRYCANCEL) != IDRETRY)
                return false;
            t = GetTickCount64();
        }
        else Sleep(100);
    }
    return true;
}

// Return TRUE if you can export this to disk image
bool SectorCacheMFM::allowCopyToFile() {
    return (m_diskType == SectorType::stAmiga) || (m_diskType == SectorType::stIBM);
}

// Override sector infomration
void SectorCacheMFM::overwriteSectorSettings(const SectorType systemType, const uint32_t totalCylinders, const uint32_t totalHeads, const uint32_t sectorsPerTrack, const uint32_t sectorSize) {
    {
        std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);
        m_sectorsPerTrack[0] = sectorsPerTrack;
        m_bytesPerSector[0] = sectorSize;
        m_totalCylinders[0] = min(totalCylinders, MAX_TRACKS / 2);
        m_numHeads[0] = totalHeads;
        m_diskType = systemType;
        m_tracksToFlush.clear();
    }
    resetCache();
}

// trigger new disk detection
void SectorCacheMFM::triggerNewDiskMount() {
    resetCache();
    m_diskType = SectorType::stUnknown;
    m_diskInDrive = false;
}

// Pre-populate with blank sectors
void SectorCacheMFM::createBlankSectors() {
    DecodedSector blankSector;
    blankSector.numErrors = 0;
    blankSector.data.resize(m_bytesPerSector[0]);

    for (uint32_t trk = 0; trk < m_totalCylinders[0] * m_numHeads[0]; trk++) {
        m_trackCache[0][trk].sectorsWithErrors = 0;
        m_trackCache[0][trk].sectors.clear();
        for (uint32_t sec = 0; sec < m_sectorsPerTrack[0]; sec++)
            m_trackCache[0][trk].sectors.insert(std::make_pair(sec, blankSector));
    }
}

// The motor usage has timed out
void SectorCacheMFM::motorMonitor() {
    bool sendNotify = false;
    {
        // Shoudl it time out?
        std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);
        if ((m_motorTurnOnTime) && (GetTickCount64() - m_motorTurnOnTime > MOTOR_IDLE_TIMEOUT)) {
            flushPendingWrites();
            motorEnable(false, false);
            m_ignoreErrors = false;
            m_blockWriting = false;
            m_motorTurnOnTime = 0;
        }

        // Force writing etc
        const bool isDiskNowInDrive = isDiskInDrive();
        if (isDiskNowInDrive != m_diskInDrive) {
            if (!isDiskNowInDrive) {
                cylinderSeek(0, false);
                motorEnable(false, false);

                if (m_tracksToFlush.size()) {
                    if (diskRemovedWarning()) {
                        // Trigger re-writing
                        m_motorTurnOnTime = GetTickCount64() - (MOTOR_IDLE_TIMEOUT + 1);
                        return;
                    }
                    else m_tracksToFlush.clear();
                }

                // cache really needs to be cleared!
                if (m_tracksToFlush.size() < 1) {
                    for (uint32_t trk = 0; trk < MAX_TRACKS; trk++) {
                        m_trackCache[0][trk].sectors.clear();
                        m_trackCache[0][trk].sectors.clear();
                    }
                }
            }
            m_diskInDrive = isDiskNowInDrive;
            sendNotify = true;
        }
    }

    if (sendNotify) {
        if (m_diskChangeCallback) {
            for (DecodedTrack& trk : m_trackCache[0]) trk.sectors.clear();
            for (DecodedTrack& trk : m_trackCache[1]) trk.sectors.clear();
            if (m_diskInDrive) 
                identifyFileSystem(); 
            else m_diskType = SectorType::stUnknown;
            m_diskChangeCallback(m_diskInDrive, m_diskInDrive ? m_diskType : SectorType::stUnknown);
        }
    }
}

// Signal the motor is in use.  Returns if its ok
void SectorCacheMFM::motorInUse(bool upperSide) {
    if (!m_motorTurnOnTime) motorEnable(true, upperSide);
    m_motorTurnOnTime = GetTickCount64();

    if (m_dokanfileinfo) DokanResetTimeout(DOKAN_EXTRATIME, m_dokanfileinfo);
}

// Waits for the drive to be ready, and if it times out, returns false
bool SectorCacheMFM::waitForMotor(bool upperSide) {
    motorInUse(upperSide);
    while (!motorReady()) {
        Sleep(100);
        if (GetTickCount64() - m_motorTurnOnTime > MOTOR_TIMEOUT_TIME) 
            return false;
        motorInUse(upperSide);
    }

    if (m_dokanfileinfo) DokanResetTimeout(DOKAN_EXTRATIME, m_dokanfileinfo);

    return true;
}

// Release
SectorCacheMFM::~SectorCacheMFM() {
    releaseDrive();

    // FLUSH
    std::lock_guard<std::mutex> guard(m_motorTimerProtect);
    if (m_timer) {
        // Disable the motor timer
        DeleteTimerQueueTimer(m_timerQueue, m_timer, 0);
        m_timer = 0;
    }
    if (DeleteTimerQueueEx(m_timerQueue, NULL)) m_timerQueue = 0;    

    if (m_mfmBuffer) free(m_mfmBuffer);
}

// Get size of the disk in bytes
uint32_t SectorCacheMFM::getDiskDataSize() {
    if (!available()) return 0;
    if (m_totalCylinders[0]) return m_bytesPerSector[0] * m_sectorsPerTrack[0] * m_numHeads[0] * m_totalCylinders[0];
    return m_bytesPerSector[0] * m_sectorsPerTrack[0] * m_numHeads[0] * 82;
}

// Read *all* data from a specific cylinder and side
bool SectorCacheMFM::readDataAllFS(const uint32_t fileSystem, const uint32_t sectorNumber, const uint32_t sectorSize, void* data) {
    if (sectorSize != m_bytesPerSector[fileSystem])
        return false;

    const int track = sectorNumber / m_sectorsPerTrack[fileSystem];
    const int trackBlock = sectorNumber % m_sectorsPerTrack[fileSystem];
    const bool upperSurface = track % m_numHeads[fileSystem];
    const int cylinder = track / m_numHeads[fileSystem];

    if (track >= MAX_TRACKS)
        return false;

    std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);

    checkFlushPendingWrites();

    // Retry several times
    uint32_t retries = 0;
    for (;;) {
        // First, see if we have a perfect sector already
        auto it = m_trackCache[fileSystem][track].sectors.find(trackBlock);

        if (it != m_trackCache[fileSystem][track].sectors.end()) {
            // No errors? (or are we skipping them?)
            if ((it->second.numErrors == 0) || (m_ignoreErrors)) {
                memcpy_s(data, sectorSize, it->second.data.data(), min(it->second.data.size(), sectorSize));
                return true;
            }
        }

        // Retry monitor
        if (retries > MAX_RETRIES) {
            if (m_ignoreErrors) return false;
            retries = 0;

            if (m_dokanfileinfo) DokanResetTimeout(30000, m_dokanfileinfo);
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
                cylinderSeek(79, upperSurface);
            else
                cylinderSeek(0, upperSurface);

            // Wait for the seek, or it will get removed! 
            Sleep(300);
        }

        // If we get here then this sector isn't in the cache (or has errors), so we'll read and update ALL sectors for this cylinder
        motorInUse(upperSurface);
        cylinderSeek(cylinder, upperSurface);

        // Wait for the motor to spin up properly
        if (!waitForMotor(upperSurface))
            return false;

        // Actually do the read
        doTrackReading(fileSystem, track, retries > 1);

        retries++;
    }

    return true;
}

// Do reading
bool SectorCacheMFM::internalReadData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) {
    if (sectorSize != m_bytesPerSector[0]) return false;

    return readDataAllFS(0, sectorNumber, sectorSize, data);
}

// Do reading
bool SectorCacheMFM::internalHybridReadData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) {
    uint32_t fs = (m_diskType == SectorType::stHybrid) ? 1 : 0;

    if (sectorSize != m_bytesPerSector[fs]) return false;

    return readDataAllFS(fs, sectorNumber, sectorSize, data);
}


// Internal single attempt to read a track
bool SectorCacheMFM::doTrackReading(const uint32_t fileSystem, const uint32_t track, bool retryMode) {
    // Read some track data, with some delay for a retry
    ULONGLONG start = GetTickCount64();
    uint32_t bitsReceived;
    do {
        motorInUse(track % m_numHeads[fileSystem]);
        bitsReceived = mfmRead(track / m_numHeads[fileSystem], track % m_numHeads[fileSystem], retryMode, m_mfmBuffer, MAX_TRACK_SIZE);
        if (!bitsReceived) {
            if (GetTickCount64() - start > TRACK_READ_TIMEOUT) return false;
            else Sleep(50);
        }
    } while (!bitsReceived);

    // Try to identify the file system
    if (m_diskType == SectorType::stUnknown) {
        // Some defaults
        m_serialNumber[0] = 0x554E4B4E;
        m_serialNumber[1] = 0x554E4B4E;
        m_numHeads[0] = 2;
        m_numHeads[1] = 2;
        getTrackDetails_AMIGA(isHD(), m_sectorsPerTrack[0], m_bytesPerSector[0]);
        DecodedTrack trAmiga;
        findSectors_AMIGA((const unsigned char*)m_mfmBuffer, bitsReceived, isHD(), track, 0, trAmiga);
        DecodedTrack trIBM;
        bool nonStandard = false;
        findSectors_IBM((const unsigned char*)m_mfmBuffer, bitsReceived, isHD(), track, 0, trIBM, nonStandard);
        uint32_t serialNumber;
        uint32_t sectorsPerTrack;
        uint32_t bytesPerSector;

        if (trAmiga.sectors.size()) {
            m_diskType = SectorType::stAmiga;
            m_sectorsPerTrack[0] = max(m_sectorsPerTrack[0], (uint32_t)trAmiga.sectors.size());
            m_serialNumber[0] = 0x414D4644; // AMFD
        }
        else m_diskType = SectorType::stUnknown;

        if (trIBM.sectors.size() >= 5) {
            m_diskType = SectorType::stIBM;
            uint32_t totalSectors;
            uint32_t numHeads;
            if (getTrackDetails_IBM(&trIBM, serialNumber, numHeads, totalSectors, sectorsPerTrack, bytesPerSector)) {
                if ((trIBM.sectors.size() >= 5) && (trAmiga.sectors.size() > 1)) {
                    m_diskType = SectorType::stHybrid;
                }
                else
                    if (nonStandard || (numHeads < 2)) m_diskType = SectorType::stAtari;
                uint32_t i = (m_diskType == SectorType::stHybrid) ? 1 : 0;
                m_sectorsPerTrack[i] = sectorsPerTrack;
                m_bytesPerSector[i] = bytesPerSector;
                m_serialNumber[i] = serialNumber;
                m_numHeads[i] = numHeads;
                m_totalCylinders[i] = max(80, (totalSectors / sectorsPerTrack) / m_numHeads[i]);
            }
            else {
                m_sectorsPerTrack[0] = isHD() ? 18 : 9;
                m_bytesPerSector[0] = 512;
                m_serialNumber[0] = 0xAAAAAAAA;
                m_totalCylinders[0] = 80;
                m_numHeads[0] = 2;
            }
        }
    }

    if (m_diskType == SectorType::stHybrid) {
        if (m_numHeads[1] == 2) {  // Has 2 sides? Treat everything as normal
            findSectors_AMIGA((const unsigned char*)m_mfmBuffer, bitsReceived, isHD(), track, m_sectorsPerTrack[0], m_trackCache[0][track]);
            findSectors_IBM((const unsigned char*)m_mfmBuffer, bitsReceived, isHD(), track, m_sectorsPerTrack[1], m_trackCache[1][track]);
        }
        else // Atari is single sided. Amiga is ALWAYS double sided
            if (fileSystem == 1) {
                findSectors_AMIGA((const unsigned char*)m_mfmBuffer, bitsReceived, isHD(), track * 2, m_sectorsPerTrack[0], m_trackCache[0][track * 2]);
                findSectors_IBM((const unsigned char*)m_mfmBuffer, bitsReceived, isHD(), track, m_sectorsPerTrack[1], m_trackCache[1][track]);
            }
            else {
                findSectors_AMIGA((const unsigned char*)m_mfmBuffer, bitsReceived, isHD(), track, m_sectorsPerTrack[0], m_trackCache[0][track]);
                if ((track & 1) == 0)
                    findSectors_IBM((const unsigned char*)m_mfmBuffer, bitsReceived, isHD(), track, m_sectorsPerTrack[1], m_trackCache[1][track >> 1]);
            }
    }
    else
        if (m_diskType == SectorType::stAmiga)
            findSectors_AMIGA((const unsigned char*)m_mfmBuffer, bitsReceived, isHD(), track, m_sectorsPerTrack[0], m_trackCache[0][track]);
    if ((m_diskType == SectorType::stAtari) || (m_diskType == SectorType::stIBM))
        findSectors_IBM((const unsigned char*)m_mfmBuffer, bitsReceived, isHD(), track, m_sectorsPerTrack[0], m_trackCache[0][track]);    

    return true;
}

// Do writing
bool SectorCacheMFM::internalWriteData(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data) {
    if (m_blockWriting) return false;
    if ((m_diskType == SectorType::stHybrid) || (m_diskType == SectorType::stUnknown)) return false;
    if (isDiskWriteProtected()) return false;

    const int track = sectorNumber / m_sectorsPerTrack[0];
    if (track >= MAX_TRACKS) return false;
    const int trackBlock = sectorNumber % m_sectorsPerTrack[0];
    const bool upperSurface = track % m_numHeads[0];
    const int cylinder = track / m_numHeads[0];

    std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);

    // Now replace the sector we're overwriting, just in memory at this point
    auto it = m_trackCache[0][track].sectors.find(trackBlock);
    if (it != m_trackCache[0][track].sectors.end()) {
        if (memcmp(it->second.data.data(), data, min(sectorSize, it->second.data.size())) == 0) {
            if (it->second.numErrors == 0) return true;
            it->second.numErrors = 0;                
        }
        else {
            // No errors? (or are we skipping them?)
            memcpy_s(it->second.data.data(), it->second.data.size(), data, min(sectorSize, it->second.data.size()));
            it->second.numErrors = 0;
        }
    }
    else {
        // Add the track
        DecodedSector sector;
        sector.data.resize(m_bytesPerSector[0]);
        memcpy_s(sector.data.data(), sector.data.size(), data, min(sector.data.size(), sectorSize));
        sector.numErrors = 0;
        m_trackCache[0][track].sectors.insert(std::make_pair(trackBlock, sector));
    }

    auto i = m_tracksToFlush.find(track);
    if (i == m_tracksToFlush.end())
        m_tracksToFlush.insert(std::make_pair(track, 1));
    else i->second++;

    motorInUse(upperSurface);
    checkFlushPendingWrites();

    return true;
}

// Checks for pending writes, if theres too many then flush them
void SectorCacheMFM::checkFlushPendingWrites() {
    if (m_tracksToFlush.size() < FORCE_FLUSH_AT_TRACKS) return;
    flushPendingWrites();
}

// Removes anything that failed from the cache so it has to be re-read from the disk
void SectorCacheMFM::removeFailedWritesFromCache() {
    for (auto& trk : m_tracksToFlush)
        if (trk.second)
            m_trackCache[0][trk.first].sectors.clear();
    m_tracksToFlush.clear();
}

// Flush any writing thats still pending - lock must already be obtained
bool SectorCacheMFM::flushPendingWrites() {
    if (m_blockWriting) return false;

    for (auto& trk : m_tracksToFlush) {
        const uint32_t track = trk.first;
        const bool upperSurface = track % m_numHeads[0];
        const int cylinder = track / m_numHeads[0];

        // Motor shouldn't stop here
        motorInUse(upperSurface);
        cylinderSeek(cylinder, upperSurface);
        if (!waitForMotor(upperSurface)) {
            m_tracksToFlush.clear();
            return false;
        }

        // Assemble and commit an entire track.  First see if any data is missing
        bool fillData = m_trackCache[0][track].sectors.size() < m_sectorsPerTrack[0];
        if (!fillData)
            for (const auto& it : m_trackCache[0][track].sectors)
                if (it.second.numErrors) {
                    fillData = true;
                    break;
                }

        // Theres some missing data. We we'll request the track again and fill in the gaps
        if (fillData) {
            // 1. Take a copy
            const std::unordered_map<int, DecodedSector> backup = m_trackCache[0][track].sectors;
            if (m_writeOnly) {
                for (uint32_t sec = 0; sec < m_sectorsPerTrack[0]; sec++) {
                    auto it = m_trackCache[0][track].sectors.find(sec);
                    // Does a sector with this number exist?
                    if (it == m_trackCache[0][track].sectors.end()) {
                        DecodedSector tmp;
                        memset(&tmp, 0, sizeof(tmp));
                        tmp.numErrors = 0;
                        m_trackCache[0][track].sectors.insert(std::make_pair(sec, tmp));
                    }
                }
            }
            else {
                // 2. *try* to read the track (but dont care if it fails)
                doTrackReading(0, track, false);
            }
            // 3. Replace any tracks now read with any we have in our backup that have errors = 0
            for (const auto& sec : backup) {
                if (sec.second.numErrors == 0) {
                    auto it = m_trackCache[0][track].sectors.find(sec.first);
                    if (it != m_trackCache[0][track].sectors.end())
                        it->second = sec.second;
                }
            }
        }

        // We will now have a complete track worth of sectors so we can now finally commit this to disk (hopefully) plus we will verify it
        uint32_t numBytes;
        switch (m_diskType) {
        case SectorType::stAmiga: numBytes = encodeSectorsIntoMFM_AMIGA(isHD(), m_trackCache[0][track], track, MAX_TRACK_SIZE, m_mfmBuffer); break;
        case SectorType::stIBM: numBytes = encodeSectorsIntoMFM_IBM(isHD(), false, &m_trackCache[0][track], track, MAX_TRACK_SIZE, m_mfmBuffer); break;
        case SectorType::stAtari: numBytes = encodeSectorsIntoMFM_IBM(isHD(), true, &m_trackCache[0][track], track, MAX_TRACK_SIZE, m_mfmBuffer); break;
        case SectorType::stHybrid:
            // Need to work out which type of track it is although technically hybrid isnt supported for writing
            if ((m_trackCache[0][track].sectors.size() == 11) || (m_trackCache[0][track].sectors.size() == 22))
                numBytes = encodeSectorsIntoMFM_AMIGA(isHD(), m_trackCache[0][track], track, MAX_TRACK_SIZE, m_mfmBuffer);
            else numBytes = encodeSectorsIntoMFM_IBM(isHD(), true, &m_trackCache[0][track], track, MAX_TRACK_SIZE, m_mfmBuffer);
            break;
        default:
            numBytes = 0;
            break;
        }

        if (numBytes < 0) {
            // this *should* never happen
            removeFailedWritesFromCache();
            return false;
        }

        // Write retries
        uint32_t retries = 0;
        for (;;) {
            // Handle a re-seek - might clean the head
            if (retries == MAX_RETRIES / 2) {
                motorInUse(upperSurface);

                if (cylinder < 40)
                    cylinderSeek(79, upperSurface);
                else
                    cylinderSeek(0, upperSurface);

                // Wait for the seek, or it will get removed! 
                Sleep(300);
                cylinderSeek(cylinder, upperSurface);
                retries = 0;
            }

            // Commit to disk
            motorInUse(upperSurface);

            if (!isDiskInDrive()) {
                if (!diskRemovedWarning()) {
                    removeFailedWritesFromCache();
                    return false;
                }
            }

            if (mfmWrite(cylinder, upperSurface, (m_diskType == SectorType::stIBM) || (m_diskType == SectorType::stAtari), m_mfmBuffer, numBytes )) {
                // Now wait until it completes - approx 400-500ms as this will also read it back to verify it
                ULONGLONG start = GetTickCount64();
                bool doRetry = false;
                while (!writeCompleted()) {
                    if (GetTickCount64() - start > DISK_WRITE_TIMEOUT) {
                        if (m_dokanfileinfo) DokanResetTimeout(30000, m_dokanfileinfo);
                        resetDrive(cylinder);
                        m_motorTurnOnTime = 0;
                        Sleep(200);
                        if (!isDiskInDrive()) {
                            if (diskRemovedWarning()) {
                                doRetry = true;
                                break;
                            }
                            else {
                                m_blockWriting = true;
                                removeFailedWritesFromCache();
                                return false;
                            }
                        }
                        else
                            switch (MessageBox(GetDesktopWindow(), L"Disk writing is taking too long.\nWhat would you like to do?", L"Disk Writing Timeout", MB_ABORTRETRYIGNORE | MB_ICONQUESTION)) {
                            case IDABORT:
                                m_blockWriting = true;
                                removeFailedWritesFromCache();
                                return false;
                            case IDRETRY:
                                doRetry = true;
                                break;
                            default:
                                removeFailedWritesFromCache();
                                return false;
                            }
                    }
                }
                if (doRetry) {
                    retries = 0;
                }
                else {
                    // Writing succeeded. Now to do a verify!
                    const std::unordered_map<int, DecodedSector> backup = m_trackCache[0][track].sectors;
                    for (;;) {
                        if (!doTrackReading(0, track, retries > 1)) {
                            if (m_dokanfileinfo) DokanResetTimeout(30000, m_dokanfileinfo);
                            m_motorTurnOnTime = 0;
                            if (!isDiskInDrive()) {
                                if (!diskRemovedWarning()) {
                                    removeFailedWritesFromCache();
                                    return false;
                                }
                            }
                            else
                                if (MessageBox(GetDesktopWindow(), L"Disk verifying is taking too long.\nWhat would you like to do?", L"Disk Verifying Timeout", MB_ABORTRETRYIGNORE | MB_ICONQUESTION) != IDRETRY) {
                                    removeFailedWritesFromCache();
                                    return false;
                                }
                            // Wait and try again
                            Sleep(100);
                        }
                        else break;
                    }

                    // Check what was read back matches what we wrote
                    bool errors = false;
                    for (const auto& sec : backup) {
                        auto search = m_trackCache[0][track].sectors.find(sec.first);
                        // Sector no longer exists.  ERROR!
                        if (search == m_trackCache[0][track].sectors.end()) {
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
                                if (search->second.data.size() != sec.second.data.size()) {
                                    // BAD read back wrong sector size
                                    errors = true;
                                    break;
                                }
                                else
                                    if (memcmp(search->second.data.data(), sec.second.data.data(), search->second.data.size()) != 0) {
                                        // BAD read back even though there were no errors
                                        errors = true;
                                        break;
                                    }
                            }
                        }
                    }

                    if (!errors) break;
                }
            }
            else {
                // this shouldnt ever happen
                removeFailedWritesFromCache();
                return false;
            }

            retries++;
        }

        // Mark that its done!
        trk.second = 0;
    }

    removeFailedWritesFromCache();
    return true;
}