#include "graphics/debug/DebugUI.h"

#if defined(_DEBUG)

#include "core/Logger.h"

#include "graphics/render/D3D12Graphics.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/StringConversion.h"
#include "platform/windows/WinApp.h"

#include <directxtk12/DescriptorHeap.h>

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include <Windows.h>
#include <array>
#include <cstddef>
#include <d3d12.h>
#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <wrl/client.h>

// この関数はバックエンドのヘッダー内で宣言が無効化されているため、
// 呼び出す側で宣言する
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

/// <summary>
/// UIのコンテキストと描画用リソースを保持する
/// </summary>
struct DebugUI::Impl {
	static constexpr UINT kDescriptorCount = 128;

	// 現在の構成では、同時に使用するUIコンテキストは1つ
	inline static Impl *active_ = nullptr;

	/// <summary>
	/// UIフレームの状態を確認し、このコンテキストを選択する
	/// </summary>
	void UseFrame() const {
		if (!context_ || !frameStarted_) {
			throw std::logic_error(
			    "DebugUI: ウィジェットの操作にはBeginFrameが必要です。");
		}

		ImGui::SetCurrentContext(context_);
	}

	/// <summary>
	/// ImGui用のSRVを割り当てる
	/// </summary>
	/// <param name="info">初期化情報。UserDataから内部実装を取得する</param>
	/// <param name="cpuHandle">割り当てたCPUハンドル</param>
	/// <param name="gpuHandle">割り当てたGPUハンドル</param>
	static void AllocateSrv(ImGui_ImplDX12_InitInfo *info,
	                        D3D12_CPU_DESCRIPTOR_HANDLE *cpuHandle,
	                        D3D12_GPU_DESCRIPTOR_HANDLE *gpuHandle) {
		auto *ui = static_cast<Impl *>(info->UserData);

		for (UINT i = 0; i < kDescriptorCount; ++i) {
			if (!ui->descriptorUsed_[i]) {
				ui->descriptorUsed_[i] = true;

				*cpuHandle = ui->srvHeap_->GetCpuHandle(i);
				*gpuHandle = ui->srvHeap_->GetGpuHandle(i);
				return;
			}
		}

		throw std::runtime_error("DebugUI: SRV descriptor heap is full.");
	}

	/// <summary>
	/// ImGuiが使用しなくなったSRVを解放する
	/// </summary>
	/// <param name="info">初期化情報</param>
	/// <param name="cpuHandle">解放するCPUハンドル</param>
	/// <param name="gpuHandle">解放するGPUハンドル</param>
	static void FreeSrv(ImGui_ImplDX12_InitInfo *info,
	                    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
	                    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) {
		auto *ui = static_cast<Impl *>(info->UserData);

		const SIZE_T first = ui->srvHeap_->GetFirstCpuHandle().ptr;

		const UINT increment = ui->srvHeap_->Increment();

		if (increment == 0 || cpuHandle.ptr < first ||
		    (cpuHandle.ptr - first) % increment != 0) {
			Logger::Error("DebugUI::FreeSrv: ハンドルが無効です。");
			return;
		}

		const std::size_t index = (cpuHandle.ptr - first) / increment;

		if (index >= kDescriptorCount || !ui->descriptorUsed_[index] ||
		    gpuHandle.ptr != ui->srvHeap_->GetGpuHandle(index).ptr) {
			Logger::Error("DebugUI::FreeSrv: SRVの解放情報が無効です。");
			return;
		}

		ui->descriptorUsed_[index] = false;
	}

	ImGuiContext *context_ = nullptr;
	HWND hwnd_ = nullptr;

	// 参照先はDebugUIより長く保持する
	D3D12Graphics *graphics_ = nullptr;

	// バックエンドが参照している間、デバイスとキューを保持する
	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;

	std::unique_ptr<DirectX::DescriptorHeap> srvHeap_;
	std::array<bool, kDescriptorCount> descriptorUsed_{};

	bool win32Initialized_ = false;
	bool rendererInitialized_ = false;
	bool frameStarted_ = false;

	// ImGuiが参照する保存先の文字列を、コンテキストの破棄まで保持する
	std::string iniFilePath_;
};

DebugUI::DebugUI() : impl_(std::make_unique<Impl>()) {}

DebugUI::~DebugUI() {
	// 呼び出し側でGPUの利用完了を保証してから破棄する
	Finalize();
}

bool DebugUI::Initialize(const WinApp &app, D3D12Graphics &graphics,
                         int framesInFlight) {
	const HWND hwnd = app.GetHwnd();
	auto *device = graphics.GetDevice();
	auto *commandQueue = graphics.GetCommandQueue();

	if (!hwnd || !IsWindow(hwnd) || !device || !commandQueue ||
	    framesInFlight <= 0) {
		Logger::Error("DebugUI::Initialize: 引数が無効です。");
		return false;
	}

	if (impl_->context_ || impl_->srvHeap_ || Impl::active_ ||
	    ImGui::GetCurrentContext()) {
		Logger::Error("DebugUI::Initialize: ImGuiはすでに初期化されています。");
		return false;
	}

	if (commandQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
		Logger::Error(
		    "DebugUI::Initialize: DIRECT型のコマンドキューが必要です。");
		return false;
	}

	try {
		const auto executableDirectory = PathUtils::GetExecutableDirectory();

		if (executableDirectory.empty()) {
			Logger::Error("DebugUI::Initialize: 実行ファイルのフォルダを"
			              "取得できませんでした。");
			return false;
		}

		const auto configDirectory = executableDirectory / L"config";

		// 保存先フォルダがなければ作成する
		std::filesystem::create_directories(configDirectory);

		const auto iniPath = configDirectory / L"imgui.ini";

		impl_->iniFilePath_ = StringConversion::WideToUtf8(iniPath.wstring());

		if (impl_->iniFilePath_.empty()) {
			Logger::Error("DebugUI::Initialize: 設定ファイルのパスを"
			              "UTF-8に変換できませんでした。");
			return false;
		}

		impl_->graphics_ = &graphics;
		impl_->hwnd_ = hwnd;

		impl_->device_ = device;
		impl_->commandQueue_ = commandQueue;

		impl_->srvHeap_ = std::make_unique<DirectX::DescriptorHeap>(
		    device, Impl::kDescriptorCount);

		impl_->descriptorUsed_.fill(false);

		IMGUI_CHECKVERSION();
		impl_->context_ = ImGui::CreateContext();

		if (!impl_->context_) {
			Logger::Error("ImGuiコンテキストの作成に失敗しました。");
			Finalize();
			return false;
		}

		Impl::active_ = impl_.get();

		ImGui::SetCurrentContext(impl_->context_);

		ImGuiIO &io = ImGui::GetIO();

		// UI配置設定を実行ファイルの隣のconfigフォルダへ保存する
		io.IniFilename = impl_->iniFilePath_.c_str();

		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		ImGui::StyleColorsDark();

		if (!ImGui_ImplWin32_Init(hwnd)) {
			Logger::Error("ImGuiのWin32初期化に失敗しました。");
			Finalize();
			return false;
		}

		impl_->win32Initialized_ = true;

		ImGui_ImplDX12_InitInfo info{};
		info.Device = impl_->device_.Get();
		info.CommandQueue = impl_->commandQueue_.Get();
		info.NumFramesInFlight = framesInFlight;

		// 現在のD3D12Graphicsの描画形式に合わせる
		info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		info.DSVFormat = DXGI_FORMAT_D32_FLOAT;

		info.UserData = impl_.get();
		info.SrvDescriptorHeap = impl_->srvHeap_->Heap();
		info.SrvDescriptorAllocFn = Impl::AllocateSrv;
		info.SrvDescriptorFreeFn = Impl::FreeSrv;

		if (!ImGui_ImplDX12_Init(&info)) {
			Logger::Error("ImGuiのDX12初期化に失敗しました。");
			Finalize();
			return false;
		}

		impl_->rendererInitialized_ = true;

		if (!ImGui_ImplDX12_CreateDeviceObjects()) {
			Logger::Error("ImGuiの描画リソース作成に失敗しました。");
			Finalize();
			return false;
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format("DebugUIの初期化中に例外が発生しました: {}",
		                          error.what()));

		Finalize();
		return false;
	}

	Logger::Info("DebugUIを初期化しました。");
	return true;
}

bool DebugUI::BeginFrame() {
	if (!impl_->context_ || !impl_->win32Initialized_ ||
	    !impl_->rendererInitialized_ || impl_->frameStarted_) {
		Logger::Error(
		    "DebugUI::BeginFrame: 未初期化、またはフレーム開始済みです。");
		return false;
	}

	ImGui::SetCurrentContext(impl_->context_);

	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	impl_->frameStarted_ = true;
	return true;
}

bool DebugUI::Render() {
	if (!impl_->graphics_ || !impl_->context_ || !impl_->rendererInitialized_ ||
	    !impl_->frameStarted_) {
		Logger::Error(
		    "DebugUI::Render: 未初期化、またはフレームの状態が無効です。");
		return false;
	}

	auto *commandList = impl_->graphics_->GetCommandList();

	if (!commandList) {
		Logger::Error("DebugUI::Render: コマンドリストがありません。");
		return false;
	}

	ImGui::SetCurrentContext(impl_->context_);

	try {
		// UIの構築を終了して、描画データを確定する
		ImGui::Render();
		impl_->frameStarted_ = false;

		ID3D12DescriptorHeap *heaps[] = {
		    impl_->srvHeap_->Heap(),
		};

		commandList->SetDescriptorHeaps(1, heaps);

		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("DebugUIの描画処理に失敗しました: {}", error.what()));

		return false;
	}

	return true;
}

bool DebugUI::BeginWindow(const char *title) const {
	impl_->UseFrame();
	return ImGui::Begin(title);
}

void DebugUI::EndWindow() const {
	impl_->UseFrame();
	ImGui::End();
}

bool DebugUI::Checkbox(const char *label, bool &value) const {
	impl_->UseFrame();
	return ImGui::Checkbox(label, &value);
}

bool DebugUI::CollapsingHeader(const char *label, bool defaultOpen) const {
	impl_->UseFrame();

	const ImGuiTreeNodeFlags flags =
	    defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;

	return ImGui::CollapsingHeader(label, flags);
}

bool DebugUI::SliderFloat(const char *label, float &value, float minimum,
                          float maximum, const char *format) const {
	impl_->UseFrame();

	return ImGui::SliderFloat(label, &value, minimum, maximum, format,
	                          ImGuiSliderFlags_AlwaysClamp);
}

bool DebugUI::DragFloat3(const char *label, DirectX::XMFLOAT3 &value,
                         float speed, float minimum, float maximum,
                         const char *format) const {
	impl_->UseFrame();

	float values[] = {
	    value.x,
	    value.y,
	    value.z,
	};

	if (!ImGui::DragFloat3(label, values, speed, minimum, maximum, format,
	                       ImGuiSliderFlags_AlwaysClamp)) {
		return false;
	}

	value = {values[0], values[1], values[2]};
	return true;
}

bool DebugUI::ColorEdit3(const char *label, DirectX::XMFLOAT3 &color) const {
	impl_->UseFrame();

	float values[] = {
	    color.x,
	    color.y,
	    color.z,
	};

	if (!ImGui::ColorEdit3(label, values)) {
		return false;
	}

	color = {values[0], values[1], values[2]};
	return true;
}

bool DebugUI::ColorEdit4(const char *label, DirectX::XMFLOAT4 &color) const {
	impl_->UseFrame();

	float values[] = {
	    color.x,
	    color.y,
	    color.z,
	    color.w,
	};

	if (!ImGui::ColorEdit4(label, values)) {
		return false;
	}

	color = {
	    values[0],
	    values[1],
	    values[2],
	    values[3],
	};

	return true;
}

void DebugUI::PushID(int id) const {
	impl_->UseFrame();
	ImGui::PushID(id);
}

void DebugUI::PopID() const {
	impl_->UseFrame();
	ImGui::PopID();
}

void DebugUI::Separator() const {
	impl_->UseFrame();
	ImGui::Separator();
}

bool DebugUI::Button(const char *label) const {
	impl_->UseFrame();
	return ImGui::Button(label);
}

bool DebugUI::Selectable(const char *label, bool selected) const {
	impl_->UseFrame();
	return ImGui::Selectable(label, selected);
}

void DebugUI::Text(std::string_view text) const {
	impl_->UseFrame();

	if (text.empty()) {
		ImGui::TextUnformatted("");
		return;
	}

	ImGui::TextUnformatted(text.data(), text.data() + text.size());
}

bool DebugUI::WantsKeyboard() const {
	if (!impl_->context_) {
		return false;
	}

	ImGui::SetCurrentContext(impl_->context_);
	return ImGui::GetIO().WantCaptureKeyboard;
}

bool DebugUI::WantsMouse() const {
	if (!impl_->context_) {
		return false;
	}

	ImGui::SetCurrentContext(impl_->context_);
	return ImGui::GetIO().WantCaptureMouse;
}

bool DebugUI::HandleMessage(void *nativeWindow, std::uint32_t message,
                            std::uintptr_t wParam, std::intptr_t lParam) {
	auto *ui = Impl::active_;

	if (!ui || !ui->context_ || !ui->win32Initialized_) {
		return false;
	}

	const HWND hwnd = static_cast<HWND>(nativeWindow);

	if (hwnd != ui->hwnd_) {
		return false;
	}

	ImGui::SetCurrentContext(ui->context_);

	return ImGui_ImplWin32_WndProcHandler(hwnd, static_cast<UINT>(message),
	                                      static_cast<WPARAM>(wParam),
	                                      static_cast<LPARAM>(lParam)) != 0;
}

void DebugUI::Finalize() {
	// 終了処理中は、ウィンドウからのメッセージを転送しない
	if (Impl::active_ == impl_.get()) {
		Impl::active_ = nullptr;
	}

	if (impl_->context_) {
		ImGui::SetCurrentContext(impl_->context_);

		if (impl_->frameStarted_) {
			ImGui::EndFrame();
			impl_->frameStarted_ = false;
		}

		// SRVヒープは、バックエンドの終了処理まで保持する
		if (impl_->rendererInitialized_) {
			ImGui_ImplDX12_Shutdown();
			impl_->rendererInitialized_ = false;
		}

		if (impl_->win32Initialized_) {
			ImGui_ImplWin32_Shutdown();
			impl_->win32Initialized_ = false;
		}

		ImGui::DestroyContext(impl_->context_);
		impl_->context_ = nullptr;
	}

	impl_->iniFilePath_.clear();

	impl_->srvHeap_.reset();

	impl_->descriptorUsed_.fill(false);

	impl_->commandQueue_.Reset();
	impl_->device_.Reset();

	impl_->graphics_ = nullptr;
	impl_->hwnd_ = nullptr;
}

#else

/// <summary>
/// Release構成ではUIリソースを保持しない
/// </summary>
struct DebugUI::Impl {};

DebugUI::DebugUI() = default;

DebugUI::~DebugUI() = default;

bool DebugUI::Initialize(const WinApp &, D3D12Graphics &, int) { return true; }

bool DebugUI::BeginFrame() { return true; }

bool DebugUI::Render() { return true; }

bool DebugUI::BeginWindow(const char *) const { return false; }

void DebugUI::EndWindow() const {}

bool DebugUI::Checkbox(const char *, bool &) const { return false; }

bool DebugUI::CollapsingHeader(const char *, bool) const { return false; }

bool DebugUI::SliderFloat(const char *, float &, float, float,
                          const char *) const {
	return false;
}

bool DebugUI::DragFloat3(const char *, DirectX::XMFLOAT3 &, float, float, float,
                         const char *) const {
	return false;
}

bool DebugUI::ColorEdit3(const char *, DirectX::XMFLOAT3 &) const {
	return false;
}

bool DebugUI::ColorEdit4(const char *, DirectX::XMFLOAT4 &) const {
	return false;
}

void DebugUI::PushID(int) const {}

void DebugUI::PopID() const {}

void DebugUI::Separator() const {}

void DebugUI::Text(std::string_view) const {}

bool DebugUI::WantsKeyboard() const { return false; }

bool DebugUI::WantsMouse() const { return false; }

bool DebugUI::HandleMessage(void *, std::uint32_t, std::uintptr_t,
                            std::intptr_t) {
	return false;
}

void DebugUI::Finalize() {}

bool DebugUI::Button(const char *) const { return false; }

bool DebugUI::Selectable(const char *, bool) const { return false; }

#endif
