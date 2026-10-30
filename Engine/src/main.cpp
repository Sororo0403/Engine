#include "core/FrameTimer.h"
#include "core/Logger.h"
#include "graphics/debug/DebugUI.h"
#include "graphics/render/D3D12Graphics.h"
#include "graphics/render/MeshRenderer.h"
#include "graphics/render/ShaderCompiler.h"
#include "input/InputSystem.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/StringConversion.h"
#include "platform/windows/WinApp.h"
#include "platform/windows/WindowsInputBackend.h"
#include "scene/GameScene.h"
#include "scene/SceneManager.h"

#include <Windows.h>
#include <format>
#include <memory>
#include <objbase.h>

namespace {

/// <summary>
/// 共通機能の初期化・メインループ・終了処理を実行する
/// </summary>
/// <param name="hInstance">アプリケーションのインスタンスハンドル</param>
/// <returns>0:正常終了 -1:異常終了</returns>
int RunEngine(HINSTANCE hInstance) {
	constexpr int windowWidth = 1280;
	constexpr int windowHeight = 720;

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

	MeshRenderer renderer;

	if (!renderer.Initialize(graphics.GetDevice(), vertexShader.Get(),
	                         pixelShader.Get())) {
		graphics.Finalize();
		renderer.Finalize();
		app.Finalize();
		return -1;
	}

	InputSystem input;

	if (!input.Initialize(
	        std::make_unique<WindowsInputBackend>(app.GetHwnd()))) {
		graphics.Finalize();
		input.Finalize();
		renderer.Finalize();
		app.Finalize();
		return -1;
	}

	DebugUI debugUI;

	if (!debugUI.Initialize(app.GetHwnd(), graphics.GetDevice(),
	                        graphics.GetCommandQueue())) {
		graphics.Finalize();
		debugUI.Finalize();
		input.Finalize();
		renderer.Finalize();
		app.Finalize();
		return -1;
	}

	// シーンより先に、シーンが参照する共通機能を初期化しておく
	SceneManager sceneManager;

	if (!sceneManager.RequestChange(std::make_unique<GameScene>(
	        graphics, renderer, input, app, debugUI)) ||
	    !sceneManager.ApplyPendingChange()) {
		graphics.Finalize();
		sceneManager.Finalize();
		debugUI.Finalize();
		input.Finalize();
		renderer.Finalize();
		app.Finalize();
		return -1;
	}

	// 最初のシーンの読み込みが終わってから計測を開始する
	FrameTimer frameTimer;

	const Gamepad &gamepad = input.GetGamepad();
	bool wasGamepadConnected = false;

	const float clearColor[4] = {0.1f, 0.2f, 0.6f, 1.0f};
	int exitCode = 0;

	Logger::Info("メインループを開始しました。");

	while (app.ProcessMessages()) {
		if (!input.Update()) {
			exitCode = -1;
			break;
		}

		const bool connected = gamepad.IsConnected();

		if (connected != wasGamepadConnected) {
			Logger::Info(connected ? "コントローラーを接続しました。"
			                       : "コントローラーを切断しました。");

			wasGamepadConnected = connected;
		}

		if (gamepad.IsPressed(GamepadButton::SOUTH)) {
			Logger::Info("コントローラーの下側ボタンが押されました。");
		}

		if (gamepad.IsReleased(GamepadButton::SOUTH)) {
			Logger::Info("コントローラーの下側ボタンが離されました。");
		}

		if (gamepad.IsPressed(GamepadButton::MENU)) {
			const GamepadState &state = gamepad.GetState();

			Logger::Info(std::format("左スティック=({:.2f}, {:.2f}) "
			                         "右スティック=({:.2f}, {:.2f}) "
			                         "左トリガー={:.2f} 右トリガー={:.2f}",
			                         state.leftStick.x, state.leftStick.y,
			                         state.rightStick.x, state.rightStick.y,
			                         state.leftTrigger, state.rightTrigger));
		}

		const int width = app.GetWidth();
		const int height = app.GetHeight();

		// 最小化中はシーンの更新・描画を止める
		if (width <= 0 || height <= 0) {
			WaitMessage();
			frameTimer.Reset();
			continue;
		}

		if (!graphics.Resize(width, height)) {
			exitCode = -1;
			break;
		}

		const float deltaTime = frameTimer.Tick();

		// 前フレームのEndFrameでGPUの完了を待っているため、
		// UIや描画を開始する前のここでシーンを切り替える
		if (!sceneManager.ApplyPendingChange()) {
			exitCode = -1;
			break;
		}

		// シーンの読み込み時間を、次フレームの経過時間から除く
		frameTimer.Reset();

		if (!debugUI.BeginFrame()) {
			exitCode = -1;
			break;
		}

		// GameScene内でUIを構築し、物体・カメラ・ライトを更新する
		if (!sceneManager.Update(deltaTime)) {
			exitCode = -1;
			break;
		}

		if (!graphics.BeginFrame(clearColor)) {
			exitCode = -1;
			break;
		}

		sceneManager.Draw();

		// シーンの描画後に、UIを重ねる
		if (!debugUI.Render(graphics.GetCommandList())) {
			exitCode = -1;
			break;
		}

		// 描画を実行し、GPUの完了まで待機する
		if (!graphics.EndFrame()) {
			exitCode = -1;
			break;
		}
	}

	// GPUの利用完了を待ってから、シーンを解放する
	graphics.Finalize();

	// シーンが参照している共通機能は、その後に終了する
	sceneManager.Finalize();
	debugUI.Finalize();
	input.Finalize();
	renderer.Finalize();
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

	const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

	if (FAILED(comResult)) {
		Logger::Error(std::format("COMの初期化に失敗しました。HRESULT=0x{:08X}",
		                          static_cast<unsigned long>(comResult)));

		Logger::Finalize();
		return -1;
	}

	const int exitCode = RunEngine(hInstance);

	CoUninitialize();

	if (exitCode == 0) {
		Logger::Info("Engineを正常終了しました。");
	} else {
		Logger::Error(
		    std::format("Engineを異常終了しました。終了コード={}", exitCode));
	}

	Logger::Finalize();
	return exitCode;
}
