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


#include "shellMenus.h"
#include "sectorCache.h"
#include "dokaninterface.h"

const WCHAR* ShellRegistery::DiskImageFiles[MAX_DISK_IMAGE_FILES] = { L"amiga.fd",L"ibmpc",L"atarist"};
const int    ShellRegistery::DiskImageIcon[MAX_DISK_IMAGE_FILES] = { 1, 0, 3 };

#define REG_DRIVE_KEYNAME_CLEAN  L"DiskFlashback.Clean"
#define REG_DRIVE_KEYNAME_FORMAT  L"DiskFlashback.Format"
#define REG_DRIVE_KEYNAME_EJECT  L"DiskFlashback.Eject"
#define REG_DRIVE_KEYNAME_COPY  L"DiskFlashback.Copy"
#define REG_DRIVE_KEYNAME_BB  L"DiskFlashback.InstallBootBlock"

// Add some hacks to the registery to make the drive context menu work
void ShellRegistery::addDriveAction(WCHAR driveLetter, const std::wstring& section, const std::wstring menuLabel, const int iconIndex, const std::wstring& commandParams) {
	WCHAR path[256];
	std::wstring drive;	
	drive.resize(3); drive[0] = driveLetter; drive[1] = L':'; drive[2] = L'\\';

	swprintf_s(path, L"Software\\Classes\\Drive\\shell\\%s_%c", section.c_str(), driveLetter);
	RegSetValue(HKEY_CURRENT_USER, path, REG_SZ, menuLabel.c_str(), (DWORD)(menuLabel.length() * 2));

	RegSetKeyValue(HKEY_CURRENT_USER, path, L"AppliesTo", REG_SZ, drive.c_str(), (DWORD)(drive.length() * 2));
	RegSetKeyValue(HKEY_CURRENT_USER, path, L"MultiSelectModel", REG_SZ, L"Single", 6 * 2);

	const std::wstring iconNumber = L"\"" + m_mainEXE + L"\"," + std::to_wstring(iconIndex);
	RegSetKeyValue(HKEY_CURRENT_USER, path, L"Icon", REG_SZ, iconNumber.c_str(), (DWORD)(iconNumber.length() * 2));

	std::wstring cmd = L"\"" + m_mainEXE + L"\" CONTROL " + drive.substr(0, 1) + L" " + commandParams;
	wcscat_s(path, L"\\command");
	RegSetValue(HKEY_CURRENT_USER, path, REG_SZ, cmd.c_str(), (DWORD)(cmd.length() * 2));
}

// Remove those hacks
void ShellRegistery::removeDriveAction(WCHAR driveLetter, const std::wstring& section) {
	WCHAR path[256];
	swprintf_s(path, L"Software\\Classes\\Drive\\shell\\%s_%c", section.c_str(), driveLetter);
	RegDeleteTree(HKEY_CURRENT_USER, path);
}


// APPLICATION_NAME
// Populate "normal" command options for a file type
void ShellRegistery::setupContextForFileType(bool add, const std::wstring& path, uint32_t icon, WCHAR driveLetter) {
	std::wstring clsRoot = path + L"\\CopyToDisk";
	if (add) {
		const std::wstring cmd = L"\"" + m_mainEXE + L"\" CONTROL " + driveLetter + L" 2DISK \"%1\"";
		const std::wstring iconNumber = L"\"" + m_mainEXE + L"\"," + std::to_wstring(icon);

		HKEY key = 0;
		RegCreateKey(HKEY_CURRENT_USER, clsRoot.c_str(), &key);
		if (key) RegCloseKey(key);

		RegSetValue(HKEY_CURRENT_USER, clsRoot.c_str(), REG_SZ, L"&Copy to Disk...", 16*2);
		RegSetKeyValue(HKEY_CURRENT_USER, clsRoot.c_str(), L"Icon", REG_SZ, iconNumber.c_str(), (DWORD)iconNumber.length() * 2);
		clsRoot += L"\\command";
		RegSetValue(HKEY_CURRENT_USER, clsRoot.c_str(), REG_SZ, cmd.c_str(), (DWORD)cmd.length() * 2);
	}
	else {
		RegDeleteTree(HKEY_CURRENT_USER, clsRoot.c_str());
	}
}

// Add data to the context menu for disk images
void ShellRegistery::setupFiletypeContextMenu(bool add, WCHAR driveLetter) {
	for (uint32_t fType = 0; fType < MAX_DISK_IMAGE_FILES; fType++) {
		setupContextForFileType(add, L"Software\\Classes\\DiskFlashback." + std::wstring(DiskImageFiles[fType]) + L"\\shell", DiskImageIcon[fType], driveLetter);
	}
}

void ShellRegistery::setupDriveIcon(bool enable, WCHAR driveLetter, uint32_t iconIndex, bool isPhysicalDisk) {
	WCHAR path[128];
	swprintf_s(path, L"Software\\Classes\\Applications\\Explorer.exe\\Drives\\%c\\DefaultIcon", driveLetter);

	if (enable) {
		std::wstring tmp = m_mainEXE + L"," + std::to_wstring(iconIndex);
		RegSetValue(HKEY_CURRENT_USER, path, REG_SZ, tmp.c_str(), (DWORD)(tmp.length() * 2));

		addDriveAction(driveLetter, REG_DRIVE_KEYNAME_EJECT, L"&Eject", 0, L"EJECT");
		if (isPhysicalDisk) {
			addDriveAction(driveLetter, REG_DRIVE_KEYNAME_CLEAN, L"C&lean Drive...", 0, L"CLEAN");
			addDriveAction(driveLetter, REG_DRIVE_KEYNAME_FORMAT, L"Form&at...", 0, L"FORMAT");
		}
	}
	else {
		RegDeleteKey(HKEY_CURRENT_USER, path);

		path[wcslen(path) - 12] = L'\0';
		RegDeleteKey(HKEY_CURRENT_USER, path);

		removeDriveAction(driveLetter, REG_DRIVE_KEYNAME_CLEAN);
		removeDriveAction(driveLetter, REG_DRIVE_KEYNAME_FORMAT);
		removeDriveAction(driveLetter, REG_DRIVE_KEYNAME_EJECT);
	}
}

void ShellRegistery::mountDismount(bool mounted, WCHAR driveLetter, SectorCacheEngine* sectorSource) {
	const bool physicalDisk = sectorSource ? sectorSource->isPhysicalDisk() : false;
	const bool copyToADF = sectorSource ? sectorSource->allowCopyToFile() : false;
	setupFiletypeContextMenu(physicalDisk, driveLetter);

	removeDriveAction(driveLetter, REG_DRIVE_KEYNAME_BB);
	removeDriveAction(driveLetter, REG_DRIVE_KEYNAME_COPY);

	if (mounted) {		
		
		if (copyToADF) {
			switch (sectorSource->getSystemType()) {
			case SectorType::stAmiga: addDriveAction(driveLetter, REG_DRIVE_KEYNAME_COPY, L"&Copy to .ADF...", 0, L"BACKUP"); break;
			case SectorType::stIBM: addDriveAction(driveLetter, REG_DRIVE_KEYNAME_COPY, L"&Copy to .IMG...", 0, L"BACKUP"); break;
			case SectorType::stAtari: addDriveAction(driveLetter, REG_DRIVE_KEYNAME_COPY, L"&Copy to .ST...", 0, L"BACKUP"); break;
			}
		}

		if ((sectorSource->getSystemType() == SectorType::stAmiga) && (!sectorSource->isDiskWriteProtected())) {
			addDriveAction(driveLetter, REG_DRIVE_KEYNAME_BB, L"&Install Bootblock...", 0, L"BB");
		}		
	}
}
