#pragma once

#include <dokan/dokan.h>
#include <string>

class SectorCacheEngine;

#define ICON_BLANK   
#define ICON_AMIGA
#define ICON_PC
#define ICON_ST      
 
class ShellRegistery {
private:
	#define MAX_DISK_IMAGE_FILES 3
	static const WCHAR* DiskImageFiles[MAX_DISK_IMAGE_FILES];
	static const int    DiskImageIcon[MAX_DISK_IMAGE_FILES];

	std::wstring m_mainEXE;

	// Add some hacks to the registery to make the drive context menu work
	void addDriveAction(WCHAR driveLetter, const std::wstring& section, const std::wstring menuLabel, const int iconIndex, const std::wstring& commandParams);
	// Remove those hacks
	void removeDriveAction(WCHAR driveLetter, const std::wstring& section);

	// Add data to the context menu for disk images
	void setupContextForFileType(bool add, const std::wstring& path, uint32_t icon, WCHAR driveLetter);
public:
	ShellRegistery(const std::wstring& mainEXE) : m_mainEXE(mainEXE) {};

	void mountDismount(bool mounted, WCHAR driveLetter, SectorCacheEngine* sectorSource);
	void setupFiletypeContextMenu(bool add, WCHAR driveLetter);
	void setupDriveIcon(bool enable, WCHAR driveLetter, uint32_t iconIndex, bool isPhysicalDisk);
};