#pragma once

#include "pll.h"
#include "mfminterface.h"
#include <unordered_map>


class SCPFile : public SectorCacheMFM {
private:
    struct Revolution {
        uint32_t sizeInBits;
        std::vector<uint8_t> mfmData;
    };

    struct Track {
        uint32_t m_fileOffset;
        uint32_t lastRev;

        bool m_trackIsBad;        
        std::vector<Revolution> revolutions;
    };

    uint32_t m_firstTrack = 0;
    uint32_t m_lastTrack = 80 * 2;
    uint32_t m_fluxMultiplier = 25;
    uint32_t m_numRevolutions = 1;

    std::unordered_map<uint32_t, Track> m_tracks;

    PLL m_pll;
    uint8_t m_density = 0;  // 0=Unknown, 1=DD, 2=HD
    HANDLE m_file;

    void checkHD();

    // Decode a specific track into MFM
    bool decodeTrack(uint32_t track);
protected:
    virtual bool restoreDrive() override { return isDiskInDrive(); };
    virtual void releaseDrive() override {};

    virtual bool isDiskInDrive() override;
    virtual uint32_t mfmRead(uint32_t cylinder, bool upperSide, bool retryMode, void* data, uint32_t maxLength) override { return 0; };
    virtual uint32_t mfmRead(uint32_t track, bool retryMode, void* data, uint32_t maxLength) override;

    virtual bool isDriveWriteProtected() override { return true; };
    virtual bool motorEnable(bool enable, bool upperSide) override { return isDiskInDrive(); };
    virtual bool motorReady() override { return isDiskInDrive(); };
    virtual bool resetDrive(uint32_t cylinder) override { return isDiskInDrive(); };
    virtual bool writeCompleted() override { return false; };
    virtual bool cylinderSeek(uint32_t cylinder, bool upperSide) override { return isDiskInDrive(); };
    virtual bool mfmWrite(uint32_t cylinder, bool upperSide, bool fromIndex, void* data, uint32_t maxLength) override { return false; };
    virtual bool shouldPrompt() override { return false; };

    // Actually read the file
    bool readSCPFile();

public:
	SCPFile(HANDLE file, std::function<void(bool diskInserted, SectorType diskFormat)> diskChangeCallback);
    ~SCPFile();

    // Return TRUE if this is actually a physical "REAL" drive
    virtual bool isPhysicalDisk() override { return false; };
   
    // Return an ID to identify this with
    virtual uint32_t id() override { return 0x53435020; }

    // Returns TRUE if the inserted disk is HD
    virtual bool isHD() override { return m_density == 2; };

    // Is it working?
    virtual bool available() override { return isDiskInDrive(); };

    // Returns the name of the driver providing access
    virtual std::wstring getDriverName() override { return L"SCP File"; };

    // Rapid shutdown
    virtual void quickClose() override;
};
