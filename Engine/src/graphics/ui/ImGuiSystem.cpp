#include "graphics/ui/ImGuiSystem.h"

#include "core/Logger.h"
#include "graphics/render/D3D12Graphics.h"
#include "graphics/render/RenderTexture.h"
#include "graphics/ui/DockLayout.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/StringConversion.h"
#include "platform/windows/WinApp.h"

#include <directxtk12/DescriptorHeap.h>

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>

#include <Windows.h>
#include <array>
#include <cmath>
#include <cstddef>
#include <d3d12.h>
#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
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
struct ImGuiSystem::Impl {
	static constexpr UINT kDescriptorCount = 128;

	// 現在の構成では、同時に使用するUIコンテキストは1つ
	inline static Impl *active_ = nullptr;

	/// <summary>
	/// UIフレームの状態を確認し、このコンテキストを選択する
	/// </summary>
	void UseFrame() const {
		if (!context_ || !frameStarted_) {
			throw std::logic_error(
			    "ImGuiSystem: ウィジェットの操作にはBeginFrameが必要です。");
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

		throw std::runtime_error("ImGuiSystem: SRV descriptor heap is full.");
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
			Logger::Error("ImGuiSystem::FreeSrv: ハンドルが無効です。");
			return;
		}

		const std::size_t index = (cpuHandle.ptr - first) / increment;

		if (index >= kDescriptorCount || !ui->descriptorUsed_[index] ||
		    gpuHandle.ptr != ui->srvHeap_->GetGpuHandle(index).ptr) {
			Logger::Error("ImGuiSystem::FreeSrv: SRVの解放情報が無効です。");
			return;
		}

		ui->descriptorUsed_[index] = false;
	}

	ImGuiContext *context_ = nullptr;
	HWND hwnd_ = nullptr;

	// 参照先はImGuiSystemより長く保持する
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

	// このフレームの画像表示に使用しているSRVの格納位置
	std::vector<UINT> imageDescriptors_;
};

ImGuiSystem::ImGuiSystem() : impl_(std::make_unique<Impl>()) {}

ImGuiSystem::~ImGuiSystem() {
	// 呼び出し側でGPUの利用完了を保証してから破棄する
	Finalize();
}

bool ImGuiSystem::Initialize(const WinApp &app, D3D12Graphics &graphics,
                             int framesInFlight) {
	const HWND hwnd = app.GetHwnd();
	auto *device = graphics.GetDevice();
	auto *commandQueue = graphics.GetCommandQueue();

	if (!hwnd || !IsWindow(hwnd) || !device || !commandQueue ||
	    framesInFlight <= 0) {
		Logger::Error("ImGuiSystem::Initialize: 引数が無効です。");
		return false;
	}

	if (impl_->context_ || impl_->srvHeap_ || Impl::active_ ||
	    ImGui::GetCurrentContext()) {
		Logger::Error(
		    "ImGuiSystem::Initialize: ImGuiはすでに初期化されています。");
		return false;
	}

	if (commandQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
		Logger::Error(
		    "ImGuiSystem::Initialize: DIRECT型のコマンドキューが必要です。");
		return false;
	}

	try {
		const auto executableDirectory = PathUtils::GetExecutableDirectory();

		if (executableDirectory.empty()) {
			Logger::Error("ImGuiSystem::Initialize: 実行ファイルのフォルダを"
			              "取得できませんでした。");
			return false;
		}

		const auto configDirectory = executableDirectory / L"config";

		// 保存先フォルダがなければ作成する
		std::filesystem::create_directories(configDirectory);

		const auto iniPath = configDirectory / L"imgui.ini";

		impl_->iniFilePath_ = StringConversion::WideToUtf8(iniPath.wstring());

		if (impl_->iniFilePath_.empty()) {
			Logger::Error("ImGuiSystem::Initialize: 設定ファイルのパスを"
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
		impl_->imageDescriptors_.clear();

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
		Logger::Error(std::format(
		    "ImGuiSystemの初期化中に例外が発生しました: {}", error.what()));

		Finalize();
		return false;
	}

	Logger::Info("ImGuiSystemを初期化しました。");
	return true;
}

bool ImGuiSystem::BeginFrame() {
	if (!impl_->context_ || !impl_->win32Initialized_ ||
	    !impl_->rendererInitialized_ || impl_->frameStarted_) {
		Logger::Error(
		    "ImGuiSystem::BeginFrame: 未初期化、またはフレーム開始済みです。");
		return false;
	}

	// 前フレームのGPU処理完了後なので、
	// 画像表示に使用したSRVを再利用できる
	for (const UINT index : impl_->imageDescriptors_) {
		impl_->descriptorUsed_[index] = false;
	}

	impl_->imageDescriptors_.clear();

	ImGui::SetCurrentContext(impl_->context_);

	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	impl_->frameStarted_ = true;
	return true;
}

bool ImGuiSystem::Render() {
	if (!impl_->graphics_ || !impl_->context_ || !impl_->rendererInitialized_ ||
	    !impl_->frameStarted_) {
		Logger::Error(
		    "ImGuiSystem::Render: 未初期化、またはフレームの状態が無効です。");
		return false;
	}

	auto *commandList = impl_->graphics_->GetCommandList();

	if (!commandList) {
		Logger::Error("ImGuiSystem::Render: コマンドリストがありません。");
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
		Logger::Error(std::format("ImGuiSystemの描画処理に失敗しました: {}",
		                          error.what()));
		return false;
	}

	return true;
}

bool ImGuiSystem::EnableDocking() const {
	if (!impl_->context_ || impl_->frameStarted_) {
		Logger::Error("ImGuiSystem::EnableDocking: "
		              "未初期化、またはフレーム開始済みです。");
		return false;
	}

	ImGui::SetCurrentContext(impl_->context_);

	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	return true;
}

void ImGuiSystem::DrawDockSpace(const DockLayout &layout,
                                bool resetLayout) const {
	impl_->UseFrame();

	const ImGuiIO &io = ImGui::GetIO();

	if ((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) == 0) {
		throw std::logic_error(
		    "ImGuiSystem::DrawDockSpace: ドッキングが有効ではありません。");
	}

	if (!layout.name || !*layout.name || !layout.leftWindow ||
	    !*layout.leftWindow || !layout.centerWindow || !*layout.centerWindow ||
	    !layout.rightWindow || !*layout.rightWindow ||
	    !std::isfinite(layout.leftRatio) || !std::isfinite(layout.rightRatio) ||
	    layout.leftRatio <= 0.0f || layout.rightRatio <= 0.0f ||
	    layout.leftRatio + layout.rightRatio >= 1.0f) {
		throw std::invalid_argument(
		    "ImGuiSystem::DrawDockSpace: 配置情報が無効です。");
	}

	const ImGuiViewport *viewport = ImGui::GetMainViewport();

	if (viewport->WorkSize.x <= 0.0f || viewport->WorkSize.y <= 0.0f) {
		return;
	}

	const ImGuiID dockspaceId = ImHashStr(layout.name);

	// 保存済みの領域があれば、ユーザーが変更した配置を維持する
	if (resetLayout || ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
		ImGui::DockBuilderRemoveNode(dockspaceId);

		ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);

		ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

		ImGuiID centerId = dockspaceId;
		ImGuiID leftId = 0;
		ImGuiID rightId = 0;

		ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Left, layout.leftRatio,
		                            &leftId, &centerId);

		// 左領域を除いた残りの幅に対する割合へ変換する
		const float rightRatio = layout.rightRatio / (1.0f - layout.leftRatio);

		ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Right, rightRatio,
		                            &rightId, &centerId);

		ImGui::DockBuilderDockWindow(layout.leftWindow, leftId);
		ImGui::DockBuilderDockWindow(layout.centerWindow, centerId);
		ImGui::DockBuilderDockWindow(layout.rightWindow, rightId);

		ImGui::DockBuilderFinish(dockspaceId);
	}

	ImGui::DockSpaceOverViewport(dockspaceId, viewport,
	                             ImGuiDockNodeFlags_None);
}

bool ImGuiSystem::BeginWindow(const char *title) const {
	impl_->UseFrame();
	return ImGui::Begin(title);
}

void ImGuiSystem::EndWindow() const {
	impl_->UseFrame();
	ImGui::End();
}

bool ImGuiSystem::Checkbox(const char *label, bool &value) const {
	impl_->UseFrame();
	return ImGui::Checkbox(label, &value);
}

bool ImGuiSystem::CollapsingHeader(const char *label, bool defaultOpen) const {
	impl_->UseFrame();

	const ImGuiTreeNodeFlags flags =
	    defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;

	return ImGui::CollapsingHeader(label, flags);
}

bool ImGuiSystem::SliderFloat(const char *label, float &value, float minimum,
                              float maximum, const char *format) const {
	impl_->UseFrame();

	return ImGui::SliderFloat(label, &value, minimum, maximum, format,
	                          ImGuiSliderFlags_AlwaysClamp);
}

bool ImGuiSystem::DragFloat3(const char *label, DirectX::XMFLOAT3 &value,
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

bool ImGuiSystem::ColorEdit3(const char *label,
                             DirectX::XMFLOAT3 &color) const {
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

bool ImGuiSystem::ColorEdit4(const char *label,
                             DirectX::XMFLOAT4 &color) const {
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

void ImGuiSystem::PushID(int id) const {
	impl_->UseFrame();
	ImGui::PushID(id);
}

void ImGuiSystem::PopID() const {
	impl_->UseFrame();
	ImGui::PopID();
}

void ImGuiSystem::Separator() const {
	impl_->UseFrame();
	ImGui::Separator();
}

bool ImGuiSystem::Button(const char *label) const {
	impl_->UseFrame();
	return ImGui::Button(label);
}

bool ImGuiSystem::Selectable(const char *label, bool selected) const {
	impl_->UseFrame();
	return ImGui::Selectable(label, selected);
}

void ImGuiSystem::Text(std::string_view text) const {
	impl_->UseFrame();

	if (text.empty()) {
		ImGui::TextUnformatted("");
		return;
	}

	ImGui::TextUnformatted(text.data(), text.data() + text.size());
}

bool ImGuiSystem::WantsKeyboard() const {
	if (!impl_->context_) {
		return false;
	}

	ImGui::SetCurrentContext(impl_->context_);
	return ImGui::GetIO().WantCaptureKeyboard;
}

bool ImGuiSystem::WantsMouse() const {
	if (!impl_->context_) {
		return false;
	}

	ImGui::SetCurrentContext(impl_->context_);
	return ImGui::GetIO().WantCaptureMouse;
}

void ImGuiSystem::SetNextWindowSize(float width, float height) const {
	impl_->UseFrame();

	ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_FirstUseEver);
}

DirectX::XMFLOAT2 ImGuiSystem::GetContentSize() const {
	impl_->UseFrame();

	const ImVec2 size = ImGui::GetContentRegionAvail();
	return {size.x, size.y};
}

ViewportInput ImGuiSystem::GetViewportInput() const {
	impl_->UseFrame();

	const ImGuiIO &io = ImGui::GetIO();

	ViewportInput input;
	input.imageHovered = ImGui::IsItemHovered();
	input.windowFocused = ImGui::IsWindowFocused();
	input.applicationFocused = !io.AppFocusLost;

	input.widgetActive = io.WantTextInput || ImGui::IsAnyItemActive();

	return input;
}

void ImGuiSystem::Image(const RenderTexture &texture, float width,
                        float height) const {
	impl_->UseFrame();

	if (!std::isfinite(width) || !std::isfinite(height)) {
		throw std::invalid_argument(
		    "ImGuiSystem::Image: 表示サイズが無効です。");
	}

	if (width <= 0.0f || height <= 0.0f) {
		return;
	}

	if (!texture.Uses(*impl_->graphics_) || !texture.GetColorResource()) {
		throw std::logic_error(
		    "ImGuiSystem::Image: 描画基盤が異なる、または画像が未作成です。");
	}

	for (UINT i = 0; i < Impl::kDescriptorCount; ++i) {
		if (impl_->descriptorUsed_[i]) {
			continue;
		}

		// 格納先の確保に成功してから、使用中として扱う
		impl_->imageDescriptors_.push_back(i);
		impl_->descriptorUsed_[i] = true;

		D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		desc.Texture2D.MipLevels = 1;

		// ImGuiが使用するヒープ内に画像のSRVを作成する
		impl_->device_->CreateShaderResourceView(
		    texture.GetColorResource(), &desc,
		    impl_->srvHeap_->GetCpuHandle(i));

		const auto gpuHandle = impl_->srvHeap_->GetGpuHandle(i);

		ImGui::Image(ImTextureRef(static_cast<ImTextureID>(gpuHandle.ptr)),
		             ImVec2(width, height));

		return;
	}

	throw std::runtime_error(
	    "ImGuiSystem::Image: SRV descriptor heap is full.");
}

bool ImGuiSystem::HandleMessage(void *nativeWindow, std::uint32_t message,
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

void ImGuiSystem::Finalize() {
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
	impl_->imageDescriptors_.clear();
	impl_->descriptorUsed_.fill(false);

	impl_->commandQueue_.Reset();
	impl_->device_.Reset();

	impl_->graphics_ = nullptr;
	impl_->hwnd_ = nullptr;
}
