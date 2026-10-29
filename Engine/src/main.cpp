#include <Windows.h>

#include "platform/windows/WinApp.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
	WinApp app;

	if (!app.Initialize(hInstance, L"MyGame", 1280, 720)) {
		return -1;
	}

	while (app.ProcessMessages()) {
		// ゲームの更新
		// ゲームの描画
	}

	app.Finalize();

	return 0;
}
