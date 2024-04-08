#pragma once

#include "dokaninterface.h"
#include "adf_operations.h"

class ShellRegistery;
class VolumeManager;
class SectorCacheEngine;
class DokanFileSystemManager;
struct AdfDevice;

class MountedVolume : public DokanFileSystemManager {
private:
	VolumeManager* m_manager; 
    SectorCacheEngine* m_io = nullptr;
    AdfDevice* m_ADFdevice = nullptr;
    AdfVolume* m_ADFvolume = nullptr;
    uint32_t m_partitionIndex = 0;
    DokanFileSystemAmigaFS* m_amigaFS = nullptr;
    bool m_tempUnmount = false;
    ShellRegistery* m_registry;
public:
	MountedVolume(VolumeManager* manager, const std::wstring& mainEXE, SectorCacheEngine* io, const WCHAR driveLetter, const bool forceWriteProtect);
    virtual ~MountedVolume();

    // mount AMIGA file system
	bool mountFileSystem(AdfDevice* adfDevice, uint32_t partitionIndex);

    // Unmount *any* file system 
	void unmountFileSystem();

    virtual bool isDiskInDrive() override;
    virtual bool isDriveLocked() override;
    virtual bool isWriteProtected() override;
    virtual uint32_t volumeSerial() override;
    virtual const std::wstring getDriverName() override;
    virtual SectorCacheEngine* getBlockDevice() override;
    virtual bool isPhysicalDevice() override;
    virtual void temporaryUnmountDrive() override;
    virtual void restoreUnmountedDrive() override;
    virtual uint32_t getTotalTracks() override;

    // Returns FALSE if files are open
    bool setLocked(bool enableLock);

    // Install bootblock for Amiga drives
    bool installAmigaBootBlock();

    // Notifications of the file system being mounted
    virtual void onMounted(const std::wstring& mountPoint, PDOKAN_FILE_INFO dokanfileinfo) override;
    virtual void onUnmounted(PDOKAN_FILE_INFO dokanfileinfo) override;

    AdfDevice* getADFDevice();
    AdfVolume* getADFVolume();
};