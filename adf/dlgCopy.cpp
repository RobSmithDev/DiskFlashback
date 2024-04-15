#include "dlgCopy.h"
#include <Windows.h>
#include "sectorCache.h"
#include "resource.h"
#include "adflib/src/adflib.h"
#include <CommCtrl.h>
#include "readwrite_floppybridge.h"
#include "readwrite_file.h"
#include "readwrite_dms.h"
#include "MountedVolume.h"


DialogCOPY::DialogCOPY(HINSTANCE hInstance, HWND hParent, SectorCacheEngine* io, MountedVolume* fs, const std::wstring& sourceADF) :
	m_hInstance(hInstance), m_hParent(hParent), m_io(io), m_fs(fs), m_backup(false), m_filename(sourceADF)  {
}

DialogCOPY::DialogCOPY(HINSTANCE hInstance, HWND hParent, SectorCacheEngine* io, MountedVolume* fs) :
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
		std::wstring filter;
		std::wstring defaultFormat;
		switch (m_io->getSystemType()) {
		case SectorType::stAmiga:
			dlg.lpstrFilter = L"Amiga Disk Files (*.adf)\0*.adf\0All Files(*.*)\0*.*\0\0";
			m_fileExtension = L"adf";
			break;
		case SectorType::stIBM:
			dlg.lpstrFilter = L"IBM Disk Files (*.img, *.ima)\0*.img;*.ima\0All Files(*.*)\0*.*\0\0";
			m_fileExtension = L"img";
			break;
		case SectorType::stAtari:
			dlg.lpstrFilter = L"Atari ST Disk Files (*.st)\0*.st\0All Files(*.*)\0*.*\0\0";
			m_fileExtension = L"st";
			break;
		default:
			return 0; // not supported
		}

		m_titleExtension = m_fileExtension;
		for (WCHAR& c : m_titleExtension) c = towupper(c);
		std::wstring title = L"Save Disk to " + m_titleExtension + L" File...";

		dlg.Flags = OFN_CREATEPROMPT | OFN_ENABLESIZING | OFN_EXPLORER | OFN_EXTENSIONDIFFERENT | OFN_HIDEREADONLY | OFN_NOREADONLYRETURN | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
		dlg.lpstrDefExt = m_fileExtension.c_str();
		dlg.lpstrFile = filename;
		dlg.lpstrTitle = title.c_str();
		dlg.nMaxFile = MAX_PATH;
		if (!GetSaveFileName(&dlg))  return 0;
		m_filename = filename;
	}
	else {
		// Extract extension
		size_t pos = m_filename.rfind(L".");
		if (pos != std::wstring::npos) {
			m_titleExtension = m_filename.substr(pos + 1);
			for (WCHAR& c : m_titleExtension) c = towupper(c);
		}

		std::wstring msg = L"All data on disk in drive "+m_fs->getMountPoint().substr(0,2) + L" will be overwritten.\nAre you sure you want to continue?";
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
		m_windowCaption = L"Copy Disk " + m_fs->getMountPoint().substr(0, 2) + L" to " + m_titleExtension + L" File";
		SetWindowText(ctrl, (L"Copying disk to " + fname).c_str());
	}
	else {
		m_windowCaption = L"Copy " + m_titleExtension + L" file to Disk " + m_fs->getMountPoint().substr(0, 2);
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
	m_fs->temporaryUnmountDrive();

	if (m_backup) {		
		
		uint32_t totalTracks = m_io->totalNumTracks();
		if (totalTracks == 0) totalTracks = m_fs->getTotalTracks();
		if (totalTracks == 0) totalTracks = 80 * 2; // unknown - default

		SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETRANGE, 0, MAKELPARAM(0, totalTracks));
		SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETPOS, 0, 0);
		DWORD written;

		uint32_t sectorNumber = 0;
		void* sectorData = malloc(m_io->sectorSize());
		if (!sectorData) return false;  // out of memory
		
		for (uint32_t track = 0; track < totalTracks; track++) {
			for (uint32_t sec = 0; sec < m_io->numSectorsPerTrack(); sec++) {
				if (!m_io->readData(sectorNumber, m_io->sectorSize(), sectorData)) {
					free(sectorData);
					return false;
				}
				if (m_abortCopy) {
					free(sectorData);
					return false;
				}

				if (!WriteFile(fle, sectorData, m_io->sectorSize(), &written, NULL)) {
					free(sectorData);
					MessageBox(m_hParent, L"Error writing to file. Copy aborted.", m_windowCaption.c_str(), MB_OK | MB_ICONEXCLAMATION);
					return false;
				}
				if (m_abortCopy) {
					free(sectorData);
					return false;
				}
				sectorNumber++;
			}
			SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETPOS, track + 1, 0);
		}

		free(sectorData);

		return true;
	}
	else {
		if (!source) return false;
		SectorRW_FloppyBridge* target = dynamic_cast<SectorRW_FloppyBridge*>(m_io);
		if (!target) return false; // *shouldnt* happen

		// Find out whats in the supplied file
		const uint32_t sectorSize = source->sectorSize();
		const uint32_t totalSectors = source->getDiskDataSize() / source->sectorSize();
		const uint32_t secPerTrack = source->numSectorsPerTrack();
		const uint32_t numCyl = totalSectors / (2 * secPerTrack);
		const uint32_t totalTracks = numCyl * 2;

		// Double density?
		bool isHD = secPerTrack > 11;		
		// Is the actual disk HD?
		if (target->numSectorsPerTrack() > 11) {
			if (!isHD) {
				if (MessageBox(m_hParent, L"Inserted disk was detected as High Density.\nSelected file is Double Density.\nAttempt to force writing as Double Density? (not recommended)", m_windowCaption.c_str(), MB_OKCANCEL | MB_ICONEXCLAMATION) != IDOK) return false;
				target->setForceDensityMode(FloppyBridge::BridgeDensityMode::bdmDDOnly);
			}
		}
		else {
			if (isHD) {
				if (MessageBox(m_hParent, L"Inserted disk was detected as Double Density.\nSelected file is High Density.\nAttempt to force writing as High Density? (not recommended)", m_windowCaption.c_str(), MB_OKCANCEL | MB_ICONEXCLAMATION) != IDOK) return false;
				target->setForceDensityMode(FloppyBridge::BridgeDensityMode::bdmHDOnly);
			}
		}		
		// Force the target to be the same as the source
		target->overwriteSectorSettings(source->getSystemType(), numCyl*2, source->numSectorsPerTrack(), source->sectorSize());

		SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETRANGE, 0, MAKELPARAM(0, totalTracks));
		SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETPOS, 0, 0);

		uint32_t sectorNumber = 0;
		void* sectorData = malloc(source->sectorSize());
		if (!sectorData) return false;  // out of memory

		uint32_t i = 0;
		for (uint32_t track = 0; track < totalTracks; track++) {
			for (uint32_t sec = 0; sec < secPerTrack; sec++) {
				if (!source->readData(i++, source->sectorSize(), sectorData)) {
					MessageBox(m_hParent, L"Error reading from input file. Copy aborted.", m_windowCaption.c_str(), MB_OK | MB_ICONEXCLAMATION);
					target->setForceDensityMode(FloppyBridge::BridgeDensityMode::bdmAuto);
					free(sectorData);
					return false;
				}
				if (!target->writeData(sectorNumber, source->sectorSize(), sectorData)) {
					target->setForceDensityMode(FloppyBridge::BridgeDensityMode::bdmAuto);
					free(sectorData);
					return false;
				}				
				if (m_abortCopy) {
					target->setForceDensityMode(FloppyBridge::BridgeDensityMode::bdmAuto);
					free(sectorData);
					return false;
				}
				sectorNumber++;
			}
			if (!target->flushWriteCache()) {
				target->setForceDensityMode(FloppyBridge::BridgeDensityMode::bdmAuto);
				free(sectorData);
				return false;
			}
			SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETPOS, track + 1, 0);
		}

		free(sectorData);

		target->setForceDensityMode(FloppyBridge::BridgeDensityMode::bdmAuto);

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
					else source = new SectorRW_File(m_filename, fle);
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
			m_fs->restoreUnmountedDrive();
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




