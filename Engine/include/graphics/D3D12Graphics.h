#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <dxgi1_6.h>

class D3D12Graphics {
public:
	/// <summary>
	/// Direct3D12の描画基盤を初期化する
	/// </summary>
	/// <param name="hwnd">描画先のウィンドウハンドル</param>
	/// <param name="width">描画領域の幅(ピクセル)</param>
	/// <param name="height">描画領域の高さ(ピクセル)</param>
	/// <returns>true:初期化成功 false:初期化失敗</returns>
	bool Initialize(HWND hwnd, int width, int height);

	/// <summary>
	/// GPUリソースを解放し、描画基盤を終了する
	/// </summary>
	void Finalize();

private:
	Microsoft::WRL::ComPtr<IDXGIFactory6> factory_;
	Microsoft::WRL::ComPtr<ID3D12Device> device_;
};
