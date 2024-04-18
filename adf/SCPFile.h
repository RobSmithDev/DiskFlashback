#pragma once

#include "pll.h"
#include "mfminterface.h"


class SCPFile : public SectorCacheMFM {
private:
    struct TrackRev {
        std::vector<uint16_t> mfmData;
        std::vector<uint8_t> decoded;  // so we dont decode it twice
    };

    struct Track {
        uint32_t lastRev = 0;
        std::vector<TrackRev> revs;
    };

    uint32_t numHeads = 2;
    std::unordered_map<uint32_t, Track> m_tracks;
    PLL m_pll;
    bool m_hd = false;

    void checkHD();
protected:
    virtual bool restoreDrive() override { return isDiskInDrive(); };
    virtual void releaseDrive() override {};

    virtual bool isDiskInDrive() override;
    virtual uint32_t mfmRead(uint32_t cylinder, bool upperSide, bool retryMode, void* data, uint32_t maxLength) override;

    virtual bool isDriveWriteProtected() override { return true; };
    virtual bool motorEnable(bool enable, bool upperSide) override { return isDiskInDrive(); };
    virtual bool motorReady() override { return isDiskInDrive(); };
    virtual bool resetDrive(uint32_t cylinder) override { return isDiskInDrive(); };
    virtual bool writeCompleted() override { return false; };
    virtual bool cylinderSeek(uint32_t cylinder, bool upperSide) override { return isDiskInDrive(); };
    virtual bool mfmWrite(uint32_t cylinder, bool upperSide, bool fromIndex, void* data, uint32_t maxLength) override { return false; };
    virtual bool shouldPrompt() override { return false; };

    // Actually read the file
    bool readSCPFile(HANDLE file);

public:
	SCPFile(HANDLE file, std::function<void(bool diskInserted, SectorType diskFormat)> diskChangeCallback);

    // Return TRUE if this is actually a physical "REAL" drive
    virtual bool isPhysicalDisk() override { return false; };
   
    // Return an ID to identify this with
    virtual uint32_t id() override { return 0x53435020; }

    // Returns TRUE if the inserted disk is HD
    virtual bool isHD() override { return m_hd; };

    // Is it working?
    virtual bool available() override { return isDiskInDrive(); };

    // Returns the name of the driver providing access
    virtual std::wstring getDriverName() override { return L"SCP File"; };
};
