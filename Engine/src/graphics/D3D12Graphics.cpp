#include "graphics/D3D12Graphics.h"

#include <d3d12sdklayers.h>

bool D3D12Graphics::Initialize(HWND hwnd, int width, int height) {
	if (!hwnd || width <= 0 || height <= 0 ||
		factory_ || device_ || commandQueue_ || swapChain_) {
		return false;
	}

	EnableDebugLayer();

	if (!CreateFactory() ||
		!CreateDevice() ||
		!CreateCommandQueue() ||
		!CreateSwapChain(hwnd, width, height) ||
		!CreateRenderTargets() ||
		!CreateCommandObjects() ||
		!CreateSynchronizationObjects()) {
		Finalize();
		return false;
	}

	viewport_ = {};
	viewport_.Width = static_cast<float>(width);
	viewport_.Height = static_cast<float>(height);
	viewport_.MinDepth = 0.0f;
	viewport_.MaxDepth = 1.0f;

	scissor_ = {};
	scissor_.right = static_cast<LONG>(width);
	scissor_.bottom = static_cast<LONG>(height);

	return true;
}

bool D3D12Graphics::BeginFrame(const float(&clearColor)[4]) {
	bufferIndex_ = swapChain_->GetCurrentBackBufferIndex();

	// 前フレームのGPU処理はEndFrameで完了を待っている
	HRESULT hr = commandAllocator_->Reset();

	if (FAILED(hr)) {
		return false;
	}

	// コマンドの記録を開始する
	hr = commandList_->Reset(commandAllocator_.Get(), nullptr);

	if (FAILED(hr)) {
		return false;
	}

	// バックバッファを表示用から描画用に切り替える
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = backBuffers_[bufferIndex_].Get();
	barrier.Transition.Subresource =
		D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

	commandList_->ResourceBarrier(1, &barrier);

	// 今回使うRTVを取得する
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
		rtvHeap_->GetCPUDescriptorHandleForHeapStart();

	rtvHandle.ptr +=
		static_cast<SIZE_T>(bufferIndex_) * rtvDescriptorSize_;

	commandList_->ClearRenderTargetView(
		rtvHandle,
		clearColor,
		0,
		nullptr
	);

	// 描画先を設定する
	commandList_->OMSetRenderTargets(
		1,
		&rtvHandle,
		FALSE,
		nullptr
	);

	commandList_->RSSetViewports(1, &viewport_);
	commandList_->RSSetScissorRects(1, &scissor_);

	return true;
}

bool D3D12Graphics::EndFrame() {
	// バックバッファを描画用から表示用に戻す
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = backBuffers_[bufferIndex_].Get();
	barrier.Transition.Subresource =
		D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

	commandList_->ResourceBarrier(1, &barrier);

	// 記録を終了する
	HRESULT hr = commandList_->Close();

	if (FAILED(hr)) {
		return false;
	}

	// GPUへ命令を送る
	ID3D12CommandList *lists[] = {commandList_.Get()};
	commandQueue_->ExecuteCommandLists(1, lists);

	// 描画した画像を表示する
	const HRESULT presentResult = swapChain_->Present(1, 0);

	// 表示に失敗した場合も、送信済みのGPU処理の完了を確認する
	const bool gpuCompleted = WaitForGpu();

	return SUCCEEDED(presentResult) && gpuCompleted;
}

void D3D12Graphics::Finalize() {
	if (device_ && commandQueue_ && fence_ && fenceEvent_) {
		WaitForGpu();
	}

	if (fenceEvent_) {
		CloseHandle(fenceEvent_);
		fenceEvent_ = nullptr;
	}

	fence_.Reset();
	fenceValue_ = 0;

	commandList_.Reset();
	commandAllocator_.Reset();

	for (auto &buffer : backBuffers_) {
		buffer.Reset();
	}

	rtvHeap_.Reset();
	rtvDescriptorSize_ = 0;

	swapChain_.Reset();
	commandQueue_.Reset();
	device_.Reset();
	factory_.Reset();
}

ID3D12Device *D3D12Graphics::GetDevice() const {
	return device_.Get();
}

ID3D12GraphicsCommandList *D3D12Graphics::GetCommandList() const {
	return commandList_.Get();
}

bool D3D12Graphics::CreateFactory() {
	HRESULT hr = CreateDXGIFactory2(
		0,
		IID_PPV_ARGS(factory_.GetAddressOf())
	);

	return SUCCEEDED(hr);
}

bool D3D12Graphics::CreateDevice() {
	for (UINT index = 0; ; ++index) {
		Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;

		// 高性能なGPUを優先して取得する
		HRESULT hr = factory_->EnumAdapterByGpuPreference(
			index,
			DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
			IID_PPV_ARGS(adapter.GetAddressOf())
		);

		if (hr == DXGI_ERROR_NOT_FOUND) {
			break;
		}

		if (FAILED(hr)) {
			return false;
		}

		// GPUの情報を取得する
		DXGI_ADAPTER_DESC1 desc{};
		hr = adapter->GetDesc1(&desc);

		if (FAILED(hr)) {
			continue;
		}

		// ソフトウェアアダプターは除外する
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
			continue;
		}

		// このGPUでデバイスの作成を試す
		hr = D3D12CreateDevice(
			adapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(device_.GetAddressOf())
		);

		if (SUCCEEDED(hr)) {
			return true;
		}
	}

	return false;
}

bool D3D12Graphics::CreateCommandQueue() {
	D3D12_COMMAND_QUEUE_DESC desc{};
	desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	HRESULT hr = device_->CreateCommandQueue(
		&desc,
		IID_PPV_ARGS(commandQueue_.GetAddressOf())
	);

	return SUCCEEDED(hr);
}

bool D3D12Graphics::CreateSwapChain(HWND hwnd, int width, int height) {
	DXGI_SWAP_CHAIN_DESC1 desc{};
	desc.Width = static_cast<UINT>(width);
	desc.Height = static_cast<UINT>(height);
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.BufferCount = kBufferCount;
	desc.Scaling = DXGI_SCALING_STRETCH;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

	Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;

	HRESULT hr = factory_->CreateSwapChainForHwnd(
		commandQueue_.Get(),
		hwnd,
		&desc,
		nullptr,
		nullptr,
		swapChain.GetAddressOf()
	);

	if (FAILED(hr)) {
		return false;
	}


	hr = factory_->MakeWindowAssociation(
		hwnd,
		DXGI_MWA_NO_ALT_ENTER
	);

	if (FAILED(hr)) {
		return false;
	}

	// IDXGISwapChain3のインターフェースを取得する
	hr = swapChain.As(&swapChain_);

	return SUCCEEDED(hr);
}

bool D3D12Graphics::CreateRenderTargets() {
	// RTVをバッファ枚数分格納できるヒープを作成する
	D3D12_DESCRIPTOR_HEAP_DESC desc{};
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	desc.NumDescriptors = kBufferCount;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	HRESULT hr = device_->CreateDescriptorHeap(
		&desc,
		IID_PPV_ARGS(rtvHeap_.GetAddressOf())
	);

	if (FAILED(hr)) {
		return false;
	}

	rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(
		D3D12_DESCRIPTOR_HEAP_TYPE_RTV
	);

	// 最初のRTVの格納位置
	D3D12_CPU_DESCRIPTOR_HANDLE handle =
		rtvHeap_->GetCPUDescriptorHandleForHeapStart();

	for (UINT index = 0; index < kBufferCount; ++index) {
		// スワップチェーンから画像を取得する
		hr = swapChain_->GetBuffer(
			index,
			IID_PPV_ARGS(backBuffers_[index].GetAddressOf())
		);

		if (FAILED(hr)) {
			return false;
		}

		// この画像に対応するRTVを作成する
		device_->CreateRenderTargetView(
			backBuffers_[index].Get(),
			nullptr,
			handle
		);

		// 次のRTVの格納位置へ進む
		handle.ptr += rtvDescriptorSize_;
	}

	return true;
}

bool D3D12Graphics::CreateCommandObjects() {
	HRESULT hr = device_->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(commandAllocator_.GetAddressOf())
	);

	if (FAILED(hr)) {
		return false;
	}

	hr = device_->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		commandAllocator_.Get(),
		nullptr,
		IID_PPV_ARGS(commandList_.GetAddressOf())
	);

	if (FAILED(hr)) {
		return false;
	}

	// 作成直後は記録中なので、いったん閉じる
	hr = commandList_->Close();

	return SUCCEEDED(hr);
}

bool D3D12Graphics::CreateSynchronizationObjects() {
	fenceValue_ = 0;

	HRESULT hr = device_->CreateFence(
		fenceValue_,
		D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(fence_.GetAddressOf())
	);

	if (FAILED(hr)) {
		return false;
	}

	// 初期状態は未通知の、自動リセットイベントを作成する
	fenceEvent_ = CreateEventW(
		nullptr,
		FALSE,
		FALSE,
		nullptr
	);

	return fenceEvent_ != nullptr;
}

bool D3D12Graphics::WaitForGpu() {
	if (!device_ || !commandQueue_ || !fence_ || !fenceEvent_) {
		return false;
	}

	const UINT64 targetValue = ++fenceValue_;

	// 先に送った命令の実行後、フェンスをこの値に更新させる
	HRESULT hr = commandQueue_->Signal(
		fence_.Get(),
		targetValue
	);

	if (FAILED(hr)) {
		return false;
	}

	// まだ完了していなければ、イベントを使って待つ
	if (fence_->GetCompletedValue() < targetValue) {
		hr = fence_->SetEventOnCompletion(
			targetValue,
			fenceEvent_
		);

		if (FAILED(hr)) {
			return false;
		}

		DWORD result = WaitForSingleObject(fenceEvent_, INFINITE);

		if (result != WAIT_OBJECT_0) {
			return false;
		}
	}

	// GPUの異常による待機解除も確認する
	return SUCCEEDED(device_->GetDeviceRemovedReason());
}

void D3D12Graphics::EnableDebugLayer() {
#if defined(_DEBUG)
	Microsoft::WRL::ComPtr<ID3D12Debug> debug;

	HRESULT hr = D3D12GetDebugInterface(
		IID_PPV_ARGS(debug.GetAddressOf())
	);

	if (SUCCEEDED(hr)) {
		debug->EnableDebugLayer();
	}
#endif
}
