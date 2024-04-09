#pragma once

#include "MountedVolume.h"
#include "adflib/src/adflib.h"
#include "adflib/src/adf_blk.h"
#include "amiga_sectors.h"
#include "dokaninterface.h"
#include "shellMenus.h"
#include <Shlobj.h>

#pragma comment(lib,"Shell32.lib")


MountedVolume::MountedVolume(VolumeManager* manager, const std::wstring& mainEXE, SectorCacheEngine* io, const WCHAR driveLetter, const bool forceWriteProtect) :
	DokanFileSystemManager(driveLetter, forceWriteProtect, mainEXE), m_io(io) {
    m_registry = new ShellRegistery(mainEXE);
    m_amigaFS = new DokanFileSystemAmigaFS(this);
    m_IBMFS = new DokanFileSystemFATFS(this);
}

MountedVolume::~MountedVolume() {
    if (m_amigaFS) delete m_amigaFS;
    if (m_IBMFS) delete m_IBMFS;
    if (m_registry) delete m_registry;
}

// Special version that doesnt fail if the file system is unknown
RETCODE refreshAmigaVolume(struct AdfDevice* const dev) {
    struct AdfVolume* vol;
    struct bRootBlock root;
    char diskName[35];

    dev->cylinders = 80;
    dev->heads = 2;
    if (dev->devType == DEVTYPE_FLOPDD)
        dev->sectors = 11;
    else
        dev->sectors = 22;

    vol = (struct AdfVolume*)malloc(sizeof(struct AdfVolume));
    if (!vol) return RC_ERROR;

    vol->mounted = TRUE;
    vol->firstBlock = 0;
    vol->lastBlock = (int32_t)(dev->cylinders * dev->heads * dev->sectors - 1);
    vol->rootBlock = (vol->lastBlock + 1 - vol->firstBlock) / 2;
    vol->blockSize = 512;
    vol->dev = dev;

    if (adfReadRootBlock(vol, (uint32_t)vol->rootBlock, &root) == RC_OK) {
        memset(diskName, 0, 35);
        memcpy_s(diskName, 35, root.diskName, root.nameLen);
        diskName[34] = '\0';  // make sure its null terminted
    }
    else diskName[0] = '\0';
    vol->volName = _strdup(diskName);

    if (dev->volList) {
        for (size_t i = 0; i < dev->nVol; i++) 
            if (dev->volList[i]) {
                if (dev->volList[i]->volName) free(dev->volList[i]->volName);
                free(dev->volList[i]);
            }
        free(dev->volList);
    }
    dev->volList = (struct AdfVolume**)malloc(sizeof(struct AdfVolume*));
    if (!dev->volList) {
        free(vol);
        return RC_ERROR;
    }
    dev->volList[0] = vol;
    dev->nVol = 1;

    return RC_OK;
}


void MountedVolume::restoreUnmountedDrive() {
    if (m_tempUnmount) {
        if (m_ADFdevice) {
            if (m_ADFvolume)
                if (m_io->isPhysicalDisk()) refreshAmigaVolume(m_ADFdevice);
            mountFileSystem(m_ADFdevice, m_partitionIndex);
        }
        if (m_FatFS) {
            mountFileSystem(m_FatFS, m_partitionIndex);
        }
        m_tempUnmount = false;
    }
}

void MountedVolume::temporaryUnmountDrive() {
    if (!m_tempUnmount) {
        AdfDevice* adfDevice = m_ADFdevice;
        FATFS* fatFS = m_FatFS;
        unmountFileSystem();
        m_ADFdevice = adfDevice;
        m_FatFS = fatFS;
        m_tempUnmount = true;
    }
}


// Returns if disk is locked and cannot be read
bool MountedVolume::isDriveLocked() {
    return m_io->isAccessLocked();
}

// Returns TRUE if write protected
bool MountedVolume::isWriteProtected() {
    return isForcedWriteProtect() || m_io->isDiskWriteProtected();
}

// Returns TRUE if theres a disk in the drive
bool MountedVolume::isDiskInDrive() {
    return m_io->isDiskPresent();
}

// Returns TRUE if this is a real disk
bool MountedVolume::isPhysicalDevice() {
    return m_io->isPhysicalDisk();
}

// Returns the name of the driver used for FloppyBridge
const std::wstring MountedVolume::getDriverName() {
    return m_io->getDriverName();
}

// Returns FALSE if files are open
bool MountedVolume::setLocked(bool enableLock) {
    if (isDriveInUse()) return false;
    m_io->flushWriteCache();
    m_io->setLocked(enableLock);
    return true;
}

// Install bootblock for Amiga drives
bool MountedVolume::installAmigaBootBlock() {
    if (!getADFVolume()) return false;
    if (isWriteProtected()) return false;

    const std::string appName = "Installed with " APPLICATION_NAME;

    // 1024 bytes is required
    uint8_t* mem = (uint8_t*)malloc(1024);
    if (!mem) return false;

    memset(mem, 0, 1024);
    fetchBootBlockCode_AMIGA(isFFS(getADFVolume()->dosType), mem, appName);

    // Nothing writes to where thr boot block is so it's safe to do this
    bool ok = adfInstallBootBlock(getADFVolume(), mem) == RC_OK;

    free(mem);

    return ok;
}

// Fetch serial number
uint32_t MountedVolume::volumeSerial() {
    return m_io->serialNumber();
}
// Fetch root block device
SectorCacheEngine* MountedVolume::getBlockDevice() {
    return m_io;
}

AdfVolume* MountedVolume::getADFVolume() {
    return m_ADFvolume;
}

AdfDevice* MountedVolume::getADFDevice() {
    return m_ADFdevice;
}

// Notifications of the file system being mounted
void MountedVolume::onMounted(const std::wstring& mountPoint, PDOKAN_FILE_INFO dokanfileinfo) {
    DokanFileSystemManager::onMounted(mountPoint, dokanfileinfo);
    m_registry->setupDriveIcon(true, mountPoint[0]);
}
void MountedVolume::onUnmounted(PDOKAN_FILE_INFO dokanfileinfo) {
    DokanFileSystemManager::onUnmounted(dokanfileinfo);
    m_registry->setupDriveIcon(false, getMountPoint()[0]);
}

uint32_t MountedVolume::getTotalTracks() {
    if (m_ADFdevice) return m_ADFdevice->cylinders * m_ADFdevice->heads;
    return 0;
}

// Mount a Fat12 device
bool MountedVolume::mountFileSystem(FATFS* ftFSDevice, uint32_t partitionIndex) {
    m_partitionIndex = partitionIndex;

    // Hook up FS_operations
    m_IBMFS->setCurrentVolume(ftFSDevice);
    setActiveFileSystem(m_IBMFS);
    m_FatFS = ftFSDevice;

    m_registry->mountDismount(ftFSDevice != nullptr, getMountPoint()[0], m_io);
    m_registry->setupDriveIcon(true, getMountPoint()[0], 0);
    if (m_FatFS) {
        //SHChangeNotify(SHCNE_MEDIAREMOVED, SHCNF_PATH, m_drive.c_str(), NULL);
        SHChangeNotify(SHCNE_DRIVEREMOVED, SHCNF_PATH, getMountPoint().c_str(), NULL);
        SHChangeNotify(SHCNE_DRIVEADD, SHCNF_PATH, getMountPoint().c_str(), NULL);
        //SHChangeNotify(SHCNE_MEDIAINSERTED, SHCNF_PATH, m_drive.c_str(), NULL);
    }
    m_tempUnmount = false;

    return m_FatFS != nullptr;
}

bool MountedVolume::mountFileSystem(AdfDevice* adfDevice, uint32_t partitionIndex) {
    if (m_ADFvolume) {
        adfUnMount(m_ADFvolume);
        m_ADFvolume = nullptr;
    }
    m_ADFdevice = adfDevice;
    m_partitionIndex = partitionIndex;
    m_ADFvolume = m_ADFdevice ? adfMount(m_ADFdevice, partitionIndex, isForcedWriteProtect()) : nullptr;
   

    // Hook up ADF_operations
    if (m_ADFvolume) {
        m_amigaFS->setCurrentVolume(m_ADFvolume);
        setActiveFileSystem(m_amigaFS);
    }
    else setActiveFileSystem(nullptr);
    m_registry->setupDriveIcon(true, getMountPoint()[0], 1);
    m_registry->mountDismount(m_ADFvolume != nullptr, getMountPoint()[0], m_io);
    if (m_ADFvolume) {
        //SHChangeNotify(SHCNE_MEDIAREMOVED, SHCNF_PATH, m_drive.c_str(), NULL);
        SHChangeNotify(SHCNE_DRIVEREMOVED, SHCNF_PATH, getMountPoint().c_str(), NULL);
        SHChangeNotify(SHCNE_DRIVEADD, SHCNF_PATH, getMountPoint().c_str(), NULL);
        //SHChangeNotify(SHCNE_MEDIAINSERTED, SHCNF_PATH, m_drive.c_str(), NULL);
    }
    m_tempUnmount = false;

    return m_ADFvolume != nullptr;
}

void MountedVolume::unmountFileSystem() {
    if (m_ADFvolume) {
        adfUnMount(m_ADFvolume);
        m_ADFvolume = nullptr;
    }
    m_amigaFS->setCurrentVolume(nullptr);
    m_IBMFS->setCurrentVolume(nullptr);
    setActiveFileSystem(nullptr);
    m_ADFdevice = nullptr;
    m_FatFS = nullptr;
    m_registry->setupDriveIcon(true, getMountPoint()[0], 2);
    m_registry->mountDismount(false, getMountPoint()[0], m_io);
    SHChangeNotify(SHCNE_MEDIAREMOVED, SHCNF_PATH, getMountPoint().c_str(), NULL);
}

