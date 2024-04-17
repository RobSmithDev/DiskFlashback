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

    // Rapid shutdown
    void quickClose();
};
