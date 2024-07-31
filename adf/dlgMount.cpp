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



#include "dlgMount.h"
#include "resource.h"
#include <CommCtrl.h>
#include "drivecontrol.h"
#include <Dbt.h>


DialogMount::DialogMount(HINSTANCE hInstance, HWND hParent) : m_hInstance(hInstance), m_hParent(hParent) {
}

DialogMount::~DialogMount() {
}

INT_PTR CALLBACK mountCallback(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_INITDIALOG) SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)lParam);
	DialogMount* dlg = (DialogMount*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	if (dlg) return dlg->handleDialogProc(hwnd, msg, wParam, lParam); else return FALSE;
}

void DialogMount::run(const std::wstring exeName) {
	if (DialogBoxParam(m_hInstance, MAKEINTRESOURCE(IDD_MOUNTRAW), m_hParent, mountCallback, (LPARAM)this)) {
		// Trigger mounting
		std::wstring cmd = L"\"" + exeName + L"\" " + COMMANDLINE_MOUNTRAW + L" ? " + (m_readOnly ? L"0" : L"1") + L" \"" + m_selectedDriveName + L"\"";
		PROCESS_INFORMATION pi;
		STARTUPINFO si;
		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);
		ZeroMemory(&pi, sizeof(pi));
		CreateProcess(NULL, (LPWSTR)cmd.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
		if (pi.hThread) CloseHandle(pi.hThread);
		if (pi.hProcess) CloseHandle(pi.hProcess);
	}
}

// Init dialog
void DialogMount::handleInitDialog(HWND hwnd) {
	m_dialogBox = hwnd;

	HICON icon = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_ICON1));
	SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
	SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);

	LVCOLUMNW col;
	memset(&col, 0, sizeof(col));
	col.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_FMT | LVCF_SUBITEM;

	m_listView = GetDlgItem(m_dialogBox, IDC_DRIVELIST);
	col.cx = 200; col.pszText = (LPWSTR)L"Device Name"; col.iSubItem = 0; ListView_InsertColumn(m_listView, 0, &col);
	col.cx = 45; col.pszText = (LPWSTR)L"Partition";  col.iSubItem = 1; ListView_InsertColumn(m_listView, 1, &col);
	col.cx = 100; col.pszText = (LPWSTR)L"File Systems";  col.iSubItem = 2; ListView_InsertColumn(m_listView, 2, &col);
	col.cx = 230; col.pszText = (LPWSTR)L"Volume Names";  col.iSubItem = 3; ListView_InsertColumn(m_listView, 3, &col);
	SendMessage(GetDlgItem(m_dialogBox, IDC_READONLY), BM_SETCHECK, BST_CHECKED, 0);

	ListView_SetExtendedListViewStyle(m_listView, LVS_EX_FULLROWSELECT);
	EnableWindow(GetDlgItem(m_dialogBox, ID_MOUNT), FALSE);
	
	BringWindowToTop(hwnd);
	SetForegroundWindow(hwnd);

	// Delay reading until the dialog is open
	SetTimer(m_dialogBox, 100, 100, NULL);
} 

// Refresh current selection stuff
void DialogMount::refreshSelection() {
	EnableWindow(m_dialogBox, FALSE);
	HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
	ListView_DeleteAllItems(m_listView);
	m_driveList.refreshList();
	const CDriveList::DeviceList list = m_driveList.getDevices();

	LVITEM itm;
	memset(&itm, 0, sizeof(itm));
	itm.mask = LVIF_TEXT;

	for (size_t index = 0; index < list.size(); index++) {
		itm.pszText = (wchar_t*)list[index].deviceName.c_str();
		itm.iItem = index;
		ListView_InsertItem(m_listView, &itm);
		if (list[index].partitionNumber) {
			WCHAR tmp[10];
			swprintf_s(tmp, L"%i", list[index].partitionNumber);
			ListView_SetItemText(m_listView, index, 1, tmp);
		}
		else ListView_SetItemText(m_listView, index, 1, (LPWSTR)L"");

		if (list[index].isAccessible) {
			ListView_SetItemText(m_listView, index, 2, (LPWSTR)list[index].fileSystem.c_str());
			ListView_SetItemText(m_listView, index, 3, (LPWSTR)list[index].volumeName.c_str());
		}
		else {
			ListView_SetItemText(m_listView, index, 2, (LPWSTR)L"Access Denied");
			ListView_SetItemText(m_listView, index, 3, (LPWSTR)L"Access Denied");
		}
	}
	EnableWindow(GetDlgItem(m_dialogBox, ID_MOUNT), ListView_GetNextItem(m_listView, -1, LVNI_SELECTED) >= 0);
	EnableWindow(m_dialogBox, TRUE);
	SetCursor(old);
}

void DialogMount::endDialogSuccess() {
	int selected = ListView_GetNextItem(m_listView, -1, LVNI_SELECTED);
	m_readOnly = SendMessage(GetDlgItem(m_dialogBox, IDC_READONLY), BM_GETCHECK, 0, 0) == BST_CHECKED;

	const CDriveList::DeviceList list = m_driveList.getDevices();
	if ((selected >= 0) && (selected < (int)list.size())) {
		m_selectedDriveName = list[selected].deviceName;
		EndDialog(m_dialogBox, TRUE);
	}
}

// Dialog window message handler
INT_PTR DialogMount::handleDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_INITDIALOG:
		handleInitDialog(hwnd);
		return TRUE;

	case WM_NOTIFY:
		switch (((LPNMHDR)lParam)->code) {
			case LVN_ITEMCHANGED:
				EnableWindow(GetDlgItem(m_dialogBox, ID_MOUNT), ListView_GetNextItem(m_listView, -1, LVNI_SELECTED) >= 0);
				break;
			case NM_RETURN:
			case NM_DBLCLK:
				endDialogSuccess();
				break;

		}
		break;

	case WM_TIMER:
		if (wParam == 100) {
			KillTimer(m_dialogBox, wParam);
			refreshSelection();
		}
		break;

	case WM_DEVICECHANGE:
		if ((wParam == DBT_DEVICEARRIVAL) || (wParam == DBT_DEVNODES_CHANGED) || (wParam == DBT_DEVICEREMOVEPENDING) || (wParam == DBT_DEVICEREMOVECOMPLETE)) {
			KillTimer(m_dialogBox, 100);
			SetTimer(m_dialogBox, 100, 250, NULL);
			refreshSelection();
		}			
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case ID_REFRESH: refreshSelection(); break;
		case ID_MOUNT:
			endDialogSuccess();
			break;

		case ID_CLOSE:
		case IDCANCEL:
			EndDialog(hwnd, FALSE);
			return TRUE;
		}
	}

	return FALSE;
}




