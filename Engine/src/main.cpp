#include "platform/windows/Window.h"

#include <Windows.h>

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	Window window;
	window.Create(hInstance, L"test", 1280, 720);

	return 0;
}
