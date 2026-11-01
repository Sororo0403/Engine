#include "core/Engine.h"

#include "core/EngineContext.h"
#include "core/FrameTimer.h"
#include "core/Logger.h"
#include "graphics/model/ModelManager.h"
#include "graphics/render/D3D12Graphics.h"
#include "graphics/render/DebugLineRenderer.h"
#include "graphics/render/MeshRenderer.h"
#include "graphics/render/RenderSystem.h"
#include "graphics/render/ShaderCompiler.h"
#include "graphics/ui/ImGuiSystem.h"
#include "input/InputSystem.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/StringConversion.h"
#include "platform/windows/WinApp.h"
#include "platform/windows/WindowsInputBackend.h"
#include "scene/SceneManager.h"

#include <Windows.h>
#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <objbase.h>
#include <string>

namespace {

/// <summary>
/// COMの初期化と終了処理を対応させる
/// </summary>
class ComScope {
  public:
	ComScope() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}

	~ComScope() {
		if (SUCCEEDED(result_)) {
			CoUninitialize();
		}
	}

	ComScope(const ComScope &) = delete;
	ComScope &operator=(const ComScope &) = delete;

	HRESULT GetResult() const { return result_; }

  private:
	HRESULT result_;
};

} // namespace

/// <summary>
/// エンジン内部の共通機能と、その実行処理
/// </summary>
struct Engine::Impl {
	Impl()
	    : models_(graphics_),
	      render_(graphics_, meshRenderer_, debugLineRenderer_, app_) {}

	~Impl() {
		// シーンや共有リソースを解放する前にGPUの完了を待つ
		if (graphicsInitialized_) {
			if (!graphics_.WaitForGpu()) {
				Logger::Error("終了時のGPU完了待機に失敗しました。");
			}
		}

		// 参照している側から順に終了する
		sceneManager_.Finalize();
		models_.Finalize();

		ui_.Finalize();
		input_.Finalize();

		debugLineRenderer_.Finalize();
		meshRenderer_.Finalize();
		shaderCompiler_.Finalize();

		graphics_.Finalize();
		app_.Finalize();
	}

	Impl(const Impl &) = delete;
	Impl &operator=(const Impl &) = delete;

	/// <summary>
	/// 共通機能と最初のシーンを初期化する
	/// </summary>
	bool Initialize(HINSTANCE hInstance, const EngineConfig &config,
	                SceneFactory sceneFactory,
	                const std::filesystem::path &executableDirectory) {
		if (!shaderCompiler_.Initialize()) {
			return false;
		}

		const auto shaderDirectory = executableDirectory / L"resources" /
		                             L"engine" / L"shaders" / L"mesh";

		auto vertexShader = shaderCompiler_.Compile(
		    (shaderDirectory / L"Mesh.VS.hlsl").wstring(), L"VSMain",
		    L"vs_6_0");

		auto pixelShader = shaderCompiler_.Compile(
		    (shaderDirectory / L"Mesh.PS.hlsl").wstring(), L"PSMain",
		    L"ps_6_0");

		const auto debugShaderDirectory = executableDirectory / L"resources" /
		                                  L"engine" / L"shaders" / L"debug";

		auto debugVertexShader = shaderCompiler_.Compile(
		    (debugShaderDirectory / L"DebugLine.VS.hlsl").wstring(), L"VSMain",
		    L"vs_6_0");

		auto debugPixelShader = shaderCompiler_.Compile(
		    (debugShaderDirectory / L"DebugLine.PS.hlsl").wstring(), L"PSMain",
		    L"ps_6_0");

		shaderCompiler_.Finalize();

		if (!vertexShader || !pixelShader || !debugVertexShader ||
		    !debugPixelShader) {
			return false;
		}

		if (!app_.Initialize(hInstance, config.windowTitle, config.windowWidth,
		                     config.windowHeight)) {
			return false;
		}

		if (!graphics_.Initialize(app_.GetHwnd(), config.windowWidth,
		                          config.windowHeight)) {
			return false;
		}

		graphicsInitialized_ = true;

		if (!meshRenderer_.Initialize(graphics_.GetDevice(), vertexShader.Get(),
		                              pixelShader.Get(),
		                              graphics_.GetRenderTargetFormat(),
		                              graphics_.GetDepthStencilFormat())) {
			return false;
		}

		if (!debugLineRenderer_.Initialize(
		        graphics_.GetDevice(), debugVertexShader.Get(),
		        debugPixelShader.Get(), graphics_.GetRenderTargetFormat(),
		        graphics_.GetDepthStencilFormat())) {
			return false;
		}

		if (!input_.Initialize(
		        std::make_unique<WindowsInputBackend>(app_.GetHwnd()))) {
			return false;
		}

		if (!ui_.Initialize(app_, graphics_)) {
			return false;
		}

		// アプリへ公開する機能だけを渡す
		context_ = std::make_unique<EngineContext>(EngineContext{
		    models_,
		    render_,
		    input_,
		    ui_,
		});

		if (!sceneManager_.RequestChange(sceneFactory(*context_)) ||
		    !sceneManager_.ApplyPendingChange()) {
			return false;
		}

		return true;
	}

	/// <summary>
	/// ウィンドウが閉じられるまで更新・描画を実行する
	/// </summary>
	/// <returns>0:正常終了 -1:異常終了</returns>
	int MainLoop() {
		FrameTimer frameTimer;
		std::string lastWindowTitle;

		const float clearColor[4] = {
		    0.1f,
		    0.2f,
		    0.6f,
		    1.0f,
		};

		Logger::Info("メインループを開始しました。");

		while (app_.ProcessMessages()) {
			if (app_.ConsumeCloseRequest()) {
				if (sceneManager_.RequestClose()) {
					return 0;
				}

				// 最小化中でも保存確認を表示できるようにする
				if (IsIconic(app_.GetHwnd())) {
					ShowWindow(app_.GetHwnd(), SW_RESTORE);
				}
			}

			if (sceneManager_.IsCloseApproved()) {
				return 0;
			}

			if (!input_.Update()) {
				return -1;
			}

			const int width = app_.GetWidth();
			const int height = app_.GetHeight();

			if (width <= 0 || height <= 0) {
				WaitMessage();
				frameTimer.Reset();
				continue;
			}

			if (!graphics_.Resize(width, height)) {
				return -1;
			}

			const float deltaTime = frameTimer.Tick();

			if (!sceneManager_.ApplyPendingChange()) {
				return -1;
			}

			frameTimer.Reset();

			if (!ui_.BeginFrame()) {
				return -1;
			}

			// 終了確認中も更新と描画を継続する
			if (!sceneManager_.Update(deltaTime)) {
				return -1;
			}

			const std::string title = sceneManager_.GetWindowTitle();

			if (!title.empty() && title != lastWindowTitle) {
				const auto wideTitle = StringConversion::Utf8ToWide(title);

				if (wideTitle.empty() || !app_.SetTitle(wideTitle)) {
					Logger::Error("ウィンドウタイトルの変更に失敗しました。");
					return -1;
				}

				lastWindowTitle = title;
			}

			if (!graphics_.BeginFrame(clearColor)) {
				return -1;
			}

			sceneManager_.Draw();

			if (!ui_.Render()) {
				return -1;
			}

			if (!graphics_.EndFrame()) {
				return -1;
			}
		}

		return 0;
	}

  private:
	// 参照先の機能を、参照する機能より先に宣言する
	WinApp app_;
	D3D12Graphics graphics_;
	ShaderCompiler shaderCompiler_;
	MeshRenderer meshRenderer_;
	DebugLineRenderer debugLineRenderer_;

	InputSystem input_;
	ImGuiSystem ui_;

	// コンストラクターでは依存先の参照だけを保持する
	ModelManager models_;
	RenderSystem render_;

	// シーンがコンテキストを参照しても寿命を保証できるよう保持する
	std::unique_ptr<EngineContext> context_;
	SceneManager sceneManager_;

	bool graphicsInitialized_ = false;
};

int Engine::Run(const EngineConfig &config, SceneFactory sceneFactory) {
	if (!sceneFactory || config.windowWidth <= 0 || config.windowHeight <= 0) {
		return -1;
	}

	const auto executableDirectory = PathUtils::GetExecutableDirectory();

	if (executableDirectory.empty()) {
		return -1;
	}

	const auto logPath = executableDirectory / L"logs" / L"Engine.log";

	if (!Logger::Initialize(PathUtils::ToUtf8(logPath))) {
		return -1;
	}

	Logger::Info("Engineを起動しました。");

	int exitCode = -1;

	{
		ComScope com;

		if (FAILED(com.GetResult())) {
			Logger::Error(
			    std::format("COMの初期化に失敗しました。HRESULT=0x{:08X}",
			                static_cast<unsigned long>(com.GetResult())));
		} else {
			try {
				const HINSTANCE hInstance = GetModuleHandleW(nullptr);

				if (!hInstance) {
					Logger::Error("アプリケーションのインスタンスハンドルを"
					              "取得できませんでした。");
				} else {
					Impl engine;

					if (engine.Initialize(hInstance, config, sceneFactory,
					                      executableDirectory)) {
						exitCode = engine.MainLoop();
					}

					// このスコープを抜けると共通機能を終了する
				}
			} catch (const std::exception &error) {
				Logger::Error(std::format(
				    "Engineの実行中に例外が発生しました: {}", error.what()));

				exitCode = -1;
			} catch (...) {
				Logger::Error("Engineの実行中に不明な例外が発生しました。");

				exitCode = -1;
			}
		}

		// 共通機能の終了後にCOMを終了する
	}

	if (exitCode == 0) {
		Logger::Info("Engineを正常終了しました。");
	} else {
		Logger::Error(
		    std::format("Engineを異常終了しました。終了コード={}", exitCode));
	}

	Logger::Finalize();
	return exitCode;
}
