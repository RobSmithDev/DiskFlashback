
#include <dokan/dokan.h>
#include <string>
#include <vector>
#include <ShlObj.h>
#include "MountedVolumes.h"
#include <errno.h>
#include "SignalWnd.h"
#include "dlgFormat.h"
#include "dlgCopy.h"
#include "menu.h"

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// Command line is:
//   COMMANDLINE_ constant
//   DRIVELETTER
//   If MOUNT:
//            0=Read Only, 1=Read/Write
//   If CONTROL:
//          one of the CTRL_PARAM_ values
//   Mount File/Drive Params (if mount)

#include "dlgConfig.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    int argc = 0;
    WCHAR exeName[MAX_PATH];
    GetModuleFileName(NULL, exeName, MAX_PATH);

    {
        DialogConfig config(hInstance, GetDesktopWindow());
        config.doModal();
        return 0;
    }

    LPWSTR* argv = nullptr;
    if (wcslen(pCmdLine)) argv = CommandLineToArgvW(pCmdLine, &argc);

    // BRIDGE L 1 config  FILE L 1 f:\testing.adf FILE L 1 d:\pc.img   BRIDGE L 1 config   FILE L f:\testdisk.adf  FILE L f:\rom\XTHardDiskBackup.hdf  FILE L f:\testdisk.adf  FILE L "D:\Virtual Floppy Drive\dokany-master\xdms\xdms\x64\Debug\moo\amos3d.dms" FILE L 1 "E:\Youtube Backup\PlipBox\amiga\BestWB.1.3.1\DOpus.dms" 
    // See if its just a disk image on the command line
    if (argc == 1) {
        std::wstring txt = argv[0];
        size_t pos = txt.rfind(L".");
        if (pos != std::wstring::npos) {
            std::wstring ext = txt.substr(pos+1);
            for (WCHAR& c : ext) c = towupper(c);
            // Disk image file
            if ((ext == L"ADF") || (ext == L"DMS") || (ext == L"ST") || (ext == L"IMG") || (ext == L"IMA") || (ext == L"HDA") || (ext == L"HDF")) {
                VolumeManager* vol = new VolumeManager(hInstance, exeName, '?', false);
                if (!vol->mountFile(txt)) {
                    delete vol;
                    return RETURNCODE_MOUNTFAIL;
                }
                try {
                    vol->run();
                }
                catch (const std::exception& ex) {
                    UNREFERENCED_PARAMETER(ex);
                }
                delete vol;
                return 0;
            }
        }
    }

    if (argc < 3) {
        if (FindWindow(MESSAGEWINDOW_CLASS_NAME, APP_TITLE)) return 0;
        CTrayMenu menu(hInstance, exeName);
        menu.run();
        return 0;
    }


    // Check Drive Letter
    const WCHAR driveLetter = argv[1][0];

    if (std::wstring(argv[0]) == COMMANDLINE_CONTROL) {
        if ((driveLetter < 'A') || (driveLetter > 'Z')) return RETURNCODE_BADLETTER;

        // Find the control interface
        HWND wnd = VolumeManager::FindControlWindowForDrive(driveLetter);
        std::wstring param = argv[2];
        if (wnd) {
            if (param == CTRL_PARAM_FORMAT) SendMessage(wnd, WM_USER, REMOTECTRL_FORMAT, (LPARAM)driveLetter);
            if (param == CTRL_PARAM_EJECT) SendMessage(wnd, WM_USER, REMOTECTRL_EJECT, (LPARAM)driveLetter);
            if (param == CTRL_PARAM_INSTALLBB) SendMessage(wnd, WM_USER, REMOTECTRL_INSTALLBB, (LPARAM)driveLetter);
            if (param == CTRL_PARAM_COPY2DISK) {
                // filename shoud be on the command line too
                if (argc < 4) return RETURNCODE_BADARGS;
                std::wstring msg = driveLetter + std::wstring(argv[3]);
                COPYDATASTRUCT tmp;
                tmp.lpData = (void*)msg.c_str();
                tmp.cbData = (DWORD)(msg.length() * 2);  // unicode
                tmp.dwData = REMOTECTRL_COPYTODISK;
                CMessageWindow wnd2(hInstance, L"CopyDataTemp");
                SendMessage(wnd, WM_COPYDATA, (WPARAM)wnd2.hwnd(), (LPARAM)&tmp);
            }
            if (param == CTRL_PARAM_BACKUP) SendMessage(wnd, WM_USER, REMOTECTRL_COPYTOADF, (LPARAM)driveLetter);
        }
        else {
            // Shouldn't ever happen
            MessageBox(GetDesktopWindow(), L"Could not locate drive", L"Drive not found", MB_OK | MB_ICONEXCLAMATION);
        }
    }
    else {
        if (argc < 4) return RETURNCODE_BADARGS;

        const BOOL readOnly = std::wstring(argv[2]) != L"1";

        if (driveLetter != '?')
            if ((driveLetter < 'A') || (driveLetter > 'Z')) return RETURNCODE_BADLETTER;

        VolumeManager* vol = new VolumeManager(hInstance, exeName, driveLetter, readOnly);

        if (std::wstring(argv[0]) == COMMANDLINE_MOUNTFILE) {
            if (!vol->mountFile(argv[3])) {
                delete vol;
                return RETURNCODE_MOUNTFAIL;
            }
        }
        else
            if (std::wstring(argv[0]) == COMMANDLINE_MOUNTDRIVE) {
                if (!vol->mountDrive(argv[3])) {
                    delete vol;
                    return RETURNCODE_MOUNTFAIL;
                }
            }
            else return RETURNCODE_BADARGS;

        try {
            vol->run();
        }
        catch (const std::exception& ex) {
            UNREFERENCED_PARAMETER(ex);
        }

        delete vol;
    }

    LocalFree(argv + 1);

    return RETURNCODE_OK;
}
