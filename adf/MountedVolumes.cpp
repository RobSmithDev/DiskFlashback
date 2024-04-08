
#include "MountedVolumes.h"
#include "MountedVolume.h"
#include "psapi.h"
#include "readwrite_floppybridge.h"
#include "readwrite_dms.h"
#include "readwrite_file.h"
#include "adflib/src/adflib.h"
#include "amiga_sectors.h"
#include "dlgFormat.h"
#include "dlgCopy.h"

#define TIMERID_MONITOR_FILESYS 1000
#define WM_DISKCHANGE (WM_USER + 1)

#pragma region CONTROL_INTERFACE
typedef struct {
    WCHAR letter[2];
    HWND windowFound;
} WindowSearch;

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

#pragma region ADFLIB
// ADFLib Native Functions
RETCODE adfInitDevice(struct AdfDevice* const dev, const char* const name, const BOOL ro) {
    // it's a *terrible* hack, but we're using the name pointer to point back to the SectorCacheEngine class
    SectorCacheEngine* cache = (SectorCacheEngine*)name;
    if (!cache) return RC_ERROR;
    dev->size = cache->getDiskDataSize();
    dev->nativeDev = (void*)cache;

    return RC_OK;
}
RETCODE adfReleaseDevice(struct AdfDevice* const dev) {
    dev->nativeDev = nullptr;
    return RC_OK;
}
RETCODE adfNativeReadSector(struct AdfDevice* const dev, const uint32_t n, const unsigned size, uint8_t* const buf) {
    SectorCacheEngine* d = (SectorCacheEngine*)dev->nativeDev;
    return d->readData(n, size, buf) ? RC_OK : RC_ERROR;
}
RETCODE adfNativeWriteSector(struct AdfDevice* const dev, const uint32_t n, const unsigned size, const uint8_t* const buf) {
    SectorCacheEngine* d = (SectorCacheEngine*)dev->nativeDev;
    return d->writeData(n, size, buf) ? RC_OK : RC_ERROR;
}
BOOL adfIsDevNative(const char* const devName) {
    return true;
}
void Warning(char* msg) {
#ifdef _DEBUG
    fprintf(stderr, "Warning <%s>\n", msg);
#endif
}
void Error(char* msg) {
#ifdef _DEBUG
    fprintf(stderr, "Error <%s>\n", msg);
#endif
    // exit(1);
}
void Verbose(char* msg) {
#ifdef _DEBUG
    fprintf(stderr, "Verbose <%s>\n", msg);
#endif
}
#pragma endregion ADFLIB

// Full path to main exe name (this program)
const std::wstring m_mainExeFilename;

VolumeManager::VolumeManager(HINSTANCE hInstance, const std::wstring& mainExe, WCHAR firstDriveLetter, bool forceReadOnly) :
    m_window(hInstance, L"Booting"), m_mainExeFilename(mainExe), m_firstDriveLetter(firstDriveLetter), 
    m_currentSectorFormat(SectorType::stUnknown), m_forceReadOnly(forceReadOnly), m_hInstance(hInstance)  {
    DokanInit();

    // Prepare the ADF library
    adfEnvInitDefault();
    adfSetEnvFct((AdfLogFct)Error, (AdfLogFct)Warning, (AdfLogFct)Verbose, NULL);
    struct AdfNativeFunctions native;
    native.adfInitDevice = adfInitDevice;
    native.adfReleaseDevice = adfReleaseDevice;
    native.adfNativeReadSector = adfNativeReadSector;
    native.adfNativeWriteSector = adfNativeWriteSector;
    native.adfIsDevNative = adfIsDevNative;
    adfSetNative(&native);
}

VolumeManager::~VolumeManager()  {
    DokanShutdown();
    if (m_adfDevice) adfUnMountDev(m_adfDevice);
    adfEnvCleanUp();
}

// start mounting IBM volumes using m_volume[startPoint]
uint32_t VolumeManager::mountIBMVolumes(uint32_t startPoint) {
    // TODO
    return 0;
}


// Mount the new amiga volumes
uint32_t VolumeManager::mountAmigaVolumes(uint32_t startPoint) {
    if (!m_adfDevice) return 0;

    WCHAR letter = m_firstDriveLetter;
    uint32_t fileSystems = 0;
    
    // Attempt to mount all drives
    for (int volumeNumber = 0; volumeNumber < m_adfDevice->nVol; volumeNumber++) {
        if (startPoint + m_volumes.size() <= volumeNumber) {
            // new volume required
            m_volumes.push_back(new MountedVolume(this, m_mainExeFilename, m_io, letter, m_forceReadOnly));
        }

        if (m_volumes[startPoint + volumeNumber]->mountFileSystem(m_adfDevice, volumeNumber)) {
            if (letter != L'?') letter++;
            fileSystems++;
        }
    }
    return fileSystems;
}

// Start any volumes that aren't running
void VolumeManager::startVolumes() {
    // Try!
    for (MountedVolume* vol : m_volumes)
        vol->start();
}

// Notification received that the current disk changed
void VolumeManager::diskChanged(bool diskInserted, SectorType diskFormat) {    
    if (!diskInserted) {
        for (MountedVolume* volume : m_volumes)
            volume->unmountFileSystem();

        if (m_adfDevice) adfUnMountDev(m_adfDevice);
        m_adfDevice = nullptr;
    }
    else {
        // Create device based on what system was detected
        switch (diskFormat) {
            case SectorType::stAmiga:
                m_adfDevice = adfMountDev((char*)m_io, m_forceReadOnly);
                if (!m_adfDevice) diskFormat = SectorType::stUnknown;                     
                break;
            case SectorType::stHybrid:
                m_adfDevice = adfMountDev((char*)m_io, m_forceReadOnly);
                if (!m_adfDevice) diskFormat = SectorType::stUnknown;                 
                break;
        }

        // Mount all drives using the new file system
        mountIBMVolumes(mountAmigaVolumes(0));

        // Start the physical drive letters
        startVolumes();

        // Update the window title based on what's left
        std::wstring title = m_mountMode + std::to_wstring(m_io->id()) + L"_";

        for (MountedVolume* volume : m_volumes) title += volume->getMountPoint().substr(0, 1);
        m_window.setWindowTitle(title);
    }
}

// triggers the re-mounting of a disk
void VolumeManager::triggerRemount() {
    if (m_io)
        PostMessage(m_window.hwnd(), WM_DISKCHANGE, m_io->isDiskPresent() ? 1 : 0, (LPARAM)m_io->getSystemType());
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
        return true;
    }
    else {
        // Assume its some kind of image file
        m_io = new SectorRW_File(filename, fle);
        if (!m_io->available()) {
            CloseHandle(fle);
            return false;
        }
        return true;
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
        return dlg.doModal();
    }));

    return MESSAGE_RESPONSE_OK;
}

// Handle other remote requests
LRESULT VolumeManager::handleRemoteRequest(MountedVolume* volume, const WPARAM commandID, HWND parentWindow) {
    switch (commandID) {

    case REMOTECTRL_RELEASE:
        //fs->releaseDrive();
        return 0;

    case REMOTECTRL_RESTORE:
        //fs->restoreDrive();
        return 0;

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
            return dlg.doModal();
            }));
        return MESSAGE_RESPONSE_OK;

    case REMOTECTRL_EJECT: {
        bool filesOpen = volume->isDriveInUse();
        bool locked = volume->isDriveLocked();
        if (filesOpen || locked) {
            m_threads.emplace_back(std::thread([this, volume, parentWindow, filesOpen, locked]() {
                std::wstring msg = L"Cannot eject drive " + volume->getMountPoint().substr(0, 2) + L" - " + (locked ? L"the drive is busy" : L"files are currently open");
                MessageBox(parentWindow, msg.c_str(), L"Eject", MB_OK | MB_ICONSTOP);
                }));
        }
        else {
            volume->stop();
        }
        return MESSAGE_RESPONSE_OK;
    }

    default:
        return MESSAGE_RESPONSE_FAILED;
    }
}

// Actually run the drive
bool VolumeManager::run() {
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

    // Start a timer to monitor things
    SetTimer(m_window.hwnd(), TIMERID_MONITOR_FILESYS, 200, NULL);

    // Mount the drives
    triggerRemount();

    MSG msg;
    BOOL bRet;
    while ((bRet = GetMessage(&msg, NULL, 0, 0)) != 0) {
        if (bRet == -1) {
            KillTimer(m_window.hwnd(), TIMERID_MONITOR_FILESYS);
            for (MountedVolume* volume : m_volumes) volume->stop();
            break;
        }
        else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return true;
}