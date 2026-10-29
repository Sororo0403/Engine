#include "platform/windows/WinApp.h"
#include "core/Logger.h"

bool WinApp::Initialize(HINSTANCE hInstance, const std::wstring &title,
                        int width, int height) {
	return window_.Create(hInstance, title, width, height);
}

bool WinApp::ProcessMessages() {
	MSG msg{};

	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT) {
			Logger::Info("ウィンドウの終了要求を受け取りました。");
			return false;
		}

		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return true;
}

void WinApp::Finalize() { window_.Destroy(); }

HWND WinApp::GetHwnd() const { return window_.GetHwnd(); }

int WinApp::GetWidth() const { return window_.GetWidth(); }

int WinApp::GetHeight() const { return window_.GetHeight(); }
