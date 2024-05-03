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

class MountedVolume;
class SectorCacheEngine;

class DialogFORMAT {
private:
	HINSTANCE m_hInstance;
	HWND m_hParent;
	SectorCacheEngine* m_io;
	MountedVolume* m_fs;
	HWND m_dialogBox = 0;
	std::wstring m_windowCaption;
	HCURSOR m_lastCursor = 0;

	// Thread to handle the actual formatting
	std::thread* m_formatThread = nullptr;
	bool m_abortFormat = false;

	// Init dialog
	void handleInitDialog(HWND hwnd);

	// Returns TRUE if its OK to close
	bool shouldClose();

	// Handle starting the formatting process
	void doFormat();

	// Enable/disable controls on the dialog
	void enableControls(bool enable);

	// Actually do the format
	bool runFormatCommand(bool quickFormat, bool dirCache, bool intMode, bool installBB, uint32_t density, uint32_t formatMode, const std::string& volumeLabel);

public:
	DialogFORMAT(HINSTANCE hInstance, HWND hParent, SectorCacheEngine* io, MountedVolume* fs);
	INT_PTR doModal();

	// Dialog window message handler
	INT_PTR handleDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};