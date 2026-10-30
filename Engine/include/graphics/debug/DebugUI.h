#pragma once

#include <directxtk12/DescriptorHeap.h>

#include <Windows.h>
#include <array>
#include <d3d12.h>
#include <memory>
#include <wrl/client.h>

struct ImGuiContext;
struct ImGui_ImplDX12_InitInfo;

/// <summary>
/// Win32とDirectX 12を使用したデバッグUIを管理する
/// </summary>
class DebugUI {
  public:
	DebugUI() = default;

	DebugUI(const DebugUI &) = delete;
	DebugUI &operator=(const DebugUI &) = delete;

	/// <summary>
	/// ImGuiと描画用リソースを初期化する
	/// </summary>
	/// <param name="hwnd">UIを表示するウィンドウ</param>
	/// <param name="device">D3D12デバイス</param>
	/// <param name="commandQueue">同じデバイスのDIRECT型コマンドキュー</param>
	/// <param name="framesInFlight">描画バッファ数。GPU処理中のフレーム数以上にする</param>
	/// <returns>true:成功 false:引数が無効、初期化済み、または作成失敗</returns>
	bool Initialize(HWND hwnd, ID3D12Device *device,
	                ID3D12CommandQueue *commandQueue, int framesInFlight = 2);

	/// <summary>
	/// UIのフレームを開始する。この後でImGuiのウィジェットを作成する
	/// </summary>
	/// <returns>true:成功 false:未初期化、またはフレーム開始済み</returns>
	bool BeginFrame();

	/// <summary>
	/// UIの描画命令を記録する。3D描画後、EndFrameより前に呼び出す
	/// </summary>
	/// <param name="commandList">描画先のRTVが設定済みの記録中のコマンドリスト</param>
	/// <returns>true:成功 false:引数や状態が無効、または描画処理失敗</returns>
	bool Render(ID3D12GraphicsCommandList *commandList);

	/// <summary>
	/// UIがキーボード入力を使用しているか取得する
	/// </summary>
	/// <returns>true:UI操作を優先する false:それ以外</returns>
	bool WantsKeyboard() const;

	/// <summary>
	/// UIがマウス入力を使用しているか取得する
	/// </summary>
	/// <returns>true:UI操作を優先する false:それ以外</returns>
	bool WantsMouse() const;

	/// <summary>
	/// WindowsのメッセージをImGuiへ渡す
	/// </summary>
	/// <param name="hwnd">メッセージを受け取ったウィンドウ</param>
	/// <param name="msg">メッセージ番号</param>
	/// <param name="wParam">追加情報1</param>
	/// <param name="lParam">追加情報2</param>
	/// <returns>true:処理済み false:通常のウィンドウ処理を続ける</returns>
	static bool HandleMessage(HWND hwnd, UINT msg, WPARAM wParam,
	                          LPARAM lParam);

	/// <summary>
	/// UIを終了する。GPUの利用完了後、ウィンドウを破棄する前に呼び出す
	/// </summary>
	void Finalize();

  private:
	static constexpr UINT kDescriptorCount = 128;

	/// <summary>
	/// ImGui用のSRVを割り当てる
	/// </summary>
	/// <param name="info">初期化情報。UserDataからこのクラスを取得する</param>
	/// <param name="cpuHandle">割り当てたCPUハンドル</param>
	/// <param name="gpuHandle">割り当てたGPUハンドル</param>
	static void AllocateSrv(ImGui_ImplDX12_InitInfo *info,
	                        D3D12_CPU_DESCRIPTOR_HANDLE *cpuHandle,
	                        D3D12_GPU_DESCRIPTOR_HANDLE *gpuHandle);

	/// <summary>
	/// ImGuiが使用しなくなったSRVを解放する
	/// </summary>
	/// <param name="info">初期化情報</param>
	/// <param name="cpuHandle">解放するCPUハンドル</param>
	/// <param name="gpuHandle">解放するGPUハンドル</param>
	static void FreeSrv(ImGui_ImplDX12_InitInfo *info,
	                    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
	                    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);

	ImGuiContext *context_ = nullptr;

	// ImGuiのバックエンドが参照している間、デバイスとキューを保持する
	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;

	std::unique_ptr<DirectX::DescriptorHeap> srvHeap_;
	std::array<bool, kDescriptorCount> descriptorUsed_{};

	bool win32Initialized_ = false;
	bool rendererInitialized_ = false;
	bool frameStarted_ = false;
};
