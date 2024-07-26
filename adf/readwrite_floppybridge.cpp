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


#include "readwrite_floppybridge.h"
#include "amiga_sectors.h"
#include "ibm_sectors.h"
#include <stdio.h>

#define REPEAT_COUNT 5

bool SectorRW_FloppyBridge::motorEnable(bool enable, bool upperSide) {
    if (!m_bridge) return false;
    m_bridge->setMotorStatus(upperSide, enable);
    return true;
}
bool SectorRW_FloppyBridge::motorReady() {
    return m_bridge && m_bridge->isReady();
}
bool SectorRW_FloppyBridge::resetDrive(uint32_t cylinder) {
    return m_bridge && m_bridge->resetDrive(cylinder);
}
bool SectorRW_FloppyBridge::writeCompleted() {
    return m_bridge && m_bridge->isWriteComplete();
}
bool SectorRW_FloppyBridge::cylinderSeek(uint32_t cylinder, bool upperSide) {
    if (!m_bridge) return false;
    m_bridge->gotoCylinder(cylinder, upperSide);
    return true;
}
uint32_t SectorRW_FloppyBridge::mfmRead(uint32_t cylinder, bool upperSide, bool retryMode, void* data, uint32_t maxLength) {
    if (!m_bridge) return false;
    return m_bridge->getMFMTrack(upperSide, cylinder, retryMode, maxLength, data);
}
bool SectorRW_FloppyBridge::mfmWrite(uint32_t cylinder, bool upperSide, bool fromIndex, void* data, uint32_t maxLength) {
    if (!m_bridge) return false;
    return m_bridge->writeMFMTrackToBuffer(upperSide, cylinder, fromIndex, maxLength, data);
}
 
void SectorRW_FloppyBridge::releaseDrive() {
    if (m_bridge) m_bridge->shutdown();
    SectorCacheMFM::releaseDrive();
}

bool SectorRW_FloppyBridge::restoreDrive() {
    if (!m_bridge) return false;

    if (!m_bridge->initialise()) {
        delete m_bridge;
        m_bridge = nullptr;
        return false;
    }

    if (!m_bridge->resetDrive(0)) {
        delete m_bridge;
        m_bridge = nullptr;
        return false;
    }

    // Its much faster, but of no use to *UAE!
    m_bridge->setDirectMode(true);

    return true;
}

// Return an ID to identify this with
uint32_t SectorRW_FloppyBridge::id() {
    if (m_bridge) 
        return m_bridge->getDriverTypeIndex();
    else return 0xFFFF;
}

// Returns the name of the driver providing access
std::wstring SectorRW_FloppyBridge::getDriverName() {
    if (!m_bridge) return L"Unavailable";
    switch (m_bridge->getDriverTypeIndex()) {
    case 0: return L"Drawbridge";
    case 1: return L"Greaseweazle";
    case 2: return L"Supercard Pro";
    default: return L"Unknown";
    }
}

// Runs cleaning on the drive
bool SectorRW_FloppyBridge::runCleaning(std::function<bool(uint16_t position, uint16_t total)> progress) {
    if (!m_bridge) return false;
    uint16_t steps = m_bridge->getMaxCylinder();
    if (steps > 40) steps /= 10; else steps /= 5;

    const uint32_t progressMax = (m_bridge->getMaxCylinder() / steps) * 2 * REPEAT_COUNT;

    if (!progress(0, progressMax)) return false;

    // Turn on motor
    m_bridge->setMotorStatus(false, true);
    uint32_t counter = 0;
    // Wait for spinup
    for (counter=0; counter<12; counter++) {
        Sleep(50);
        if (!progress(0, progressMax)) {
            m_bridge->setMotorStatus(false, false);
            return false;
        }
    }

    // Run cleaning cycle
    counter = 1;
    for (uint32_t cycle = 0; cycle < REPEAT_COUNT; cycle++) {
        for (uint32_t cylinder = 0; cylinder < m_bridge->getMaxCylinder() - steps; cylinder += steps) {
            m_bridge->gotoCylinder(cylinder + steps - 1, false);
            Sleep(300);
            if (!progress(++counter, progressMax)) {
                m_bridge->setMotorStatus(false, false);
                m_bridge->gotoCylinder(0, false);
                return false;
            }
            m_bridge->gotoCylinder(cylinder, false);
            Sleep(300);
            if (!progress(++counter, progressMax)) {
                m_bridge->setMotorStatus(false, false);
                m_bridge->gotoCylinder(0, false);
                return false;
            }
        }
    }
    progress(progressMax, progressMax);
    m_bridge->setMotorStatus(false, false);
    m_bridge->gotoCylinder(0, false);
    return true;
}


// Constructor
SectorRW_FloppyBridge::SectorRW_FloppyBridge(const std::string& profile, std::function<void(bool diskInserted, SectorType diskFormat)> diskChangeCallback) : SectorCacheMFM(diskChangeCallback) {
    FloppyBridgeAPI::enableUsageNotifications(false);
    m_bridge = FloppyBridgeAPI::createDriverFromString(profile.c_str());
    if (m_bridge) {
        m_bridge->setBridgeMode(FloppyBridge::BridgeMode::bmStalling);
        setReady();
    }
}

bool SectorRW_FloppyBridge::isDiskInDrive() {
    return m_bridge && m_bridge->isDiskInDrive();
}

bool SectorRW_FloppyBridge::isDriveWriteProtected() {
    return m_bridge && m_bridge->isWriteProtected();
}

// Is available?
bool SectorRW_FloppyBridge::available() {
    return m_bridge && m_bridge->isAvailable() && m_bridge->isStillWorking();
}

// Rapid shutdown
void SectorRW_FloppyBridge::quickClose() {
    if (m_bridge) {
        m_bridge->shutdown();
        delete m_bridge;
        m_bridge = nullptr;
    }
}


// Release
SectorRW_FloppyBridge::~SectorRW_FloppyBridge() {
    quickClose();
}

// Returns TRUE if the inserted disk is HD
bool SectorRW_FloppyBridge::isHD() {
    if (!m_bridge) return false;
    if (m_densityMode != FloppyBridge::BridgeDensityMode::bdmAuto)
        return m_densityMode == FloppyBridge::BridgeDensityMode::bdmHDOnly;
    return m_bridge->getDriveTypeID() == FloppyDiskBridge::DriveTypeID::dti35HD;
}

// Change the denity more of the bridge
bool SectorRW_FloppyBridge::setForceDensityMode(FloppyBridge::BridgeDensityMode mode) {
    if (!m_bridge) return false;
    m_densityMode = mode;
    return m_bridge->setBridgeDensityMode(mode);
}
