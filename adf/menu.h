#pragma once

#include <string>
#include <vector>
#include "dlgConfig.h"

#define APP_TITLE L"DiskFlashback Tray Control"
#define WM_PHYSICAL_EJECT     (WM_USER+1)

// Sent by other programs to "dismount" any real drives while they operate
// WPARAM = 0 to switch drive OFF, and 1 to switch back ON again
// LPARAM is the process ID making the request.  The application will monitor and auto restore if the process exits
#define WM_REMOTEUSAGE        (WM_USER+2)

struct DriveInfo {
    HWND hWnd;
    std::wstring volumeName;
    std::wstring volumeType;
    bool isPhysicalDrive;
};

class SectorCacheEngine;

class CTrayMenu {
private:   
    AppConfig m_config;
    HINSTANCE m_hInstance;
    CMessageWindow m_window;
    NOTIFYICONDATA m_notify;
    HMENU m_hMenu = 0;
    HMENU m_hDriveMenu = 0;
    HMENU m_hPhysicalMenu = 0;
    HMENU m_hCreateList = 0;
    HMENU m_hCreateListDD = 0;
    HMENU m_hCreateListHD = 0;
    HMENU m_hUpdates = 0;
    PROCESS_INFORMATION m_floppyPi;
    bool m_lastBalloonIsUpdate = false;
    bool m_didNotifyFail = false;
    HANDLE m_processUsingDrive = 0;

    std::map<std::wstring, DriveInfo> m_drives;
    const std::wstring m_exeName;

    // Prepare the icon
    void setupIcon();

    // Prepare the menu
    void setupMenu();

    // Refresh drive list
    void populateDrives();

    // Handle mouse input
    void handleMenuInput(UINT uID, UINT iMouseMsg);

    // Mount disk image
    void mountDisk();

    // Handle the config dialog
    void doConfig();

    // Handle the menu click result
    void handleMenuResult(uint32_t index);

    // Run the "create new disk image" code
    void runCreateImage(bool isHD, uint32_t option);

    // Create file system on file
    void installAmigaFS(bool isHD, SectorCacheEngine* fle);
    void installIBMPCFS(bool isHD, bool isIBMPC, SectorCacheEngine* fle);


    void checkForUpdates(bool force);
    DWORD getAppVersion();

    // Handle tray contect menu
    void doContextMenu(POINT pt);

    // Monitor the physical drive and enable/close as required
    void monitorPhysicalDrive();

    // Eject/close physical drives running
    bool closePhysicalDrive(bool dontNotify = false);


public:
    CTrayMenu(HINSTANCE hInstance, const std::wstring& exeName);
    ~CTrayMenu();

    // Main loop
    void run();
};

