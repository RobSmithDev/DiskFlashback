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


DialogConfig::DialogConfig(HINSTANCE hInstance, HWND hParent) : m_hInstance(hInstance), m_hParent(hParent) {
}


INT_PTR CALLBACK configCallback(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_INITDIALOG) SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)lParam);
	DialogConfig* dlg = (DialogConfig*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	if (dlg) return dlg->handleDialogProc(hwnd, msg, wParam, lParam); else return FALSE;
}

INT_PTR DialogConfig::doModal() {
	

	return DialogBoxParam(m_hInstance, MAKEINTRESOURCE(IDD_CONFIG), m_hParent, configCallback, (LPARAM)this);
}

// Save profile
void DialogConfig::saveProfile(const std::string) {

}

// Load profile
std::string DialogConfig::loadProfile() {
	return "";
}

// Init dialog
void DialogConfig::handleInitDialog(HWND hwnd) {
	m_dialogBox = hwnd;


	FloppyBridgeAPI::getDriverList(m_driverList);

	HWND ctrl = GetDlgItem(hwnd, IDC_DRIVER);
	for (const FloppyBridgeAPI::DriverInformation& driver : m_driverList) {
		LRESULT pos = SendMessage(ctrl, CB_ADDSTRING, 0, (LPARAM)driver.name);
		SendMessage(ctrl, CB_SETITEMDATA, (WPARAM)pos, (LPARAM)&driver);
	}
	SendMessage(ctrl, CB_SETCURSEL, 0, 0);

	ctrl = GetDlgItem(hwnd, IDC_COMPORT);
	SendMessage(ctrl, CB_SETDROPPEDWIDTH, 230, 0);
	FloppyBridgeAPI::enumCOMPorts(m_comPortList);
	for (const TCHAR* port : m_comPortList) {
		LRESULT pos = SendMessage(ctrl, CB_ADDSTRING, 0, (LPARAM)port);
		SendMessage(ctrl, CB_SETITEMDATA, (WPARAM)pos, (LPARAM)port);
	}
	SendMessage(ctrl, CB_SETCURSEL, 0, 0);

	refreshSelection();
	
	BringWindowToTop(hwnd);
	SetForegroundWindow(hwnd);
} 

// Refresh current selection stuff
void DialogConfig::refreshSelection() {
	HWND ctrl = GetDlgItem(m_dialogBox, IDC_DRIVER);
	uint32_t selIndex = SendMessage(ctrl, CB_GETCURSEL, 0, 0);
	FloppyBridgeAPI::DriverInformation* info = (FloppyBridgeAPI::DriverInformation*)SendMessage(ctrl, CB_GETITEMDATA, selIndex, 0);

	// Cable select
	HWND w = GetDlgItem(m_dialogBox, IDC_CABLE);
	EnableWindow(w, (info->configOptions & (FloppyBridgeAPI::ConfigOption_DriveABCable | FloppyBridgeAPI::ConfigOption_SupportsShugartMode)) != 0);
	while (SendMessage(w, CB_GETCOUNT, 0, 0) > 0) SendMessage(w, CB_DELETESTRING, 0, 0);
	SendMessage(w, CB_ADDSTRING, 0, (LPARAM)L"IBMPC Drive as A"); SendMessage(w, CB_SETITEMDATA, (WPARAM)0, 0);
	SendMessage(w, CB_ADDSTRING, 0, (LPARAM)L"IBMPC Drive as B"); SendMessage(w, CB_SETITEMDATA, (WPARAM)1, 1);
	if (info->configOptions & FloppyBridgeAPI::ConfigOption_SupportsShugartMode) {
		SendMessage(w, CB_ADDSTRING, 0, (LPARAM)L"Shugart Drive 0 (Not Recommended)"); SendMessage(w, CB_SETITEMDATA, (WPARAM)2, 1);
		SendMessage(w, CB_ADDSTRING, 0, (LPARAM)L"Shugart Drive 1 (Not Recommended)"); SendMessage(w, CB_SETITEMDATA, (WPARAM)3, 1);
		SendMessage(w, CB_ADDSTRING, 0, (LPARAM)L"Shugart Drive 2 (Not Recommended)"); SendMessage(w, CB_SETITEMDATA, (WPARAM)4, 1);
		SendMessage(w, CB_ADDSTRING, 0, (LPARAM)L"Shugart Drive 3 (Not Recommended)"); SendMessage(w, CB_SETITEMDATA, (WPARAM)5, 1);
	}

	EnableWindow(GetDlgItem(m_dialogBox, IDC_AUTODETECT), (info->configOptions & FloppyBridgeAPI::ConfigOption_AutoDetectComport) != 0);
	EnableWindow(GetDlgItem(m_dialogBox, IDC_COMPORT), ((info->configOptions & FloppyBridgeAPI::ConfigOption_ComPort) != 0) && (SendMessage(GetDlgItem(m_dialogBox, IDC_AUTODETECT), BM_GETCHECK, 0, 0) == BST_UNCHECKED));
}

// Dialog window message handler
INT_PTR DialogConfig::handleDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_INITDIALOG:
		handleInitDialog(hwnd);
		return TRUE;

	case WM_DESTROY:
		break;

	case WM_USER:

		//EndDialog(hwnd, FALSE);
		break;


	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_DRIVER:
		case IDC_AUTODETECT:
			refreshSelection();
			break;

		case ID_CLOSE:
		case IDCANCEL:
			EndDialog(hwnd, FALSE);
			return TRUE;
		}
	}

	return FALSE;
}




