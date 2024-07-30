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

#include <thread>
#include <string>

class SectorCacheEngine;
class SectorRW_FloppyBridge;
class MountedVolume;

#define CLEANWINDOW_TITLE L"Floppy Drive Head Cleaner"

class DialogCLEAN {
private:
	HINSTANCE m_hInstance;
	HWND m_hParent;
	HWND m_dialogBox = 0;
	HWND m_link = 0;
	HCURSOR m_handPoint = 0;
	std::wstring m_windowCaption;
	SectorRW_FloppyBridge* m_bridge;
	MountedVolume* m_fs;

	// Thread to handle the actual formatting
	std::thread* m_cleanThread = nullptr;
	bool m_abortClean = false;

	// Init dialog
	void handleInitDialog(HWND hwnd);

	// Returns TRUE if its OK to close
	bool shouldClose();

	// Handle starting the cleaning process
	void doClean();

	// Enable/disable controls on the dialog
	void enableControls(bool enable);

	// Actually do the clean
	bool runCleanCommand();

public:
	DialogCLEAN(HINSTANCE hInstance, HWND hParent, SectorCacheEngine* io, MountedVolume* fs);
	~DialogCLEAN();

	INT_PTR doModal();

	// Dialog window message handler
	INT_PTR handleDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};