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
		bool m_remoteLockout = false;

		

		
		// Add some hacks to the registery to make the drive context menu work
		void applyRegistryAction(const std::wstring registeryKeyName, const std::wstring menuLabel, const int iconIndex, const std::wstring& commandParams);

		// Remove those hacks
		void removeRegisteryAction(const std::wstring registeryKeyName);

		// Make windows realise a change occured
		void refreshDriveInformation();

		// Add data to the context menu for ADF
		void controlADFMenu(bool add);
		// Add data to the context menu for DMS
		void controlDMSMenu(bool add);

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

		

		// Returns if disk is locked and cannot be accessed
		bool isLocked();
		// Returns FALSE if files are open
		bool setLocked(bool enableLock);

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

		// Notify and setup things when the drive becomes mounted
		void mounted(const std::wstring& mountPoint, bool wasMounted);

		// Release the drive so it can be used by other apps
		void releaseDrive();

		// Restore the drive after it was released
		void restoreDrive();

		// Special command to unmount the volume
		void unmountVolume();
		// special command to re-mount the unmounted volume
		void remountVolume();

};


