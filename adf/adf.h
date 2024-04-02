#pragma once

#include <windows.h>

#include <dokan/dokan.h>
#include <dokan/fileinfo.h>

#include <string>

#include "adflib/src/adflib.h"
#include "adf_operations.h"

#include <iostream>
#include <map>
#include <unordered_map>

#define APPLICATION_NAME "AMount"


class fs;

// Simple nasty class to auto release (so I don't forget) details about an active file i/o occuring. 
// Everything is single threadded so this is OK
class ActiveFileIO {
private:
	fs* m_owner;
public:
	ActiveFileIO(fs* owner);
	// Remove copy constructor
	ActiveFileIO(const ActiveFileIO&) = delete;
	// Add Move constructor
	ActiveFileIO(ActiveFileIO&& source) noexcept;
	~ActiveFileIO();
};


extern RETCODE adfMountFlopButDontFail(struct AdfDevice* const dev);

class fs {
	friend class ActiveFileIO;

	private:
		DOKAN_HANDLE m_instance = nullptr;
		struct AdfDevice* m_adfDevice;
		struct AdfVolume* m_adfVolume;
		int m_volumeNumber;
		std::wstring m_drive;
		bool m_readOnly;

		// Files in use
		std::unordered_map<struct AdfFile*, int> m_inUse;

		// Reverse mapping for badly named files
		std::map<std::string, std::wstring> m_safeFilenameMap;
		
		// Add some hacks to the registery to make the drive context menu work
		void applyRegistryAction(const std::wstring registeryKeyName, const std::wstring menuLabel, const int iconIndex, const std::wstring& commandParams);

		// Remove those hacks
		void removeRegisteryAction(const std::wstring registeryKeyName);

		// Sets a current file info block as active (or NULL for not) so DokanResetTimeout can be called if needed
		void setActiveFileIO(PDOKAN_FILE_INFO dokanfileinfo);
		void clearFileIO();

		// Make windows realise a change occured
		void refreshDriveInformation();

		// Add data to the context menu for ADF
		void controlADFMenu(bool add);


	public:
		struct AdfDevice* device() { return m_adfDevice; };
		struct AdfVolume* volume() { return m_adfVolume; };
		const std::wstring driveLetter() const { return m_drive; };
		// Returns a serial number for this volume   - thats actually "Amig"
		DWORD volumeSerial() { return 0x416D6967; };


		fs(struct AdfDevice* adfFile, struct AdfVolume* adfVolume, int volumeNumber, WCHAR driveLetter, bool readOnly);
		void start();
		void stop();
		bool isRunning();
		void wait(DWORD timeout);

		// Install bootblock
		bool installBootBlock();

		// Handles fixing filenames so they're amiga compatable
		void amigaFilenameToWindowsFilename(const std::string& amigaFilename, std::wstring& windowsFilename);
		void windowsFilenameToAmigaFilename(const std::wstring& windowsFilename, std::string& amigaFilename);

		// Returns if disk is locked and cannot be accessed
		bool isLocked();
		// Returns FALSE if files are open
		bool setLocked(bool enableLock);

		// Return TRUE if file is in use for the new requested mode
		bool isFileInUse(const char* const name, const AdfFileMode mode);
		void addTrackFileInUse(struct AdfFile* handle);
		void releaseFileInUse(struct AdfFile* handle);

		// Returns TRUE if write protected
		bool isWriteProtected();
		// Returns TRUE if theres a disk in the drive
		bool isDiskInDrive();
		// Returns the name of the driver used for FloppyBridge
		std::wstring getDriverName();

		// Returns TRUE if this is a real disk
		bool isPhysicalDevice();

		// Return true if the drive has files open
		bool driveInUse();

		// Let the system know I/O is currently happenning.  ActiveFileIO must be kepyt in scope until io is complete
		ActiveFileIO notifyIOInUse(PDOKAN_FILE_INFO dokanfileinfo);

		// Notify and setup things when the drive becomes mounted
		void mounted(const std::wstring& mountPoint, bool wasMounted);

		

		// Special command to unmount the volume
		void unmountVolume();
		// special command to re-mount the unmounted volume
		void remountVolume();

};


extern void wideToAnsi(const std::wstring& wstr, std::string& str);
extern void ansiToWide(const std::string& wstr, std::wstring& str);



