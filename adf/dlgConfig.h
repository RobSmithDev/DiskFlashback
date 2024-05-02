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



#pragma once

#include "adf_operations.h"
#include <thread>
#include "floppybridge_lib.h"


struct AppConfig {
	std::string floppyProfile;
	char        driveLetter;
	bool		enabled;
	bool		checkForUpdates;
	uint32_t	lastCheck;
	bool		autoRename;
};


class DialogConfig {
private:
	HINSTANCE m_hInstance;
	HWND m_hParent;
	HWND m_dialogBox;
	std::vector<FloppyBridgeAPI::DriverInformation> m_driverList;
	std::vector<const TCHAR*> m_comPortList;
	AppConfig m_config;
	FloppyBridgeAPI* m_api = nullptr;
	TCharString m_activePort;

	// Init dialog
	void handleInitDialog(HWND hwnd);

	// Refresh current selection stuff
	void refreshSelection();

	// Grab the config from the dialog
	void grabConfig();

public:
	DialogConfig(HINSTANCE hInstance, HWND hParent);
	bool doModal();
	
	// Dialog window message handler
	INT_PTR handleDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

// Load config from the registry
extern bool loadConfiguration(AppConfig& config);
// Save config to the registry
extern bool saveConfiguration(const AppConfig& config);

// Returns a number representing days
uint32_t getStamp();