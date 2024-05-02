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

class SectorCacheEngine;
class MountedVolume;

class DialogCOPY {
private:
	HINSTANCE m_hInstance;
	HWND m_hParent;
	SectorCacheEngine* m_io;
	MountedVolume* m_fs;
	HWND m_dialogBox = 0;
	std::wstring m_windowCaption;
	std::wstring m_filename;
	HCURSOR m_lastCursor = 0;
	bool m_backup;
	std::wstring m_fileExtension;
	std::wstring m_titleExtension;

	// Thread to handle the actual operation
	std::thread* m_copyThread = nullptr;
	bool m_abortCopy = false;

	// Init dialog
	void handleInitDialog(HWND hwnd);

	// Returns TRUE if its OK to close
	bool shouldClose();

	// Handle starting the copy process
	void doCopy();

	// Actually do the copy
	bool runCopyCommand(HANDLE fle, SectorCacheEngine* source);

public:
	DialogCOPY(HINSTANCE hInstance, HWND hParent, SectorCacheEngine* io, MountedVolume* fs);
	DialogCOPY(HINSTANCE hInstance, HWND hParent, SectorCacheEngine* io, MountedVolume* fs, const std::wstring& sourceADF);
	INT_PTR doModal(bool fileSystemRecognised);

	// Dialog window message handler
	INT_PTR handleDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};