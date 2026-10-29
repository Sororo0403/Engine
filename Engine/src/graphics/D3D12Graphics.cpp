#include "graphics/D3D12Graphics.h"

bool D3D12Graphics::Initialize(HWND hwnd, int width, int height) {
	if (!hwnd || width <= 0 || height <= 0 || factory_ || device_) {
		return false;
	}

	HRESULT hr = CreateDXGIFactory2(
		0,
		IID_PPV_ARGS(factory_.GetAddressOf())
	);

	if (FAILED(hr)) {
		return false;
	}

	for (UINT index = 0; ; ++index) {
		Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;

		hr = factory_->EnumAdapterByGpuPreference(
			index,
			DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
			IID_PPV_ARGS(adapter.GetAddressOf())
		);

		if (hr == DXGI_ERROR_NOT_FOUND) {
			break;
		}

		if (FAILED(hr)) {
			Finalize();
			return false;
		}

		DXGI_ADAPTER_DESC1 desc{};
		hr = adapter->GetDesc1(&desc);

		if (FAILED(hr)) {
			continue;
		}

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
			continue;
		}

		hr = D3D12CreateDevice(
			adapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(device_.GetAddressOf())
		);

		if (SUCCEEDED(hr)) {
			return true;
		}
	}

	Finalize();
	return false;
}

void D3D12Graphics::Finalize() {
	device_.Reset();
	factory_.Reset();
}
