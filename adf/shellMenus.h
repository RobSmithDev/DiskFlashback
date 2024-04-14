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
	// Add data to the context menu for disk images
	void setupDiskImageMenu(bool add, WCHAR driveLetter);

	void populateDiskImageMenu(bool add, const std::wstring& path, WCHAR driveLetter);
public:
	ShellRegistery(const std::wstring& mainEXE) : m_mainEXE(mainEXE) {};

	void mountDismount(bool mounted, WCHAR driveLetter, SectorCacheEngine* sectorSource);

	void setupDriveIcon(bool enable, WCHAR driveLetter, uint32_t iconIndex, bool isPhysicalDisk);

};