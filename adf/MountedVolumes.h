#pragma once

#include <vector>
#include <thread>
#include "SignalWnd.h"
#include "MountedVolume.h"
#include "sectorCache.h"
#include "fatfs/source/ff.h"

class VolumeManager {
private:
	// Full path to main exe name (this program)
	const std::wstring m_mainExeFilename;
	std::wstring m_mountMode;
	SectorType m_currentSectorFormat;
	bool m_forceReadOnly;
	HINSTANCE m_hInstance;
	bool m_triggerExplorer = false;

	// If we have an Amiga disk inserted
	AdfDevice* m_adfDevice = nullptr;
	FATFS* m_fatDevice = nullptr;

	// Notification window
	CMessageWindow m_window;

	// The device or file
	SectorCacheEngine* m_io = nullptr;

	// Currently mounted volumes
	std::vector<MountedVolume*> m_volumes;

	// The first letter to start mounting from
	WCHAR m_firstDriveLetter;

	// Active threads we're keeping track of
	std::vector<std::thread> m_threads;


	// General cleanup and monitoring to see if file systems have dropped out
	void checkRunningFileSystems();

	// Handle a request to copy a file to the active drive
	LRESULT handleCopyToDiskRequest(const std::wstring message);

	// Searches active mounted volumes and looks for one with a drive letter and returns it
	MountedVolume* findVolumeFromDriveLetter(const WCHAR driveLetter);

	// Handle other remote requests
	LRESULT handleRemoteRequest(MountedVolume* volume, const WPARAM commandID, HWND parentWindow);

	// Notification received that the current disk changed
	void diskChanged(bool diskInserted, SectorType diskFormat); 

	// triggers the re-mounting of a disk
	void triggerRemount();

	// clean up threads
	void cleanThreads();

	// Mount the new amiga volumes
	uint32_t mountAmigaVolumes(uint32_t startPoint);

	// start mounting IBM volumes using m_volume[startPoint]
	uint32_t mountIBMVolumes(uint32_t startPoint);

	// Start any volumes that aren't running
	void startVolumes();
public:
	VolumeManager(HINSTANCE hInstance, const std::wstring& mainExe, WCHAR firstDriveLetter, bool forceReadOnly);
	~VolumeManager();

	// Start a file mount
	bool mountFile(const std::wstring& filename);

	// Start a drive mount
	bool mountDrive(const std::wstring& floppyProfile);

	// Main loop
	bool run(bool triggerExplorer = false);

	// Search for the control window matching the drive letter supplied
	static HWND FindControlWindowForDrive(WCHAR driveLetter);
	// Looks to see if the active foreground window is explorer and if so use it as a parent
	static HWND FindPotentialExplorerParent();

	// Refresh the window title
	void refreshWindowTitle();
};



// ADFLib Native Functions
RETCODE adfInitDevice(struct AdfDevice* const dev, const char* const name, const BOOL ro);
RETCODE adfReleaseDevice(struct AdfDevice* const dev);
RETCODE adfNativeReadSector(struct AdfDevice* const dev, const uint32_t n, const unsigned size, uint8_t* const buf);
RETCODE adfNativeWriteSector(struct AdfDevice* const dev, const uint32_t n, const unsigned size, const uint8_t* const buf);
BOOL adfIsDevNative(const char* const devName);
void Warning(char* msg);
void Error(char* msg);
void Verbose(char* msg);
void setFatFSSectorCache(SectorCacheEngine* _fatfsSectorCache);