#include "platform/windows/Window.h"
#include "platform/windows/StringConversion.h"
#include "core/Logger.h"

#include <format>

Window::~Window() {
	Destroy();
}

bool Window::Create(HINSTANCE hInstance, const std::wstring &title, int width, int height) {
	if (hwnd_ || isClassRegistered_) {
		Logger::Error("Window::Create: ウィンドウはすでに作成されています。");
		return false;
	}

	if (width <= 0 || height <= 0) {
		Logger::Error(std::format("Window::Create: 不正なサイズです。{}x{}", width, height));
		return false;
	}

	if (!hInstance) {
		Logger::Error("Window::Create: hInstanceが無効です。");
		return false;
	}

	hInstance_ = hInstance;

	WNDCLASSEX wc{};
	wc.cbSize = sizeof(WNDCLASSEX);

	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = Window::WndProc;
	wc.hInstance = hInstance_;
	wc.lpszClassName = kClassName.c_str();

	if (!RegisterClassEx(&wc)) {
		const DWORD error = GetLastError();
		Logger::Error(std::format("RegisterClassExに失敗しました。Win32=0x{:08X}", error));
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
		const DWORD error = GetLastError();
		Logger::Error(std::format("AdjustWindowRectExに失敗しました。Win32=0x{:08X}", error));
		Destroy();
		return false;
	}

	width_ = width;
	height_ = height;

	hwnd_ = CreateWindowEx(
		0,
		kClassName.c_str(),
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
		const DWORD error = GetLastError();
		Logger::Error(std::format("CreateWindowExに失敗しました。Win32=0x{:08X}", error));
		Destroy();
		return false;
	}

	ShowWindow(hwnd_, SW_SHOW);
	Logger::Info(std::format("ウィンドウを作成しました: {} ({}x{})",
		StringConversion::WideToUtf8(title), width_, height_));

	return true;
}

void Window::Destroy() {
	const bool hadResources = hwnd_ || isClassRegistered_;
	if (hwnd_) {
		if (!DestroyWindow(hwnd_)) {
			const DWORD error = GetLastError();
			Logger::Error(std::format("DestroyWindowに失敗しました。Win32=0x{:08X}", error));
			return;
		}

		hwnd_ = nullptr;
	}

	if (isClassRegistered_) {
		if (!UnregisterClass(kClassName.c_str(), hInstance_)) {
			const DWORD error = GetLastError();
			Logger::Error(std::format("UnregisterClassに失敗しました。Win32=0x{:08X}", error));
			return;
		}

		isClassRegistered_ = false;
	}

	hInstance_ = nullptr;

	width_ = 0;
	height_ = 0;
	if (hadResources) {
		Logger::Info("ウィンドウのリソースを解放しました。");
	}
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
