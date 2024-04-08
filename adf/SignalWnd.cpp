
#include "SignalWnd.h"

static bool classCreated = false;


LRESULT CALLBACK MessageWindowProc(_In_ HWND   hwnd, _In_ UINT   uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam) {
	if (uMsg == WM_CREATE) {
		CREATESTRUCT* ct = (CREATESTRUCT*)lParam;
		SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)ct->lpCreateParams);
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	CMessageWindow* handler = (CMessageWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	if (handler) return handler->injestMessage(hwnd, uMsg, wParam, lParam);

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void CMessageWindow::clearMessageHandler(_In_ UINT uMsg) {
	auto resp = m_messageMap.find(uMsg);
	if (resp != m_messageMap.end())
		m_messageMap.erase(resp);
}

LRESULT CMessageWindow::injestMessage(_In_ HWND hwnd, _In_ UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam) {
	auto resp = m_messageMap.find(uMsg);
	if (resp == m_messageMap.end()) return DefWindowProc(hwnd, uMsg, wParam, lParam);
	return resp->second(wParam, lParam);
}

CMessageWindow::CMessageWindow(HINSTANCE hInstance, const std::wstring& windowCaption) {
	if (!classCreated) {
		classCreated = true;
		WNDCLASSEX wx = {};
		wx.cbSize = sizeof(WNDCLASSEX);
		wx.lpfnWndProc = MessageWindowProc;        // function which will handle messages
		wx.hInstance = hInstance;
		wx.lpszClassName = MESSAGEWINDOW_CLASS_NAME;
		RegisterClassEx(&wx);
	}
	m_hWnd = CreateWindowEx(0, MESSAGEWINDOW_CLASS_NAME, windowCaption.c_str(), WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, NULL, NULL, hInstance, (LPVOID)this);
}

void CMessageWindow::setWindowTitle(const std::wstring& windowCaption) {
	SetWindowText(m_hWnd, windowCaption.c_str());
}

void CMessageWindow::setMessageHandler(UINT uMsg, std::function<LRESULT(_In_ WPARAM wParam, _In_ LPARAM lParam)> callback) {
	m_messageMap[uMsg] = callback;
}

CMessageWindow::~CMessageWindow() {
	DestroyWindow(m_hWnd);
}

const HWND CMessageWindow::hwnd() const {
	return m_hWnd;
};