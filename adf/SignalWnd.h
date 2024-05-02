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

#include <dokan/dokan.h>
#include <functional>
#include <map>
#include "drivecontrol.h"



// This can be used to receive Windows messages
class CMessageWindow {
private:
	HWND m_hWnd;
	std::unordered_map<UINT, std::function<LRESULT(_In_ WPARAM wParam, _In_ LPARAM lParam)>> m_messageMap;
public:
	CMessageWindow(HINSTANCE hInstance, const std::wstring& windowCaption);
	~CMessageWindow();

	const HWND hwnd() const;

	void setMessageHandler(UINT uMsg, std::function<LRESULT(_In_ WPARAM wParam, _In_ LPARAM lParam)> callback);
	void clearMessageHandler(_In_ UINT uMsg);

	void setWindowTitle(const std::wstring& windowCaption);

	// Push in a message (usually automatic)
	LRESULT injestMessage(_In_ HWND hwnd, _In_ UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam);
};
