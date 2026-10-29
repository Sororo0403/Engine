#include "platform/windows/Window.h"

Window::~Window() {
	Destroy();
}

bool Window::Create(HINSTANCE hInstance, const std::wstring &title, int width, int height) {
	if (hwnd_ || isClassRegistered_) {
		return false;
	}

	hInstance_ = hInstance;

	WNDCLASSEX wc{};
	wc.cbSize = sizeof(WNDCLASSEX);

	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = Window::WndProc;
	wc.hInstance = hInstance_;
	wc.lpszClassName = kClassName_.c_str();

	if (!RegisterClassEx(&wc)) {
		hInstance_ = nullptr;
		return false;
	}

	isClassRegistered_ = true;

	RECT rect{
		0,
		0,
		width,
		height
	};

	if (!AdjustWindowRectEx(
		&rect,
		WS_OVERLAPPEDWINDOW,
		FALSE,
		0
	)) {
		Destroy();
		return false;
	}

	hwnd_ = CreateWindowEx(
		0,
		kClassName_.c_str(),
		title.c_str(),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		rect.right - rect.left,
		rect.bottom - rect.top,
		nullptr,
		nullptr,
		hInstance_,
		nullptr
	);

	if (!hwnd_) {
		Destroy();
		return false;
	}

	ShowWindow(hwnd_, SW_SHOW);

	return true;
}

void Window::Destroy() {
	if (hwnd_) {
		if (!DestroyWindow(hwnd_)) {
			return;
		}

		hwnd_ = nullptr;
	}

	if (isClassRegistered_) {
		if (hInstance_) {
			if (!UnregisterClass(kClassName_.c_str(), hInstance_)) {
				return;
			}

			isClassRegistered_ = false;
		}

		hInstance_ = nullptr;
	}
}

HWND Window::GetHwnd() const {
	return hwnd_;
}

LRESULT CALLBACK Window::WndProc(
	HWND hwnd,
	UINT msg,
	WPARAM wParam,
	LPARAM lParam
) {
	switch (msg) {
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}
