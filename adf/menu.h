#pragma once

#include <string>
#include <vector>
#include "dlgConfig.h"

#define APP_TITLE L"DiskFlashback Tray Control"

struct DriveInfo {
    HWND hWnd;
    std::wstring volumeName;
    std::wstring volumeType;
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

public:
    CTrayMenu(HINSTANCE hInstance, const std::wstring& exeName);
    ~CTrayMenu();

    // Main loop
    void run();
};

