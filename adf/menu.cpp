#include <dokan/dokan.h>
#include <shellapi.h>
#include "SignalWnd.h"
#include "resource.h"
#include "drivecontrol.h"
#include "menu.h"
#include "adflib/src/adflib.h"
#include "fatfs/source/ff.h"
#include "readwrite_file.h"
#include "MountedVolumes.h"
#include "ibm_sectors.h"
#include "drivecontrol.h"
#include "floppybridge_lib.h"
#include "psapi.h"

#define MENUID_CREATEDISK       1
#define MENUID_MOUNTDISK        2
#define MENUID_ABOUT            3
#define MENUID_QUIT             10
#define MENUID_ENABLED          20
#define MENUID_CONFIGURE        21
#define MENUID_AUTOUPDATE       22
#define MENUID_RENAMEEXT        29
#define MENUID_IMAGE_DD         30
#define MENUID_IMAGE_HD         40
#define MENUID_EJECTSTART       100

#define TIMERID_UPDATE_CHECK      1000
#define TIMERID_TRIGGER_REALDRIVE 1001

#pragma comment(lib,"Version.lib")
#pragma comment(lib,"Ws2_32.lib")

// Search windows for one that uses the drive letter specified
BOOL CALLBACK windowSearchCallback2(_In_ HWND hwnd, _In_ LPARAM lParam) {
    std::map<std::wstring, DriveInfo>* mapping = (std::map<std::wstring, DriveInfo>*)lParam;

    WCHAR tmpText[100];
    WCHAR buffer2[100], buffer3[100], path[40];
    GetClassName(hwnd, tmpText, 100);
    if (wcscmp(tmpText, MESSAGEWINDOW_CLASS_NAME) == 0) {
        GetWindowText(hwnd, tmpText, 100);
        WCHAR* pos = wcsstr(tmpText, L"_");
        if (pos) {
            pos++;
            DriveInfo info;
            info.hWnd = hwnd;
            info.isPhysicalDrive = wcsstr(tmpText, L"BRIDGE.") != nullptr;
            uint32_t counter = 0;
            if (info.isPhysicalDrive) {
                info.driveType = _wtoi(tmpText + 7);
            }
            else info.driveType = 0xFFFF;

            while (*pos) {
                counter++;
                swprintf_s(path, L"\\\\.\\%c:\\", pos[0]);
                buffer2[0] = L'\0';
                buffer3[0] = L'\0';
                GetVolumeInformation(path, buffer2, 100, NULL, NULL, NULL, buffer3, 100);
                info.volumeName = buffer2;
                info.volumeType = buffer3;
                swprintf_s(path, L"%c:", pos[0]);
                mapping->insert(std::make_pair(path, info));
                pos++;
            }
            if ((counter < 1) && (info.isPhysicalDrive)) {
                mapping->insert(std::make_pair(L"", info));
            }
        }
    }
    return TRUE;
}

// Prepare the icon
void CTrayMenu::setupIcon() {
    m_notify.cbSize = sizeof(m_notify);
    m_notify.hWnd = m_window.hwnd();
    m_notify.uID = 0;
    m_notify.uFlags = NIF_ICON | NIF_TIP | NIF_STATE | NIF_MESSAGE;
    m_notify.uCallbackMessage = WM_USER;
    m_notify.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_ICON1));
    wcscpy_s(m_notify.szTip, L"DiskFlashback Control");
    m_notify.dwState = 0;
    m_notify.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIcon(NIM_ADD, &m_notify);
    Shell_NotifyIcon(NIM_SETVERSION, &m_notify);
}

// Prepare the menu
void CTrayMenu::setupMenu() {
    m_hMenu = CreatePopupMenu();
    m_hDriveMenu = CreatePopupMenu();
    m_hCreateList = CreatePopupMenu();
    m_hUpdates = CreatePopupMenu();
    m_hCreateListDD = CreatePopupMenu();
    m_hCreateListHD = CreatePopupMenu();

    AppendMenu(m_hCreateListDD, MF_STRING, MENUID_IMAGE_DD    , L"Amiga ADF...");
    AppendMenu(m_hCreateListDD, MF_STRING, MENUID_IMAGE_DD + 1, L"IBM PC...");    
    AppendMenu(m_hCreateListDD, MF_STRING, MENUID_IMAGE_DD + 2, L"Atari ST (Single Sided)...");
    AppendMenu(m_hCreateListDD, MF_STRING, MENUID_IMAGE_DD + 3, L"Atari ST (Double Sided)...");
    AppendMenu(m_hCreateListDD, MF_STRING, MENUID_IMAGE_DD + 4, L"Atari ST (Extended)...");

    AppendMenu(m_hCreateListHD, MF_STRING, MENUID_IMAGE_HD,     L"Amiga ADF...");
    AppendMenu(m_hCreateListHD, MF_STRING, MENUID_IMAGE_HD + 1, L"IBM PC...");
    AppendMenu(m_hCreateListHD, MF_STRING, MENUID_IMAGE_HD + 2, L"Atari ST...");

    AppendMenu(m_hCreateList, MF_STRING | MF_POPUP, (UINT_PTR)m_hCreateListDD, L"Double Density");
    AppendMenu(m_hCreateList, MF_STRING | MF_POPUP, (UINT_PTR)m_hCreateListHD, L"High Density");

    
    AppendMenu(m_hMenu, MF_STRING | MF_POPUP,   (UINT_PTR)m_hCreateList,    L"Create Disk Image");
    AppendMenu(m_hMenu, MF_STRING,              MENUID_MOUNTDISK,           L"Mount Disk Image...");
    AppendMenu(m_hMenu, MF_STRING | MF_POPUP,   (UINT_PTR)m_hDriveMenu,     L"Eject");
    AppendMenu(m_hMenu, MF_SEPARATOR,           0,                          NULL);
    AppendMenu(m_hMenu, MF_STRING | MF_POPUP,   (UINT_PTR)m_hUpdates,      L"Settings");
    AppendMenu(m_hMenu, MF_SEPARATOR, 0, NULL);

    AppendMenu(m_hUpdates, MF_STRING, MENUID_RENAMEEXT, L"Automatically Swap File Extensions (Amiga)");
    AppendMenu(m_hUpdates, MF_STRING, MENUID_AUTOUPDATE, L"Automatically Check for Updates");
    AppendMenu(m_hUpdates, MF_SEPARATOR, 0, NULL);
    AppendMenu(m_hUpdates, MF_STRING, MENUID_ENABLED, L"Mount Physical Drive");
    AppendMenu(m_hUpdates, MF_STRING, MENUID_CONFIGURE, L"Configure Physical Drive...");
    AppendMenu(m_hUpdates, MF_SEPARATOR, 0, NULL);
    AppendMenu(m_hUpdates, MF_STRING, MENUID_AUTOUPDATE + 1, L"Check for Updates Now");
    AppendMenu(m_hUpdates, MF_SEPARATOR, 0, NULL);
    AppendMenu(m_hUpdates, MF_STRING, MENUID_ABOUT,                            L"&About...");
    AppendMenu(m_hMenu, MF_STRING,              MENUID_QUIT,                L"&Quit");

    CheckMenuItem(m_hUpdates, MENUID_ENABLED, MF_BYCOMMAND | m_config.enabled ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m_hUpdates, MENUID_AUTOUPDATE, MF_BYCOMMAND | m_config.checkForUpdates ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m_hUpdates, MENUID_RENAMEEXT, MF_BYCOMMAND | m_config.autoRename ? MF_CHECKED : MF_UNCHECKED);    
}

// Grab version from exe
DWORD CTrayMenu::getAppVersion() {
    DWORD  verHandle = 0;
    UINT   size = 0;
    LPBYTE lpBuffer = NULL;
    DWORD  verSize = GetFileVersionInfoSize((LPCWSTR)m_exeName.c_str(), &verHandle);

    if (verSize != NULL) {
        LPSTR verData = new char[verSize];
        if (GetFileVersionInfo((LPCWSTR)m_exeName.c_str(), verHandle, verSize, verData)) {
            if (VerQueryValue(verData, L"\\", (LPVOID*)&lpBuffer, &size)) {
                if (size) {
                    VS_FIXEDFILEINFO* verInfo = (VS_FIXEDFILEINFO*)lpBuffer;
                    DWORD ver = (HIWORD(verInfo->dwProductVersionMS) << 24) | (LOWORD(verInfo->dwProductVersionMS) << 16) | (HIWORD(verInfo->dwProductVersionLS) << 8) | LOWORD(verInfo->dwProductVersionLS);
                    delete[] verData;
                    return ver;
                }
            }
        }
        delete[] verData;
    }
    return 0;
}

// Check for updates (auto is only every 7 days)
void CTrayMenu::checkForUpdates(bool force) {
    if (!force)
        if (getStamp() - m_config.lastCheck < 7) return;

    m_config.lastCheck = getStamp();
    saveConfiguration(m_config);

    // Fetch version from 'A' record in the DNS record
    hostent* address = gethostbyname("diskflashback.robsmithdev.co.uk");
    if ((address) && (address->h_addrtype == AF_INET)) {
        if (address->h_addr_list[0] != 0) {
            in_addr add = *((in_addr*)address->h_addr_list[0]);

            DWORD netVersion = (add.S_un.S_un_b.s_b1 << 24) | (add.S_un.S_un_b.s_b2 << 16) | (add.S_un.S_un_b.s_b3 << 8) | add.S_un.S_un_b.s_b4;
            DWORD appVersion = getAppVersion();
         
            if (appVersion < netVersion) {
                // Do popup for updates available
                m_notify.uFlags |= NIF_INFO;
                m_notify.dwInfoFlags = NIIF_INFO;
                m_lastBalloonIsUpdate = true;
                wcscpy_s(m_notify.szInfoTitle, APPLICATION_NAME_L);
                wcscpy_s(m_notify.szInfo, L"An update is available for " APPLICATION_NAME_L "\nClick to download");

                Shell_NotifyIcon(NIM_MODIFY, &m_notify);
                m_notify.uFlags &= ~NIF_INFO;
            }
        }
    }   
}

// Populate the list of drives
void CTrayMenu::populateDrives() {
    m_drives.clear();
    while (DeleteMenu(m_hDriveMenu, 0, MF_BYPOSITION)) {};
    EnumWindows(windowSearchCallback2, (LPARAM)&m_drives);
    DWORD pos = MENUID_EJECTSTART;
    for (const auto& d : m_drives) {
        std::wstring full = d.first + L" " + d.second.volumeName + L" (" + d.second.volumeType + L")";
        AppendMenu(m_hDriveMenu, MF_STRING, pos++, full.c_str());
    }
    if (m_drives.size() < 1) {
        AppendMenu(m_hDriveMenu, MF_STRING | MF_DISABLED,0, L"no drives mounted");
    }
}

// Run the config dialog
void CTrayMenu::doConfig() {
    DialogConfig config(m_hInstance, m_window.hwnd());
    if (config.doModal()) {
        loadConfiguration(m_config);
        m_didNotifyFail = false;
    }
}

// Run the "create new disk image" code
void CTrayMenu::runCreateImage(bool isHD, uint32_t option) {
    OPENFILENAME dlg;
    WCHAR filename[MAX_PATH] = { 0 };
    memset(&dlg, 0, sizeof(dlg));
    dlg.lStructSize = sizeof(dlg);
    dlg.hwndOwner = m_window.hwnd();
    std::wstring filter;
    std::wstring defaultFormat;
    std::wstring ext;
    uint32_t sectors = 0;
    switch (option) {
    case 0:
        dlg.lpstrFilter = L"Amiga Disk Files (*.adf)\0*.adf\0All Files(*.*)\0*.*\0\0";
        ext = L"adf";
        sectors = 11;
        break;
    case 1:
        dlg.lpstrFilter = L"IBM Disk Files (*.img, *.ima)\0*.img;*.ima\0All Files(*.*)\0*.*\0\0";
        ext = L"img";
        sectors = 9;
        break;
    case 2:
    case 3:
    case 4:
        dlg.lpstrFilter = L"Atart ST Disk Files (*.st)\0*.st\0All Files(*.*)\0*.*\0\0";
        ext = L"st";
        break;
    default:
        return; // not supported
    }

    if (isHD) sectors <<= 1;
    sectors = sectors * 80 * 2;

    std::wstring tmp = ext;
    for (WCHAR& c : tmp) c = towupper(c);
    std::wstring title = L"Create New " + tmp + L" Disk File...";

    dlg.Flags = OFN_CREATEPROMPT | OFN_ENABLESIZING | OFN_EXPLORER | OFN_EXTENSIONDIFFERENT | OFN_HIDEREADONLY | OFN_NOREADONLYRETURN | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    dlg.lpstrDefExt = ext.c_str();
    dlg.lpstrFile = filename;
    dlg.lpstrTitle = title.c_str();
    dlg.nMaxFile = MAX_PATH;
    if (!GetSaveFileName(&dlg))  return;

    
    if (option >= 2) {
        if (isHD) {
            createBasicDisk(filename, "empty", 80, 18, 2);
        } else
        switch (option) {
            case 2:createBasicDisk(filename, "empty", 80, 9, 1); break;
            case 3:createBasicDisk(filename, "empty", 80, 9, 2); break;
            case 4:createBasicDisk(filename, "empty", 83, 11, 2); break;
        }
    }
    else {
        // Create image
        HANDLE fle = CreateFile(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (fle == INVALID_HANDLE_VALUE) {
            MessageBox(m_window.hwnd(), L"Unable to create disk image file", L"Error", MB_OK | MB_ICONEXCLAMATION);
            return;
        }

        unsigned char blank[512] = { 0 };
        // Write blank sectors
        for (uint32_t s = 0; s < sectors; s++) {
            DWORD written;
            if (!WriteFile(fle, blank, 512, &written, NULL)) written = 0;
            if (written != 512) {
                CloseHandle(fle);
                MessageBox(m_window.hwnd(), L"An error occured creating the disk image file", L"Error", MB_OK | MB_ICONEXCLAMATION);
                return;
            }
        }

        SectorCacheEngine* engine = new SectorRW_File(filename, fle);

        // Create file system
        switch (option) {
        case 0: installAmigaFS(isHD, engine); break;
        case 1: installIBMPCFS(isHD, true, engine); break;
        }

        engine->flushWriteCache();
        delete engine;
    }

    if (MessageBox(m_window.hwnd(), L"Image file created successfully.\nWould you like to mount the new image?", L"Image Ready", MB_YESNO | MB_ICONEXCLAMATION) != IDYES) return;

    // Trigger mounting
    std::wstring cmd = L"\"" + m_exeName + L"\" \"" + filename + L"\"";
    PROCESS_INFORMATION pi;
    STARTUPINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    CreateProcess(NULL, (LPWSTR)cmd.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    if (pi.hThread) CloseHandle(pi.hThread);
    if (pi.hProcess) CloseHandle(pi.hProcess);
}

void CTrayMenu::installAmigaFS(bool isHD, SectorCacheEngine* fle) {
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
    AdfDevice* dev = adfOpenDev((char*)fle, false);
    dev->cylinders = fle->totalNumTracks() / 2;
    dev->heads = 2;
    dev->sectors = fle->numSectorsPerTrack();
    adfCreateFlop(dev, "empty", 0);
    adfUnMountDev(dev);
    fle->flushWriteCache();
    adfEnvCleanUp();
}

void CTrayMenu::installIBMPCFS(bool isHD, bool isIBMPC, SectorCacheEngine* fle) {
    BYTE work[FF_MAX_SS];
    MKFS_PARM opt;
    
    FATFS* m_fatDevice = (FATFS*)malloc(sizeof(FATFS));
    if (!m_fatDevice) return;
    memset(m_fatDevice, 0, sizeof(FATFS));

    getMkFsParams(isHD, SectorType::stIBM , opt);
    setFatFSSectorCache(fle);
    f_mkfs(L"\\", &opt, work, sizeof(work));
    f_mount(m_fatDevice, L"", 1);
    f_setlabel(L"empty");
    f_unmount(L"\\");
    free(m_fatDevice);

    fle->flushWriteCache();
    setFatFSSectorCache(nullptr);
}

// Handle the menu click result
void CTrayMenu::handleMenuResult(uint32_t index) {
    if (index >= MENUID_EJECTSTART) {
        index -= MENUID_EJECTSTART;
        if (index < m_drives.size()) {
            auto it = m_drives.begin();
            std::advance(it, index);
            PostMessage(it->second.hWnd, WM_USER, REMOTECTRL_EJECT, (LPARAM)it->first[0]);
            return;
        }
        return;
    }

    if ((index >= MENUID_IMAGE_DD) && (index < MENUID_IMAGE_DD + 5)) runCreateImage(false, index - MENUID_IMAGE_DD);
    if ((index >= MENUID_IMAGE_HD) && (index < MENUID_IMAGE_HD + 3)) runCreateImage(true, index - MENUID_IMAGE_HD);

    switch (index) {
    case MENUID_ABOUT: {
        DWORD v = getAppVersion();
            std::wstring msg = std::wstring(APPLICATION_NAME_L) + L" V" + std::to_wstring(v >> 24) + L"." + std::to_wstring((v >> 16) & 0xFF) + L"." + std::to_wstring((v >> 8) & 0xFF) + L"." + std::to_wstring(v & 0xFF) + L"\r\n";
            msg += L"Copyright \x00A9 2024 RobSmithDev\r\n\r\n";
            msg += L"Visit https://robsmithdev.co.uk/diskflashback for more information";
            MessageBox(m_window.hwnd(), msg.c_str(), L"About DiskFlashback", MB_OK | MB_ICONINFORMATION);
        }
        break;
    case MENUID_AUTOUPDATE:
        m_config.checkForUpdates = !m_config.checkForUpdates;
        saveConfiguration(m_config);
        CheckMenuItem(m_hUpdates, MENUID_AUTOUPDATE, MF_BYCOMMAND | m_config.checkForUpdates ? MF_CHECKED : MF_UNCHECKED);
        break;
    case MENUID_AUTOUPDATE+1:
        checkForUpdates(true);
        break;
    case MENUID_RENAMEEXT:
        m_config.autoRename = !m_config.autoRename;
        saveConfiguration(m_config);
        CheckMenuItem(m_hUpdates, MENUID_RENAMEEXT, MF_BYCOMMAND | m_config.autoRename ? MF_CHECKED : MF_UNCHECKED);
        populateDrives();
        for (const auto& w : m_drives)
            PostMessage(w.second.hWnd, WM_AUTORENAME, 0, m_config.autoRename ? 1 : 0);
        break;
    case MENUID_ENABLED: 
        m_config.enabled = !m_config.enabled;
        saveConfiguration(m_config);
        CheckMenuItem(m_hUpdates, MENUID_ENABLED, MF_BYCOMMAND | m_config.enabled ? MF_CHECKED : MF_UNCHECKED);
        break;
    case MENUID_CREATEDISK: break;
    case MENUID_MOUNTDISK: mountDisk(); break;
    case MENUID_QUIT:
        populateDrives();
        if (m_drives.size())
            if (MessageBox(m_window.hwnd(), L"Are you sure? This will eject all mounted disks", L"Quit DiskFlashback", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
        for (const auto& it : m_drives)
            PostMessage(it.second.hWnd, WM_USER, REMOTECTRL_EJECT, (LPARAM)it.first[0]);
        PostQuitMessage(0);
        break;

    case MENUID_CONFIGURE:
        doConfig();
        break;
    }
}

// Handle tray contect menu
void CTrayMenu::doContextMenu(POINT pt) {
    HMONITOR m = MonitorFromPoint(pt, MONITOR_DEFAULTTONULL);
    DWORD flags = TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_RETURNCMD;
    if (m) {
        MONITORINFO data;
        data.cbSize = sizeof(data);
        GetMonitorInfo(m, &data);

        if (pt.x > data.rcMonitor.left + (data.rcMonitor.right - data.rcMonitor.left) / 2)
            flags |= TPM_RIGHTALIGN; else flags |= TPM_LEFTALIGN;
        if (pt.y > data.rcMonitor.top + (data.rcMonitor.bottom - data.rcMonitor.top) / 2)
            flags |= TPM_BOTTOMALIGN; else flags |= TPM_TOPALIGN;
    }
    else flags |= TPM_BOTTOMALIGN | TPM_RIGHTALIGN;
    populateDrives();
    SetForegroundWindow(m_window.hwnd());
    uint32_t index = TrackPopupMenu(m_hMenu, flags, pt.x, pt.y, 0, m_window.hwnd(), NULL);
    PostMessage(m_window.hwnd(), WM_NULL, 0, 0);

    handleMenuResult(index);
}

// Handle mouse input
void CTrayMenu::handleMenuInput(UINT uID, UINT iMouseMsg) {
    switch (iMouseMsg) {
    case NIN_BALLOONUSERCLICK:
        if (m_lastBalloonIsUpdate)
            ShellExecute(m_window.hwnd(), L"open", L"https://robsmithdev.co.uk/diskflashback", NULL, NULL, SW_SHOW);
        else doConfig();
        break;
    case WM_CONTEXTMENU: {
        RECT r;
        NOTIFYICONIDENTIFIER id;
        id.cbSize = sizeof(id);
        id.hWnd = m_window.hwnd();
        id.uID = 0;
        id.guidItem = GUID_NULL;
        Shell_NotifyIconGetRect(&id, &r);
        doContextMenu({ (r.left + r.right) / 2, (r.top + r.bottom) / 2 });
    }
                       break;
    case WM_RBUTTONDOWN: {
        POINT pt;
        GetCursorPos(&pt);
        doContextMenu(pt);
    }
                       break;
    case WM_LBUTTONDBLCLK:
        doConfig();
        break;
    }
}

// Mount a single disk file
void CTrayMenu::mountDisk() {
    // Request filename
    OPENFILENAME dlg;
    WCHAR filename[MAX_PATH] = { 0 };
    memset(&dlg, 0, sizeof(dlg));
    dlg.lStructSize = sizeof(dlg);
    dlg.hwndOwner = m_window.hwnd();
    std::wstring filter;
    std::wstring defaultFormat;
    dlg.lpstrFilter = L"Disk Images Files\0*.adf;*.img;*.dms;*.hda;*.hdf;*.ima;*.st;*.scp\0All Files(*.*)\0*.*\0\0";
    dlg.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ENABLESIZING | OFN_EXPLORER | OFN_EXTENSIONDIFFERENT;
    dlg.lpstrTitle = L"Select disk image to mount";
    dlg.lpstrFile = filename;
    dlg.nMaxFile = MAX_PATH;
    if (GetOpenFileName(&dlg)) {
        std::wstring cmd = L"\"" + m_exeName + L"\" \"" + filename + L"\"";
        PROCESS_INFORMATION pi;
        STARTUPINFO si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));
        CreateProcess(NULL, (LPWSTR)cmd.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
        if (pi.hThread) CloseHandle(pi.hThread);
        if (pi.hProcess) CloseHandle(pi.hProcess);
    }
}

// Eject/close physical drives running
bool CTrayMenu::closePhysicalDrive(bool dontNotify, uint16_t driveType) {
    bool ret = false;
    for (auto& drive : m_drives)
        if (drive.second.isPhysicalDrive && (drive.second.driveType == driveType)) {
            PostMessage(drive.second.hWnd, WM_USER, dontNotify? REMOTECTRL_EJECT_SILENT: REMOTECTRL_EJECT, (LPARAM)drive.first[0]);
            return true;
        }
    return false;
}

// Monitor the physical drive and enable/close as required
void CTrayMenu::monitorPhysicalDrive() {

    if (m_processUsingDrive) {
        if (WaitForSingleObject(m_processUsingDrive, 0) == WAIT_OBJECT_0) {
            CloseHandle(m_processUsingDrive);
            m_processUsingDrive = 0;
            m_didNotifyFail = false;
        }        
    }

    if (m_floppyPi.hProcess) {
        // Check if it terminated
        if (WaitForSingleObject(m_floppyPi.hProcess, 0) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(m_floppyPi.hProcess, &code);
            if (code == RETURNCODE_MOUNTFAILDRIVE) {
                if (!m_didNotifyFail) {
                    m_didNotifyFail = true;
                    // Notify it failed to mount
                    std::vector<const TCHAR*> ports;
                    FloppyBridgeAPI::enumCOMPorts(ports);
                    if (ports.size()) {
                        wcscpy_s(m_notify.szInfo, L"Unable to start physical drive.\r\nEither it was not detected, or it is in use.");
                    }
                    else {
                        wcscpy_s(m_notify.szInfo, L"Unable to start physical drive.\r\nThe device COM port could not be found.");
                    }
                    m_lastBalloonIsUpdate = false;
                    // Do popup for updates available
                    m_notify.uFlags |= NIF_INFO;
                    m_notify.dwInfoFlags = NIIF_ERROR;
                    wcscpy_s(m_notify.szInfoTitle, APPLICATION_NAME_L);
                    Shell_NotifyIcon(NIM_MODIFY, &m_notify);
                    m_notify.uFlags &= ~NIF_INFO;
                }
            } else m_didNotifyFail = false;
            CloseHandle(m_floppyPi.hProcess);
            memset(&m_floppyPi, 0, sizeof(m_floppyPi));
        }
    }

    if (m_config.enabled) {
        if ((m_processUsingDrive == 0) && (m_floppyPi.hProcess == 0) && (!m_config.floppyProfile.empty())) {
            std::string driveLetter; driveLetter.resize(1); driveLetter[0] = m_config.driveLetter;
            std::wstring driveLetterW; ansiToWide(driveLetter, driveLetterW);
            std::wstring profileW; ansiToWide(m_config.floppyProfile, profileW);

            std::wstring cmd = L"\"" + m_exeName + L"\" BRIDGE " +driveLetterW + L" 1 \"" + profileW + L"\"";
            STARTUPINFO si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);

            CreateProcess(NULL, (LPWSTR)cmd.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &m_floppyPi);
            if (m_floppyPi.hThread) CloseHandle(m_floppyPi.hThread);
            m_floppyPi.hThread = 0;
        }
    }
    else {
        // It's running, so lets shut it down
        if (m_floppyPi.hProcess) {
            populateDrives();
            closePhysicalDrive();
            CloseHandle(m_floppyPi.hProcess);
            memset(&m_floppyPi, 0, sizeof(m_floppyPi));
        }
    }
}

CTrayMenu::CTrayMenu(HINSTANCE hInstance, const std::wstring& exeName) : m_hInstance(hInstance), m_window(hInstance, APP_TITLE), m_exeName(exeName) {
    memset(&m_notify, 0, sizeof(m_notify));
    loadConfiguration(m_config);

    memset(&m_floppyPi, 0, sizeof(m_floppyPi));

    // Start winsock
    WSADATA data;
    WSAStartup(MAKEWORD(2, 0), &data);

    setupIcon();
    setupMenu();   

    // Make sure the icon stays even if explorer dies
    m_window.setMessageHandler(RegisterWindowMessage(L"TaskbarCreated"), [this](WPARAM wParam, LPARAM lParam) ->LRESULT {
        setupIcon(); return 0; });

    m_window.setMessageHandler(WM_USER, [this](WPARAM wParam, LPARAM lParam)->LRESULT {
        handleMenuInput((UINT)wParam, (UINT)lParam); return 0; });

    m_window.setMessageHandler(WM_PHYSICAL_EJECT, [this](WPARAM wParam, LPARAM lParam)->LRESULT {
        if (m_processUsingDrive) return 0;
        // Turn off the "enabled" option
        m_config.enabled = false;
        saveConfiguration(m_config);
        CheckMenuItem(m_hUpdates, MENUID_ENABLED, MF_BYCOMMAND | MF_UNCHECKED);
        return 0;
        });

    m_window.setMessageHandler(WM_REMOTEUSAGE, [this](WPARAM wParam, LPARAM lParam)->LRESULT {
        // Turn off the "enabled" option
        if (m_processUsingDrive) CloseHandle(m_processUsingDrive);
        m_processUsingDrive = 0;

        uint16_t driveType = wParam & 0x7FFF;
        bool releaseDrive = (wParam & 0x8000) == 0;

        if (releaseDrive && lParam) {
            m_processUsingDrive = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)lParam);

            if (m_processUsingDrive) {
                populateDrives();

                if (closePhysicalDrive(true, driveType)) {
                    m_didNotifyFail = true;
                    WCHAR name[1024];
                    m_lastBalloonIsUpdate = false;

                    HANDLE tmp = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, (DWORD)lParam);
                    if (tmp) {
                        wcscpy_s(m_notify.szInfo, L"Suspending real floppy drive while ");
                        name[GetProcessImageFileName(tmp, name, 1024)] = L'\0';
                        std::wstring n = name;
                        size_t i = n.rfind(L"\\");
                        if (i != std::wstring::npos) n = n.substr(i + 1);
                        wcscat_s(m_notify.szInfo, n.c_str());
                        wcscat_s(m_notify.szInfo, L" requires it.");
                        CloseHandle(tmp);
                    }
                    else {
                        wcscpy_s(m_notify.szInfo, L"Suspending real floppy drive while it is in use");
                    }
                    // Do popup for updates available
                    m_notify.uFlags |= NIF_INFO;
                    m_notify.dwInfoFlags = NIIF_INFO;
                    wcscpy_s(m_notify.szInfoTitle, APPLICATION_NAME_L);
                    Shell_NotifyIcon(NIM_MODIFY, &m_notify);
                    m_notify.uFlags &= ~NIF_INFO;

                    // Wait a short time for it to *actually* disappear
                    for (uint32_t t = 0; t < 10; t++) {
                        Sleep(200);
                        populateDrives();
                        if (!closePhysicalDrive(true, driveType)) return 0;
                    }
                }
                else {
                    CloseHandle(m_processUsingDrive);
                    m_processUsingDrive = 0;
                }
            }
        }
        return 0;
    });
    
    m_window.setMessageHandler(WM_TIMER, [this](WPARAM wParam, LPARAM lParam)->LRESULT {
        switch (wParam) {
        case TIMERID_UPDATE_CHECK:
            KillTimer(m_window.hwnd(), TIMERID_UPDATE_CHECK);
            if (m_config.checkForUpdates) checkForUpdates(false);
            SetTimer(m_window.hwnd(), TIMERID_UPDATE_CHECK, 60000 * 60, NULL);
            break;

        case TIMERID_TRIGGER_REALDRIVE:
            KillTimer(m_window.hwnd(), TIMERID_TRIGGER_REALDRIVE);
            monitorPhysicalDrive();
            SetTimer(m_window.hwnd(), TIMERID_TRIGGER_REALDRIVE, 2000, NULL);
            break;
        default:
            break;
        }
        return 0;
     });
   

#ifdef _DEBUG
    SetTimer(m_window.hwnd(), TIMERID_UPDATE_CHECK, 1000, NULL);
#else
    SetTimer(m_window.hwnd(), TIMERID_UPDATE_CHECK, 60000, NULL);
#endif
    // Start 5 seconds after running
    SetTimer(m_window.hwnd(), TIMERID_TRIGGER_REALDRIVE, 5000, NULL);
}

CTrayMenu::~CTrayMenu() {
    KillTimer(m_window.hwnd(), TIMERID_UPDATE_CHECK);
    Shell_NotifyIcon(NIM_DELETE, &m_notify);
    DestroyMenu(m_hMenu);
    DestroyMenu(m_hDriveMenu);
    DestroyMenu(m_hCreateList);
    DestroyMenu(m_hCreateListDD);
    DestroyMenu(m_hCreateListHD);
    DestroyMenu(m_hUpdates);
    if (m_floppyPi.hProcess) CloseHandle(m_floppyPi.hProcess);
}

// Main loop
void CTrayMenu::run() {
    MSG msg;
    BOOL bRet;
    while ((bRet = GetMessage(&msg, NULL, 0, 0)) != 0) {
        if (bRet == -1) break; else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}


