#include <Windows.h>

#include "platform/windows/WinApp.h"
#include "graphics/D3D12Graphics.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/TriangleRenderer.h"
#include "core/Logger.h"
#include "platform/windows/StringConversion.h"
#include "platform/windows/PathUtils.h"

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
			graphics.Finalize();
			app.Finalize();
			return -1;
		}

		const float clearColor[4] = {0.1f, 0.2f, 0.6f, 1.0f};
		int exitCode = 0;
		Logger::Info("メインループを開始しました。");

		while (app.ProcessMessages()) {
			const int width = app.GetWidth();
			const int height = app.GetHeight();

			// 最小化中は描画せず、次のメッセージを待つ
			if (width <= 0 || height <= 0) {
				WaitMessage();
				continue;
			}

			// サイズが変わっていたら描画領域を作り直す
			if (!graphics.Resize(width, height)) {
				exitCode = -1;
				break;
			}

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
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
	const auto executableDirectory =
		PathUtils::GetExecutableDirectory();

	if (executableDirectory.empty()) {
		Logger::Error("実行ファイルのフォルダを取得できませんでした。");
		return -1;
	}

	const auto logPath =
		executableDirectory / L"logs" / L"Engine.log";

	if (!Logger::Initialize(
		StringConversion::WideToUtf8(logPath.wstring())
	)) {
		return -1;
	}

	Logger::Info("Engineを起動しました。");

	// RunEngine内のオブジェクトが破棄されるまでログを有効にする。
	const int exitCode = RunEngine(hInstance);

	if (exitCode == 0) {
		Logger::Info("Engineを正常終了しました。");
	} else {
		Logger::Error(std::format("Engineを異常終了しました。終了コード={}", exitCode));
	}

	Logger::Finalize();
	return exitCode;
}
