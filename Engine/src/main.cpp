#include "core/FrameTimer.h"
#include "core/Logger.h"
#include "graphics/Camera.h"
#include "graphics/D3D12Graphics.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/TriangleRenderer.h"
#include "platform/windows/Keyboard.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/StringConversion.h"
#include "platform/windows/WinApp.h"

#include <Windows.h>
#include <cmath>
#include <filesystem>
#include <format>

namespace {
int RunEngine(HINSTANCE hInstance) {
	int windowWidth = 1280;
	int windowHeight = 720;

	ShaderCompiler shaderCompiler;

	if (!shaderCompiler.Initialize()) {
		return -1;
	}

	auto vertexShader = shaderCompiler.Compile(
	    L"resources/shaders/triangle/Triangle.VS.hlsl", L"VSMain", L"vs_6_0");

	auto pixelShader = shaderCompiler.Compile(
	    L"resources/shaders/triangle/Triangle.PS.hlsl", L"PSMain", L"ps_6_0");

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

	if (!triangle.Initialize(graphics.GetDevice(), vertexShader.Get(),
	                         pixelShader.Get())) {
		graphics.Finalize();
		app.Finalize();
		return -1;
	}

	const float clearColor[4] = {0.1f, 0.2f, 0.6f, 1.0f};
	int exitCode = 0;
	Logger::Info("メインループを開始しました。");

	Transform transform{};
	transform.scale = {0.7f, 0.7f, 1.0f};
	transform.rotation.z = 0.5f;
	transform.translation.x = 0.2f;

	triangle.SetTransform(transform);

	Camera camera;

	FrameTimer frameTimer;

	Keyboard keyboard;

	while (app.ProcessMessages()) {
		const int width = app.GetWidth();
		const int height = app.GetHeight();

		// 最小化中は描画せず、次のメッセージを待つ
		if (width <= 0 || height <= 0) {
			WaitMessage();
			frameTimer.Reset();
			continue;
		}

		// サイズが変わっていたら描画領域を作り直す
		if (!graphics.Resize(width, height)) {
			exitCode = -1;
			break;
		}

		const float deltaTime = frameTimer.Tick();

		keyboard.Update(app.GetHwnd());

		float moveX = 0.0f;
		float moveZ = 0.0f;

		if (keyboard.IsDown('W')) {
			moveZ += 1.0f;
		}
		if (keyboard.IsDown('S')) {
			moveZ -= 1.0f;
		}
		if (keyboard.IsDown('A')) {
			moveX -= 1.0f;
		}
		if (keyboard.IsDown('D')) {
			moveX += 1.0f;
		}

		// 斜め移動でも同じ速さになるようにする
		const float length = std::sqrt(moveX * moveX + moveZ * moveZ);

		if (length > 0.0f) {
			constexpr float moveSpeed = 2.0f;
			const float distance = moveSpeed * deltaTime;

			camera.Translate(
			    {moveX / length * distance, 0.0f, moveZ / length * distance});
		}

		// 移動後の位置で行列を更新する
		if (!camera.Update(width, height)) {
			Logger::Error("カメラの更新に失敗しました。");
			exitCode = -1;
			break;
		}

		// 1秒あたり1ラジアン回転する
		constexpr float rotationSpeed = 1.0f;
		transform.rotation.z += rotationSpeed * deltaTime;

		triangle.SetTransform(transform);
		triangle.Update(camera);

		if (!graphics.BeginFrame(clearColor)) {
			exitCode = -1;
			break;
		}

		triangle.Draw(graphics.GetCommandList());

		if (!graphics.EndFrame()) {
			exitCode = -1;
			break;
		}
	}

	graphics.Finalize();
	triangle.Finalize();
	app.Finalize();

	return exitCode;
}
} // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
	const auto executableDirectory = PathUtils::GetExecutableDirectory();

	if (executableDirectory.empty()) {
		Logger::Error("実行ファイルのフォルダを取得できませんでした。");
		return -1;
	}

	const auto logPath = executableDirectory / L"logs" / L"Engine.log";

	if (!Logger::Initialize(StringConversion::WideToUtf8(logPath.wstring()))) {
		return -1;
	}

	Logger::Info("Engineを起動しました。");

	// RunEngine内のオブジェクトが破棄されるまでログを有効にする。
	const int exitCode = RunEngine(hInstance);

	if (exitCode == 0) {
		Logger::Info("Engineを正常終了しました。");
	} else {
		Logger::Error(
		    std::format("Engineを異常終了しました。終了コード={}", exitCode));
	}

	Logger::Finalize();
	return exitCode;
}
