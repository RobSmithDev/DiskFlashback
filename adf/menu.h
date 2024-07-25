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

#include <string>
#include <vector>
#include "dlgConfig.h"


#define WM_DOQUIT             (WM_USER+10)

struct DriveInfo {
    HWND hWnd;
    std::wstring volumeName;
    std::wstring volumeType;
    uint16_t driveType;
    bool isPhysicalDrive;
    bool valid;
};

class SectorCacheEngine;

class CTrayMenu {
private:   
    enum class LastBalloonType {lblUpdate, lblReminder, lblDefault};
    AppConfig m_config;
    HINSTANCE m_hInstance;
    CMessageWindow m_window;
    NOTIFYICONDATA m_notify;
    HMENU m_hMenu = 0;
    HMENU m_hDriveMenu = 0;
    HMENU m_hCreateList = 0;
    HMENU m_hCreateListDD = 0;
    HMENU m_hCreateListHD = 0;
    HMENU m_hUpdates = 0;
    HMENU m_hCopy = 0;
    PROCESS_INFORMATION m_floppyPi;
    LastBalloonType m_lastBalloonType = LastBalloonType::lblDefault;
    bool m_didNotifyFail = false;
    HANDLE m_processUsingDrive = 0;
    DWORD m_timeoutBeforeRestart = 10;

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
    bool closePhysicalDrive(bool dontNotify = false, uint16_t driveType = 0xFFFF);

    // Is this drive letter one of ours?
    bool isOurDrive(WCHAR letter);

    // Tidy up drive icons and associations incase the host was accidently shut down without being tidied up
    void cleanupDriveIcons();

    // Trigger copy disk to image
    void handleCopyToImage();

    // Trigger copy image to disk
    void handleCopyToDisk();

public:
    CTrayMenu(HINSTANCE hInstance, const std::wstring& exeName, bool isSilentStart);
    ~CTrayMenu();

    // Main loop
    void run();
};

