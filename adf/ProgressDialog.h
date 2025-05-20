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


#include <windows.h>
#include <Shobjidl.h>

// A base class for dialogs that provides progress information on the icon in the task bar
class ProgressDialog {
private: 
	ITaskbarList3* m_spTaskbarList = nullptr;
	int m_maxValue;
	HWND m_hwnd;
	bool m_stateNeedsToChange = true;

public:
	ProgressDialog() {
		CoInitialize(NULL);
		HRESULT hr = ::CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER, __uuidof(ITaskbarList3), reinterpret_cast<void**>(&m_spTaskbarList));
		if (SUCCEEDED(hr)) m_spTaskbarList->HrInit(); else m_spTaskbarList = nullptr;
	}
	virtual ~ProgressDialog() {
		if (m_spTaskbarList) {
			m_spTaskbarList->Release();
			m_spTaskbarList = nullptr;
		}
		CoUninitialize();
	};

	void setProgressWindowHandle(HWND hwnd) {
		m_hwnd = hwnd;
	}

	void setDialogError() {
		if (!m_spTaskbarList) return;
		m_spTaskbarList->SetProgressState(m_hwnd, TBPF_ERROR);
		m_stateNeedsToChange = true;
	}

	void setDialogProgress(int position, int max=-1) {
		if (!m_spTaskbarList) return;
		if (max >= 0) m_maxValue = max;
		if (m_stateNeedsToChange) {
			m_stateNeedsToChange = false;
			m_spTaskbarList->SetProgressState(m_hwnd, TBPF_NORMAL);
		}
		m_spTaskbarList->SetProgressValue(m_hwnd, position, m_maxValue);
	};

	void setBusyProgress() {
		if (!m_spTaskbarList) return;
		m_spTaskbarList->SetProgressState(m_hwnd, TBPF_INDETERMINATE);
		m_stateNeedsToChange = true;
	}

	void resetProgress(int max) {
		if (!m_spTaskbarList) return;
		m_spTaskbarList->SetProgressState(m_hwnd, TBPF_NOPROGRESS);
		m_maxValue = max;
		m_stateNeedsToChange = true;
	};

};