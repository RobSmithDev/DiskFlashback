#pragma once

#include "adf.h"
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
	INT_PTR doModal();

	// Dialog window message handler
	INT_PTR handleDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};