#include "core/FrameTimer.h"
#include "core/Logger.h"
#include "graphics/Camera.h"
#include "graphics/D3D12Graphics.h"
#include "graphics/DirectionalLight.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/MeshInstance.h"
#include "graphics/MeshRenderer.h"
#include "graphics/ModelLoader.h"
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
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <format>
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

	Mesh modelMesh;
	MeshData meshData;

	if (!ModelLoader::Load(L"resources/models/box.glb", meshData) ||
	    !modelMesh.Initialize(graphics.GetDevice(), meshData.vertices,
	                          meshData.indices)) {
		graphics.Finalize();
		app.Finalize();
		return -1;
	}

	std::array<Texture, 2> textures;

	const wchar_t *texturePaths[] = {L"resources/textures/sample.png",
	                                 L"resources/textures/sample2.png"};

	for (std::size_t i = 0; i < textures.size(); ++i) {
		if (!textures[i].Load(graphics.GetDevice(), graphics.GetCommandQueue(),
		                      texturePaths[i])) {
			graphics.Finalize();
			app.Finalize();
			return -1;
		}
	}

	std::unique_ptr<DirectX::DescriptorHeap> srvHeap;

	try {
		// テクスチャの枚数分、SRVの格納場所を用意する
		srvHeap = std::make_unique<DirectX::DescriptorHeap>(
		    graphics.GetDevice(), textures.size());

		for (std::size_t i = 0; i < textures.size(); ++i) {
			DirectX::CreateShaderResourceView(graphics.GetDevice(),
			                                  textures[i].GetResource(),
			                                  srvHeap->GetCpuHandle(i));
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format("テクスチャ用SRVの作成に失敗しました: {}",
		                          error.what()));

		graphics.Finalize();
		app.Finalize();
		return -1;
	}

	Logger::Info("テクスチャ用SRVを作成しました。");

	// 全物体で共有する描画設定
	MeshRenderer renderer;

	if (!renderer.Initialize(graphics.GetDevice(), vertexShader.Get(),
	                         pixelShader.Get())) {
		graphics.Finalize();
		app.Finalize();
		return -1;
	}

	std::array<Material, 2> materials;
	std::array<MaterialData, 2> materialData{};

	materialData[0].uvScale = {2.0f, 2.0f};

	materialData[0].baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
	materialData[1].baseColor = {1.0f, 1.0f, 1.0f, 1.0f};

	for (std::size_t i = 0; i < materials.size(); ++i) {
		if (!materials[i].Initialize(graphics.GetDevice(),
		                             srvHeap->GetGpuHandle(i)) ||
		    !materials[i].Update(materialData[i])) {
			graphics.Finalize();

			for (auto &material : materials) {
				material.Finalize();
			}

			renderer.Finalize();
			app.Finalize();
			return -1;
		}
	}

	// 各物体がそれぞれ変形と定数バッファを持つ
	std::array<MeshInstance, 2> instances;

	for (std::size_t i = 0; i < instances.size(); ++i) {
		if (!instances[i].Initialize(graphics.GetDevice(), modelMesh,
		                             materials[i])) {
			graphics.Finalize();

			for (auto &instance : instances) {
				instance.Finalize();
			}

			for (auto &material : materials) {
				material.Finalize();
			}

			renderer.Finalize();
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

		for (auto &material : materials) {
			material.Finalize();
		}

		renderer.Finalize();
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

		for (auto &material : materials) {
			material.Finalize();
		}

		renderer.Finalize();
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

		// 物体ごとに別々の変換行列を更新する
		for (std::size_t i = 0; i < instances.size(); ++i) {
			Transform transform = instances[i].GetTransform();

			const float rotationSpeed = (i == 0) ? 1.0f : -0.7f;

			transform.rotation.y += rotationSpeed * deltaTime;

			instances[i].SetTransform(transform);
			instances[i].Update(camera);
		}

		// 右の物体では、テクスチャを横方向にスクロールさせる
		materialData[1].uvOffset.x =
		    std::fmod(materialData[1].uvOffset.x + 0.25f * deltaTime, 1.0f);

		bool materialUpdateSucceeded = true;

		for (std::size_t i = 0; i < materials.size(); ++i) {
			if (!materials[i].Update(materialData[i])) {
				materialUpdateSucceeded = false;
				break;
			}
		}

		if (!materialUpdateSucceeded) {
			exitCode = -1;
			break;
		}

		if (!graphics.BeginFrame(clearColor)) {
			exitCode = -1;
			break;
		}

		auto *commandList = graphics.GetCommandList();

		ID3D12DescriptorHeap *heaps[] = {srvHeap->Heap()};
		commandList->SetDescriptorHeaps(1, heaps);

		for (const auto &instance : instances) {
			renderer.Draw(commandList, instance, light);
		}

		if (!graphics.EndFrame()) {
			exitCode = -1;
			break;
		}
	}

	input.Finalize();

	// GPUの利用が終わってから各物体のリソースを解放する
	graphics.Finalize();

	for (auto &instance : instances) {
		instance.Finalize();
	}

	for (auto &material : materials) {
		material.Finalize();
	}

	light.Finalize();
	renderer.Finalize();
	modelMesh.Finalize();
	srvHeap.reset();
	for (auto &texture : textures) {
		texture.Finalize();
	}

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

	// RunEngine内の全オブジェクトが破棄されるまで維持する
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
