#pragma once

#include "adf_operations.h"
#include <thread>
#include "floppybridge_lib.h"

class DialogConfig {
private:
	HINSTANCE m_hInstance;
	HWND m_hParent;
	HWND m_dialogBox;
	std::vector<FloppyBridgeAPI::DriverInformation> m_driverList;
	std::vector<const TCHAR*> m_comPortList;

	// Init dialog
	void handleInitDialog(HWND hwnd);

	// Save profile
	void saveProfile(const std::string);

	// Refresh current selection stuff
	void refreshSelection();
public:
	DialogConfig(HINSTANCE hInstance, HWND hParent);
	INT_PTR doModal();

	// Load profile
	static std::string loadProfile();

	// Dialog window message handler
	INT_PTR handleDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};