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
