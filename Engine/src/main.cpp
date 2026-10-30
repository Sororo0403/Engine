#include "core/FrameTimer.h"
#include "core/Logger.h"
#include "graphics/Camera.h"
#include "graphics/D3D12Graphics.h"
#include "graphics/DirectionalLight.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/MeshInstance.h"
#include "graphics/MeshRenderer.h"
#include "graphics/ModelData.h"
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
#include <vector>

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

	ModelData modelData;

	if (!ModelLoader::Load(L"resources/models/box.glb", modelData)) {
		graphics.Finalize();
		app.Finalize();
		return -1;
	}

	// 各メッシュに対応するGPUバッファを作る
	std::vector<Mesh> meshes(modelData.meshes.size());

	for (std::size_t i = 0; i < meshes.size(); ++i) {
		const MeshData &meshData = modelData.meshes[i].mesh;

		if (!meshes[i].Initialize(graphics.GetDevice(), meshData.vertices,
		                          meshData.indices)) {
			graphics.Finalize();
			app.Finalize();
			return -1;
		}
	}

	const std::size_t materialCount = modelData.materials.size();

	// 0番は画像なし用の白、それ以降は各材質の画像
	std::vector<Texture> textures(materialCount + 1);

	const std::array<std::uint8_t, 4> white{255, 255, 255, 255};

	if (!textures[0].CreateFromRGBA(graphics.GetDevice(),
	                                graphics.GetCommandQueue(), 1, 1, white)) {
		graphics.Finalize();
		app.Finalize();
		return -1;
	}

	for (std::size_t i = 0; i < materialCount; ++i) {
		const ModelTextureData &image = modelData.materials[i].baseColorTexture;

		Texture &texture = textures[i + 1];
		bool loaded = false;

		switch (image.source) {
		case ModelTextureSource::NONE:
			continue;

		case ModelTextureSource::FILE:
			loaded = texture.Load(graphics.GetDevice(),
			                      graphics.GetCommandQueue(), image.filePath);
			break;

		case ModelTextureSource::ENCODED:
			loaded = texture.LoadFromMemory(
			    graphics.GetDevice(), graphics.GetCommandQueue(), image.bytes);
			break;

		case ModelTextureSource::RGBA8:
			loaded = texture.CreateFromRGBA(
			    graphics.GetDevice(), graphics.GetCommandQueue(), image.width,
			    image.height, image.bytes);
			break;
		}

		if (!loaded) {
			Logger::Error(
			    std::format("材質{}のテクスチャを読み込めませんでした。", i));

			graphics.Finalize();
			app.Finalize();
			return -1;
		}
	}

	std::unique_ptr<DirectX::DescriptorHeap> srvHeap;

	try {
		// 材質ごとにSRVを1つ用意する
		srvHeap = std::make_unique<DirectX::DescriptorHeap>(
		    graphics.GetDevice(), materialCount);

		for (std::size_t i = 0; i < materialCount; ++i) {
			ID3D12Resource *resource = textures[i + 1].GetResource();

			if (!resource) {
				resource = textures[0].GetResource();
			}

			DirectX::CreateShaderResourceView(graphics.GetDevice(), resource,
			                                  srvHeap->GetCpuHandle(i));
		}
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("モデル用SRVの作成に失敗しました: {}", error.what()));

		graphics.Finalize();
		app.Finalize();
		return -1;
	}

	Logger::Info("モデル用のテクスチャとSRVを作成しました。");

	// 全物体で共有する描画設定
	MeshRenderer renderer;

	if (!renderer.Initialize(graphics.GetDevice(), vertexShader.Get(),
	                         pixelShader.Get())) {
		graphics.Finalize();
		app.Finalize();
		return -1;
	}

	constexpr std::size_t objectCount = 2;

	std::vector<Material> materials(objectCount * materialCount);
	std::vector<MaterialData> materialData(materials.size());

	for (std::size_t i = 0; i < materials.size(); ++i) {
		const std::size_t modelMaterialIndex = i % materialCount;

		materialData[i].baseColor =
		    modelData.materials[modelMaterialIndex].baseColor;

		if (!materials[i].Initialize(
		        graphics.GetDevice(),
		        srvHeap->GetGpuHandle(modelMaterialIndex)) ||
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

	std::vector<MeshInstance> instances(meshes.size() * objectCount);

	for (std::size_t i = 0; i < instances.size(); ++i) {
		const std::size_t objectIndex = i / meshes.size();
		const std::size_t meshIndex = i % meshes.size();

		const std::size_t modelMaterialIndex =
		    modelData.meshes[meshIndex].materialIndex;

		const std::size_t materialIndex =
		    objectIndex * materialCount + modelMaterialIndex;

		if (!instances[i].Initialize(graphics.GetDevice(), meshes[meshIndex],
		                             materials[materialIndex])) {
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
		transform.translation.x = (objectIndex == 0) ? -0.6f : 0.6f;

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
		// カメラを移動する
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

		// カメラの更新後に、各メッシュの変換行列を更新する
		for (std::size_t i = 0; i < instances.size(); ++i) {
			const std::size_t objectIndex = i / meshes.size();

			Transform transform = instances[i].GetTransform();

			const float rotationSpeed = (objectIndex == 0) ? 1.0f : -0.7f;

			transform.rotation.y += rotationSpeed * deltaTime;

			instances[i].SetTransform(transform);
			instances[i].Update(camera);
		}

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
	for (auto &mesh : meshes) {
		mesh.Finalize();
	}
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
