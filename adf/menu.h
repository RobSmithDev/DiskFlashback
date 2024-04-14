#pragma once

#include <string>
#include <vector>

#define APP_TITLE L"DiskFlashback Tray Control"

struct DriveInfo {
    HWND hWnd;
    std::wstring volumeName;
    std::wstring volumeType;
};

class CTrayMenu {
private:   

    HINSTANCE m_hInstance;
    CMessageWindow m_window;
    NOTIFYICONDATA m_notify;
    HMENU m_hMenu = 0;
    HMENU m_hDriveMenu = 0;
    HMENU m_hPhysicalMenu = 0;
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
public:
    CTrayMenu(HINSTANCE hInstance, const std::wstring& exeName);
    ~CTrayMenu();

    // Main loop
    void run();
};

