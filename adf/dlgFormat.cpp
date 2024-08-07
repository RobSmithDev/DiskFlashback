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

#include "dlgFormat.h"
#include "sectorCache.h"
#include "resource.h"
#include "adflib/src/adflib.h"
#include "fatfs/source/ff.h"
#include "MountedVolume.h"
#include "readwrite_floppybridge.h"
#include "ibm_sectors.h"

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

	HICON icon = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_ICON1));
	SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
	SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);


	m_windowCaption = L"Format ";
	m_windowCaption += (m_io->isPhysicalDisk() ? L"Disk Drive " : L"Disk Image File ");
	m_windowCaption += m_fs->getMountPoint().substr(0, 2);

	SetWindowText(hwnd, m_windowCaption.c_str());
	if (m_hParent) {
		RECT r;
		GetWindowRect(m_hParent, &r);
		SetWindowPos(hwnd, m_hParent, r.left + 70, r.top + 70, 0, 0, SWP_NOSIZE);
	}

	// Populate controls
	HWND ctrl = GetDlgItem(hwnd, IDC_FILESYSTEM);
	SendMessage(ctrl, CB_ADDSTRING, 0, (LPARAM)L"Amiga OFS");
	SendMessage(ctrl, CB_ADDSTRING, 1, (LPARAM)L"Amiga FFS");
	SendMessage(ctrl, CB_ADDSTRING, 2, (LPARAM)L"FAT (IBM PC)");
	SendMessage(ctrl, CB_ADDSTRING, 3, (LPARAM)L"Atari (Single Sided)");
	SendMessage(ctrl, CB_ADDSTRING, 4, (LPARAM)L"Atari (Double Sided)");
	SendMessage(ctrl, CB_ADDSTRING, 5, (LPARAM)L"Atari (Extended)");
	switch (m_io->getSystemType()) {
	case SectorType::stAmiga: SendMessage(ctrl, CB_SETCURSEL, 0, 0); break;
	case SectorType::stIBM: SendMessage(ctrl, CB_SETCURSEL, 2, 0); break;
	case SectorType::stAtari: SendMessage(ctrl, CB_SETCURSEL, 4, 0); break;
	default: SendMessage(ctrl, CB_SETCURSEL, 0, 0); break;
	}
	

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

	CheckRadioButton(hwnd, IDC_DD, IDC_AUTO, IDC_AUTO);

	BringWindowToTop(hwnd);
	SetForegroundWindow(hwnd);
	enableControls(true);
} 
	
// Enable/disable controls on the dialog
void DialogFORMAT::enableControls(bool enable) {
	EnableWindow(GetDlgItem(m_dialogBox, IDC_FILESYSTEM), enable);

	uint32_t sel = (uint32_t)SendMessage(GetDlgItem(m_dialogBox, IDC_FILESYSTEM), CB_GETCURSEL, 0, 0);

	EnableWindow(GetDlgItem(m_dialogBox, IDC_LABEL), enable);
	EnableWindow(GetDlgItem(m_dialogBox, IDC_QUICK), enable);
	EnableWindow(GetDlgItem(m_dialogBox, IDC_CACHE), enable && (sel < 2));
	EnableWindow(GetDlgItem(m_dialogBox, IDC_INT), enable && (sel < 2));
	EnableWindow(GetDlgItem(m_dialogBox, IDC_BB), enable && (sel < 2));
	EnableWindow(GetDlgItem(m_dialogBox, ID_START), enable);

	SectorRW_FloppyBridge* bridge = dynamic_cast<SectorRW_FloppyBridge*>(m_io);

	EnableWindow(GetDlgItem(m_dialogBox, IDC_DD), enable && bridge);
	EnableWindow(GetDlgItem(m_dialogBox, IDC_HD), enable && bridge);
	EnableWindow(GetDlgItem(m_dialogBox, IDC_AUTO), enable && bridge);
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

// see if the block if memory is all zeros
bool isBlank(uint8_t* mem, uint32_t size) {
	uint32_t* m = (uint32_t*)mem;
	while (size >= 4) {
		if (*m++) return false;
		size -= 4;
	}
	return true;
}

// Actually do the format
bool DialogFORMAT::runFormatCommand(bool quickFormat, bool dirCache, bool intMode, bool installBB, uint32_t density, uint32_t formatMode, const std::string& volumeLabel) {
	if (!m_io->isDiskPresent()) {
		MessageBox(m_dialogBox, L"No disk in drive. Format aborted.", m_windowCaption.c_str(), MB_OK | MB_ICONINFORMATION);
		return false;
	}
	if (m_io->isDiskWriteProtected()) {
		MessageBox(m_dialogBox, L"Disk in drive is write protected. Format aborted.", m_windowCaption.c_str(), MB_OK | MB_ICONINFORMATION);
		return false;
	}
	m_fs->temporaryUnmountDrive();

	uint8_t mode = formatMode ? ADF_DOSFS_FFS : 0;
	if (intMode) mode |= ADF_DOSFS_INTL;
	if (dirCache) mode |= ADF_DOSFS_DIRCACHE;

	uint32_t totalTracks = 80 * 2;
	//if (totalTracks == 0) totalTracks = 80 * 2;
	SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETRANGE, 0, MAKELPARAM(0, totalTracks + 4));
	SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETPOS, 0, 0);

	SectorRW_FloppyBridge* bridge = dynamic_cast<SectorRW_FloppyBridge*>(m_io);
	bool isHD;
	switch (density) {
	case 0: isHD = false; break;
	case 1: isHD = true; break;
	default: isHD = bridge->isHD(); break;
	}
	bridge->setForceDensityMode(isHD ? FloppyBridge::BridgeDensityMode::bdmHDOnly : FloppyBridge::BridgeDensityMode::bdmDDOnly);

	m_io->resetCache();
	uint32_t numSectors = 0;
	uint32_t numHeads = 2;
	if (bridge) {
		switch (formatMode) {
		case 0:
		case 1:
			numSectors = isHD ? 22 : 11;
			bridge->overwriteSectorSettings(SectorType::stAmiga, totalTracks/2,2,  numSectors, 512);
			break;
		case 2:
			numSectors = isHD ? 18 : 9;
			bridge->overwriteSectorSettings(SectorType::stIBM, totalTracks/2,2,  numSectors, 512);
			break;
		case 3:
			numSectors = isHD ? 18 : 9;
			totalTracks = 80;
			numHeads = 1;
			bridge->overwriteSectorSettings(SectorType::stAtari, totalTracks, 1, numSectors, 512);
			break;
		case 4:
			numSectors = isHD ? 18 : 9;
			totalTracks = 80 * 2;
			bridge->overwriteSectorSettings(SectorType::stAtari, totalTracks / 2, 2, numSectors, 512);
			break;
		case 5:
			numSectors = isHD ? 22 : 11;
			totalTracks = 83 * 2;
			bridge->overwriteSectorSettings(SectorType::stAtari, totalTracks / 2, 2, numSectors, 512);
			break;

		default:
			return false;
		}
	}

	uint32_t progress = 0;
	if (!quickFormat) {
		uint8_t sector[512];
		uint32_t sectorNumber = 0;
		memset(sector, 0, 512);
		for (uint32_t track = 0; track < totalTracks; track++) {
			for (uint32_t sec = 0; sec < numSectors; sec++) {
				if (m_abortFormat) 
					return false;
				if (!m_io->writeData(sectorNumber, DEFAULT_SECTOR_BYTES, sector))
					return false;
				sectorNumber++;
			}
			if (!m_io->flushWriteCache()) 
				return false;
			SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETPOS, track+1, 0);
		}
		progress = totalTracks;
		m_io->resetCache();
	}
	else bridge->createBlankSectors();

	if (m_abortFormat) return false;


	switch (formatMode) {
	case 0:  // AMIGAAAA
	case 1:
	{
		AdfDevice* dev = adfDevOpenWithDriver(DISKFLASHBACK_AMIGA_DRIVER, (char*)m_io, ADF_ACCESS_MODE_READWRITE);
		if (!dev) return false;
		adfDevMount(dev);
		dev->heads = 2;
		dev->sectors = numSectors;
		dev->cylinders = totalTracks / 2;
		if (adfCreateFlop(dev, volumeLabel.c_str(), mode) != ADF_RC_OK) {
			adfDevUnMount(dev);
			return false;
		}
		adfDevUnMount(dev);
	}
		break;
	case 2:  // PC
	{
		BYTE work[FF_MAX_SS];
		MKFS_PARM opt;
		getMkFsParams(isHD, SectorType::stIBM, opt);
		if (f_mkfs(L"\\", &opt, work, sizeof(work)) != FR_OK) return false;
		std::wstring t;
		ansiToWide(volumeLabel, t);
		FATFS* m_fatDevice = (FATFS*)malloc(sizeof(FATFS));
		if (m_fatDevice) {
			memset(m_fatDevice, 0, sizeof(FATFS));
			f_mount(m_fatDevice, L"", 1);
			f_setlabel(t.c_str());
			f_unmount(L"\\");
			free(m_fatDevice);
		}
	}
	break;
	case 3: // Atari ST
	case 4: // Atari ST
	case 5: // Atari ST
		// This is real hacky.  We get a copy of what the blank disk should look like and then just write the non-zero tracks
	{
		uint32_t memSize = 0;
		uint8_t* mem = createBasicDisk(volumeLabel, totalTracks / numHeads, numSectors, numHeads, &memSize);
		if (!mem) return false;
		uint8_t* trackStart = mem;
		uint32_t dataPerTrack = DEFAULT_SECTOR_BYTES * numSectors;
		for (uint32_t track = 0; track < totalTracks; track++) {
			// See if theres anything here
			if (!isBlank(trackStart, dataPerTrack)) {
				uint8_t* sector = trackStart;
				for (uint32_t sec = 0; sec < numSectors; sec++) {
					if (!m_io->writeData((track * numSectors) + sec, DEFAULT_SECTOR_BYTES, sector)) {
						free(mem);
						return false;
					}
					sector += DEFAULT_SECTOR_BYTES;
				}
				// Need to write this track
				if (!m_io->flushWriteCache()) {
					free(mem);
					return false;
				}
			}
			trackStart += dataPerTrack;
		}
		free(mem);
	}
	break;
	}

	if (!m_io->flushWriteCache()) return false;

	SendMessage(GetDlgItem(m_dialogBox, IDC_PROGRESS), PBM_SETPOS, progress + 2, 0);

	bridge->triggerNewDiskMount();

	if (installBB && formatMode<2) {
		if (m_abortFormat) return false;
		m_io->setWritingOnlyMode(false);
		m_fs->setLocked(false);
		m_fs->restoreUnmountedDrive(false);
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

		uint32_t density = 2;
		if (SendMessage(GetDlgItem(m_dialogBox, IDC_DD), BM_GETCHECK, 0, 0) == BST_CHECKED) density = 0; else
			if (SendMessage(GetDlgItem(m_dialogBox, IDC_HD), BM_GETCHECK, 0, 0) == BST_CHECKED) density = 1;

		uint32_t formatMode = (uint32_t)SendMessage(GetDlgItem(m_dialogBox, IDC_FILESYSTEM), CB_GETCURSEL, 0, 0);
		std::string volumeLabel;
		WCHAR name[64];
		GetWindowText(GetDlgItem(m_dialogBox, IDC_LABEL), name, 64);
		wideToAnsi(name, volumeLabel);

		m_formatThread = new std::thread([this, quickFormat, dirCache, intMode, installBB, density, formatMode, volumeLabel]() {
			m_io->setWritingOnlyMode(true);
			bool ret = runFormatCommand(quickFormat, dirCache, intMode, installBB, density, formatMode, volumeLabel);
			{
				SectorRW_FloppyBridge* bridge = dynamic_cast<SectorRW_FloppyBridge*>(m_io);
				if (bridge) bridge->setForceDensityMode(FloppyBridge::BridgeDensityMode::bdmAuto);
			}
			m_io->setWritingOnlyMode(false);
			m_fs->setLocked(false);
			m_fs->restoreUnmountedDrive(false);
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


		case IDC_FILESYSTEM:
			enableControls(true);
			break;

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




