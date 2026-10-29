#include "platform/windows/Window.h"

Window::~Window() {
	Destroy();
}

bool Window::Create(HINSTANCE hInstance, const std::wstring &title, int width, int height) {
	if (hwnd_ || isClassRegistered_) {
		return false;
	}

	if (width <= 0 || height <= 0) {
		return false;
	}

	if (!hInstance) {
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

	width_ = width;
	height_ = height;

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
		this
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
		if (!UnregisterClass(kClassName_.c_str(), hInstance_)) {
			return;
		}

		isClassRegistered_ = false;
	}

	hInstance_ = nullptr;

	width_ = 0;
	height_ = 0;
}

HWND Window::GetHwnd() const {
	return hwnd_;
}

int Window::GetWidth() const {
	return width_;
}

int Window::GetHeight() const {
	return height_;
}

LRESULT CALLBACK Window::WndProc(
	HWND hwnd,
	UINT msg,
	WPARAM wParam,
	LPARAM lParam
) {
	if (msg == WM_NCCREATE) {
		auto *cs = reinterpret_cast<CREATESTRUCT *>(lParam);
		auto *window = static_cast<Window *>(cs->lpCreateParams);

		SetWindowLongPtr(
			hwnd,
			GWLP_USERDATA,
			reinterpret_cast<LONG_PTR>(window)
		);
	}

	auto *window = reinterpret_cast<Window *>(
		GetWindowLongPtr(hwnd, GWLP_USERDATA)
		);

	switch (msg) {
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_SIZE:
		if (window) {
			window->width_ = LOWORD(lParam);
			window->height_ = HIWORD(lParam);
		}

		return 0;
	case WM_NCDESTROY:
		SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);

		if (window) {
			window->hwnd_ = nullptr;
			window->width_ = 0;
			window->height_ = 0;
		}

		break;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}
