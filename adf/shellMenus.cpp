
#include "shellMenus.h"
#include "sectorCache.h"



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

// Add data to the context menu for ADF
void ShellRegistery::setupADFMenu(bool add, WCHAR driveLetter) {
	WCHAR path[128];

	// Add a entry to the ADF context menu
	LONG len = 128;
	if (FAILED(RegQueryValue(HKEY_CURRENT_USER, L"Software\\Classes\\.adf", path, &len))) {
		wcscpy_s(path, L"amiga.adf.file");
		RegSetValue(HKEY_CURRENT_USER, L"Software\\Classes\\.adf", REG_SZ, path, wcslen(path) * 2);
	}

	std::wstring clsRoot = L"Software\\Classes\\" + std::wstring(path) + L"\\shell";
	if (add) {
		std::wstring cmd = L"\"" + m_mainEXE + L"\" CONTROL " + driveLetter + L" 2DISK \"%1\"";
		clsRoot += L"\\Copy to Disk...";
		const std::wstring iconNumber = L"\"" + m_mainEXE + L"\", " + std::to_wstring(0);
		RegSetKeyValue(HKEY_CURRENT_USER, clsRoot.c_str(), L"Icon", REG_SZ, iconNumber.c_str(), iconNumber.length() * 2);
		clsRoot += L"\\command";
		RegSetValue(HKEY_CURRENT_USER, clsRoot.c_str(), REG_SZ, cmd.c_str(), cmd.length() * 2);
	}
	else {
		std::wstring clsRoot2 = clsRoot + L"\\Copy to Disk...";
		std::wstring clsRoot3 = clsRoot2 + L"\\command";
		RegDeleteKey(HKEY_CURRENT_USER, clsRoot3.c_str());
		RegDeleteKey(HKEY_CURRENT_USER, clsRoot2.c_str());
	}
}

// Add data to the context menu for DMS
void ShellRegistery::setupDMSMenu(bool add, WCHAR driveLetter) {
	WCHAR path[128];

	// Add a entry to the ADF context menu
	LONG len = 128;
	if (FAILED(RegQueryValue(HKEY_CURRENT_USER, L"Software\\Classes\\.dms", path, &len))) {
		wcscpy_s(path, L"amiga.dms.file");
		RegSetValue(HKEY_CURRENT_USER, L"Software\\Classes\\.dms", REG_SZ, path, wcslen(path) * 2);
	}

	std::wstring clsRoot = L"Software\\Classes\\" + std::wstring(path) + L"\\shell";
	if (add) {
		std::wstring cmd = L"\"" + m_mainEXE + L"\" CONTROL " + driveLetter + L" 2DISK \"%1\"";
		clsRoot += L"\\Copy to Disk...";
		const std::wstring iconNumber = L"\"" + m_mainEXE + L"\", " + std::to_wstring(0);
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

void ShellRegistery::mountDiscount(bool mounted, WCHAR driveLetter, SectorCacheEngine* sectorSource) {
	WCHAR path[128];
	swprintf_s(path, L"Software\\Classes\\Applications\\Explorer.exe\\Drives\\%c\\DefaultIcon", driveLetter);

	// Clear anything previously there
	removeRegisteryAction(driveLetter, L"ADFMounterSystemBB");
	removeRegisteryAction(driveLetter, L"ADFMounterSystemFormat");
	removeRegisteryAction(driveLetter, L"ADFMounterSystemEject");
	removeRegisteryAction(driveLetter, L"ADFMounterSystemCopy");

	RegDeleteKeyValue(HKEY_CURRENT_USER, path, NULL);
	RegDeleteKey(HKEY_CURRENT_USER, path);
	path[wcslen(path) - 12] = L'\0';
	RegDeleteKey(HKEY_CURRENT_USER, path);

	const bool physicalDisk = sectorSource->isPhysicalDisk();
	const bool copyToADF = sectorSource->allowCopyToFile(); 
	if (physicalDisk) {
		setupADFMenu(false, driveLetter);
		setupDMSMenu(false, driveLetter);
	}

	swprintf_s(path, L"Software\\Classes\\Applications\\Explorer.exe\\Drives\\%c\\DefaultIcon", driveLetter);

	if (mounted) {
		RegSetValue(HKEY_CURRENT_USER, path, REG_SZ, m_mainEXE.c_str(), (DWORD)(m_mainEXE.length() * 2));
		if (physicalDisk && (!sectorSource->isDiskWriteProtected()))
			applyRegistryAction(driveLetter, L"ADFMounterSystemFormat", L"Form&at...", 0, L"FORMAT");

		if (copyToADF) {
			applyRegistryAction(driveLetter, L"ADFMounterSystemCopy", L"&Copy to ADF...", 0, L"BACKUP");
			setupADFMenu(true, driveLetter);
			setupDMSMenu(true, driveLetter);
		}
		if ((sectorSource->getSystemType() == SectorType::stAmiga) && (!sectorSource->isDiskWriteProtected())) {
			applyRegistryAction(driveLetter, L"ADFMounterSystemBB", L"&Install Bootblock...", 0, L"BB");
		}
		applyRegistryAction(driveLetter, L"ADFMounterSystemEject", L"&Eject", 0, L"EJECT");
	}
}
