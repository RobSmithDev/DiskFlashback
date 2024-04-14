#include <dokan/dokan.h>
#include <shellapi.h>
#include "SignalWnd.h"
#include "resource.h"
#include "drivecontrol.h"
#include "menu.h"
#include "dlgConfig.h"

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
            while (*pos) {
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
    Shell_NotifyIcon(NIM_ADD, &m_notify);
}

// Prepare the menu
void CTrayMenu::setupMenu() {
    m_hMenu = CreatePopupMenu();
    m_hDriveMenu = CreatePopupMenu();
    m_hPhysicalMenu = CreatePopupMenu();

    AppendMenu(m_hPhysicalMenu, MF_STRING,      20,                          L"Enabled");
    AppendMenu(m_hPhysicalMenu, MF_STRING,      21,                          L"Configure...");
    
    AppendMenu(m_hMenu, MF_STRING,              1,                          L"Create Disk Image...");
    AppendMenu(m_hMenu, MF_STRING,              2,                          L"Mount Disk Image...");
    AppendMenu(m_hMenu, MF_STRING | MF_POPUP,   (UINT_PTR)m_hDriveMenu,     L"Eject");
    AppendMenu(m_hMenu, MF_SEPARATOR,           0,                          NULL);
    AppendMenu(m_hMenu, MF_STRING | MF_POPUP,   (UINT_PTR)m_hPhysicalMenu,  L"&Physical Drive");
    AppendMenu(m_hMenu, MF_SEPARATOR,           0,                          NULL);
    AppendMenu(m_hMenu, MF_STRING,              10,                         L"&Quit");
}

void CTrayMenu::populateDrives() {
    m_drives.clear();
    while (DeleteMenu(m_hDriveMenu, 0, MF_BYPOSITION)) {};
    EnumWindows(windowSearchCallback2, (LPARAM)&m_drives);
    DWORD pos = 100;
    for (const auto& d : m_drives) {
        std::wstring full = d.first + L" " + d.second.volumeName + L" (" + d.second.volumeType + L")";
        AppendMenu(m_hDriveMenu, MF_STRING, pos++, full.c_str());
    }
    if (m_drives.size() < 1) {
        AppendMenu(m_hDriveMenu, MF_STRING | MF_DISABLED,0, L"no drives mounted");
    }
}

// Handle mouse input
void CTrayMenu::handleMenuInput(UINT uID, UINT iMouseMsg) {
    switch (iMouseMsg) {
            case WM_RBUTTONDOWN: {
                POINT pt;
                GetCursorPos(&pt);
                populateDrives();
                
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
                
                SetForegroundWindow(m_window.hwnd());
                uint32_t index = TrackPopupMenu(m_hMenu,flags, pt.x, pt.y, 0, m_window.hwnd(), NULL);
                PostMessage(m_window.hwnd(), WM_NULL, 0, 0);

                if (index >= 100) {
                    index -= 100;
                    if (index < m_drives.size()) {
                        auto it = m_drives.begin();
                        std::advance(it, index);
                        PostMessage(it->second.hWnd, WM_USER, REMOTECTRL_EJECT, (LPARAM)it->first[0]);
                        return;
                    }
                    return;
                }

                switch (index) {
                
                case 2: mountDisk(); break;
                case 10: 
                    populateDrives();
                    if (m_drives.size())
                        if (MessageBox(m_window.hwnd(), L"Are you sure? This will eject all mounted disks", L"Quit DiskFlashback", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
                    for (const auto& it : m_drives) 
                        PostMessage(it.second.hWnd, WM_USER, REMOTECTRL_EJECT, (LPARAM)it.first[0]);
                    PostQuitMessage(0);
                    break;

                case 21: {
                    DialogConfig config(m_hInstance, m_window.hwnd());
                    config.doModal();
                    }
                       break;

                }
            }
            break;
        case WM_LBUTTONDBLCLK:
            break;
    }
}


void CTrayMenu::mountDisk() {
    // Request filename
    OPENFILENAME dlg;
    WCHAR filename[MAX_PATH] = { 0 };
    memset(&dlg, 0, sizeof(dlg));
    dlg.lStructSize = sizeof(dlg);
    dlg.hwndOwner = m_window.hwnd();
    std::wstring filter;
    std::wstring defaultFormat;
    dlg.lpstrFilter = L"Disk Images Files\0*.adf;*.img;*.dms;*.hda;*.hdf;*.ima;*.st\0All Files(*.*)\0*.*\0\0";
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

CTrayMenu::CTrayMenu(HINSTANCE hInstance, const std::wstring& exeName) : m_hInstance(hInstance), m_window(hInstance, APP_TITLE), m_exeName(exeName) {
    memset(&m_notify, 0, sizeof(m_notify));
    setupIcon();
    setupMenu();

    // Make sure the icon stays even if explorer dies
    m_window.setMessageHandler(RegisterWindowMessage(L"TaskbarCreated"), [this](WPARAM wParam, LPARAM lParam) ->LRESULT { setupIcon(); return 0; });
    m_window.setMessageHandler(WM_USER, [this](WPARAM wParam, LPARAM lParam)->LRESULT { 
        handleMenuInput((UINT)wParam, (UINT)lParam); return 0; });
}

CTrayMenu::~CTrayMenu() {
    Shell_NotifyIcon(NIM_DELETE, &m_notify);
    DestroyMenu(m_hMenu);
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


