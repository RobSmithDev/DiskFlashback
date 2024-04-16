
#include "shellMenus.h"
#include "sectorCache.h"
#include "dokaninterface.h"


// Add some hacks to the registery to make the drive context menu work
void ShellRegistery::applyRegistryAction(WCHAR driveLetter, const std::wstring registeryKeyName, const std::wstring menuLabel, const int iconIndex, const std::wstring& commandParams) {
	WCHAR path[256];
	std::wstring drive;
	drive.resize(3);
	drive[0] = driveLetter;
	drive[1] = L':';
	drive[2] = L'\\';
	swprintf_s(path, L"Software\\Classes\\Drive\\shell\\%s_%c", registeryKeyName.c_str(), driveLetter);
	RegSetValue(HKEY_CURRENT_USER, path, REG_SZ, menuLabel.c_str(), (DWORD)(menuLabel.length() * 2));
	RegSetKeyValue(HKEY_CURRENT_USER, path, L"AppliesTo", REG_SZ, drive.c_str(), (DWORD)(drive.length() * 2));
	RegSetKeyValue(HKEY_CURRENT_USER, path, L"MultiSelectModel", REG_SZ, L"Single", 6 * 2);
	const std::wstring iconNumber = L"\"" + m_mainEXE + L"\", " + std::to_wstring(iconIndex);
	RegSetKeyValue(HKEY_CURRENT_USER, path, L"Icon", REG_SZ, iconNumber.c_str(), (DWORD)(iconNumber.length() * 2));
	std::wstring cmd = L"\"" + m_mainEXE + L"\" CONTROL " + drive.substr(0, 1) + L" " + commandParams;
	wcscat_s(path, L"\\command");
	RegSetValue(HKEY_CURRENT_USER, path, REG_SZ, cmd.c_str(), (DWORD)(cmd.length() * 2));
}

// Remove those hacks
void ShellRegistery::removeRegisteryAction(WCHAR driveLetter, const std::wstring registeryKeyName) {
	WCHAR path[256];
	swprintf_s(path, L"Software\\Classes\\Drive\\shell\\%s_%c", registeryKeyName.c_str(), driveLetter);
	std::wstring path2 = std::wstring(path) + L"\\command";
	RegDeleteKey(HKEY_CURRENT_USER, path2.c_str());
	RegDeleteKey(HKEY_CURRENT_USER, path);
}


// APPLICATION_NAME
// Populate "normal" command options for a file type
void ShellRegistery::populateDiskImageMenu(bool add, const std::wstring& path, WCHAR driveLetter) {
	std::wstring clsRoot = path;
	if (add) {
		std::wstring cmd = L"\"" + m_mainEXE + L"\" CONTROL " + driveLetter + L" 2DISK \"%1\"";
		clsRoot += L"\\CopyToDisk";
		const std::wstring iconNumber = L"\"" + m_mainEXE + L"\", " + std::to_wstring(0);
		HKEY key = 0;
		HRESULT r = RegCreateKey(HKEY_CURRENT_USER, clsRoot.c_str(), &key);
		if (key) RegCloseKey(key);
		RegSetValue(HKEY_CURRENT_USER, clsRoot.c_str(), REG_SZ, L"&Copy to Disk...", 16*2);

		 r = RegSetKeyValue(HKEY_CURRENT_USER, clsRoot.c_str(), L"Icon", REG_SZ, iconNumber.c_str(), (DWORD)iconNumber.length() * 2);
		clsRoot += L"\\command";
		RegSetValue(HKEY_CURRENT_USER, clsRoot.c_str(), REG_SZ, cmd.c_str(), (DWORD)cmd.length() * 2);
	}
	else {
		std::wstring clsRoot2 = clsRoot + L"\\CopyToDisk";
		std::wstring clsRoot3 = clsRoot2 + L"\\command";
		RegDeleteKey(HKEY_CURRENT_USER, clsRoot3.c_str());
		RegDeleteKey(HKEY_CURRENT_USER, clsRoot2.c_str());
	}
}

// Add data to the context menu for disk images
void ShellRegistery::setupDiskImageMenu(bool add, WCHAR driveLetter) {
	#define MAX_DISK_IMAGE_FILES 7
	static const WCHAR* DiskImageFiles[MAX_DISK_IMAGE_FILES] = { L"adf",L"dms",L"hda",L"hdf",L"st",L"img",L"ima" };
	static const bool   DiskImageCopy[MAX_DISK_IMAGE_FILES] = { true, true, false, false, true, true, true };
	static const WCHAR* DiskImageDesc[MAX_DISK_IMAGE_FILES] = { L"Amiga Floppy Disk" , L"DMS Disk Image", L"Amiga Hard Disk", L"Amiga Hard Disk", L"Atari ST Disk Image", L"IBM PC Disk Image" , L"IBM PC Disk Image" };

	WCHAR path[128];
	WCHAR keyname[128];
	for (uint32_t fType = 0; fType < MAX_DISK_IMAGE_FILES; fType++) {

		// Add entries to context menus
		LONG len = 128;
		swprintf_s(path, L"Software\\Classes\\.%s", DiskImageFiles[fType]);
		if (((RegQueryValue(HKEY_CURRENT_USER, path, keyname, &len) != ERROR_SUCCESS)) || (len<=2)) {
			std::wstring clsRoot = path + std::wstring(keyname);
			std::wstring name = DiskImageDesc[fType];
			RegSetValue(HKEY_CURRENT_USER, clsRoot.c_str(), REG_SZ, name.c_str(), (DWORD)name.length() * 2);
		}
		std::wstring tmp = L"Software\\Classes\\." + std::wstring(DiskImageFiles[fType]) + L"\\OpenWithProgIds";
		std::wstring tmp2 = L"";
		RegSetKeyValueW(HKEY_CURRENT_USER, tmp.c_str(), APPLICATION_NAME_L, REG_SZ, tmp2.c_str(), (DWORD)tmp2.length() * 2);
		tmp = L"Software\\Classes\\" + std::wstring(keyname) + L"\\shell";
		if (DiskImageCopy[fType]) populateDiskImageMenu(add, tmp, driveLetter);		
	}

	if (add) {
		HKEY key = 0;
		RegCreateKey(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\" APPLICATION_NAME_L, &key);
		if (key) RegCloseKey(key);
		key = 0;
		RegCreateKey(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\" APPLICATION_NAME_L L"\\shell", &key);
		if (key) RegCloseKey(key);
		populateDiskImageMenu(add, L"Software\\Classes\\Applications\\" APPLICATION_NAME_L L"\\shell", driveLetter);
	}
}

void ShellRegistery::setupDriveIcon(bool enable, WCHAR driveLetter, uint32_t iconIndex, bool isPhysicalDisk) {
	WCHAR path[128];
	swprintf_s(path, L"Software\\Classes\\Applications\\Explorer.exe\\Drives\\%c\\DefaultIcon", driveLetter);

	mountDismount(false, driveLetter, nullptr);

	if (enable) {
		std::wstring tmp = m_mainEXE + L"," + std::to_wstring(iconIndex);
		RegSetValue(HKEY_CURRENT_USER, path, REG_SZ, tmp.c_str(), (DWORD)(tmp.length() * 2));

		applyRegistryAction(driveLetter, L"ADFMounterSystemEject", L"&Eject", 0, L"EJECT");

		if (isPhysicalDisk)
			applyRegistryAction(driveLetter, L"ADFMounterSystemFormat", L"Form&at...", 0, L"FORMAT");

	}
	else {
		RegDeleteKeyValue(HKEY_CURRENT_USER, path, NULL);
		RegDeleteKey(HKEY_CURRENT_USER, path);
		path[wcslen(path) - 12] = L'\0';
		RegDeleteKey(HKEY_CURRENT_USER, path);
		removeRegisteryAction(driveLetter, L"ADFMounterSystemEject");
		removeRegisteryAction(driveLetter, L"ADFMounterSystemFormat");
	}
}


void ShellRegistery::mountDismount(bool mounted, WCHAR driveLetter, SectorCacheEngine* sectorSource) {
	const bool physicalDisk = sectorSource ? sectorSource->isPhysicalDisk() : false;
	const bool copyToADF = sectorSource ? sectorSource->allowCopyToFile() : false;
	setupDiskImageMenu(physicalDisk, driveLetter);

	removeRegisteryAction(driveLetter, L"ADFMounterSystemBB");	
	removeRegisteryAction(driveLetter, L"ADFMounterSystemCopy");

	if (mounted) {		
		
		if (copyToADF) {
			switch (sectorSource->getSystemType()) {
			case SectorType::stAmiga: applyRegistryAction(driveLetter, L"ADFMounterSystemCopy", L"&Copy to .ADF...", 0, L"BACKUP"); break;
			case SectorType::stIBM: applyRegistryAction(driveLetter, L"ADFMounterSystemCopy", L"&Copy to .IMG...", 0, L"BACKUP"); break;
			case SectorType::stAtari: applyRegistryAction(driveLetter, L"ADFMounterSystemCopy", L"&Copy to .ST...", 0, L"BACKUP"); break;
			}
		}

		if ((sectorSource->getSystemType() == SectorType::stAmiga) && (!sectorSource->isDiskWriteProtected())) {
			applyRegistryAction(driveLetter, L"ADFMounterSystemBB", L"&Install Bootblock...", 0, L"BB");
		}		
	}
}
