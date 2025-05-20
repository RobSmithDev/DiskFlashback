/* DiskFlashback, Copyright (C) 2021-2025 Robert Smith (@RobSmithDev)
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



#include "dlgDuplicator.h"
#include <Windows.h>
#include "sectorCache.h"
#include "resource.h"
#include "adflib/src/adflib.h"
#include <CommCtrl.h>
#include "readwrite_floppybridge.h"
#include "readwrite_file.h"
#include "readwrite_dms.h"
#include "MountedVolume.h"


DialogDuplicate::DialogDuplicate(HINSTANCE hInstance, HWND hParent, SectorCacheEngine* io, MountedVolume* fs, const std::wstring& sourceADF) :
	m_hInstance(hInstance), m_hParent(hParent), m_io(io), m_fs(fs),m_filename(sourceADF)  {
}

INT_PTR CALLBACK duplicateCallback(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_INITDIALOG) SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)lParam);
	DialogDuplicate* dlg = (DialogDuplicate*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	if (dlg) return dlg->handleDialogProc(hwnd, msg, wParam, lParam); else return FALSE;
}

INT_PTR DialogDuplicate::doModal() {	
	// Extract extension
	size_t pos = m_filename.rfind(L".");
	if (pos != std::wstring::npos) {
		m_titleExtension = m_filename.substr(pos + 1);
		for (WCHAR& c : m_titleExtension) c = towupper(c);
	}

	if (m_io->isDiskPresent()) {
		MessageBox(m_dialogBox, L"There is a disk in the drive. Overwite?", L"Disk in Drive", MB_OK | MB_ICONINFORMATION);
		return false;
	}	

	if (!m_fs->setLocked(true)) {
		MessageBox(m_dialogBox, L"Unable to start copy. There are files currently open.\nPlease close all open files and try again.", m_windowCaption.c_str(), MB_OK | MB_ICONEXCLAMATION);
		return false;
	}

	m_fs->temporaryUnmountDrive();

	bool isDMS = false;
	INT_PTR ret;
	m_sourceFile = CreateFile(m_filename.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if (m_sourceFile == INVALID_HANDLE_VALUE) {
		MessageBox(m_dialogBox, L"Unable to open input file. Disk copy aborted.", m_windowCaption.c_str(), MB_ICONSTOP | MB_OK);
		ret = IDOK;
	}
	else {
		char buf[5] = { 0 };
		DWORD read;
		if (!ReadFile(m_sourceFile, buf, 4, &read, NULL)) read = 0;
		SetFilePointer(m_sourceFile, 0, NULL, FILE_BEGIN);
		if (strcmp(buf, "DMS!") == 0) 
			m_source = new SectorRW_DMS(m_sourceFile);
		else m_source = new SectorRW_File(m_filename, m_sourceFile);
		ret = DialogBoxParam(m_hInstance, MAKEINTRESOURCE(IDD_DUPLICATE), GetDesktopWindow(), duplicateCallback, (LPARAM)this);
		delete m_source;
		m_sourceFile = INVALID_HANDLE_VALUE;   // this is closed when the above delete happens
	}

	m_fs->setLocked(false);
	SectorRW_FloppyBridge* target = dynamic_cast<SectorRW_FloppyBridge*>(m_io);
	target->triggerNewDiskMount();
	target->setForceDensityMode(FloppyBridge::BridgeDensityMode::bdmAuto);
	m_fs->restoreUnmountedDrive(true);

	return ret;
}

// Init dialog
void DialogDuplicate::handleInitDialog(HWND hwnd) {	
	m_dialogBox = hwnd;
	HWND ctrl = GetDlgItem(hwnd, IDC_CAPTION);

	HICON icon = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_ICON1));
	SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
	SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);

	// Get "just" the filename
	std::wstring fname = m_filename;
	size_t pos = fname.rfind(L"\\");
	if (pos != std::wstring::npos) fname = fname.substr(pos + 1);

	m_windowCaption = L"Duplicate " + m_titleExtension + L" to Multiple Disks " + m_fs->getMountPoint().substr(0, 2);
	SetWindowText(ctrl, (L"Copying " + fname+ L" to multiple disk").c_str());
	SetWindowText(hwnd, m_windowCaption.c_str());
	if (m_hParent) {
		RECT r;
		GetWindowRect(m_hParent, &r);
		SetWindowPos(hwnd, m_hParent, r.left + 70, r.top + 70, 0, 0, SWP_NOSIZE);
	}

	ctrl = GetDlgItem(hwnd, IDC_PROGRESS);
	SendMessage(ctrl, PBM_SETRANGE, 0, MAKELPARAM(0, 160));
	SendMessage(ctrl, PBS_SMOOTH, 0, 0);
	SetWindowLong(ctrl, GWL_STYLE, GetWindowLong(ctrl, GWL_STYLE) | PBS_SMOOTH);
	SendMessage(ctrl, PBM_SETSTEP, 1, 0);

	m_textColor = RGB(0, 0, 0);
	m_backgroundColor = RGB(255, 255, 255);
	m_backgroundBrush = NULL;

	m_statusText = GetDlgItem(hwnd, IDC_STATUS);
	HFONT fnt = (HFONT)SendMessage(m_statusText, WM_GETFONT, 0, 0);
	LOGFONT lf;
	GetObject(fnt, sizeof(lf), &lf);
	lf.lfWeight = FW_BOLD;
	lf.lfHeight = (LONG)(lf.lfHeight * 1.5f);

	fnt = CreateFontIndirect(&lf);
	SendMessage(m_statusText, WM_SETFONT, (WPARAM)fnt, TRUE);
	SendMessage(GetDlgItem(m_dialogBox, IDC_SUPRESS), BM_SETCHECK, BST_CHECKED, 0);

	BringWindowToTop(hwnd);
	SetForegroundWindow(hwnd);

	setProgressWindowHandle(hwnd);
} 

// Returns TRUE if its OK to close
bool DialogDuplicate::shouldClose() {
	if (m_copyThread && (!m_abortCopy)) {
		m_abortCopy = true;
		m_lastCursor = SetCursor(LoadCursor(0, IDC_WAIT));
		return false;
	}
	return true;
}

// Actually do the format
bool DialogDuplicate::runCopyCommand() {	
	SectorRW_FloppyBridge* target = dynamic_cast<SectorRW_FloppyBridge*>(m_io);
	if (!target) return false; // *shouldnt* happen

	// Find out whats in the supplied file
	const uint32_t sectorSize = m_source->sectorSize();
	const uint32_t totalSectors = m_source->getDiskDataSize() / m_source->sectorSize();
	const uint32_t secPerTrack = m_source->numSectorsPerTrack();
	const uint32_t numHeads = m_source->getNumHeads();
	const uint32_t totalTracks = m_source->totalNumTracks();
	const uint32_t numCyl = totalTracks / numHeads;

	// Double density?
	bool isHD = secPerTrack > 11;		
	// Is the actual disk HD?
	if (target->numSectorsPerTrack() > 11) {
		if (!isHD) target->setForceDensityMode(FloppyBridge::BridgeDensityMode::bdmDDOnly);
	}
	else {
		if (isHD) target->setForceDensityMode(FloppyBridge::BridgeDensityMode::bdmHDOnly);
	}		
	// Force the target to be the same as the source
	target->overwriteSectorSettings(m_source->getSystemType(), numCyl, numHeads, m_source->numSectorsPerTrack(), m_source->sectorSize());

	SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETRANGE, 0, MAKELPARAM(0, totalTracks));
	SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETPOS, 0, 0);
	resetProgress(totalTracks);

	uint32_t sectorNumber = 0;
	void* sectorData = malloc(m_source->sectorSize());
	if (!sectorData) return false;  // out of memory
	uint32_t i = 0;
	for (uint32_t track = 0; track < totalTracks; track++) {
		for (uint32_t sec = 0; sec < secPerTrack; sec++) {
			if (!m_source->readData(i++, m_source->sectorSize(), sectorData)) {
				MessageBox(m_dialogBox, L"Error reading from input file. Copy aborted.", m_windowCaption.c_str(), MB_OK | MB_ICONEXCLAMATION);				
				free(sectorData);
				return false;
			}
			if (!target->writeData(sectorNumber, m_source->sectorSize(), sectorData)) {				
				free(sectorData);
				return false;
			}				
			if (m_abortCopy) {				
				free(sectorData);
				return false;
			}
			sectorNumber++;
		}
		if (!target->flushWriteCache()) {			
			free(sectorData);
			return false;
		}
		SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETPOS, track + 1, 0);
		setDialogProgress(track + 1);
	}

	free(sectorData);
	

	return true;
}

// Handle starting the formatting process
void DialogDuplicate::doCopy() {
	m_abortCopy = false;
	HWND counterWindow = GetDlgItem(m_dialogBox, IDC_COUNTER);
	HWND checkboxWindow = GetDlgItem(m_dialogBox, IDC_SUPRESS);

	m_copyThread = new std::thread([this, counterWindow, checkboxWindow]() {
		SectorRW_FloppyBridge* target = dynamic_cast<SectorRW_FloppyBridge*>(m_io);
		target->setShoundPromptCallback([this, checkboxWindow]() -> bool {
			return SendMessage(checkboxWindow, BM_GETCHECK, 0, 0) != BST_CHECKED;
		});

		uint32_t counter = 1;
		while (!m_abortCopy) {
			m_textColor = RGB(255, 255, 255);
			m_backgroundColor = RGB(0, 0, 255);
			if (m_backgroundBrush) DeleteObject(m_backgroundBrush);
			m_backgroundBrush = CreateSolidBrush(m_backgroundColor);
			SetWindowText(m_statusText, L"...Please Insert Destination Disk...");
			MessageBeep(MB_ICONWARNING);
			resetProgress(10);
			// Wait for eject
			while ((!m_abortCopy) && (!m_io->isDiskPresent())) {
				Sleep(100);
			}
			if (m_abortCopy) break;

			if (m_io->isDiskWriteProtected()) {
				m_textColor = RGB(255, 255, 255);
				m_backgroundColor = RGB(255, 0, 255);
				DeleteObject(m_backgroundBrush); m_backgroundBrush = CreateSolidBrush(m_backgroundColor);
				BringWindowToTop(m_dialogBox);
				SetForegroundWindow(m_dialogBox);
				setBusyProgress();

				SetWindowText(m_statusText, L"Disk is Write Protected. Eject Disk.");
				MessageBeep(MB_ICONWARNING);

				// Wait for eject
				while ((!m_abortCopy) && (m_io->isDiskPresent())) {
					Sleep(100);
				}
			}
			else {
				m_blinking = false;
				m_textColor = RGB(0, 0, 0);
				m_backgroundColor = RGB(192, 192, 192);
				DeleteObject(m_backgroundBrush); m_backgroundBrush = CreateSolidBrush(m_backgroundColor);
				SetWindowText(m_statusText, L"Writing Image to Disk...");

				std::wstring msg = L"Copy Count: " + std::to_wstring(counter);
				SetWindowText(counterWindow, msg.c_str());

				m_io->resetCache();
				target->setForceDensityMode(FloppyBridge::BridgeDensityMode::bdmAuto);
				bool ret = runCopyCommand();
				target->setForceDensityMode(FloppyBridge::BridgeDensityMode::bdmAuto);
				m_io->resetCache();

				DeleteObject(m_backgroundBrush);

				if (ret) {
					m_textColor = RGB(255, 255, 255);
					m_backgroundColor = RGB(0, 128, 0);
					m_backgroundBrush = CreateSolidBrush(m_backgroundColor);
					BringWindowToTop(m_dialogBox);
					SetForegroundWindow(m_dialogBox);
					resetProgress(10);
					
					MessageBeep(MB_ICONINFORMATION);
					m_blinking = true;
					SetWindowText(m_statusText, L"✅ Copy Completed Successfully. Please Eject Disk.");
				}
				else {
					if (!m_abortCopy) {
						m_textColor = RGB(255, 255, 255);
						m_backgroundColor = RGB(255, 0, 0);
						m_backgroundBrush = CreateSolidBrush(m_backgroundColor);

						BringWindowToTop(m_dialogBox);
						SetForegroundWindow(m_dialogBox);
						setDialogError();

						MessageBeep(MB_ICONERROR);
						m_blinking = true;
						SetWindowText(m_statusText, L"❗Copy Failed. Please Eject Disk.");
					}
				}

				// Wait for eject
				while ((!m_abortCopy) && (m_io->isDiskPresent())) {
					Sleep(100);
				}
				SetWindowText(m_statusText, L"");
				counter++;
			}
			SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETPOS, 0, 0);			
		}

		target->setShoundPromptCallback(nullptr);

		PostMessage(m_dialogBox, WM_USER, 2, 0);
	});
}

// Dialog window message handler
INT_PTR DialogDuplicate::handleDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_INITDIALOG:
		handleInitDialog(hwnd);
		PostMessage(hwnd, WM_USER, 1, 0);		
		SetTimer(hwnd, 999, 200, NULL);
		return TRUE;

	case WM_TIMER:
		if (wParam == 999) {
			m_isVisible = !m_isVisible;
			RedrawWindow(m_statusText, NULL, 0, RDW_INVALIDATE | RDW_NOERASE);
			return 0;
		}
		break;

	case WM_DESTROY:
		KillTimer(hwnd, 999);
		if (m_copyThread) {
			m_abortCopy = true;
			if (m_copyThread->joinable()) m_copyThread->join();
			delete m_copyThread;
			m_copyThread = nullptr;
		}
		break;

	case WM_USER:
		switch (wParam) {
		case 1: doCopy(); break;
		case 2: 
				if (m_copyThread) {
					if (m_copyThread->joinable()) m_copyThread->join();
					delete m_copyThread;
					if (m_lastCursor) SetCursor(m_lastCursor);
					m_lastCursor = 0;
					m_copyThread = nullptr;
				}
				EndDialog(hwnd, FALSE);
				break;
		}
		break;

	case WM_CTLCOLORSTATIC:
		if ((HWND)lParam == m_statusText) {
			SetTextColor((HDC)wParam, (m_isVisible| !m_blinking) ? m_textColor : m_backgroundColor);
			SetBkColor((HDC)wParam, m_backgroundColor);
			return (INT_PTR)m_backgroundBrush; 
		}
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case ID_CLOSE:
		case IDCANCEL:
			if (shouldClose())
				EndDialog(hwnd, FALSE);
			return TRUE;
		}
	}

	return FALSE;
}




