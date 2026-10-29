#include "platform/windows/WinApp.h"

bool WinApp::Initialize(HINSTANCE hInstance, const std::wstring &title, int width, int height) {
	return window_.Create(hInstance, title, width, height);
}

bool WinApp::ProcessMessages() {
	MSG msg{};

	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT) {
			return false;
		}

		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return true;
}

void WinApp::Finalize() {
	window_.Destroy();
}
