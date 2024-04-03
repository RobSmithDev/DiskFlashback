
#include <Windows.h>
#include <string>
#include <vector>
#include <ShlObj.h>

#define REMOTECTRL_RELEASE          1
#define REMOTECTRL_RESTORE          2
#define REMOTECTRL_FORMAT           3
#define REMOTECTRL_INSTALLBB        4
#define REMOTECTRL_COPYTODISK       5
#define REMOTECTRL_COPYTOADF        6
#define REMOTECTRL_EJECT            7

#include "adf.h"
#include "adflib/src/adflib.h"
#include "adf_operations.h"
#include <errno.h>
#include "sectorCache.h"
#include "readwrite_file.h"
#include "readwrite_dms.h"
#include "readwrite_floppybridge.h"
#include "SignalWnd.h"
#include "dlgFormat.h"
#include "psapi.h"
#include "dlgCopy.h"

//#pragma comment(lib, "")

// Active threads we're keeping track of
std::vector<std::thread> m_threads;

// Device open
struct AdfDevice* adfFile = nullptr;

// File systems currently open
std::vector<fs*> dokan_fs;


#define TIMERID_MONITOR_FILESYS 1000

typedef struct  {
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
HWND findControlWindowForDrive(WCHAR driveLetter) {
    WindowSearch search;
    search.letter[0] = driveLetter;
    search.letter[1] = '\0';
    search.windowFound = 0;
    EnumWindows(windowSearchCallback, (LPARAM)&search);
    return search.windowFound;
}

// Looks to see if the active foreground window is explorer and if so use it as a parent
HWND findPotentialExplorerParent() {
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


// ADFLib Warning
void Warning(char* msg) {
#ifdef _DEBUG
    fprintf(stderr,"Warning <%s>\n",msg);
#endif
}

// ADFLib Errors
void Error(char* msg) {
#ifdef _DEBUG
    fprintf(stderr,"Error <%s>\n",msg);
#endif
   // exit(1);
}

// ADFLib Verbose Logging
void Verbose(char* msg) {
#ifdef _DEBUG
    fprintf(stderr,"Verbose <%s>\n",msg);
#endif
}

// ADFLib Native Functions
RETCODE adfInitDevice(struct AdfDevice* const dev, const char* const name, const BOOL ro) {    
    if (!name) return RC_ERROR;
    if (strlen(name) < 2) return RC_ERROR;

    SectorCacheEngine* d = nullptr;
    HANDLE fle;

    switch (name[0]) {
    case '1': // FILE Mode
        fle = CreateFileA(&name[1], GENERIC_READ | (ro ? 0 : GENERIC_WRITE), 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, 0);
        if (fle == INVALID_HANDLE_VALUE) {
            int err = GetLastError();
            return RC_ERROR;
        }

        {
            char buffer[4] = { 0 };
            DWORD read = 0;
            if (!ReadFile(fle, buffer, 4, &read, NULL)) return RC_ERROR;
            if (read != 4) return RC_ERROR;

            if ((buffer[0] = 'D') && (buffer[1] = 'M') && (buffer[2] = 'S') && (buffer[3] = '!')) {
                SetFilePointer(fle, 0, NULL, FILE_BEGIN);
                d = new SectorRW_DMS(fle);
            }
            else {
                // Default the cache to allow a entire HD floppy disk to get into the cache. 
                d = new SectorRW_File(512 * 84 * 2 * 2 * 11, fle);
            }
        }
        if (!d) {
            CloseHandle(fle);
            return RC_ERROR;
        }

        break;

    case '2': // BRIDGE Mode
        d = new SectorRW_FloppyBridge(&name[1], [](bool diskInserted) {
            for (fs* fs : dokan_fs) {
                if (diskInserted) fs->remountVolume(); else fs->unmountVolume();
            }
        });
        break;

    default:
        // Unknown mode
        return RC_ERROR;
    }

    if (!d->available()) {
        delete d;
        return RC_ERROR;
    }

    dev->size = d->getDiskDataSize();
    dev->nativeDev = (void*)d;

    return RC_OK;
}
RETCODE adfReleaseDevice(struct AdfDevice* const dev) {
    SectorCacheEngine* d = (SectorCacheEngine*)dev->nativeDev;
    if (d) {
        delete d;
        dev->nativeDev = nullptr;
    }
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
    // If disk starts with DOS then its not a native device
    return true;
}

// Setup
void prepADFLib() {
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

// Request to mount all found volumes
void mountVolumes(WCHAR firstLetter, bool forceReadOnly) {
    if (!adfFile) return;

    // Attempt to mount all drives
    for (int volumeNumber = 0; volumeNumber < adfFile->nVol; volumeNumber++) {
        AdfVolume* vol = adfMount(adfFile, volumeNumber, forceReadOnly);
        dokan_fs.push_back(new fs(adfFile, vol, volumeNumber, firstLetter, forceReadOnly));
        firstLetter++;
    }
}

// clean up threads
void cleanThreads() {
    auto it = m_threads.begin();
    while (it < m_threads.end()) {
        if (it->joinable()) {
            it->join();
            it = m_threads.erase(it);
        }
        else it++;
    }
}

// Main loop for the file system
void runMountedVolumes(HINSTANCE hInstance, const std::wstring mode, SectorCacheEngine* io) {
    std::wstring title = std::wstring(mode) + std::to_wstring(io->id()) + L"_";
    for (auto& fs : dokan_fs) {
        fs->start();
        title += fs->driveLetter().substr(0, 1);
    }

    // For other processess to control this one
    CMessageWindow wnd(hInstance, title);

    // Handle TIMER events - for monitoring the filesystem for termination
    wnd.setMessageHandler(WM_TIMER, [&wnd](WPARAM timerID, LPARAM lpUser) -> LRESULT {
        if (timerID == TIMERID_MONITOR_FILESYS) {
            // Clean up finished threads
            cleanThreads();

            // See if the process is done for
            bool running = m_threads.size();
            if (!running) for (auto& fs : dokan_fs) running |= fs->isRunning();
            if (!running) {
                KillTimer(wnd.hwnd(), TIMERID_MONITOR_FILESYS);
                PostQuitMessage(0);
            }
            return 0;
        }
        return DefWindowProc(wnd.hwnd(), WM_TIMER, timerID, lpUser);
    });

    wnd.setMessageHandler(WM_COPYDATA, [&wnd, hInstance, io](WPARAM window, LPARAM param) -> LRESULT {
        COPYDATASTRUCT* cp = (COPYDATASTRUCT*)param;
        if (!cp) return 0;
        if (cp->dwData == REMOTECTRL_COPYTODISK) {
            if (cp->cbData < 6) return 0;
            if (cp->cbData > (MAX_PATH+2)*2) return 0;
            HWND potentialParent = findPotentialExplorerParent();

            // Extract the letter and string
            const WCHAR* string = (WCHAR*)cp->lpData;
            std::wstring filename;
            WCHAR letter = *string; string++;
            filename.resize((cp->cbData / 2) - 1);
            memcpy_s(&filename[0], filename.length() * 2, string, cp->cbData - 2);    

            fs* fs = nullptr;
            for (auto& fsSearch : dokan_fs)
                if ((fsSearch) && (fsSearch->driveLetter()[0] == letter)) {
                    fs = fsSearch;
                    break;
                }
            if (!fs) return 0;

            m_threads.emplace_back(std::thread([hInstance, potentialParent, io, fs, filename]() {
                DialogCOPY dlg(hInstance, potentialParent, io, fs, filename);
                return dlg.doModal();
            }));
        }
        return 0;
     });


    // Handle remote control messages
    wnd.setMessageHandler(WM_USER, [&wnd, hInstance, io](WPARAM commandID, LPARAM param) -> LRESULT {
        fs* fs = nullptr;
        for (auto& fsSearch : dokan_fs)
            if ((fsSearch) && (fsSearch->driveLetter()[0] == (WCHAR)param)) {
                fs = fsSearch;
                break;
            }
        if (!fs) return 0;
        HWND potentialParent = findPotentialExplorerParent();

        switch (commandID) {

        case REMOTECTRL_RELEASE:
            fs->releaseDrive();
            return 0;

        case REMOTECTRL_RESTORE:
            fs->restoreDrive();
            return 0;

        case REMOTECTRL_FORMAT: 
            m_threads.emplace_back(std::thread([hInstance, potentialParent, io, fs]() {
                // This doesnt modify whats passed unless it returns TRUE
                DialogFORMAT dlg(hInstance, potentialParent, io, fs);
                return dlg.doModal();
            }));
            return 0;

        case REMOTECTRL_INSTALLBB: 
            m_threads.emplace_back(std::thread([hInstance, potentialParent, io, fs]() {
                  std::wstring msg = L"Install bootblock on drive " + fs->driveLetter().substr(0, 2) + L"?";
                  if (MessageBox(potentialParent, msg.c_str(), L"Install Bootblock?", MB_YESNO | MB_ICONQUESTION) == IDYES)
                      if (!fs->installBootBlock())
                          MessageBox(potentialParent, L"An error occured writing the boot block", L"Boot Block Error", MB_OK | MB_ICONEXCLAMATION);
            }));
            return 0;

        case REMOTECTRL_COPYTOADF:
            m_threads.emplace_back(std::thread([hInstance, potentialParent, io, fs]() {
                DialogCOPY dlg(hInstance, potentialParent, io, fs);
                return dlg.doModal();
            }));
            return 0;

        case REMOTECTRL_EJECT: {
            bool filesOpen = fs->driveInUse();
            bool locked = fs->isLocked();
            if (filesOpen || locked) {
                m_threads.emplace_back(std::thread([hInstance, potentialParent, io, fs, filesOpen, locked]() {
                    std::wstring msg = L"Cannot eject drive " + fs->driveLetter().substr(0, 2) + L" - " + (locked ? L"the drive is busy" : L"files are currently open");
                    MessageBox(potentialParent, msg.c_str(), L"Eject", MB_OK | MB_ICONSTOP);
                    }));
            }
            else fs->stop();
            return 0;
        }

        default:
            return 0;
        }
    });

    SetTimer(wnd.hwnd(), TIMERID_MONITOR_FILESYS, 200, NULL);
     
    MSG msg;
    BOOL bRet;
    while ((bRet = GetMessage(&msg, NULL, 0, 0)) != 0) {
        if (bRet == -1) {
            KillTimer(wnd.hwnd(), TIMERID_MONITOR_FILESYS);
            for (auto& fs : dokan_fs) fs->stop();
            break;
        }
        else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }    
}
  
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    int argc = 0;
    WCHAR exeName[MAX_PATH];
    GetModuleFileName(NULL, exeName, MAX_PATH);

    LPWSTR* argv = CommandLineToArgvW(pCmdLine, &argc);
    argv--; argc++;

    mainEXE = exeName;
    
    try {
        if (argc < 3) return 2;
        BOOL readOnly = FALSE; // TODO
            // Bad letter
        WCHAR letter = argv[2][0];
        if ((letter < 'A') || (letter > 'Z')) return 3;

        if (std::wstring(argv[1]) == L"CONTROL") {
            HWND wnd = findControlWindowForDrive(letter);
            std::wstring param = argv[3];
            if (wnd) {
                if (param == L"FORMAT") SendMessage(wnd, WM_USER, REMOTECTRL_FORMAT, (LPARAM)letter);
                if (param == L"EJECT") SendMessage(wnd, WM_USER, REMOTECTRL_EJECT, (LPARAM)letter);
                if (param == L"BB") SendMessage(wnd, WM_USER, REMOTECTRL_INSTALLBB, (LPARAM)letter);
                if (param == L"2DISK") {
                    // filename shoud be on the command line too
                    if (argc < 4) return 4;
                    std::wstring msg = letter + std::wstring(argv[4]);
                    COPYDATASTRUCT tmp;
                    tmp.lpData = (void*)msg.c_str();
                    tmp.cbData = (DWORD)(msg.length() * 2);  // unicode
                    tmp.dwData = REMOTECTRL_COPYTODISK;
                    CMessageWindow wnd2(hInstance, L"CopyDataTemp");
                    SendMessage(wnd, WM_COPYDATA, (WPARAM)wnd2.hwnd(), (LPARAM)&tmp);
                }
                if (param == L"BACKUP") SendMessage(wnd, WM_USER, REMOTECTRL_COPYTOADF, (LPARAM)letter);
            }
            else {
                // Shouldnt ever happen
                MessageBox(GetDesktopWindow(), L"Could not locate drive", L"Drive not found", MB_OK | MB_ICONEXCLAMATION);
            }
        } else
        if (std::wstring(argv[1]) == L"FILE") {
            prepADFLib();
            std::string ansiFilename;
            wideToAnsi(argv[3], ansiFilename);
            ansiFilename = "1" + ansiFilename;
            adfFile = adfMountDev(ansiFilename.c_str(), readOnly);            
            if (!adfFile) return 4;
        }
        else 
        if (std::wstring(argv[1]) == L"BRIDGE") {
            prepADFLib();
            std::string ansiConfig;
            wideToAnsi(argv[3], ansiConfig); 
            ansiConfig = "2" + ansiConfig;

            // This has to be forced in bridge mode
            adfFile = adfOpenDev(ansiConfig.c_str(), readOnly);
            if (!adfFile) return 4;

            if ((adfFile->devType == DEVTYPE_FLOPDD) || (adfFile->devType == DEVTYPE_FLOPHD)) {
                if (adfMountFlopButDontFail(adfFile) != RC_OK) {
                    return 4;
                }
            }             
        }

        if (adfFile) {            
            mountVolumes(letter, readOnly);

            // See if anything mounted
            SectorCacheEngine* d = (SectorCacheEngine*)adfFile->nativeDev;
            if (!d) return 0;

            DokanInit();
            runMountedVolumes(hInstance, argv[1], d);            
            DokanShutdown();
        }
    } catch (const std::exception& ex) {
        UNREFERENCED_PARAMETER(ex);
    }

    for (auto& fs : dokan_fs) fs->stop();

    if (adfFile) {
        adfUnMountDev(adfFile);
        adfFile = nullptr;
    }

    adfEnvCleanUp();
    LocalFree(argv + 1);

    return 0;
}
