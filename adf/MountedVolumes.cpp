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




#include "MountedVolumes.h"
#include "MountedVolume.h"
#include "psapi.h"
#include "readwrite_floppybridge.h"
#include "readwrite_dms.h"
#include "readwrite_file.h"
#include "adflib/src/adflib.h"
#include "adflib/src/adf_dev_driver.h"
#include "amiga_sectors.h"
#include "dlgFormat.h"
#include "dlgCopy.h"
#include "fatfs/source/ff.h"
#include "fatfs/source/diskio.h"
#include "resource.h"
#include "menu.h"
#include "SCPFile.h"
#include "dlgClean.h"
#include "DriveList.h"

#define TIMERID_MONITOR_FILESYS 1000
#define WM_DISKCHANGE (WM_USER + 1)
#define WM_COPYTOFILE (WM_USER + 2)

#pragma region CONTROL_INTERFACE
typedef struct {
    WCHAR letter[2];
    HWND windowFound;
} WindowSearch;

// Searches for an empty drive letter with the ideal one being supplied
WCHAR findEmptyLetter(const WCHAR letter) {
    DWORD drv = GetLogicalDrives();
    uint32_t index = (letter == L'?' ? 6 : ((uint32_t)(letter - L'A')) % 26);
    for (uint32_t i = 0; i < 26; i++) {
        // Is this drive in use?
        if ((drv & (1 << index)) == 0) {
            // No? Perfect
            return index + L'A';
        }
        index = (index + 1) % 26;
    }
    return letter;
}

// Search windows for one that uses the drive letter specified
BOOL CALLBACK windowSearchCallback(_In_ HWND hwnd, _In_ LPARAM lParam) {
    WCHAR tmpText[100];
    GetClassName(hwnd, tmpText, 100);
    if (wcscmp(tmpText, MESSAGEWINDOW_CLASS_NAME) == 0) {
        GetWindowText(hwnd, tmpText, 100);
        WCHAR* pos = wcsstr(tmpText, L"_");
        if (pos) {
            pos++;
            WindowSearch* s = (WindowSearch*)lParam;
            // See if the letter is in here
            if (wcsstr(pos, s->letter)) {
                s->windowFound = hwnd;
                return FALSE;
            }
        }
    }
    return TRUE;
}

// Search for the control window matching the drive letter supplied
HWND VolumeManager::FindControlWindowForDrive(WCHAR driveLetter) {
    WindowSearch search;
    search.letter[0] = driveLetter;
    search.letter[1] = '\0';
    search.windowFound = 0;
    EnumWindows(windowSearchCallback, (LPARAM)&search);
    return search.windowFound;
}

// Looks to see if the active foreground window is explorer and if so use it as a parent
HWND VolumeManager::FindPotentialExplorerParent() {
    // See if the currently active window is explorer
    HWND parent = GetForegroundWindow();
    if (parent) {
        DWORD pid;
        if (GetWindowThreadProcessId(parent, &pid)) {
            HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
            if (proc) {
                WCHAR exeName[MAX_PATH];
                if (GetProcessImageFileName(proc, exeName, MAX_PATH)) {
                    std::wstring name = exeName;
                    for (WCHAR& c : name) c = towupper(c);
                    if (wcsstr(name.c_str(), L"EXPLORER.EXE") == nullptr)
                        return 0;
                }
                CloseHandle(proc);
            }
        }
    }
    return parent;
}
#pragma endregion CONTROL_INTERFACE

    #pragma region FATFS

// I hate this being here
static SectorCacheEngine* fatfsSectorCache = nullptr;
void setFatFSSectorCache(SectorCacheEngine* _fatfsSectorCache) {
    fatfsSectorCache = _fatfsSectorCache;
}
DSTATUS disk_status(BYTE pdrv) {
    if (fatfsSectorCache && (pdrv == 0)) {
        if (!fatfsSectorCache->isDiskPresent()) return STA_NODISK;
        if (fatfsSectorCache->isDiskWriteProtected()) return STA_PROTECT;
        return 0;
    }
    return STA_NOINIT;
}
DSTATUS disk_initialize(BYTE pdrv) {
    if (fatfsSectorCache && (pdrv==0)) {
        if (!fatfsSectorCache->isDiskPresent()) return STA_NODISK;
        if (fatfsSectorCache->isDiskWriteProtected()) return STA_PROTECT;
        return 0;
    }
    return STA_NOINIT;
}
DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
    if (fatfsSectorCache && (pdrv == 0)) {
        if (!fatfsSectorCache->isDiskPresent()) return RES_NOTRDY;

        while (count) {
            if (!fatfsSectorCache->hybridReadData(sector, fatfsSectorCache->sectorSize(), buff))
                return RES_ERROR;
            count--;
            sector++;
            buff += fatfsSectorCache->hybridSectorSize();
        }
        return RES_OK;
    }
    return RES_PARERR;
}
DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    if (fatfsSectorCache && (pdrv == 0)) {
        if (!fatfsSectorCache->isDiskPresent()) return RES_NOTRDY;
        if (fatfsSectorCache->isDiskWriteProtected()) return RES_WRPRT;
        while (count) {
            if (!fatfsSectorCache->writeData(sector, fatfsSectorCache->sectorSize(), buff))
                return RES_ERROR;

            count--;
            sector++;
            buff += fatfsSectorCache->sectorSize();
        }
        return RES_OK;
    }
    return RES_PARERR;
}
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    if (fatfsSectorCache && (pdrv == 0)) {
        if (!fatfsSectorCache->isDiskPresent()) return RES_NOTRDY;
       
        switch (cmd) {
        case CTRL_SYNC: // Complete pending write process (needed at FF_FS_READONLY == 0) 
            if (!fatfsSectorCache->flushWriteCache()) return RES_ERROR;
            return RES_OK;

        case GET_SECTOR_COUNT: // Get media size (needed at FF_USE_MKFS == 1)
            *((DWORD*)buff) = fatfsSectorCache->hybridNumSectorsPerTrack() * fatfsSectorCache->hybridTotalNumTracks();
            return RES_OK;

        case GET_SECTOR_SIZE: // Get sector size (needed at FF_MAX_SS != FF_MIN_SS) 
            *((DWORD*)buff) = fatfsSectorCache->hybridSectorSize();
            return RES_OK;

        case GET_BLOCK_SIZE: // Get erase block size (needed at FF_USE_MKFS == 1) 
            *((DWORD*)buff) = 1;
            return RES_OK;
        }
    }
    return RES_PARERR;
}
DWORD get_fattime(void) {    
    tm stm;
    time_t t = time(0);
    localtime_s(&stm, & t);
    return (DWORD)(stm.tm_year - 80) << 25 | (DWORD)(stm.tm_mon + 1) << 21 | (DWORD)stm.tm_mday << 16 | (DWORD)stm.tm_hour << 11 | (DWORD)stm.tm_min << 5 | (DWORD)stm.tm_sec >> 1;
}
#pragma endregion

VolumeManager::VolumeManager(HINSTANCE hInstance, const std::wstring& mainExe, WCHAR firstDriveLetter, bool forceReadOnly) :
    m_window(hInstance, L"Booting"), m_mainExeFilename(mainExe), m_firstDriveLetter(firstDriveLetter), 
    m_currentSectorFormat(SectorType::stUnknown), m_forceReadOnly(forceReadOnly), m_hInstance(hInstance)  {
    DokanInit();

    AppConfig cfg;
    loadConfiguration(cfg);
    m_autoRename = cfg.autoRename;

    // Prepare the ADF library
    adfPrepNativeDriver();

    // Prepare FatFS
    fatfsSectorCache = nullptr;
}

VolumeManager::~VolumeManager()  {
    diskChanged(false, SectorType::stUnknown);
    DokanShutdown();
    for (MountedVolume* volume : m_volumes) delete volume;
    m_volumes.clear();
    adfEnvCleanUp();
}

// start mounting IBM volumes using m_volume[startPoint]
uint32_t VolumeManager::mountIBMVolumes(uint32_t startPoint) {
    if (!m_fatDevice) return startPoint;
    
    WCHAR letter = findEmptyLetter( m_firstDriveLetter + startPoint);

    if (startPoint >= m_volumes.size()) {
        // new volume required
        m_volumes.push_back(new MountedVolume(this, m_mainExeFilename, m_io, letter, m_forceReadOnly));
        m_volumes.back()->start();
        m_volumes.back()->setSystemRecognisedSectorFormat(true);
    }
    if (m_volumes[startPoint]->mountFileSystem(m_fatDevice, 0, m_triggerExplorer)) startPoint++;

    return startPoint;
}

// Mount the new amiga volumes
uint32_t VolumeManager::mountAmigaVolumes(uint32_t startPoint) {   
    WCHAR letter = findEmptyLetter(m_firstDriveLetter + startPoint);

    bool mountAsNDOS = m_adfDevice == nullptr;
    if (m_adfDevice && m_adfDevice->nVol == 0) mountAsNDOS = true;

    if (mountAsNDOS && (m_currentSectorFormat == SectorType::stAmiga)) {
        if (startPoint >= m_volumes.size()) {
            // new volume required
            m_volumes.push_back(new MountedVolume(this, m_mainExeFilename, m_io, letter, m_forceReadOnly));
            m_volumes.back()->start();
            m_volumes.back()->setSystemRecognisedSectorFormat(true);
        }
        if (m_volumes[startPoint]->mountFileSystem(m_adfDevice, 0xFFFFFFFF, m_triggerExplorer)) startPoint++;
    }
    else {
        if (!m_adfDevice) return startPoint;
        // Attempt to mount all drives
        for (int volumeNumber = 0; volumeNumber < m_adfDevice->nVol; volumeNumber++) {
            if (startPoint >= m_volumes.size()) {
                // new volume required
                m_volumes.push_back(new MountedVolume(this, m_mainExeFilename, m_io, letter, m_forceReadOnly));
                m_volumes.back()->start();
            }
            if (m_volumes[startPoint]->mountFileSystem(m_adfDevice, volumeNumber, m_triggerExplorer)) startPoint++;

            letter++;
            if (letter > 'Z') letter = 'A';
            letter = findEmptyLetter(letter);
        }
    }

    return startPoint;
}

// Start any volumes that aren't running
void VolumeManager::startVolumes() {
    // Try!
    for (MountedVolume* vol : m_volumes)
        vol->start();
}

// Actually unmount anything mounted
void VolumeManager::unmountPhysicalFileSystems() {
    for (MountedVolume* volume : m_volumes)
        volume->unmountFileSystem();

    if (m_adfDevice) {
        adfDevUnMount(m_adfDevice);
        m_adfDevice = nullptr;
    }

    if (m_fatDevice) {
        f_unmount(L"");
        free(m_fatDevice);
        m_fatDevice = nullptr;
    }
}

// Special version to call adfDevMount that monitors the cylinder count for weirdness
void VolumeManager::adfDevMountCylinders() {
    if (!m_adfDevice) return;

    ADF_RETCODE code = adfDevMount(m_adfDevice);
    if ((code != ADF_RC_OK) && ((m_adfDevice->devType == ADF_DEVTYPE_FLOPDD) || (m_adfDevice->devType == ADF_DEVTYPE_FLOPHD))) {
        while (m_adfDevice->cylinders > 80) {
            m_adfDevice->cylinders--;
            code = adfDevMount(m_adfDevice);
            if (code == ADF_RC_OK) return;
        }
    }
}



// Notification received that the current disk changed
void VolumeManager::diskChanged(bool diskInserted, SectorType diskFormat) {    
    unmountPhysicalFileSystems();

    if (!diskInserted) {        
        m_currentSectorFormat = SectorType::stUnknown;
        while (m_volumes.size() > 1) {            
            MountedVolume* v = m_volumes[m_volumes.size() - 1];
            m_volumes.erase(m_volumes.begin() + m_volumes.size() - 1);
            v->shutdownFS();

            m_threads.emplace_back(std::thread([v]() {               
                v->stop();
                delete v;
            }));
        }
        for (MountedVolume* volume : m_volumes)
            volume->setSystemRecognisedSectorFormat(false);
    }
    else {
        m_currentSectorFormat = diskFormat;
        uint32_t volumesNeeded = 1;
        for (MountedVolume* volume : m_volumes)
            volume->setSystemRecognisedSectorFormat(diskFormat != SectorType::stUnknown);

        // Create device based on what system was detected
        switch (diskFormat) {
            case SectorType::stAmiga:   
                m_adfDevice = adfDevOpenWithDriver(DISKFLASHBACK_AMIGA_DRIVER, (char*)m_io, m_forceReadOnly ? AdfAccessMode::ADF_ACCESS_MODE_READONLY : AdfAccessMode::ADF_ACCESS_MODE_READWRITE);
                if (m_adfDevice) adfDevMountCylinders();
                break;
            case SectorType::stHybrid:
                m_adfDevice = adfDevOpenWithDriver(DISKFLASHBACK_AMIGA_DRIVER, (char*)m_io, m_forceReadOnly ? AdfAccessMode::ADF_ACCESS_MODE_READONLY : AdfAccessMode::ADF_ACCESS_MODE_READWRITE); 
                if (m_adfDevice) adfDevMountCylinders();
                m_fatDevice = (FATFS*)malloc(sizeof(FATFS));
                if (m_fatDevice) {
                    memset(m_fatDevice, 0, sizeof(FATFS));
                    if (f_mount(m_fatDevice, L"", 1) != FR_OK) {
                        free(m_fatDevice);
                        m_fatDevice = nullptr;
                    }
                }
                if (m_adfDevice && !m_fatDevice) diskFormat = SectorType::stAmiga; else
                    if (!m_adfDevice && m_fatDevice) diskFormat = SectorType::stIBM; else
                        if (!m_adfDevice && !m_fatDevice) diskFormat = SectorType::stIBM; else
                            volumesNeeded = 2;
                break;
            case SectorType::stAtari:
            case SectorType::stIBM:
                m_fatDevice = (FATFS*)malloc(sizeof(FATFS));
                if (m_fatDevice) {
                    memset(m_fatDevice, 0, sizeof(FATFS));
                    if (f_mount(m_fatDevice, L"", 1) != FR_OK) {
                        free(m_fatDevice);
                        m_fatDevice = nullptr;
                    }
                }
                break;
        }
        // Remove non-needed file systems
        while (m_volumes.size() > volumesNeeded) {
            MountedVolume* v = m_volumes[m_volumes.size() - 1];
            m_volumes.erase(m_volumes.begin() + m_volumes.size() - 1);
            v->shutdownFS();
            m_threads.emplace_back(std::thread([v]() {
                v->stop();
                delete v;
                }));
        }

        // Mount all drives using the new file system
        mountIBMVolumes(mountAmigaVolumes(0));
        m_triggerExplorer = false;
    }
    // Start the physical drive letters
    startVolumes();

    // Update the window title based on what's left
    refreshWindowTitle();
}

void VolumeManager::refreshWindowTitle() {
    if (m_ejecting) return;

    if (m_io) {
        std::wstring title = m_mountMode;
        if (m_io->isPhysicalDisk()) {
            title += L"."+std::to_wstring(m_io->id());
        }
        title += L"_";

        for (MountedVolume* volume : m_volumes) 
            if (volume->getMountPoint().substr(0, 1) != L"?")
                title += volume->getMountPoint().substr(0, 1);
        m_window.setWindowTitle(title);
    }
}

// triggers the re-mounting of a disk
void VolumeManager::triggerRemount() {
    if (m_io)
        PostMessage(m_window.hwnd(), WM_DISKCHANGE, m_io->isDiskPresent() ? 1 : 0, (LPARAM)m_io->getSystemType());
}

// Mount a raw volume
bool VolumeManager::mountRaw(const std::wstring& physicalDrive, bool readOnly) {
    CDriveList d;
    d.refreshList();
    CDriveList::DeviceList list = d.getDevices();
    CDriveAccess* drive = nullptr;

    // find the device
    for (CDriveList::CDevice& dev : list)
        if (dev.deviceName == physicalDrive) {
            // Drive found
            drive = d.connectToDrive(dev, readOnly);
            break;
        }

    if (!drive) {
        COPYDATASTRUCT str;
        str.lpData = (void*)physicalDrive.data();
        str.cbData = physicalDrive.length() * 2;
        str.dwData = COPYDATA_MOUNTRAW_FAILED;
        SendMessage(FindWindow(MESSAGEWINDOW_CLASS_NAME, APP_TITLE), WM_COPYDATA, (WPARAM)m_window.hwnd(), (LPARAM)&str);
        return false;
    }
    else {
        m_io = drive;
        m_mountMode = COMMANDLINE_MOUNTRAW;
    }
    return true;
}

// Start a file mount
bool VolumeManager::mountFile(const std::wstring& filename) {
    // Open the file
    HANDLE fle = CreateFile(filename.c_str(), GENERIC_READ | (m_forceReadOnly ? 0 : GENERIC_WRITE), 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, 0);
    if ((fle == INVALID_HANDLE_VALUE) && (!m_forceReadOnly)) {
        // Try read only
        fle = CreateFile(filename.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, 0);
        m_forceReadOnly = true;
    }
    if (fle == INVALID_HANDLE_VALUE) return false;

    // Read the first 4 bytes to help identify the type of file
    char buffer[5] = { 0 };
    DWORD read = 0;
    if ((!ReadFile(fle, buffer, 4, &read, NULL)) || (read != 4)) {
        CloseHandle(fle);
        return false;
    }

    m_mountMode = COMMANDLINE_MOUNTFILE;

    // DMS file?
    if (strcmp(buffer, "DMS!") == 0) {
        SetFilePointer(fle, 0, NULL, FILE_BEGIN);
        m_io = new SectorRW_DMS(fle);
        if (!m_io->available()) {
            CloseHandle(fle);
            return false;
        }        
        fatfsSectorCache = m_io;
        return true;
    }
    else {
        buffer[3] = '\0';
        if (strcmp(buffer, "SCP") == 0) {
            // Assume its some kind of image file
            m_io = new SCPFile(fle, [this](bool diskInserted, SectorType diskFormat) {
                // push this in the main thread incase its not!
                triggerRemount();
             });
            if (!m_io->available()) return false;
            fatfsSectorCache = m_io;
            return true;
        }
        else {

            // Assume its some kind of image file
            m_io = new SectorRW_File(filename, fle);
            if (!m_io->available()) return false;
            fatfsSectorCache = m_io;
            return true;
        }
    }

    return false;
}

// Start a drive mount
bool VolumeManager::mountDrive(const std::wstring& floppyProfile) {
    std::string profile;
    wideToAnsi(floppyProfile, profile);   

    SectorRW_FloppyBridge* b = new SectorRW_FloppyBridge(profile, [this](bool diskInserted, SectorType diskFormat) {
        // push this in the main thread incase its not!
        triggerRemount();
    });

    if (!b->available()) {
        delete b;
        return false;
    }

    m_io = b;
    setFatFSSectorCache(m_io);
    m_mountMode = COMMANDLINE_MOUNTDRIVE;
    return true;
}

// clean up threads
void VolumeManager::cleanThreads() {
    auto it = m_threads.begin();
    while (it < m_threads.end()) {
        if (it->joinable()) {
            it->join();
            it = m_threads.erase(it);
        }
        else it++;
    }
}

// General cleanup and monitoring to see if file systems have dropped out
void VolumeManager::checkRunningFileSystems() {
    cleanThreads();

    // See if the process is done for
    bool running = m_threads.size() > 0;
    if (!running) for (const MountedVolume* volume : m_volumes) running |= volume->isRunning();

    // Something's gone wrong in floppybridge?
    if ((m_io) && (m_io->isPhysicalDisk()) && (!m_io->available())) {
        KillTimer(m_window.hwnd(), TIMERID_MONITOR_FILESYS);
        PostQuitMessage(0);
    }


    if (!running) {
        KillTimer(m_window.hwnd(), TIMERID_MONITOR_FILESYS);
        PostQuitMessage(0);
    }
}

// Searches active mounted volumes and looks for one with a drive letter and returns it
MountedVolume* VolumeManager::findVolumeFromDriveLetter(const WCHAR driveLetter) {
    MountedVolume* volumeFound = nullptr;
    for (MountedVolume* volume : m_volumes)
        if ((volume) && (volume->getMountPoint()[0] == driveLetter)) 
            return volume;
        
    return nullptr;
}

// Handle a request to copy a file to the active drive
LRESULT VolumeManager::handleCopyToDiskRequest(const std::wstring message) {
    const HWND potentialParent = VolumeManager::FindPotentialExplorerParent();

    // Extract the letter and string           
    const WCHAR driveLetter = message[0];
    std::wstring filename = message.substr(1);

    MountedVolume* volumeFound = findVolumeFromDriveLetter(driveLetter);
    if (!volumeFound) return MESSAGE_RESPONSE_DRIVENOTFOUND;

    m_threads.emplace_back(std::thread([this, filename, potentialParent, volumeFound]() {        
        DialogCOPY dlg(m_hInstance, potentialParent, m_io, volumeFound, filename);
        return dlg.doModal(true);
    }));

    return MESSAGE_RESPONSE_OK;
}

// Handle other remote requests
LRESULT VolumeManager::handleRemoteRequest(MountedVolume* volume, const WPARAM commandID, HWND parentWindow) {
    switch (commandID) {

    case REMOTECTRL_CLEAN:
        m_threads.emplace_back(std::thread([this, parentWindow, volume]() {
            DialogCLEAN dlg(m_hInstance, parentWindow, m_io, volume);
            return dlg.doModal();
        }));
        return MESSAGE_RESPONSE_OK;

    case REMOTECTRL_FORMAT:
        m_threads.emplace_back(std::thread([this, parentWindow, volume]() {
            // This doesnt modify whats passed unless it returns TRUE
            DialogFORMAT dlg(m_hInstance, parentWindow, m_io, volume);
            return dlg.doModal();
        }));
        return MESSAGE_RESPONSE_OK;

    case REMOTECTRL_INSTALLBB:
        m_threads.emplace_back(std::thread([this, parentWindow, volume]() {
            std::wstring msg = L"Install bootblock on drive " + volume->getMountPoint().substr(0, 2) + L"?";
            if (MessageBox(parentWindow, msg.c_str(), L"Install Bootblock?", MB_YESNO | MB_ICONQUESTION) == IDYES)
                if (!volume->installAmigaBootBlock())
                    MessageBox(parentWindow, L"An error occured writing the boot block", L"Boot Block Error", MB_OK | MB_ICONEXCLAMATION);
            }));
        return MESSAGE_RESPONSE_OK;

    case REMOTECTRL_COPYTOADF:
        m_threads.emplace_back(std::thread([parentWindow, this, volume]() {
            DialogCOPY dlg(m_hInstance, parentWindow, m_io, volume);
            return dlg.doModal(volume->isDriveRecognised());
            }));
        return MESSAGE_RESPONSE_OK;

    case REMOTECTRL_EJECT_SILENT: 
    case REMOTECTRL_EJECT: {
        bool filesOpen = false;
        bool locked = false;
        filesOpen = volume->isDriveInUse();
        locked = volume->isDriveLocked();


        if (!(filesOpen||locked)) {
            for (auto& f : m_volumes) {
                filesOpen |= f->isDriveInUse();
                locked |= f->isDriveLocked();
                if (filesOpen || locked) {
                    volume = f;
                    break;
                }
            }
        }

        if (filesOpen || locked) {
            m_threads.emplace_back(std::thread([this, volume, parentWindow, filesOpen, locked]() {
                std::wstring msg = L"Cannot eject drive " + volume->getMountPoint().substr(0, 2) + L" - " + (locked ? L"the drive is busy" : L"files are currently open");
                MessageBox(parentWindow, msg.c_str(), L"Eject", MB_OK | MB_ICONSTOP);
                }));
        }
        else {
            m_ejecting = true;
            // Prevent it being auto-started next time
            if (commandID != REMOTECTRL_EJECT_SILENT)
                if (volume->isPhysicalDevice()) 
                    SendMessage(FindWindow(MESSAGEWINDOW_CLASS_NAME, APP_TITLE), WM_PHYSICAL_EJECT, 0, 0);
            m_window.setWindowTitle(L"");

            std::wstring title = m_mountMode;
            if (m_io->isPhysicalDisk()) title += L"." + std::to_wstring(m_io->id());
            title += L"_";
            for (auto& f : m_volumes) f->unmountFileSystem();

            m_io->flushWriteCache();
            m_io->quickClose();
            for (auto& f : m_volumes) f->stop();
            TerminateProcess(GetCurrentProcess(), 0);  // dirty but fast
        }
        return MESSAGE_RESPONSE_OK;
    }

    default:
        return MESSAGE_RESPONSE_FAILED;
    }
}

// Actually run the drive
bool VolumeManager::run(bool triggerExplorer) {
    m_triggerExplorer = triggerExplorer;
    if (!m_io) return false;
    if (m_mountMode.empty()) return false;
  
    // Handle TIMER events - for monitoring the filesystem for termination
    m_window.setMessageHandler(WM_TIMER, [this](WPARAM timerID, LPARAM lpUser) -> LRESULT {
        if (timerID == TIMERID_MONITOR_FILESYS) {
            checkRunningFileSystems();            
            return 0;
        }
        return DefWindowProc(m_window.hwnd(), WM_TIMER, timerID, lpUser);
    });

    // Copy Data us ysed to send a file name for copy to disk
    m_window.setMessageHandler(WM_COPYDATA, [this](WPARAM window, LPARAM param) -> LRESULT {
        COPYDATASTRUCT* cp = (COPYDATASTRUCT*)param;
        if (!cp) return MESSAGE_RESPONSE_FAILED;

        if (cp->dwData == REMOTECTRL_COPYTODISK) {
            if (cp->cbData < 6) return MESSAGE_RESPONSE_BADFORMAT;
            if (cp->cbData > (MAX_PATH + 2) * 2) return MESSAGE_RESPONSE_BADFORMAT;

            // Copy to a string
            std::wstring request;
            request.resize(cp->cbData / 2);
            memcpy_s(&request[0], request.length() * 2, cp->lpData, cp->cbData);

            return handleCopyToDiskRequest(request);
        }

        return MESSAGE_RESPONSE_FAILED;
    });

    m_window.setMessageHandler(WM_AUTORENAME, [this](WPARAM commandID, LPARAM param) -> LRESULT {
        m_autoRename = param == 1;
        for (MountedVolume* vol : m_volumes)
            vol->refreshRenameSettings();
        return 0;
    });

    // Handle remote control messages
    m_window.setMessageHandler(WM_USER, [this](WPARAM commandID, LPARAM param) -> LRESULT {
        MountedVolume* volume = findVolumeFromDriveLetter((WCHAR)param);
        if (!volume) return MESSAGE_RESPONSE_DRIVENOTFOUND;
        return handleRemoteRequest(volume, commandID, VolumeManager::FindPotentialExplorerParent());
    });

    // Receive a notification of disk change
    m_window.setMessageHandler(WM_DISKCHANGE, [this](WPARAM wParam, LPARAM lParam) -> LRESULT {
        diskChanged((wParam == 1), (SectorType)lParam);
        return 0;
    });

    // Receive a notification of disk change
    m_window.setMessageHandler(WM_COPYTOFILE, [this](WPARAM wParam, LPARAM lParam) -> LRESULT {
        if (m_volumes.size() < 1) return 0;
        HWND parentWindow = GetDesktopWindow();
        MountedVolume* volume = m_volumes[0];
        // Fake it 'till we make it
        handleRemoteRequest(volume, REMOTECTRL_COPYTOADF, GetDesktopWindow());
        return 0;       
    });

    // Allow these messages from less privilaged applications
    ChangeWindowMessageFilterEx(m_window.hwnd(), WM_COPYDATA, MSGFLT_ALLOW, NULL);
    ChangeWindowMessageFilterEx(m_window.hwnd(), WM_USER, MSGFLT_ALLOW, NULL);
    ChangeWindowMessageFilterEx(m_window.hwnd(), WM_AUTORENAME, MSGFLT_ALLOW, NULL);
    ChangeWindowMessageFilterEx(m_window.hwnd(), WM_COPYTOFILE, MSGFLT_ALLOW, NULL);


    // Not quite Highlander, as there There must always be one (at least one!)
    m_volumes.push_back(new MountedVolume(this, m_mainExeFilename, m_io, findEmptyLetter(m_firstDriveLetter), m_forceReadOnly));
    startVolumes();

    // Start a timer to monitor things
    SetTimer(m_window.hwnd(), TIMERID_MONITOR_FILESYS, 200, NULL);

    // Mount the drives
    triggerRemount();

    MSG msg;
    BOOL bRet;
    while ((bRet = GetMessage(&msg, NULL, 0, 0)) != 0) {
        if (bRet == -1) break; else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    // Stop everything
    m_io->flushWriteCache();
    unmountPhysicalFileSystems();
    KillTimer(m_window.hwnd(), TIMERID_MONITOR_FILESYS);
    for (MountedVolume* volume : m_volumes) volume->stop();

    // end
    return true;
}