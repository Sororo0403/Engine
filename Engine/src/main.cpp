#include <Windows.h>

#include "platform/windows/WinApp.h"
#include "graphics/D3D12Graphics.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/TriangleRenderer.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
	int windowWidth = 1280;
	int windowHeight = 720;

	ShaderCompiler shaderCompiler;

	if (!shaderCompiler.Initialize()) {
		return -1;
	}

	auto vertexShader = shaderCompiler.Compile(
		L"resources/shaders/triangle/Triangle.VS.hlsl",
		L"VSMain",
		L"vs_6_0"
	);

	auto pixelShader = shaderCompiler.Compile(
		L"resources/shaders/triangle/Triangle.PS.hlsl",
		L"PSMain",
		L"ps_6_0"
	);

	shaderCompiler.Finalize();

	if (!vertexShader || !pixelShader) {
		return -1;
	}

	WinApp app;

	if (!app.Initialize(hInstance, L"MyGame", windowWidth, windowHeight)) {
		return -1;
	}

	D3D12Graphics graphics;

	if (!graphics.Initialize(app.GetHwnd(), windowWidth, windowHeight)) {
		app.Finalize();
		return -1;
	}

	TriangleRenderer triangle;

	if (!triangle.Initialize(
		graphics.GetDevice(),
		vertexShader.Get(),
		pixelShader.Get()
	)) {
		OutputDebugStringW(L"三角形の描画準備に失敗しました。\n");
		graphics.Finalize();
		app.Finalize();
		return -1;
	}

	OutputDebugStringW(L"三角形の描画準備に成功しました。\n");

	const float clearColor[4] = {0.1f, 0.2f, 0.6f, 1.0f};

	while (app.ProcessMessages()) {
		if (!graphics.BeginFrame(clearColor)) {
			break;
		}

		triangle.Draw(graphics.GetCommandList());

		if (!graphics.EndFrame()) {
			break;
		}
	}

	graphics.Finalize();
	triangle.Finalize();
	app.Finalize();

	return 0;
}
