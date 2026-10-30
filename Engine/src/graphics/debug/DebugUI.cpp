#include "graphics/debug/DebugUI.h"
#include "core/Logger.h"

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include <cstddef>
#include <exception>
#include <format>
#include <stdexcept>

// この関数はバックエンドのヘッダー内で宣言が無効化されているため、
// 呼び出す側で宣言する
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

bool DebugUI::Initialize(HWND hwnd, ID3D12Device *device,
                         ID3D12CommandQueue *commandQueue, int framesInFlight) {
	if (!hwnd || !IsWindow(hwnd) || !device || !commandQueue ||
	    framesInFlight <= 0) {
		Logger::Error("DebugUI::Initialize: 引数が無効です。");
		return false;
	}

	if (context_ || srvHeap_ || ImGui::GetCurrentContext()) {
		Logger::Error("DebugUI::Initialize: ImGuiはすでに初期化されています。");
		return false;
	}

	if (commandQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
		Logger::Error(
		    "DebugUI::Initialize: DIRECT型のコマンドキューが必要です。");
		return false;
	}

	try {
		device_ = device;
		commandQueue_ = commandQueue;

		srvHeap_ =
		    std::make_unique<DirectX::DescriptorHeap>(device, kDescriptorCount);

		descriptorUsed_.fill(false);

		IMGUI_CHECKVERSION();
		context_ = ImGui::CreateContext();

		if (!context_) {
			Logger::Error("ImGuiコンテキストの作成に失敗しました。");
			Finalize();
			return false;
		}

		ImGui::SetCurrentContext(context_);

		ImGuiIO &io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		ImGui::StyleColorsDark();

		if (!ImGui_ImplWin32_Init(hwnd)) {
			Logger::Error("ImGuiのWin32初期化に失敗しました。");
			Finalize();
			return false;
		}

		win32Initialized_ = true;

		ImGui_ImplDX12_InitInfo info{};
		info.Device = device_.Get();
		info.CommandQueue = commandQueue_.Get();
		info.NumFramesInFlight = framesInFlight;

		// 現在のD3D12Graphicsの描画形式に合わせる
		info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		info.DSVFormat = DXGI_FORMAT_D32_FLOAT;

		info.UserData = this;
		info.SrvDescriptorHeap = srvHeap_->Heap();
		info.SrvDescriptorAllocFn = AllocateSrv;
		info.SrvDescriptorFreeFn = FreeSrv;

		if (!ImGui_ImplDX12_Init(&info)) {
			Logger::Error("ImGuiのDX12初期化に失敗しました。");
			Finalize();
			return false;
		}

		rendererInitialized_ = true;

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
	if (!context_ || !win32Initialized_ || !rendererInitialized_ ||
	    frameStarted_) {
		Logger::Error(
		    "DebugUI::BeginFrame: 未初期化、またはフレーム開始済みです。");
		return false;
	}

	ImGui::SetCurrentContext(context_);

	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	frameStarted_ = true;
	return true;
}

bool DebugUI::Render(ID3D12GraphicsCommandList *commandList) {
	if (!commandList || !context_ || !rendererInitialized_ || !frameStarted_) {
		Logger::Error("DebugUI::Render: 引数またはフレームの状態が無効です。");
		return false;
	}

	ImGui::SetCurrentContext(context_);

	// UIの構築を終了して、描画データを確定する
	ImGui::Render();
	frameStarted_ = false;

	try {
		ID3D12DescriptorHeap *heaps[] = {srvHeap_->Heap()};
		commandList->SetDescriptorHeaps(1, heaps);

		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
	} catch (const std::exception &error) {
		Logger::Error(
		    std::format("DebugUIの描画処理に失敗しました: {}", error.what()));
		return false;
	}

	return true;
}

bool DebugUI::WantsKeyboard() const {
	if (!context_) {
		return false;
	}

	ImGui::SetCurrentContext(context_);
	return ImGui::GetIO().WantCaptureKeyboard;
}

bool DebugUI::WantsMouse() const {
	if (!context_) {
		return false;
	}

	ImGui::SetCurrentContext(context_);
	return ImGui::GetIO().WantCaptureMouse;
}

bool DebugUI::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (!ImGui::GetCurrentContext() ||
	    !ImGui::GetIO().BackendPlatformUserData) {
		return false;
	}

	return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam) != 0;
}

void DebugUI::AllocateSrv(ImGui_ImplDX12_InitInfo *info,
                          D3D12_CPU_DESCRIPTOR_HANDLE *cpuHandle,
                          D3D12_GPU_DESCRIPTOR_HANDLE *gpuHandle) {
	auto *ui = static_cast<DebugUI *>(info->UserData);

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

void DebugUI::FreeSrv(ImGui_ImplDX12_InitInfo *info,
                      D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                      D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) {
	auto *ui = static_cast<DebugUI *>(info->UserData);

	const SIZE_T first = ui->srvHeap_->GetFirstCpuHandle().ptr;

	const UINT increment = ui->srvHeap_->Increment();

	if (cpuHandle.ptr < first || (cpuHandle.ptr - first) % increment != 0) {
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

void DebugUI::Finalize() {
	if (context_) {
		ImGui::SetCurrentContext(context_);

		if (frameStarted_) {
			ImGui::EndFrame();
			frameStarted_ = false;
		}

		// SRVヒープは、バックエンドの終了処理まで保持する
		if (rendererInitialized_) {
			ImGui_ImplDX12_Shutdown();
			rendererInitialized_ = false;
		}

		if (win32Initialized_) {
			ImGui_ImplWin32_Shutdown();
			win32Initialized_ = false;
		}

		ImGui::DestroyContext(context_);
		context_ = nullptr;
	}

	srvHeap_.reset();
	descriptorUsed_.fill(false);

	commandQueue_.Reset();
	device_.Reset();
}
