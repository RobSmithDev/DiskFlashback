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



#include "dlgConfig.h"
#include <Windows.h>
#include "sectorCache.h"
#include "resource.h"
#include "adflib/src/adflib.h"
#include <CommCtrl.h>
#include "readwrite_floppybridge.h"
#include "readwrite_file.h"
#include "readwrite_dms.h"
#include "floppybridge_lib.h"

#define REGISTRY_SECTIONA			"Software\\RobSmithDev\\DiskFlashback"
#define REGISTRY_SECTION			L"Software\\RobSmithDev\\DiskFlashback"
#define KEY_FLOPPY_PROFILE			"floppyprofile"
#define KEY_FLOPPY_ENABLED			"floppyenabled"
#define KEY_UPDATE_CHECK			"updatecheck"
#define KEY_DRIVE_LETTER			"driveletter"
#define KEY_LAST_UPDATE_CHECK		"lastcheck"
#define KEY_AUTO_RENAME				"autorename"

// A bit hacky but enough for what I need
uint32_t getStamp() {
	SYSTEMTIME lt;
	GetLocalTime(&lt);
	return lt.wDay + (lt.wMonth * 32) + (lt.wYear * 32 * 32);
}

// Load config from the registry
bool loadConfiguration(AppConfig& config) {
	config.checkForUpdates = true;
	config.enabled = true;
	config.floppyProfile = "";
	config.driveLetter = 'A';
	config.lastCheck = getStamp() - 5;
	config.autoRename = false;

	HKEY key;
	DWORD disp = 0;
	if (RegCreateKeyEx(HKEY_CURRENT_USER, REGISTRY_SECTION, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS | KEY_WOW64_64KEY, NULL, &key, &disp) != ERROR_SUCCESS) key = 0;
	if (!key) return false;

	DWORD dataSize = 256;
	char buffer[256];
	if (RegGetValueA(HKEY_CURRENT_USER, REGISTRY_SECTIONA, KEY_FLOPPY_PROFILE, RRF_RT_ANY, NULL, (LPBYTE)buffer, &dataSize) == ERROR_SUCCESS) {
		buffer[dataSize] = '\0';
		config.floppyProfile = buffer;
	}
	dataSize = 256;
	if (RegGetValueA(HKEY_CURRENT_USER, REGISTRY_SECTIONA, KEY_DRIVE_LETTER, RRF_RT_ANY, NULL, (LPBYTE)buffer, &dataSize) == ERROR_SUCCESS) {
		buffer[dataSize] = '\0';
		config.driveLetter = buffer[0];
	}

	DWORD dTemp;
	dataSize = sizeof(dTemp);
	if (RegQueryValueExA(key, KEY_FLOPPY_ENABLED, NULL, NULL, (LPBYTE)&dTemp, &dataSize) != ERROR_SUCCESS) dataSize = 0;
	if (dataSize == sizeof(dTemp)) config.enabled = dTemp != 0;

	dataSize = sizeof(dTemp);
	if (RegQueryValueExA(key, KEY_UPDATE_CHECK , NULL, NULL, (LPBYTE)&dTemp, &dataSize) != ERROR_SUCCESS) dataSize = 0;
	if (dataSize == sizeof(dTemp)) config.checkForUpdates = dTemp != 0;

	dataSize = sizeof(dTemp);
	if (RegQueryValueExA(key, KEY_LAST_UPDATE_CHECK, NULL, NULL, (LPBYTE)&dTemp, &dataSize) != ERROR_SUCCESS) dataSize = 0;
	if (dataSize == sizeof(dTemp)) config.lastCheck = dTemp;

	dataSize = sizeof(dTemp);
	if (RegQueryValueExA(key, KEY_AUTO_RENAME, NULL, NULL, (LPBYTE)&dTemp, &dataSize) != ERROR_SUCCESS) dataSize = 0;
	if (dataSize == sizeof(dTemp)) config.autoRename = dTemp != 0;

	RegCloseKey(key);
	return true;
}


// Save config to the registry
bool saveConfiguration(const AppConfig& config) {
	HKEY key;
	DWORD disp = 0;
	if (RegCreateKeyEx(HKEY_CURRENT_USER, REGISTRY_SECTION, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS | KEY_WOW64_64KEY, NULL, &key, &disp) != ERROR_SUCCESS) key = 0;
	if (!key) return false;

	RegSetValueExA(key, KEY_FLOPPY_PROFILE, 0, REG_SZ, (const BYTE*)config.floppyProfile.c_str(), (DWORD)config.floppyProfile.length());
	RegSetValueExA(key, KEY_DRIVE_LETTER, 0, REG_SZ, (const BYTE*)&config.driveLetter, 1);

	DWORD dTemp = config.enabled ? 1 : 0;
	RegSetValueExA(key, KEY_FLOPPY_ENABLED, 0, REG_DWORD, (const BYTE*)& dTemp, sizeof(dTemp));

	dTemp = config.checkForUpdates ? 1 : 0;
	RegSetValueExA(key, KEY_UPDATE_CHECK, 0, REG_DWORD, (const BYTE*)&dTemp, sizeof(dTemp));
	dTemp = config.autoRename ? 1 : 0;
	RegSetValueExA(key, KEY_AUTO_RENAME, 0, REG_DWORD, (const BYTE*)&dTemp, sizeof(dTemp));
	RegSetValueExA(key, KEY_LAST_UPDATE_CHECK, 0, REG_DWORD, (const BYTE*)&config.lastCheck, sizeof(config.lastCheck));

	RegCloseKey(key);
	return true;
}

DialogConfig::DialogConfig(HINSTANCE hInstance, HWND hParent) : m_hInstance(hInstance), m_hParent(hParent), m_dialogBox(0) {
	loadConfiguration(m_config);
	FloppyBridgeAPI::getDriverList(m_driverList);
	FloppyBridgeAPI::enumCOMPorts(m_comPortList);

	if (m_config.floppyProfile.length()) FloppyBridgeAPI::createDriverFromString(m_config.floppyProfile.c_str());
	if (!m_api) {
		m_api = FloppyBridgeAPI::createDriver(0);
		m_api->setAutoCacheMode(false);
		m_api->setBridgeDensityMode(FloppyBridge::BridgeDensityMode::bdmAuto);
		m_api->setComPortAutoDetect(true);
		m_api->setDriveCableSelection(FloppyBridge::DriveSelection::dsDriveA);
		m_api->setSmartSpeedEnabled(false);
	}

	bool autodetect = false;
	if (m_api->getComPortAutoDetect(&autodetect)) {
		bool found = false;
		if (m_api->getComPort(&m_activePort)) {
			for (const TCHAR* p : m_comPortList)
				if (wcscmp(p, m_activePort) == 0) {
					found = true;
					break;
				}			
			if (!found) m_comPortList.push_back(m_activePort);
		}

	}
}


INT_PTR CALLBACK configCallback(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_INITDIALOG) SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)lParam);
	DialogConfig* dlg = (DialogConfig*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	if (dlg) return dlg->handleDialogProc(hwnd, msg, wParam, lParam); else return FALSE;
}

bool DialogConfig::doModal() {
	return DialogBoxParam(m_hInstance, MAKEINTRESOURCE(IDD_CONFIG), m_hParent, configCallback, (LPARAM)this);
}

// Init dialog
void DialogConfig::handleInitDialog(HWND hwnd) {
	m_dialogBox = hwnd;

	HICON icon = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_ICON1));
	SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
	SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);

	
	/// DRIVER
	HWND ctrl = GetDlgItem(hwnd, IDC_DRIVER);
	for (const FloppyBridgeAPI::DriverInformation& driver : m_driverList) {
		LRESULT pos = SendMessage(ctrl, CB_ADDSTRING, 0, (LPARAM)driver.name);
		SendMessage(ctrl, CB_SETITEMDATA, (WPARAM)pos, (LPARAM)&driver);
	}
	// Set value
	int driverIndex = 0;
	if (!m_api->getDriverIndex(driverIndex)) driverIndex = 0;
	SendMessage(ctrl, CB_SETCURSEL, driverIndex, 0);

	/// COM PORT
	ctrl = GetDlgItem(hwnd, IDC_COMPORT);
	SendMessage(ctrl, CB_SETDROPPEDWIDTH, 230, 0);	
	TCharString portSelected = { 0 };
	if (m_api->getComPort(&portSelected))
		for (const TCHAR* port : m_comPortList) {
			LRESULT pos = SendMessage(ctrl, CB_ADDSTRING, 0, (LPARAM)port);
			SendMessage(ctrl, CB_SETITEMDATA, (WPARAM)pos, (LPARAM)port);
			if (port)
				if ((pos==0) || (wcscmp(port, portSelected) == 0)) SendMessage(ctrl, CB_SETCURSEL, pos, 0);
		}

	/// AUTO DETECT COM PORT
	bool autoDetect = true;
	m_api->getComPortAutoDetect(&autoDetect);
	SendMessage(GetDlgItem(m_dialogBox, IDC_AUTODETECT), BM_SETCHECK, autoDetect ? BST_CHECKED:BST_UNCHECKED, 0);

	ctrl = GetDlgItem(hwnd, IDC_LETTER);
	for (WCHAR letter = L'A'; letter <= L'Z'; letter++) {
		WCHAR buffer[2] = { 0 };
		buffer[0] = letter;
		SendMessage(ctrl, CB_ADDSTRING, 0, (LPARAM)buffer);
	}

	refreshSelection();

	
	BringWindowToTop(hwnd);
	SetForegroundWindow(hwnd);
} 

// Refresh current selection stuff
void DialogConfig::refreshSelection() {
	HWND ctrl = GetDlgItem(m_dialogBox, IDC_DRIVER);
	LRESULT selIndex = SendMessage(ctrl, CB_GETCURSEL, 0, 0);
	FloppyBridgeAPI::DriverInformation* info = (FloppyBridgeAPI::DriverInformation*)SendMessage(ctrl, CB_GETITEMDATA, selIndex, 0);

	// Cable select
	HWND w = GetDlgItem(m_dialogBox, IDC_CABLE);
	bool neededCable = (info->configOptions & (FloppyBridgeAPI::ConfigOption_DriveABCable | FloppyBridgeAPI::ConfigOption_SupportsShugartMode)) != 0;
	EnableWindow(w, neededCable);
	while (SendMessage(w, CB_GETCOUNT, 0, 0) > 0) SendMessage(w, CB_DELETESTRING, 0, 0);
	SendMessage(w, CB_ADDSTRING, 0, (LPARAM)L"IBMPC Drive as A"); SendMessage(w, CB_SETITEMDATA, (WPARAM)0, 0);
	SendMessage(w, CB_ADDSTRING, 0, (LPARAM)L"IBMPC Drive as B"); SendMessage(w, CB_SETITEMDATA, (WPARAM)1, 1);
	if (info->configOptions & FloppyBridgeAPI::ConfigOption_SupportsShugartMode) {
		SendMessage(w, CB_ADDSTRING, 0, (LPARAM)L"Shugart Drive 0 (Not Recommended)"); SendMessage(w, CB_SETITEMDATA, (WPARAM)2, 1);
		SendMessage(w, CB_ADDSTRING, 0, (LPARAM)L"Shugart Drive 1 (Not Recommended)"); SendMessage(w, CB_SETITEMDATA, (WPARAM)3, 1);
		SendMessage(w, CB_ADDSTRING, 0, (LPARAM)L"Shugart Drive 2 (Not Recommended)"); SendMessage(w, CB_SETITEMDATA, (WPARAM)4, 1);
		SendMessage(w, CB_ADDSTRING, 0, (LPARAM)L"Shugart Drive 3 (Not Recommended)"); SendMessage(w, CB_SETITEMDATA, (WPARAM)5, 1);
	}

	FloppyBridge::DriveSelection sel = FloppyBridge::DriveSelection::dsDriveA;
	m_api->getDriveCableSelection(&sel);
	SendMessage(w, CB_SETCURSEL, (int)sel, 0);

	EnableWindow(GetDlgItem(m_dialogBox, IDC_AUTODETECT), (info->configOptions & FloppyBridgeAPI::ConfigOption_AutoDetectComport) != 0);
	bool manualComPort = ((info->configOptions & FloppyBridgeAPI::ConfigOption_ComPort) != 0) && (SendMessage(GetDlgItem(m_dialogBox, IDC_AUTODETECT), BM_GETCHECK, 0, 0) == BST_UNCHECKED);
	EnableWindow(GetDlgItem(m_dialogBox, IDC_COMPORT), manualComPort);

	bool enabled = true;
	if (neededCable) {
		HWND w = GetDlgItem(m_dialogBox, IDC_CABLE);
		if (SendMessage(w, CB_GETCURSEL, 0, 0) < 0) enabled = false;
	}
	if (manualComPort) {
		HWND w = GetDlgItem(m_dialogBox, IDC_COMPORT);
		if (SendMessage(w, CB_GETCURSEL, 0, 0) < 0) enabled = false;
	}

	w = GetDlgItem(m_dialogBox, IDC_LETTER);	
	SendMessage(w, CB_SETCURSEL, m_config.driveLetter - 'A', 0);

	EnableWindow(GetDlgItem(m_dialogBox, IDOK), enabled);
}

// Grab the config from the dialog
void DialogConfig::grabConfig() {
	HWND w;

	w = GetDlgItem(m_dialogBox, IDC_DRIVER);
	m_api->setDriverIndex((int)SendMessage(w, CB_GETCURSEL, 0, 0));

	w = GetDlgItem(m_dialogBox, IDC_CABLE);
	m_api->setDriveCableSelection((FloppyBridge::DriveSelection)SendMessage(w, CB_GETCURSEL, 0, 0));

	w = GetDlgItem(m_dialogBox, IDC_AUTODETECT);
	m_api->setComPortAutoDetect(SendMessage(w, BM_GETCHECK, 0, 0) == BST_CHECKED);

	w = GetDlgItem(m_dialogBox, IDC_COMPORT);
	LRESULT index = SendMessage(w, CB_GETCURSEL, 0, 0);
	if (index >= 0) {
		TCHAR* res = (TCHAR*)SendMessage(w, CB_GETITEMDATA, index, 0);
		if (res) m_api->setComPort(res);
	}

	w = GetDlgItem(m_dialogBox, IDC_LETTER);
	m_config.driveLetter = (char)(SendMessage(w, CB_GETCURSEL, 0, 0) + 'A');
}

// Dialog window message handler
INT_PTR DialogConfig::handleDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_INITDIALOG:
		handleInitDialog(hwnd);
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_AUTODETECT:
			if (HIWORD(wParam) == BN_CLICKED) {
				grabConfig();
				refreshSelection();
			}
			return TRUE;

		case IDC_COMPORT:
		case IDC_DRIVER:
		case IDC_CABLE:
			if (HIWORD(wParam) == CBN_SELCHANGE) {
				grabConfig();
				refreshSelection();
			}
			return TRUE;

		case IDOK: {
			char* profile;
			grabConfig();
			m_api->getConfigAsString(&profile);
			m_config.floppyProfile = profile;
			saveConfiguration(m_config);
			EndDialog(hwnd, TRUE);
			}
			return TRUE;

		case ID_CLOSE:
		case IDCANCEL:
			EndDialog(hwnd, FALSE);
			return TRUE;
		}
	}

	return FALSE;
}




