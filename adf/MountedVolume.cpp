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

#include "MountedVolume.h"
#include "adflib/src/adflib.h"
#include "adflib/src/adf_blk.h"
#include "amiga_sectors.h"
#include "dokaninterface.h"
#include "shellMenus.h"
#include <Shlobj.h>
#include "readwrite_floppybridge.h"
#include "MountedVolumes.h"

#pragma comment(lib,"Shell32.lib")


MountedVolume::MountedVolume(VolumeManager* manager, const std::wstring& mainEXE, SectorCacheEngine* io, const WCHAR driveLetter, const bool forceWriteProtect) :
	DokanFileSystemManager(driveLetter, forceWriteProtect, mainEXE), m_io(io), m_manager(manager) {
    m_registry = new ShellRegistery(mainEXE);
    m_amigaFS = new DokanFileSystemAmigaFS(this, manager->autoRename());
    m_amigaPFS3 = new DokanFileSystemAmigaPFS3(this, manager->autoRename());
    m_IBMFS = new DokanFileSystemFATFS(this);

}

MountedVolume::~MountedVolume() {
    if (m_amigaFS) delete m_amigaFS;
    if (m_IBMFS) delete m_IBMFS;
    if (m_registry) delete m_registry;
    if (m_amigaPFS3) delete m_amigaPFS3;
}

// Special version that doesnt fail if the file system is unknown - used for floppy disks only
ADF_RETCODE refreshAmigaVolume(struct AdfDevice* const dev) {
    struct AdfVolume* vol;
    struct AdfRootBlock root;
    char diskName[35];

    dev->cylinders = 80;
    dev->heads = 2;
    if (dev->devType == ADF_DEVTYPE_FLOPDD)
        dev->sectors = 11;
    else
        dev->sectors = 22;

    vol = (struct AdfVolume*)malloc(sizeof(struct AdfVolume));
    if (!vol) return ADF_RC_ERROR;

    vol->mounted = TRUE;
    vol->firstBlock = 0;
    vol->lastBlock = (int32_t)(dev->cylinders * dev->heads * dev->sectors - 1);
    vol->rootBlock = (vol->lastBlock + 1 - vol->firstBlock) / 2;
    vol->blockSize = 512;
    vol->dev = dev;

    if (adfReadRootBlock(vol, (uint32_t)vol->rootBlock, &root) == ADF_RC_OK) {
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
        return ADF_RC_ERROR;
    }
    dev->volList[0] = vol;
    dev->nVol = 1;

    return ADF_RC_OK;
}


void MountedVolume::restoreUnmountedDrive(bool restorePreviousSystem) {
    if (m_tempUnmount) {
        if (restorePreviousSystem) {
            if (m_ADFdevice) {
                if (m_ADFvolume)
                    if (m_io->isPhysicalDisk()) refreshAmigaVolume(m_ADFdevice);
                mountFileSystem(m_ADFdevice, m_partitionIndex, false);
            }
            if (m_FatFS) {
                mountFileSystem(m_FatFS, m_partitionIndex, false);
            }
        }
        else {
            // Start from fresh
            m_manager->unmountPhysicalFileSystems();
            m_ADFdevice = nullptr;
            m_ADFvolume = nullptr;
            m_FatFS = nullptr;
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

// Used to block writing to hybrid filesystem
bool MountedVolume::isForcedWriteProtect() {
    return DokanFileSystemManager::isForcedWriteProtect() || ((m_io) && (m_io->getSystemType() == SectorType::stHybrid));
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

// Shut down the file system
void MountedVolume::shutdownFS() {
    unmountFileSystem();
    if (m_manager) m_manager->refreshWindowTitle();
}


// Install bootblock for Amiga drives
bool MountedVolume::installAmigaBootBlock() {
    if (!getADFVolume()) return false;
    if (isWriteProtected()) return false;

    // 1024 bytes is required
    uint8_t* mem = (uint8_t*)malloc(1024);
    if (!mem) return false;

    memset(mem, 0, 1024);
    fetchBootBlockCode_AMIGA(adfDosFsIsFFS(getADFVolume()->fs.type), mem);

    // Nothing writes to where thr boot block is so it's safe to do this
    bool ok = adfVolInstallBootBlock(getADFVolume(), mem) == ADF_RC_OK;

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
    m_registry->setupDriveIcon(true, mountPoint[0], 2, m_io->isPhysicalDisk());
    DokanFileSystemManager::onMounted(mountPoint, dokanfileinfo);
}
void MountedVolume::onUnmounted(PDOKAN_FILE_INFO dokanfileinfo) {
    DokanFileSystemManager::onUnmounted(dokanfileinfo);
    m_registry->setupDriveIcon(false, getMountPoint()[0], 2, m_io->isPhysicalDisk());
}

uint32_t MountedVolume::getTotalTracks() {

    if (m_ADFdevice) return max(m_ADFdevice->cylinders * m_ADFdevice->heads, 80 * 2);
    if (m_io) {
        uint32_t t = m_io->totalNumTracks();
        if (t) return max(80 * 2, t);
    }
    if (m_FatFS) {
        uint32_t totalSectorsPerTrack = m_FatFS->fsize * m_FatFS->n_fats;
        if (!totalSectorsPerTrack) return 0;
        uint32_t t = (((m_FatFS->n_fatent+(totalSectorsPerTrack-1)) / totalSectorsPerTrack) + 1) / 2;
        return max(t, 80 * 2);
    }

    return 0;
}

// Mount a Fat12 device
bool MountedVolume::mountFileSystem(FATFS* ftFSDevice, uint32_t partitionIndex, bool showExplorer) {
    m_partitionIndex = partitionIndex;

    // Hook up FS_operations
    m_IBMFS->setCurrentVolume(ftFSDevice);
    setActiveFileSystem(m_IBMFS);
    m_FatFS = ftFSDevice;

    switch (m_io->getSystemType()) {
    case SectorType::stAtari:
    case SectorType::stHybrid:
        m_registry->setupDriveIcon(true, getMountPoint()[0], 3, m_io->isPhysicalDisk());
        break;

    default:
        m_registry->setupDriveIcon(true, getMountPoint()[0], 0, m_io->isPhysicalDisk());
        break;
    }
    m_registry->mountDismount(ftFSDevice != nullptr, getMountPoint()[0], m_io);
    if (m_FatFS) {
        //SHChangeNotify(SHCNE_MEDIAREMOVED, SHCNF_PATH, m_drive.c_str(), NULL);
        SHChangeNotify(SHCNE_DRIVEREMOVED, SHCNF_PATH, getMountPoint().c_str(), NULL);
        SHChangeNotify(SHCNE_DRIVEADD, SHCNF_PATH, getMountPoint().c_str(), NULL);
        //SHChangeNotify(SHCNE_MEDIAINSERTED, SHCNF_PATH, m_drive.c_str(), NULL);
    }
    m_tempUnmount = false;
#ifndef _DEBUG
    if (showExplorer && m_FatFS) ShellExecute(GetDesktopWindow(), L"explore", getMountPoint().c_str(), NULL, NULL, SW_SHOW);
#endif
    return m_FatFS != nullptr;
}

// Set if the system recognised the sector format even if it didnt understand the disk
void MountedVolume::setSystemRecognisedSectorFormat(bool wasRecognised) {
    setIsNONDos(!wasRecognised);
}


IPFS3* createPFS3FromVolume(AdfDevice* device, int partitionIndex, SectorCacheEngine* io, bool readOnly) {
    AdfRDSKblock rdskBlock;
    if (adfReadRDSKblock(device, &rdskBlock) != ADF_RC_OK) return nullptr;

    // Find partition
    int32_t next = rdskBlock.partitionList;
    struct AdfPARTblock part;
    part.blockSize = 0;

    int partToSearch = partitionIndex + 1;
    while (next && partToSearch) {
        partToSearch--;
        if (adfReadPARTblock(device, next, &part) != ADF_RC_OK) {
            part.blockSize = 0;
            break;
        }
        next = part.next;
    }

    if (!part.blockSize) return nullptr;

    // Lets try PFS3
    IPFS3::DriveInfo drive;

    drive.numHeads = rdskBlock.heads;
    drive.numCylinders = rdskBlock.cylinders;
    drive.cylBlocks = rdskBlock.cylBlocks;
    drive.sectorSize = rdskBlock.blockSize;

    IPFS3::PartitionInfo pinfo;
    pinfo.sectorsPerBlock = part.sectorsPerBlock;
    pinfo.blocksPerTrack = part.blocksPerTrack;
    pinfo.blockSize = part.blockSize;
    pinfo.lowCyl = part.lowCyl;
    pinfo.highCyl = part.highCyl;
    pinfo.mask = part.mask;
    pinfo.numBuffer = part.numBuffer;

    IPFS3* pfs3 = IPFS3::createInstance(drive, pinfo,
        [io](uint32_t physicalSector, uint32_t readSize, void* data)->bool {
            // READ SECTOR
            return io->readData(physicalSector, readSize, data);
        },
        [io](uint32_t physicalSector, uint32_t readSize, const void* data)->bool {
            // WRITE SECTOR
            return io->writeData(physicalSector, readSize, data);
        },
        [io](const std::string& message) {                
        }
    , readOnly);
    if (!pfs3->available()) {
        delete pfs3;
        return nullptr;
    }
    return pfs3;
}

bool MountedVolume::mountFileSystem(AdfDevice* adfDevice, uint32_t partitionIndex, bool showExplorer) {
    if (m_ADFvolume) {
        adfVolUnMount(m_ADFvolume);
        m_ADFvolume = nullptr;
    }
    if (m_pfs3) {
        delete m_pfs3;
        m_pfs3 = nullptr;
    }
    m_ADFdevice = adfDevice;
    if (partitionIndex == 0xFFFFFFFF) {
        m_partitionIndex = 0;
        m_ADFvolume = nullptr;
    }
    else {
        m_partitionIndex = partitionIndex;
        m_ADFvolume = m_ADFdevice ? adfVolMount(m_ADFdevice, partitionIndex, isForcedWriteProtect() ? AdfAccessMode::ADF_ACCESS_MODE_READONLY : AdfAccessMode::ADF_ACCESS_MODE_READWRITE) : nullptr;
        // Not a standard AMIGA file system. Try PFS3
        if (!m_ADFvolume) m_pfs3 = createPFS3FromVolume(m_ADFdevice, partitionIndex, m_io, isForcedWriteProtect());
    }   

    // Hook up ADF_operations
    m_amigaFS->setCurrentVolume(m_ADFvolume); 
    m_amigaPFS3->setCurrentVolume(m_pfs3);
    m_amigaFS->resetFileSystem();
    m_amigaPFS3->resetFileSystem();
    setSystemRecognisedSectorFormat((m_ADFvolume != nullptr) || (m_pfs3 != nullptr));
    if (m_pfs3) setActiveFileSystem(m_amigaPFS3); else
        setActiveFileSystem(adfDevice ? m_amigaFS : nullptr);
    m_registry->setupDriveIcon(true, getMountPoint()[0], 1, m_io->isPhysicalDisk());
    m_registry->mountDismount(true, getMountPoint()[0], m_io);
    SHChangeNotify(SHCNE_DRIVEREMOVED, SHCNF_PATH, getMountPoint().c_str(), NULL);
    SHChangeNotify(SHCNE_DRIVEADD, SHCNF_PATH, getMountPoint().c_str(), NULL);
    m_tempUnmount = false;

    if (showExplorer && (m_ADFvolume || m_pfs3)) ShellExecute(GetDesktopWindow(), L"explore", getMountPoint().c_str(), NULL, NULL, SW_SHOW);

    return (m_ADFvolume != nullptr) || (m_pfs3 != nullptr);
}

// Refresh the auto rename
void MountedVolume::refreshRenameSettings() {
    if (m_amigaFS) m_amigaFS->changeAutoRename(m_manager->autoRename());
    if (m_amigaPFS3) m_amigaPFS3->changeAutoRename(m_manager->autoRename());
}


void MountedVolume::unmountFileSystem() {
    std::wstring path = getMountPoint();
    if (m_ADFvolume) {
        adfVolUnMount(m_ADFvolume);
        m_ADFvolume = nullptr;
    }
    if (m_pfs3) {
        delete m_pfs3;
        m_pfs3 = nullptr;
    }
    m_amigaFS->setCurrentVolume(nullptr);
    m_amigaFS->resetFileSystem();
    m_amigaPFS3->setCurrentVolume(nullptr);
    m_amigaPFS3->resetFileSystem();
    m_IBMFS->setCurrentVolume(nullptr);
    setActiveFileSystem(nullptr);
    m_ADFdevice = nullptr;
    m_FatFS = nullptr;
    m_registry->setupDriveIcon(true, path[0], 2, m_io->isPhysicalDisk());
    m_registry->mountDismount(false, path[0], m_io);
    SHChangeNotify(SHCNE_MEDIAREMOVED, SHCNF_PATH, path.c_str(), NULL);
}

