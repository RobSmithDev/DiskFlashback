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



#include <dokan/dokan.h>
#include <CommCtrl.h>

#include "dlgClean.h"
#include "sectorCache.h"
#include "resource.h"
#include "MountedVolume.h"
#include "readwrite_floppybridge.h"




DialogCLEAN::DialogCLEAN(HINSTANCE hInstance, HWND hParent, SectorCacheEngine* io, MountedVolume* fs) :
	m_hInstance(hInstance), m_hParent(hParent), m_fs(fs) {
	m_windowCaption = CLEANWINDOW_TITLE;
	m_bridge = dynamic_cast<SectorRW_FloppyBridge*>(io);

	m_handPoint = (HCURSOR)LoadCursor(NULL, IDC_HAND);
}

DialogCLEAN::~DialogCLEAN() {
	DestroyCursor(m_handPoint);
}

INT_PTR CALLBACK cleanCallback(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_INITDIALOG) SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)lParam);
	DialogCLEAN* dlg = (DialogCLEAN*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	if (dlg) return dlg->handleDialogProc(hwnd, msg, wParam, lParam); else return FALSE;
}

INT_PTR DialogCLEAN::doModal() {
	if (!m_bridge) return ID_CLOSE;
	if (m_fs) {
		if (!m_fs->setLocked(true)) {
			MessageBox(m_dialogBox, L"Unable to start clean. Couldn't lock the drive.", m_windowCaption.c_str(), MB_OK | MB_ICONEXCLAMATION);
			return ID_CLOSE;
		}
		m_fs->temporaryUnmountDrive();
	}
	m_bridge->enableFilesystemID(false);

	INT_PTR res = DialogBoxParam(m_hInstance, MAKEINTRESOURCE(IDD_CLEAN), GetDesktopWindow(), cleanCallback, (LPARAM)this);

	m_bridge->enableFilesystemID(true);
	if (m_fs) {
		m_fs->setLocked(false);
		m_fs->restoreUnmountedDrive(false);
	}
	m_bridge->triggerNewDiskMount();

	return res;
}

// Init dialog
void DialogCLEAN::handleInitDialog(HWND hwnd) {
	m_dialogBox = hwnd;

	m_link = GetDlgItem(m_dialogBox, IDC_MAKEYOUROWN);

	HICON icon = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_ICON1));
	SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
	SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
	SetWindowText(hwnd, m_windowCaption.c_str());
	
	if (m_hParent) {
		RECT r;
		GetWindowRect(m_hParent, &r);
		SetWindowPos(hwnd, m_hParent, r.left + 70, r.top + 70, 0, 0, SWP_NOSIZE);
	}
	else {
		RECT r;
		GetWindowRect(hwnd, &r);
		SetWindowPos(hwnd, m_hParent, (GetSystemMetrics(SM_CXSCREEN) - (r.right - r.left)) / 2, (GetSystemMetrics(SM_CYSCREEN) - (r.bottom - r.top)) / 2, 0, 0, SWP_NOSIZE);
	}

	BringWindowToTop(hwnd);
	SetForegroundWindow(hwnd);
	enableControls(true);
} 
	
// Enable/disable controls on the dialog
void DialogCLEAN::enableControls(bool enable) {
	EnableWindow(GetDlgItem(m_dialogBox, ID_START), enable);
	SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETPOS, 0, 0);
}


// Returns TRUE if its OK to close
bool DialogCLEAN::shouldClose() {
	if (m_cleanThread && (!m_abortClean)) {
		m_abortClean = true;
		return false;
	}
	return true;
}

// Actually do the format
bool DialogCLEAN::runCleanCommand() {
	if (!m_bridge->isDiskPresent()) {
		MessageBox(m_dialogBox, L"No disk in drive. Clean aborted.", m_windowCaption.c_str(), MB_OK | MB_ICONINFORMATION);
		return false;
	}	

	bool ret = m_bridge->runCleaning([this](uint16_t position, uint16_t total) ->bool {
		SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETRANGE, 0, MAKELPARAM(0, total));
		SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETPOS, position, 0);
		return !m_abortClean;
	});

	if (!ret) {
		MessageBox(m_dialogBox, L"Drive cleaning aborted. Please remove disk and press OK.", m_windowCaption.c_str(), MB_OK | MB_ICONINFORMATION);
	}
	

	return ret;
}

// Handle starting the formatting process
void DialogCLEAN::doClean() {
	enableControls(false);


	m_abortClean = false;
	m_cleanThread = new std::thread([this]() {
		bool ret = runCleanCommand();
		enableControls(true);
		if (ret) MessageBox(m_dialogBox, L"Clean complete.\r\nPlease remove cleaning disk from drive.", m_windowCaption.c_str(), MB_OK | MB_ICONINFORMATION);
		SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETPOS, 0, 0);
		PostMessage(m_dialogBox, WM_USER, 0, 0);
	});
}


// Dialog window message handler
INT_PTR DialogCLEAN::handleDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_INITDIALOG:
		handleInitDialog(hwnd);
		return TRUE;

	case WM_USER+20:
		AllowSetForegroundWindow(GetCurrentProcessId());
		SetForegroundWindow(hwnd);
		SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
		BringWindowToTop(hwnd);
		SetActiveWindow(hwnd);
		AllowSetForegroundWindow(ASFW_ANY);
		break;

	case WM_SHOWWINDOW:
		if (wParam) PostMessage(hwnd, WM_USER + 20, 0, 0);
		break;

	case WM_DESTROY:
		if (m_cleanThread) {
			m_abortClean = true;
			if (m_cleanThread->joinable()) m_cleanThread->join();
			delete m_cleanThread;
			m_cleanThread = nullptr;
		}
		break;

	// Colour highlight for the link
	case WM_CTLCOLORSTATIC:
		if ((HWND)lParam == m_link) {
			SetTextColor((HDC)wParam, GetSysColor(COLOR_HOTLIGHT));
			SetBkColor((HDC)wParam, GetSysColor(COLOR_3DFACE));
			SetWindowLongPtr(hwnd, DWLP_MSGRESULT, (LONG_PTR)GetSysColorBrush(COLOR_3DFACE));
			return (INT_PTR)GetSysColorBrush(COLOR_3DFACE);  // Should be TRUE but doesnt work right unless its this!?!
		}
		break;

	// Mouse cursor for the link
	case WM_SETCURSOR:
		if ((HWND)wParam == m_link) {
			SetCursor(m_handPoint);
			SetWindowLongPtr(hwnd, DWLP_MSGRESULT, (LONG)TRUE);
			return TRUE;
		}
		break;

	case WM_USER:
		if (m_cleanThread) {			
			if (m_cleanThread->joinable()) m_cleanThread->join();
			delete m_cleanThread;
			m_cleanThread = nullptr;
			enableControls(true);
		}
		if (m_abortClean) EndDialog(hwnd, FALSE);
		break;


	case WM_COMMAND:
		switch (LOWORD(wParam)) {

		case IDC_MAKEYOUROWN:
			ShellExecute(m_dialogBox, L"OPEN", L"https://youtu.be/7E4fSypg0pk", NULL, NULL, SW_SHOW);
			break;

		case ID_START:
			doClean();
			break;

		case ID_CLOSE:
		case IDCANCEL:
			if (shouldClose())
				EndDialog(hwnd, FALSE);
			return TRUE;
		}
	}

	return FALSE;
}




