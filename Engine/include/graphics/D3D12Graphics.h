#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <dxgi1_6.h>

class D3D12Graphics {
private:
	static constexpr UINT kBufferCount = 2;
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
	/// フレームの記録を開始し、指定した色で画面をクリアする
	/// </summary>
	/// <param name="clearColor">赤・緑・青・アルファの4要素の配列</param>
	/// <returns>true:成功 false:失敗</returns>
	bool BeginFrame(const float(&clearColor)[4]);

	/// <summary>
	/// 描画命令を実行して表示し、GPUの完了を待機する
	/// </summary>
	/// <returns>true:成功 false:失敗</returns>
	bool EndFrame();

	/// <summary>
	/// GPUリソースを解放し、描画基盤を終了する
	/// </summary>
	void Finalize();

	/// <summary>
	/// D3D12デバイスを取得する
	/// </summary>
	/// <returns>デバイスへの借用ポインタ</returns>
	ID3D12Device *GetDevice() const;

	/// <summary>
	/// 描画命令の記録先を取得する
	/// </summary>
	/// <returns>BeginFrame成功後からEndFrame前まで使用するコマンドリスト</returns>
	ID3D12GraphicsCommandList *GetCommandList() const;

private:
	/// <summary>
	/// DXGIファクトリを作成する
	/// </summary>
	/// <returns>true:成功 false:失敗</returns>
	bool CreateFactory();

	/// <summary>
	/// GPUを選択し、D3D12デバイスを作成する
	/// </summary>
	/// <returns>true:成功 false:失敗</returns>
	bool CreateDevice();

	/// <summary>
	/// 描画用コマンドキューを作成する
	/// </summary>
	/// <returns>true:成功 false:失敗</returns>
	bool CreateCommandQueue();

	/// <summary>
	/// ウィンドウに表示するスワップチェーンを作成する
	/// </summary>
	/// <param name="hwnd">描画先のウィンドウハンドル</param>
	/// <param name="width">描画領域の幅(ピクセル)</param>
	/// <param name="height">描画領域の高さ(ピクセル)</param>
	/// <returns>true:成功 false:失敗</returns>
	bool CreateSwapChain(HWND hwnd, int width, int height);

	/// <summary>
	/// バックバッファを取得し、描画先のビューを作成する
	/// </summary>
	/// <returns>true:成功 false:失敗</returns>
	bool CreateRenderTargets();

	/// <summary>
	/// 描画命令の記録に使うアロケーターとリストを作成する
	/// </summary>
	/// <returns>true:成功 false:失敗</returns>
	bool CreateCommandObjects();

	/// <summary>
	/// GPUとの同期に使うフェンスとイベントを作成する
	/// </summary>
	/// <returns>true:成功 false:失敗</returns>
	bool CreateSynchronizationObjects();

	/// <summary>
	/// コマンドキューへ送信済みの処理が完了するまで待機する
	/// </summary>
	/// <returns>true:成功 false:失敗</returns>
	bool WaitForGpu();

	/// <summary>
	/// Debugビルド時にD3D12のデバッグレイヤーを有効にする
	/// </summary>
	void EnableDebugLayer();

private:
	Microsoft::WRL::ComPtr<IDXGIFactory6> factory_;
	Microsoft::WRL::ComPtr<ID3D12Device> device_;

	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;

	Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain_;
	Microsoft::WRL::ComPtr<ID3D12Resource> backBuffers_[kBufferCount];
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
	UINT rtvDescriptorSize_ = 0;
	UINT bufferIndex_ = 0;
	D3D12_VIEWPORT viewport_{};
	D3D12_RECT scissor_{};

	Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
	UINT64 fenceValue_ = 0;
	HANDLE fenceEvent_ = nullptr;
};
