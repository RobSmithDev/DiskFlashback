#include "dlgCopy.h"
#include <Windows.h>
#include "sectorCache.h"
#include "resource.h"
#include "adflib/src/adflib.h"
#include "adf.h"
#include <CommCtrl.h>
#include "readwrite_floppybridge.h"
#include "readwrite_file.h"
#include "readwrite_dms.h"

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

DialogCOPY::DialogCOPY(HINSTANCE hInstance, HWND hParent, SectorCacheEngine* io, fs* fs, const std::wstring& sourceADF) :
	m_hInstance(hInstance), m_hParent(hParent), m_io(io), m_fs(fs), m_backup(false), m_filename(sourceADF)  {
}

DialogCOPY::DialogCOPY(HINSTANCE hInstance, HWND hParent, SectorCacheEngine* io, fs* fs) :
	m_hInstance(hInstance), m_hParent(hParent), m_io(io), m_fs(fs), m_backup(true) {
}


INT_PTR CALLBACK copyCallback(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_INITDIALOG) SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)lParam);
	DialogCOPY* dlg = (DialogCOPY*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	if (dlg) return dlg->handleDialogProc(hwnd, msg, wParam, lParam); else return FALSE;
}

INT_PTR DialogCOPY::doModal() {
	if (m_backup) {
		// Request filename
		OPENFILENAME dlg;
		WCHAR filename[MAX_PATH] = { 0 };
		memset(&dlg, 0, sizeof(dlg));
		dlg.lStructSize = sizeof(dlg);
		dlg.hwndOwner = m_hParent;
		dlg.lpstrFilter = L"Amiga Disk Files (*.adf)\0*.adf\0All Files (*.*)\0*.*\0\0";
		dlg.lpstrTitle = L"Save Disk to ADF...";
		dlg.Flags = OFN_CREATEPROMPT | OFN_ENABLESIZING | OFN_EXPLORER | OFN_EXTENSIONDIFFERENT | OFN_HIDEREADONLY | OFN_NOREADONLYRETURN | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
		dlg.lpstrDefExt = L"adf";
		dlg.lpstrFile = filename;
		dlg.nMaxFile = MAX_PATH;
		if (!GetSaveFileName(&dlg)) {
			DWORD moo = CommDlgExtendedError();
			return 0;
		}
		m_filename = filename;
	}
	else {
		std::wstring msg = L"All data on disk in drive "+m_fs->driveLetter().substr(0,2) + L" will be overwritten.\nAre you sure you want to continue?";
		if (MessageBox(m_hParent, msg.c_str(), L"Copy file to Disk", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return 0;
	}

	if (!m_io->isDiskPresent()) {
		MessageBox(m_hParent, L"There is no disk in the drive. Copy aborted.", L"Drive empty", MB_OK | MB_ICONINFORMATION);
		return false;
	}
	if ((!m_backup) && (m_io->isDiskWriteProtected())) {
		MessageBox(m_hParent, L"Disk in drive is write protected. Copy aborted.", L"Write Protected", MB_OK | MB_ICONINFORMATION);
		return false;
	}

	return DialogBoxParam(m_hInstance, MAKEINTRESOURCE(IDD_COPY), GetDesktopWindow(), copyCallback, (LPARAM)this);
}

// Init dialog
void DialogCOPY::handleInitDialog(HWND hwnd) {	
	m_dialogBox = hwnd;
	HWND ctrl = GetDlgItem(hwnd, IDC_CAPTION);

	// Get "just" the filename
	std::wstring fname = m_filename;
	size_t pos = fname.rfind(L"\\");
	if (pos != std::wstring::npos) fname = fname.substr(pos + 1);

	if (m_backup) {
		m_windowCaption = L"Copy Disk " + m_fs->driveLetter().substr(0, 2) + L" to ADF File";
		SetWindowText(ctrl, (L"Copying disk to " + fname).c_str());
	}
	else {
		m_windowCaption = L"Copy file to Disk " + m_fs->driveLetter().substr(0, 2);
		SetWindowText(ctrl, (L"Copying " + fname+ L" to disk").c_str());
	}
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

	BringWindowToTop(hwnd);
	SetForegroundWindow(hwnd);
} 

// Returns TRUE if its OK to close
bool DialogCOPY::shouldClose() {
	if (m_copyThread && (!m_abortCopy)) {
		m_abortCopy = true;
		m_lastCursor = SetCursor(LoadCursor(0, IDC_WAIT));
		return false;
	}
	return true;
}

// Actually do the format
bool DialogCOPY::runCopyCommand(HANDLE fle, SectorCacheEngine* source) {
	m_fs->unmountVolume();

	uint8_t sector[512];
	uint32_t sectorNumber = 0;

	if (m_backup) {		
		uint32_t totalTracks = m_fs->device()->cylinders * m_fs->device()->heads;
		SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETRANGE, 0, MAKELPARAM(0, totalTracks));
		SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETPOS, 0, 0);
		DWORD written;
		
		for (uint32_t track = 0; track < totalTracks; track++) {
			for (uint32_t sec = 0; sec < m_fs->device()->sectors; sec++) {
				if (!m_io->readData(sectorNumber, 512, sector)) 
					return false;
				if (m_abortCopy) 
					return false;
				if (!WriteFile(fle, sector, 512, &written, NULL)) {
					MessageBox(m_hParent, L"Error writing to ADF file. Copy aborted.", m_windowCaption.c_str(), MB_OK | MB_ICONEXCLAMATION);
					return false;
				}
				if (m_abortCopy) 
					return false;
				sectorNumber++;
			}
			SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETPOS, track + 1, 0);
		}
		return true;
	}
	else {
		if (!source) return false;
		SectorRW_FloppyBridge* bridge = dynamic_cast<SectorRW_FloppyBridge*>(m_io);
		if (!bridge) return false; // *shouldnt* happen

		// Find out whats in the supplied file
		uint32_t totalSectors = source->getDiskDataSize() / 512;
		uint32_t numCyl = totalSectors / (2 * 11);

		// Double density?
		bool isHD = false;
		uint32_t secPerTrack = 0;
		if (numCyl < 84) {
			secPerTrack = 11;
		}
		else {
			numCyl /= 2;
			if (numCyl < 84) {
				secPerTrack = 22;
				isHD = true;
			}
			else {
				MessageBox(m_hParent, L"Unrecognised input file. Copy aborted.", m_windowCaption.c_str(), MB_OK | MB_ICONEXCLAMATION);
				return false;
			}
		}
		
		// Is the actual disk HD?
		if (m_io->numSectorsPerTrack() == 22) {
			if (!isHD) {
				if (MessageBox(m_hParent, L"Inserted disk was detected as High Density.\nSelected file is Double Density.\nAttempt to force writing as Double Density? (not recommended)", m_windowCaption.c_str(), MB_OKCANCEL | MB_ICONEXCLAMATION) != IDOK) return false;
				bridge->setForceDensityMode(FloppyBridge::BridgeDensityMode::bdmDDOnly);
			}
		}
		else {
			if (isHD) {
				if (MessageBox(m_hParent, L"Inserted disk was detected as Double Density.\nSelected file is High Density.\nAttempt to force writing as High Density? (not recommended)", m_windowCaption.c_str(), MB_OKCANCEL | MB_ICONEXCLAMATION) != IDOK) return false;
				bridge->setForceDensityMode(FloppyBridge::BridgeDensityMode::bdmHDOnly);
			}
		}		
		m_io->resetCache();

		uint32_t totalTracks = numCyl * 2;
		SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETRANGE, 0, MAKELPARAM(0, totalTracks));
		SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETPOS, 0, 0);
		DWORD written;

		uint32_t i = 0;
		for (uint32_t track = 0; track < totalTracks; track++) {
			for (uint32_t sec = 0; sec < secPerTrack; sec++) {
				if (!source->readData(i++, 512, sector)) {
					MessageBox(m_hParent, L"Error reading from input file. Copy aborted.", m_windowCaption.c_str(), MB_OK | MB_ICONEXCLAMATION);
					bridge->setForceDensityMode(FloppyBridge::BridgeDensityMode::bdmAuto);
					return false;
				}
				if (!m_io->writeData(sectorNumber, 512, sector)) {
					bridge->setForceDensityMode(FloppyBridge::BridgeDensityMode::bdmAuto);
					return false;
				}				
				if (m_abortCopy) {
					bridge->setForceDensityMode(FloppyBridge::BridgeDensityMode::bdmAuto);
					return false;
				}
				sectorNumber++;
			}
			if (!m_io->flushWriteCache()) {
				bridge->setForceDensityMode(FloppyBridge::BridgeDensityMode::bdmAuto);
				return false;
			}
			SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETPOS, track + 1, 0);
		}

		
		bridge->setForceDensityMode(FloppyBridge::BridgeDensityMode::bdmAuto);

		return true;
	}
}

// Handle starting the formatting process
void DialogCOPY::doCopy() {
	if (m_fs->setLocked(true)) {
		m_abortCopy = false;

		m_copyThread = new std::thread([this]() {
			HANDLE fle;
			bool isDMS = false;
			SectorCacheEngine* source = nullptr;
			if (m_backup) {
				fle = CreateFile(m_filename.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
				if (fle == INVALID_HANDLE_VALUE) MessageBox(m_hParent, L"Unable to write to file. Disk copy aborted.", m_windowCaption.c_str(), MB_ICONSTOP | MB_OK);
			}
			else {
				fle = CreateFile(m_filename.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
				if (fle == INVALID_HANDLE_VALUE) MessageBox(m_hParent, L"Unable to open input file. Disk copy aborted.", m_windowCaption.c_str(), MB_ICONSTOP | MB_OK); else {
					char buf[5] = { 0 };
					DWORD read;
					if (!ReadFile(fle, buf, 4, &read, NULL)) read = 0;
					SetFilePointer(fle, 0, NULL, FILE_BEGIN);
					if (strcmp(buf, "DMS!") == 0) {
						source = new SectorRW_DMS(fle);
					}
					else source = new SectorRW_File(512 * 44, fle);
				}
			}
			bool ret = false;
			if (fle != INVALID_HANDLE_VALUE) {
				m_io->setWritingOnlyMode(!m_backup);
				ret = runCopyCommand(fle, source);
				if (!source) CloseHandle(fle);
				if (!m_backup) m_io->resetCache();
				if ((!ret) && (m_backup)) DeleteFile(m_filename.c_str());
			}
			else m_abortCopy = true;

			m_io->setWritingOnlyMode(false);
			m_fs->setLocked(false);
			m_fs->remountVolume();
			if (source) delete source;
			if (ret) {
				MessageBox(m_dialogBox, L"Copy completed.", m_windowCaption.c_str(), MB_OK | MB_ICONINFORMATION);
			}
			else 
				if (!m_abortCopy) {
					MessageBox(m_dialogBox, L"Copy aborted.", m_windowCaption.c_str(), MB_OK | MB_ICONINFORMATION);
				}
			SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETPOS, 0, 0);
			PostMessage(m_dialogBox, WM_USER, 0, 0);
		});
	}
	else {
		MessageBox(m_dialogBox, L"Unable to start copy. There are files currently open.\nPlease close all open files and try again.", m_windowCaption.c_str(), MB_OK | MB_ICONEXCLAMATION);
	}
}

// Dialog window message handler
INT_PTR DialogCOPY::handleDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_INITDIALOG:
		handleInitDialog(hwnd);
		PostMessage(hwnd, WM_USER, 1, 0);
		return TRUE;

	case WM_DESTROY:
		if (m_copyThread) {
			m_abortCopy = true;
			if (m_copyThread->joinable()) m_copyThread->join();
			delete m_copyThread;
			m_copyThread = nullptr;
		}
		break;

	case WM_USER:
		if (wParam == 1) doCopy(); else {
			if (m_copyThread) {
				if (m_copyThread->joinable()) m_copyThread->join();
				delete m_copyThread;
				if (m_lastCursor) SetCursor(m_lastCursor);
				m_lastCursor = 0;
				m_copyThread = nullptr;
			}
			EndDialog(hwnd, FALSE);
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




