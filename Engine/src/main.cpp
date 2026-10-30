#include "core/FrameTimer.h"
#include "core/Logger.h"
#include "graphics/Camera.h"
#include "graphics/render/D3D12Graphics.h"
#include "graphics/debug/DebugUI.h"
#include "graphics/DirectionalLight.h"
#include "graphics/render/MeshRenderer.h"
#include "graphics/model/Model.h"
#include "graphics/model/ModelData.h"
#include "graphics/model/ModelInstance.h"
#include "graphics/model/ModelLoader.h"
#include "graphics/render/ShaderCompiler.h"
#include "graphics/Transform.h"
#include "input/InputSystem.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/StringConversion.h"
#include "platform/windows/WinApp.h"
#include "platform/windows/WindowsInputBackend.h"

#include <Windows.h>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <imgui.h>
#include <memory>
#include <objbase.h>

namespace {

/// <summary>
/// エンジンの初期化・メインループ・終了処理を実行する
/// </summary>
/// <param name="hInstance">アプリケーションのインスタンスハンドル</param>
/// <returns>0:正常終了 -1:異常終了</returns>
int RunEngine(HINSTANCE hInstance) {
	constexpr int windowWidth = 1280;
	constexpr int windowHeight = 720;
	constexpr std::size_t objectCount = 2;

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

	// 左右の物体が共有する形状・画像・SRV
	Model model;

	{
		ModelData modelData;

		if (!ModelLoader::Load(L"resources/models/box.glb", modelData) ||
		    !model.Initialize(graphics.GetDevice(), graphics.GetCommandQueue(),
		                      modelData)) {
			graphics.Finalize();
			model.Finalize();
			app.Finalize();
			return -1;
		}
	}

	MeshRenderer renderer;

	if (!renderer.Initialize(graphics.GetDevice(), vertexShader.Get(),
	                         pixelShader.Get())) {
		graphics.Finalize();
		renderer.Finalize();
		model.Finalize();
		app.Finalize();
		return -1;
	}

	// それぞれが独立した変形と材質を持つ
	std::array<ModelInstance, objectCount> instances;

	for (std::size_t i = 0; i < instances.size(); ++i) {
		if (!instances[i].Initialize(graphics.GetDevice(), model)) {
			graphics.Finalize();

			for (auto &instance : instances) {
				instance.Finalize();
			}

			renderer.Finalize();
			model.Finalize();
			app.Finalize();
			return -1;
		}

		Transform transform{};
		transform.scale = {0.45f, 0.45f, 0.45f};
		transform.rotation = {0.3f, 0.4f, 0.0f};
		transform.translation.x = (i == 0) ? -0.6f : 0.6f;

		instances[i].SetTransform(transform);
	}

	InputSystem input;

	if (!input.Initialize(
	        std::make_unique<WindowsInputBackend>(app.GetHwnd()))) {
		graphics.Finalize();

		for (auto &instance : instances) {
			instance.Finalize();
		}

		renderer.Finalize();
		model.Finalize();
		app.Finalize();
		return -1;
	}

	DirectionalLight light;

	if (!light.Initialize(graphics.GetDevice())) {
		input.Finalize();
		graphics.Finalize();

		for (auto &instance : instances) {
			instance.Finalize();
		}

		renderer.Finalize();
		model.Finalize();
		app.Finalize();
		return -1;
	}

	DebugUI debugUI;

	if (!debugUI.Initialize(app.GetHwnd(), graphics.GetDevice(),
	                        graphics.GetCommandQueue())) {
		input.Finalize();
		graphics.Finalize();
		debugUI.Finalize();

		for (auto &instance : instances) {
			instance.Finalize();
		}

		light.Finalize();
		renderer.Finalize();
		model.Finalize();
		app.Finalize();
		return -1;
	}

	DirectionalLightData lightData{};
	lightData.direction = {1.0f, -1.0f, 1.0f};
	lightData.intensity = 0.8f;
	lightData.color = {1.0f, 1.0f, 1.0f};
	lightData.ambient = 0.2f;

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

		if (!debugUI.BeginFrame()) {
			exitCode = -1;
			break;
		}

		// ImGuiに用意されている操作例を表示する
		ImGui::ShowDemoWindow();

		float moveX = 0.0f;
		float moveZ = 0.0f;

		// UIがキーボード入力を使用している間は、カメラを動かさない
		if (!debugUI.WantsKeyboard()) {
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
		}

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

		if (!light.Update(lightData)) {
			exitCode = -1;
			break;
		}

		bool instancesUpdated = true;

		for (std::size_t i = 0; i < instances.size(); ++i) {
			Transform transform = instances[i].GetTransform();

			const float rotationSpeed = (i == 0) ? 1.0f : -0.7f;

			transform.rotation.y += rotationSpeed * deltaTime;

			instances[i].SetTransform(transform);

			// 全メッシュの変換行列と、全材質をまとめて更新する
			if (!instances[i].Update(camera)) {
				instancesUpdated = false;
				break;
			}
		}

		if (!instancesUpdated) {
			exitCode = -1;
			break;
		}

		if (!graphics.BeginFrame(clearColor)) {
			exitCode = -1;
			break;
		}

		auto *commandList = graphics.GetCommandList();

		// 各物体がSRVヒープの設定と全メッシュの描画を行う
		for (const auto &instance : instances) {
			instance.Draw(commandList, renderer, light);
		}

		if (!debugUI.Render(commandList)) {
			exitCode = -1;
			break;
		}

		if (!graphics.EndFrame()) {
			exitCode = -1;
			break;
		}
	}

	input.Finalize();

	// GPUの利用完了を待つ
	graphics.Finalize();

	// UIのGPUリソースとコンテキストを解放する
	debugUI.Finalize();

	for (auto &instance : instances) {
		instance.Finalize();
	}

	light.Finalize();
	renderer.Finalize();
	model.Finalize();
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

	// RunEngine内の全オブジェクトが破棄されるまでCOMを維持する
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
