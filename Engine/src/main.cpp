#include <Windows.h>

#include "platform/windows/WinApp.h"
#include "graphics/D3D12Graphics.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
	int windowWidth = 1280;
	int windowHeight = 720;

	WinApp app;

	if (!app.Initialize(hInstance, L"MyGame", windowWidth, windowHeight)) {
		return -1;
	}

	D3D12Graphics graphics;

	if (!graphics.Initialize(app.GetHwnd(), windowWidth, windowHeight)) {
		app.Finalize();
		return -1;
	}

	while (app.ProcessMessages()) {
		// ゲームの更新
		// ゲームの描画
	}

	app.Finalize();

	return 0;
}
