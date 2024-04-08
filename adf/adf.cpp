/*

#include "adf.h"
#include "sectorCache.h"
#include "amiga_sectors.h"
#include <Shlobj.h>


// Release the drive so it can be used by other apps
void fs::releaseDrive() {
	if (m_remoteLockout) return;
	if (!m_adfDevice->nativeDev) return;
	SectorCacheEngine* io = (SectorCacheEngine*)m_adfDevice->nativeDev;
	if (!io) return;
	if (!io->isPhysicalDisk()) return;
	if (!setLocked(true)) {
		std::wstring t = L"Unable to release the " + getDriverName() + L" floppy drive.\nThere are files currently open";
		MessageBox(GetDesktopWindow(), t.c_str(), L"Drive in use", MB_OK | MB_ICONSTOP);
		return;
	}
	unmountVolume();
	m_remoteLockout = true;
	io->releaseDrive();
}

// Restore the drive after it was released
void fs::restoreDrive() {
	if (!m_remoteLockout) return;
	if (!m_adfDevice->nativeDev) return;
	SectorCacheEngine* io = (SectorCacheEngine*)m_adfDevice->nativeDev;
	if (!io) return;
	if (!io->isPhysicalDisk()) return;
	io->resetCache();
	io->restoreDrive();
	setLocked(false);
	remountVolume();
	m_remoteLockout = false;
}


// Special command to unmount the volume
void fs::unmountVolume() {
	
	//refreshDriveInformation();
}

// Make windows realise a change occured
void fs::refreshDriveInformation() {
	
	
	//SHChangeNotify(SHCNE_FREESPACE, SHCNF_PATH, m_drive.c_str(), NULL);
}

// special command to re-mount the unmounted volume
void fs::remountVolume() {
	
}



// Add some hacks to the registery to make the drive context menu work
void fs::applyRegistryAction(const std::wstring registeryKeyName, const std::wstring menuLabel, const int iconIndex, const std::wstring& commandParams) {
	WCHAR path[256];
	swprintf_s(path, L"Software\\Classes\\Drive\\shell\\%s_%c", registeryKeyName.c_str(), m_drive[0]);
	RegSetValue(HKEY_CURRENT_USER, path, REG_SZ, menuLabel.c_str(), (DWORD)(menuLabel.length() * 2));
	RegSetKeyValue(HKEY_CURRENT_USER, path, L"AppliesTo", REG_SZ, m_drive.c_str(), (DWORD)(m_drive.length() * 2));
	RegSetKeyValue(HKEY_CURRENT_USER, path, L"MultiSelectModel", REG_SZ, L"Single", 6 * 2);
	const std::wstring iconNumber = L"\"" + mainEXE + L"\", " + std::to_wstring(iconIndex);
	RegSetKeyValue(HKEY_CURRENT_USER, path, L"Icon", REG_SZ, iconNumber.c_str(), (DWORD)(iconNumber.length() * 2));
	std::wstring cmd = L"\"" + mainEXE + L"\" CONTROL "+ m_drive.substr(0,1)+L" "+commandParams;
	wcscat_s(path, L"\\command");
	RegSetValue(HKEY_CURRENT_USER, path, REG_SZ, cmd.c_str(), (DWORD)(cmd.length() * 2));
}

// Remove those hacks
void fs::removeRegisteryAction(const std::wstring registeryKeyName) {
	WCHAR path[256];
	swprintf_s(path, L"Software\\Classes\\Drive\\shell\\%s_%c", registeryKeyName.c_str(), m_drive[0]);
	std::wstring path2 = std::wstring(path) + L"\\command";
	RegDeleteKey(HKEY_CURRENT_USER, path2.c_str());
	RegDeleteKey(HKEY_CURRENT_USER, path);
}

// Add data to the context menu for ADF
void fs::controlADFMenu(bool add) {
	WCHAR path[128];	

	// Add a entry to the ADF context menu
	LONG len = 128;
	if (FAILED(RegQueryValue(HKEY_CURRENT_USER, L"Software\\Classes\\.adf", path, &len))) {
		wcscpy_s(path, L"amiga.adf.file");
		RegSetValue(HKEY_CURRENT_USER, L"Software\\Classes\\.adf", REG_SZ, path, wcslen(path) * 2);
	}

	std::wstring clsRoot = L"Software\\Classes\\" + std::wstring(path) + L"\\shell";
	if (add) {
		std::wstring cmd = L"\"" + mainEXE + L"\" CONTROL " + m_drive.substr(0, 1) + L" 2DISK \"%1\"";
		clsRoot += L"\\Copy to Disk...";
		const std::wstring iconNumber = L"\"" + mainEXE + L"\", " + std::to_wstring(0);
		RegSetKeyValue(HKEY_CURRENT_USER, clsRoot.c_str(), L"Icon", REG_SZ, iconNumber.c_str(), iconNumber.length() * 2);
		clsRoot += L"\\command";
		RegSetValue(HKEY_CURRENT_USER, clsRoot.c_str(), REG_SZ, cmd.c_str(), cmd.length() * 2);

		//"Icon" = "C:\\Program Files\\ConsoleZ\\Console.exe"
	}
	else {
		std::wstring clsRoot2 = clsRoot + L"\\Copy to Disk...";
		std::wstring clsRoot3 = clsRoot2 + L"\\command";
		RegDeleteKey(HKEY_CURRENT_USER, clsRoot3.c_str());
		RegDeleteKey(HKEY_CURRENT_USER, clsRoot2.c_str());
	}
}


// Add data to the context menu for DMS
void fs::controlDMSMenu(bool add) {
	WCHAR path[128];

	// Add a entry to the ADF context menu
	LONG len = 128;
	if (FAILED(RegQueryValue(HKEY_CURRENT_USER, L"Software\\Classes\\.dms", path, &len))) {
		wcscpy_s(path, L"amiga.dms.file");
		RegSetValue(HKEY_CURRENT_USER, L"Software\\Classes\\.dms", REG_SZ, path, wcslen(path) * 2);
	}

	std::wstring clsRoot = L"Software\\Classes\\" + std::wstring(path) + L"\\shell";
	if (add) {
		std::wstring cmd = L"\"" + mainEXE + L"\" CONTROL " + m_drive.substr(0, 1) + L" 2DISK \"%1\"";
		clsRoot += L"\\Copy to Disk...";
		const std::wstring iconNumber = L"\"" + mainEXE + L"\", " + std::to_wstring(0);
		RegSetKeyValue(HKEY_CURRENT_USER, clsRoot.c_str(), L"Icon", REG_SZ, iconNumber.c_str(), iconNumber.length() * 2);
		clsRoot += L"\\command";
		RegSetValue(HKEY_CURRENT_USER, clsRoot.c_str(), REG_SZ, cmd.c_str(), cmd.length() * 2);

		//"Icon" = "C:\\Program Files\\ConsoleZ\\Console.exe"
	}
	else {
		std::wstring clsRoot2 = clsRoot + L"\\Copy to Disk...";
		std::wstring clsRoot3 = clsRoot2 + L"\\command";
		RegDeleteKey(HKEY_CURRENT_USER, clsRoot3.c_str());
		RegDeleteKey(HKEY_CURRENT_USER, clsRoot2.c_str());
	}
}

void fs::mounted(const std::wstring& mountPoint, bool wasMounted) {
	if (wasMounted) m_drive = mountPoint;
	if (m_drive.empty()) return;
	m_drive[0] = towupper(m_drive[0]);

	WCHAR path[128];
	swprintf_s(path, L"Software\\Classes\\Applications\\Explorer.exe\\Drives\\%s\\DefaultIcon", m_drive.substr(0, 1).c_str());

	// Clear anything previously there
	removeRegisteryAction(L"ADFMounterSystemBB");
	removeRegisteryAction(L"ADFMounterSystemFormat");
	removeRegisteryAction(L"ADFMounterSystemEject");
	removeRegisteryAction(L"ADFMounterSystemCopy");


	RegDeleteKeyValue(HKEY_CURRENT_USER, path, NULL);
	RegDeleteKey(HKEY_CURRENT_USER, path);
	path[wcslen(path) - 12] = L'\0';
	RegDeleteKey(HKEY_CURRENT_USER, path);
	SectorCacheEngine* cache = (SectorCacheEngine*)m_adfDevice->nativeDev;
	const bool physicalDisk = m_adfDevice && m_adfDevice->nativeDev && (cache->isPhysicalDisk());
	const bool copyToADF = m_adfDevice && m_adfDevice->nativeDev && (cache->allowCopyToADF());
	if (physicalDisk) {
		controlADFMenu(false);
		controlDMSMenu(false);
	}

	swprintf_s(path, L"Software\\Classes\\Applications\\Explorer.exe\\Drives\\%s\\DefaultIcon", m_drive.substr(0, 1).c_str());

	if (wasMounted) {
		RegSetValue(HKEY_CURRENT_USER, path, REG_SZ, mainEXE.c_str(), (DWORD)(mainEXE.length() * 2));
		if (cache && !cache->isDiskWriteProtected() && physicalDisk)
			applyRegistryAction(L"ADFMounterSystemFormat", L"Form&at...", 0,L"FORMAT");

		if (copyToADF) {
			applyRegistryAction(L"ADFMounterSystemCopy", L"&Copy to ADF...", 0, L"BACKUP");
			controlADFMenu(true);
			controlDMSMenu(true);
		}			
		if (m_adfVolume && cache && !cache->isDiskWriteProtected()) {
			if (m_adfVolume->dev->devType == DEVTYPE_FLOPDD || m_adfVolume->dev->devType == DEVTYPE_FLOPHD) {
				applyRegistryAction(L"ADFMounterSystemBB", L"&Install Bootblock...", 0, L"BB");
			}
		}
		applyRegistryAction(L"ADFMounterSystemEject", physicalDisk ? L"&Remove Drive" : L"&Eject", 0, L"EJECT");
	}
}

*/