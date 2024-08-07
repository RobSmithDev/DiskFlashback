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

#include "DriveList.h"
#include <thread>


class DialogMount {
private:
	HINSTANCE m_hInstance;
	HWND m_hParent;
	HWND m_listView = 0;
	HWND m_dialogBox = 0;
	CDriveList m_driveList;

	bool m_readOnly = false;
	std::wstring m_selectedDriveName;

	// Init dialog
	void handleInitDialog(HWND hwnd);

	void refreshSelection();

	void endDialogSuccess();
public:
	DialogMount(HINSTANCE hInstance, HWND hParent);
	~DialogMount();
	
	void run(const std::wstring exeName);
	
	// Dialog window message handler
	INT_PTR handleDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
