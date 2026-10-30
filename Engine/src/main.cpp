#include "core/FrameTimer.h"
#include "core/Logger.h"
#include "graphics/Camera.h"
#include "graphics/D3D12Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/MeshRenderer.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/Texture.h"
#include "graphics/Transform.h"
#include "graphics/Vertex.h"
#include "input/InputSystem.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/StringConversion.h"
#include "platform/windows/WinApp.h"
#include "platform/windows/WindowsInputBackend.h"

#include <directxtk12/DescriptorHeap.h>
#include <directxtk12/DirectXHelpers.h>

#include <Windows.h>
#include <cmath>
#include <cstdint>
#include <exception>
#include <format>
#include <memory>
#include <objbase.h>

namespace {

/// <summary>
/// 各面にテクスチャ座標を持つ立方体のメッシュを作成する
/// </summary>
/// <param name="device">D3D12デバイス</param>
/// <param name="mesh">初期化するメッシュ</param>
/// <returns>true:成功 false:失敗</returns>
bool CreateCubeMesh(ID3D12Device *device, Mesh &mesh) {
	const DirectX::XMFLOAT4 white{1.0f, 1.0f, 1.0f, 1.0f};

	const Vertex vertices[] = {// 手前の面
	                           {{-0.5f, 0.5f, -0.5f}, white, {0.0f, 0.0f}},
	                           {{0.5f, 0.5f, -0.5f}, white, {1.0f, 0.0f}},
	                           {{0.5f, -0.5f, -0.5f}, white, {1.0f, 1.0f}},
	                           {{-0.5f, -0.5f, -0.5f}, white, {0.0f, 1.0f}},

	                           // 奥の面
	                           {{0.5f, 0.5f, 0.5f}, white, {0.0f, 0.0f}},
	                           {{-0.5f, 0.5f, 0.5f}, white, {1.0f, 0.0f}},
	                           {{-0.5f, -0.5f, 0.5f}, white, {1.0f, 1.0f}},
	                           {{0.5f, -0.5f, 0.5f}, white, {0.0f, 1.0f}},

	                           // 左の面
	                           {{-0.5f, 0.5f, 0.5f}, white, {0.0f, 0.0f}},
	                           {{-0.5f, 0.5f, -0.5f}, white, {1.0f, 0.0f}},
	                           {{-0.5f, -0.5f, -0.5f}, white, {1.0f, 1.0f}},
	                           {{-0.5f, -0.5f, 0.5f}, white, {0.0f, 1.0f}},

	                           // 右の面
	                           {{0.5f, 0.5f, -0.5f}, white, {0.0f, 0.0f}},
	                           {{0.5f, 0.5f, 0.5f}, white, {1.0f, 0.0f}},
	                           {{0.5f, -0.5f, 0.5f}, white, {1.0f, 1.0f}},
	                           {{0.5f, -0.5f, -0.5f}, white, {0.0f, 1.0f}},

	                           // 上の面
	                           {{-0.5f, 0.5f, 0.5f}, white, {0.0f, 0.0f}},
	                           {{0.5f, 0.5f, 0.5f}, white, {1.0f, 0.0f}},
	                           {{0.5f, 0.5f, -0.5f}, white, {1.0f, 1.0f}},
	                           {{-0.5f, 0.5f, -0.5f}, white, {0.0f, 1.0f}},

	                           // 下の面
	                           {{-0.5f, -0.5f, -0.5f}, white, {0.0f, 0.0f}},
	                           {{0.5f, -0.5f, -0.5f}, white, {1.0f, 0.0f}},
	                           {{0.5f, -0.5f, 0.5f}, white, {1.0f, 1.0f}},
	                           {{-0.5f, -0.5f, 0.5f}, white, {0.0f, 1.0f}}};

	const std::uint16_t indices[] = {
	    0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
	    12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};

	return mesh.Initialize(device, vertices, indices);
}

/// <summary>
/// エンジンの初期化・メインループ・終了処理を実行する
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

	Mesh cubeMesh;

	if (!CreateCubeMesh(graphics.GetDevice(), cubeMesh)) {
		graphics.Finalize();
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

	std::unique_ptr<DirectX::DescriptorHeap> srvHeap;

	try {
		// シェーダーから参照できるSRVを1個格納する
		srvHeap =
		    std::make_unique<DirectX::DescriptorHeap>(graphics.GetDevice(), 1);

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

	MeshRenderer renderer;

	if (!renderer.Initialize(graphics.GetDevice(), vertexShader.Get(),
	                         pixelShader.Get())) {
		graphics.Finalize();
		app.Finalize();
		return -1;
	}

	InputSystem input;

	if (!input.Initialize(
	        std::make_unique<WindowsInputBackend>(app.GetHwnd()))) {
		graphics.Finalize();
		renderer.Finalize();
		app.Finalize();
		return -1;
	}

	Transform transform{};
	transform.scale = {0.7f, 0.7f, 0.7f};
	transform.rotation = {0.3f, 0.4f, 0.0f};
	transform.translation = {0.0f, 0.0f, 0.0f};

	renderer.SetTransform(transform);

	Camera camera;
	FrameTimer frameTimer;

	const Keyboard &keyboard = input.GetKeyboard();
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

		// 接続状態が変わったときだけ出力する
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

		// 最小化中は描画せず、次のメッセージを待つ
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

		if (!camera.Update(width, height)) {
			Logger::Error("カメラの更新に失敗しました。");
			exitCode = -1;
			break;
		}

		transform.rotation.x += 0.6f * deltaTime;
		transform.rotation.y += 1.0f * deltaTime;

		renderer.SetTransform(transform);
		renderer.Update(camera);

		if (!graphics.BeginFrame(clearColor)) {
			exitCode = -1;
			break;
		}

		auto *commandList = graphics.GetCommandList();

		ID3D12DescriptorHeap *heaps[] = {srvHeap->Heap()};
		commandList->SetDescriptorHeaps(1, heaps);

		renderer.Draw(commandList, cubeMesh, srvHeap->GetGpuHandle(0));

		if (!graphics.EndFrame()) {
			exitCode = -1;
			break;
		}
	}

	input.Finalize();

	// GPUの処理完了を待って描画基盤を終了する
	graphics.Finalize();

	// GPUの利用が終わったリソースを解放する
	renderer.Finalize();
	cubeMesh.Finalize();
	srvHeap.reset();
	texture.Finalize();

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

	// RunEngine内のオブジェクトが破棄されるまでCOMとLoggerを維持する
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
