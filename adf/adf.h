#pragma once

#include <dokan/dokan.h>
#include <dokan/fileinfo.h>

#include <string>

#include "adflib/src/adflib.h"
#include "adf_operations.h"

#include <WinBase.h>
#include <iostream>
#include <map>

class fs {
	private:
		DOKAN_HANDLE m_instance = nullptr;
		struct AdfDevice* m_adfDevice;
		struct AdfVolume* m_adfVolume;
		std::wstring m_drive;
		bool m_readOnly;

		// Reverse mapping for badly named files
		std::map<std::string, std::wstring> m_safeFilenameMap;
	public:
		fs(struct AdfDevice* adfFile, struct AdfVolume* adfVolume, WCHAR driveLetter, bool readOnly);
		void start();
		void stop();
		bool isRunning();
		void wait(DWORD timeout);

		// Handles fixing filenames so they're amiga compatable
		void amigaFilenameToWindowsFilename(const std::string& amigaFilename, std::wstring& windowsFilename);
		void windowsFilenameToAmigaFilename(const std::wstring& windowsFilename, std::string& amigaFilename);

		// Returns a serial number for this volume   - thats actually Amig
		DWORD volumeSerial() { return 0x416D6967; };

		// Return TRUE if file is in use
		bool isFileInUse(SECTNUM fileRoot, const std::string& filename) { return false; };

		// Returns TRUE if write protected
		bool isWriteProtected();

		struct AdfDevice* device() { return m_adfDevice; };
		struct AdfVolume* volume() { return m_adfVolume; };
};


extern void wideToAnsi(const std::wstring& wstr, std::string& str);
extern void ansiToWide(const std::string& wstr, std::wstring& str);



