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
	bool runFormatCommand(bool quickFormat, bool dirCache, bool intMode, bool installBB, bool doFFS, const std::string& volumeLabel);

public:
	DialogFORMAT(HINSTANCE hInstance, HWND hParent, SectorCacheEngine* io, MountedVolume* fs);
	INT_PTR doModal();

	// Dialog window message handler
	INT_PTR handleDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};