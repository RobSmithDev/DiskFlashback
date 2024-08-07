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

// Handles reading and writing from real disks, with *hopefully* reliable detection of the type of disk inserted
#include <dokan/dokan.h>
#include <map>
#include "sectorCache.h"
#include "floppybridge_lib.h"
#include "sectorCommon.h"
#include "mfminterface.h"
#include <mutex>


class SectorRW_FloppyBridge : public SectorCacheMFM {
private:
    FloppyBridgeAPI* m_bridge       = nullptr;
    FloppyBridge::BridgeDensityMode m_densityMode = FloppyBridge::BridgeDensityMode::bdmAuto;
protected:
    virtual bool restoreDrive() override;
    virtual void releaseDrive() override;
    virtual bool isDiskInDrive() override;
    virtual bool isDriveWriteProtected() override;

    virtual bool motorEnable(bool enable, bool upperSide) override;
    virtual bool motorReady() override;
    virtual bool resetDrive(uint32_t cylinder) override;
    virtual bool writeCompleted() override;
    virtual bool cylinderSeek(uint32_t cylinder, bool upperSide) override;
    virtual uint32_t mfmRead(uint32_t cylinder, bool upperSide, bool retryMode, void* data, uint32_t maxLength) override;
    virtual bool mfmWrite(uint32_t cylinder, bool upperSide, bool fromIndex, void* data, uint32_t maxLength) override;

public:
    SectorRW_FloppyBridge(const std::string& profile, std::function<void(bool diskInserted, SectorType diskFormat)> diskChangeCallback);
    ~SectorRW_FloppyBridge();

    // Return TRUE if this is actually a physical "REAL" drive
    virtual bool isPhysicalDisk() override { return true; };

    // Change the denity more of the bridge
    bool setForceDensityMode(FloppyBridge::BridgeDensityMode mode);

    // Return an ID to identify this with
    virtual uint32_t id() override;

    // Returns TRUE if the inserted disk is HD
    virtual bool isHD() override;

    // Is it working?
    virtual bool available() override;

    // Returns the name of the driver providing access
    virtual std::wstring getDriverName() override;

    // Runs cleaning on the drive
    bool runCleaning(std::function<bool(uint16_t position, uint16_t total)> progress);

    // Rapid shutdown
    virtual void quickClose() override;
};
