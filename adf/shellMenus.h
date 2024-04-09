#pragma once

#include <dokan/dokan.h>
#include <string>

class SectorCacheEngine;
 
class ShellRegistery {
private:
	std::wstring m_mainEXE;

	// Add some hacks to the registery to make the drive context menu work
	void applyRegistryAction(WCHAR driveLetter, const std::wstring registeryKeyName, const std::wstring menuLabel, const int iconIndex, const std::wstring& commandParams);
	// Remove those hacks
	void removeRegisteryAction(WCHAR driveLetter, const std::wstring registeryKeyName);
	// Add data to the context menu for DMS
	void setupDMSMenu(bool add, WCHAR driveLetter);
	// Add data to the context menu for ADF
	void setupADFMenu(bool add, WCHAR driveLetter);
public:
	ShellRegistery(const std::wstring& mainEXE) : m_mainEXE(mainEXE) {};

	void mountDismount(bool mounted, WCHAR driveLetter, SectorCacheEngine* sectorSource);

	void setupDriveIcon(bool enable, WCHAR driveLetter, uint32_t iconIndex = 2);

};