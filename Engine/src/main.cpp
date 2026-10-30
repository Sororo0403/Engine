#include "core/FrameTimer.h"
#include "core/Logger.h"
#include "graphics/Camera.h"
#include "graphics/D3D12Graphics.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/Texture.h"
#include "graphics/TriangleRenderer.h"
#include "input/InputSystem.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/StringConversion.h"
#include "platform/windows/WinApp.h"
#include "platform/windows/WindowsInputBackend.h"

#include <directxtk12/DescriptorHeap.h>
#include <directxtk12/DirectXHelpers.h>

#include <Windows.h>
#include <cmath>
#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <objbase.h>

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

	Texture texture;

	if (!texture.Load(graphics.GetDevice(), graphics.GetCommandQueue(),
	                  L"resources/textures/sample.png")) {
		graphics.Finalize();
		app.Finalize();
		return -1;
	}

	// シェーダーから参照できるディスクリプタヒープ
	std::unique_ptr<DirectX::DescriptorHeap> srvHeap;

	try {
		// SRVを1個格納できるヒープを作成する
		srvHeap =
		    std::make_unique<DirectX::DescriptorHeap>(graphics.GetDevice(), 1);

		// ヒープの0番に、テクスチャを読むためのSRVを作成する
		DirectX::CreateShaderResourceView(graphics.GetDevice(),
		                                  texture.GetResource(),
		                                  srvHeap->GetCpuHandle(0));
	} catch (const std::exception &error) {
		Logger::Error(std::format("テクスチャ用SRVの作成に失敗しました: {}",
		                          error.what()));

		graphics.Finalize();
		app.Finalize();
		return -1;
	}

	Logger::Info("テクスチャ用SRVを作成しました。");

	TriangleRenderer triangle;

	if (!triangle.Initialize(graphics.GetDevice(), vertexShader.Get(),
	                         pixelShader.Get())) {
		graphics.Finalize();
		app.Finalize();
		return -1;
	}

	InputSystem input;

	if (!input.Initialize(
	        std::make_unique<WindowsInputBackend>(app.GetHwnd()))) {
		graphics.Finalize();
		triangle.Finalize();
		app.Finalize();
		return -1;
	}

	const float clearColor[4] = {0.1f, 0.2f, 0.6f, 1.0f};
	int exitCode = 0;
	Logger::Info("メインループを開始しました。");

	Transform transform{};
	transform.scale = {0.7f, 0.7f, 0.7f};
	transform.rotation = {0.3f, 0.4f, 0.0f};
	transform.translation = {0.0f, 0.0f, 0.0f};

	triangle.SetTransform(transform);

	Camera camera;

	FrameTimer frameTimer;

	const Keyboard &keyboard = input.GetKeyboard();
	const Gamepad &gamepad = input.GetGamepad();
	bool wasGamepadConnected = false;

	while (app.ProcessMessages()) {
		if (!input.Update()) {
			exitCode = -1;
			break;
		}
		// 接続状態が変わったときだけ出力する
		const bool connected = gamepad.IsConnected();

		if (connected != wasGamepadConnected) {
			Logger::Info(connected ? "コントローラーを接続しました。"
			                       : "コントローラーを切断しました。");

			wasGamepadConnected = connected;
		}

		// 下側のボタン（XboxのAなど）
		if (gamepad.IsPressed(GamepadButton::SOUTH)) {
			Logger::Info("コントローラーの下側ボタンが押されました。");
		}

		if (gamepad.IsReleased(GamepadButton::SOUTH)) {
			Logger::Info("コントローラーの下側ボタンが離されました。");
		}

		// MENUを押した瞬間のスティック・トリガーの値を確認する
		if (gamepad.IsPressed(GamepadButton::MENU)) {
			const GamepadState &state = gamepad.GetState();

			Logger::Info(std::format("左スティック=({:.2f}, {:.2f}) "
			                         "右スティック=({:.2f}, {:.2f}) "
			                         "左トリガー={:.2f} 右トリガー={:.2f}",
			                         state.leftStick.x, state.leftStick.y,
			                         state.rightStick.x, state.rightStick.y,
			                         state.leftTrigger, state.rightTrigger));
		}

		// この後に既存のサイズ確認・描画処理を続ける
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

		float moveX = 0.0f;
		float moveZ = 0.0f;

		if (keyboard.IsDown(Key::W)) {
			moveZ += 1.0f;
		}
		if (keyboard.IsDown(Key::S)) {
			moveZ -= 1.0f;
		}
		if (keyboard.IsDown(Key::A)) {
			moveX -= 1.0f;
		}
		if (keyboard.IsDown(Key::D)) {
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

		// X軸・Y軸で回転させて、各面を確認する
		transform.rotation.x += 0.6f * deltaTime;
		transform.rotation.y += 1.0f * deltaTime;

		triangle.SetTransform(transform);
		triangle.Update(camera);

		if (!graphics.BeginFrame(clearColor)) {
			exitCode = -1;
			break;
		}

		auto *commandList = graphics.GetCommandList();

		// 使用するSRVヒープを設定する
		ID3D12DescriptorHeap *heaps[] = {srvHeap->Heap()};
		commandList->SetDescriptorHeaps(1, heaps);

		// ヒープの0番にあるテクスチャを渡す
		triangle.Draw(commandList, srvHeap->GetGpuHandle(0));

		if (!graphics.EndFrame()) {
			exitCode = -1;
			break;
		}
	}

	input.Finalize();
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
