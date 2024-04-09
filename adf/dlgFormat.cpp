#include <dokan/dokan.h>
#include <CommCtrl.h>

#include "dlgFormat.h"
#include "sectorCache.h"
#include "resource.h"
#include "adflib/src/adflib.h"
#include "MountedVolume.h"

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

DialogFORMAT::DialogFORMAT(HINSTANCE hInstance, HWND hParent, SectorCacheEngine* io, MountedVolume* fs) :
	m_hInstance(hInstance), m_hParent(hParent), m_io(io), m_fs(fs) {
}


INT_PTR CALLBACK formatCallback(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_INITDIALOG) SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)lParam);
	DialogFORMAT* dlg = (DialogFORMAT*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	if (dlg) return dlg->handleDialogProc(hwnd, msg, wParam, lParam); else return FALSE;
}

INT_PTR DialogFORMAT::doModal() {
	return DialogBoxParam(m_hInstance, MAKEINTRESOURCE(IDD_FORMAT), GetDesktopWindow(), formatCallback, (LPARAM)this);
}

// Init dialog
void DialogFORMAT::handleInitDialog(HWND hwnd) {
	m_dialogBox = hwnd;
	m_windowCaption = L"Format ";
	m_windowCaption += (m_io->isPhysicalDisk() ? L"Disk Drive " : L"ADF File ");
	m_windowCaption += m_fs->getMountPoint().substr(0, 2);

	SetWindowText(hwnd, m_windowCaption.c_str());
	if (m_hParent) {
		RECT r;
		GetWindowRect(m_hParent, &r);
		SetWindowPos(hwnd, m_hParent, r.left + 70, r.top + 70, 0, 0, SWP_NOSIZE);
	}

	// Populate controls
	HWND ctrl = GetDlgItem(hwnd, IDC_FILESYSTEM);
	SendMessage(ctrl, CB_ADDSTRING, 0, (LPARAM)L"OFS");
	SendMessage(ctrl, CB_ADDSTRING, 1, (LPARAM)L"FFS");
	SendMessage(ctrl, CB_SETCURSEL, 0, 0);

	ctrl = GetDlgItem(hwnd, IDC_LABEL);
	SendMessage(ctrl, EM_SETLIMITTEXT, 34, 0);
	SetWindowText(ctrl, L"empty");

	ctrl = GetDlgItem(hwnd, IDC_QUICK);
	SendMessage(ctrl, BM_SETCHECK, m_io->isPhysicalDisk() ? BST_UNCHECKED : BST_CHECKED, 0);

	ctrl = GetDlgItem(hwnd, IDC_CACHE);
	SendMessage(ctrl, BM_SETCHECK, BST_UNCHECKED, 0);

	ctrl = GetDlgItem(hwnd, IDC_INT);
	SendMessage(ctrl, BM_SETCHECK, BST_UNCHECKED, 0);

	ctrl = GetDlgItem(hwnd, IDC_BB);
	SendMessage(ctrl, BM_SETCHECK, BST_UNCHECKED, 0);

	ctrl = GetDlgItem(hwnd, IDC_PROGRESS);
	SendMessage(ctrl, PBM_SETRANGE, 0, MAKELPARAM(0, 160));
	SendMessage(ctrl, PBS_SMOOTH, 0, 0);
	SetWindowLong(ctrl, GWL_STYLE, GetWindowLong(ctrl, GWL_STYLE) | PBS_SMOOTH);
	SendMessage(ctrl, PBM_SETSTEP, 1, 0);

	BringWindowToTop(hwnd);
	SetForegroundWindow(hwnd);
} 
	
// Enable/disable controls on the dialog
void DialogFORMAT::enableControls(bool enable) {
	EnableWindow(GetDlgItem(m_dialogBox, IDC_FILESYSTEM), enable);
	EnableWindow(GetDlgItem(m_dialogBox, IDC_LABEL), enable);
	EnableWindow(GetDlgItem(m_dialogBox, IDC_QUICK), enable);
	EnableWindow(GetDlgItem(m_dialogBox, IDC_CACHE), enable);
	EnableWindow(GetDlgItem(m_dialogBox, IDC_INT), enable);
	EnableWindow(GetDlgItem(m_dialogBox, IDC_BB), enable);
	EnableWindow(GetDlgItem(m_dialogBox, ID_START), enable);
	SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETPOS, 0, 0);
}


// Returns TRUE if its OK to close
bool DialogFORMAT::shouldClose() {
	if (m_formatThread && (!m_abortFormat)) {
		m_abortFormat = true;
		m_lastCursor = SetCursor(LoadCursor(0, IDC_WAIT));
		return false;
	}
	return true;
}

// Actually do the format
bool DialogFORMAT::runFormatCommand(bool quickFormat, bool dirCache, bool intMode, bool installBB, bool doFFS, const std::string& volumeLabel) {
	if (!m_io->isDiskPresent()) {
		MessageBox(m_hParent, L"No disk in drive. Format aborted.", m_windowCaption.c_str(), MB_OK | MB_ICONINFORMATION);
		return false;
	}
	if (m_io->isDiskWriteProtected()) {
		MessageBox(m_hParent, L"Disk in drive is write protected. Format aborted.", m_windowCaption.c_str(), MB_OK | MB_ICONINFORMATION);
		return false;
	}
	m_fs->temporaryUnmountDrive();

	uint8_t mode = doFFS ? FSMASK_FFS : 0;
	if (intMode) mode |= FSMASK_INTL;
	if (dirCache) mode |= FSMASK_DIRCACHE;

	uint32_t totalTracks = m_fs->getTotalTracks();
	if (totalTracks == 0) totalTracks = 80 * 2;
	SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETRANGE, 0, MAKELPARAM(0, totalTracks + 4));
	SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETPOS, 0, 0);

	m_io->resetCache();

	uint32_t progress = 0;
	if (!quickFormat) {
		uint8_t sector[512];
		uint32_t sectorNumber = 0;
		memset(sector, 0, 512);
		for (uint32_t track = 0; track < totalTracks; track++) {
			for (uint32_t sec = 0; sec < m_fs->getADFDevice()->sectors; sec++) {
				if (m_abortFormat) return false;
				if (!m_io->writeData(sectorNumber, 512, sector)) 
					return false;
				sectorNumber++;
			}
			if (!m_io->flushWriteCache()) return false;
			SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETPOS, track+1, 0);
		}
		progress = totalTracks;
	}

	if (m_abortFormat) return false;
	if (adfCreateFlop(m_fs->getADFDevice(), volumeLabel.c_str(), mode) != RC_OK) return false;
	if (!m_io->flushWriteCache()) return false;

	SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETPOS, progress + 2, 0);

	if (installBB) {
		if (m_abortFormat) return false;
		m_io->setWritingOnlyMode(false);
		m_fs->setLocked(false);
		m_fs->restoreUnmountedDrive();
		if (!m_fs->installAmigaBootBlock()) return false;
		if (!m_io->flushWriteCache()) return false;
	} 

	SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETRANGE, 0, MAKELPARAM(0, totalTracks + 4));
	SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETPOS, totalTracks + 4, 0);

	return true;
}

// Handle starting the formatting process
void DialogFORMAT::doFormat() {
	enableControls(false);
	if (m_fs->setLocked(true)) {
		m_abortFormat = false;

		bool quickFormat = SendMessage(GetDlgItem(m_dialogBox, IDC_QUICK), BM_GETCHECK, 0, 0) == BST_CHECKED;
		bool dirCache = SendMessage(GetDlgItem(m_dialogBox, IDC_CACHE), BM_GETCHECK, 0, 0) == BST_CHECKED;
		bool intMode = SendMessage(GetDlgItem(m_dialogBox, IDC_INT), BM_GETCHECK, 0, 0) == BST_CHECKED;
		bool installBB = SendMessage(GetDlgItem(m_dialogBox, IDC_BB), BM_GETCHECK, 0, 0) == BST_CHECKED;
		bool doFFS = SendMessage(GetDlgItem(m_dialogBox, IDC_FILESYSTEM), CB_GETCURSEL, 0, 0) == 1;
		std::string volumeLabel;
		WCHAR name[64];
		GetWindowText(GetDlgItem(m_dialogBox, IDC_LABEL), name, 64);
		wideToAnsi(name, volumeLabel);

		m_formatThread = new std::thread([this, quickFormat, dirCache, intMode, installBB, doFFS, volumeLabel]() {
			m_io->setWritingOnlyMode(true);
			bool ret = runFormatCommand(quickFormat, dirCache, intMode, installBB, doFFS, volumeLabel);
			m_io->setWritingOnlyMode(false);
			m_fs->setLocked(false);
			m_fs->restoreUnmountedDrive();
			enableControls(true);
			if (ret) {
				MessageBox(m_dialogBox, L"Format complete.", m_windowCaption.c_str(), MB_OK | MB_ICONINFORMATION);
			}
			else 
			if (!m_abortFormat) {
				MessageBox(m_dialogBox, L"Format aborted.", m_windowCaption.c_str(), MB_OK | MB_ICONINFORMATION);
			}
			SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETPOS, 0, 0);
			// Signal thread is complete
			PostMessage(m_dialogBox, WM_USER, 0, 0);
		});
	}
	else {
		MessageBox(m_dialogBox, L"Unable to start format. There are files currently open.\nPlease close all open files and try again.", m_windowCaption.c_str(), MB_OK | MB_ICONEXCLAMATION);
	}
}

// Dialog window message handler
INT_PTR DialogFORMAT::handleDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_INITDIALOG:
		handleInitDialog(hwnd);
		return TRUE;

	case WM_DESTROY:
		if (m_formatThread) {
			m_abortFormat = true;
			if (m_formatThread->joinable()) m_formatThread->join();
			delete m_formatThread;
			m_formatThread = nullptr;
		}
		break;

	case WM_USER:
		if (m_formatThread) {			
			if (m_formatThread->joinable()) m_formatThread->join();
			delete m_formatThread;
			if (m_lastCursor) SetCursor(m_lastCursor);
			m_lastCursor = 0;
			m_formatThread = nullptr;
			enableControls(true);
		}
		if (m_abortFormat) EndDialog(hwnd, FALSE);
		break;


	case WM_COMMAND:
		switch (LOWORD(wParam)) {

		case ID_START:
			if (MessageBox(m_dialogBox, L"WARNING: Formatting will erase ALL data on this disk.\nTo format the disk click OK. To Quit click CANCEL", m_windowCaption.c_str(), MB_OKCANCEL | MB_ICONWARNING) != IDOK) return 0;
			doFormat();
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




