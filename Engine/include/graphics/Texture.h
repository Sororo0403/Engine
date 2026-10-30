#pragma once

#include <cstdint>
#include <d3d12.h>
#include <filesystem>
#include <span>
#include <wrl/client.h>

class Texture {
  public:
	/// <summary>
	/// 画像ファイルを読み込み、GPUへの転送完了まで待機する
	/// </summary>
	/// <param name="device">テクスチャを作成するD3D12デバイス</param>
	/// <param name="commandQueue">同じデバイスで作成したDIRECT型のコマンドキュー</param>
	/// <param name="filePath">WICが対応する画像ファイルのパス</param>
	/// <returns>true:成功 false:引数が無効、読み込み済み、または読み込み・転送失敗</returns>
	bool Load(ID3D12Device *device, ID3D12CommandQueue *commandQueue,
	          const std::filesystem::path &filePath);

	/// <summary>
	/// メモリ上の画像を読み込み、GPUへの転送完了まで待機する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <param name="commandQueue">同じデバイスのDIRECT型コマンドキュー</param>
	/// <param name="imageData">PNGやJPEGなど、WIC対応形式の画像データ</param>
	/// <returns>true:成功 false:引数が無効、読み込み済み、または読み込み・転送失敗</returns>
	bool LoadFromMemory(ID3D12Device *device, ID3D12CommandQueue *commandQueue,
	                    std::span<const std::uint8_t> imageData);

	/// <summary>
	/// RGBA8の画素からテクスチャを作成し、GPUへの転送完了まで待機する
	/// </summary>
	/// <param name="device">D3D12デバイス</param>
	/// <param name="commandQueue">同じデバイスのDIRECT型コマンドキュー</param>
	/// <param name="width">画像の幅</param>
	/// <param name="height">画像の高さ</param>
	/// <param name="pixels">左上から行順に並べたRGBA各8ビットの画素。行間の余白は不要</param>
	/// <returns>true:成功 false:引数が無効、読み込み済み、または作成・転送失敗</returns>
	bool CreateFromRGBA(ID3D12Device *device, ID3D12CommandQueue *commandQueue,
	                    UINT width, UINT height,
	                    std::span<const std::uint8_t> pixels);

	/// <summary>
	/// テクスチャを解放する。描画に使用している場合はGPUの利用完了後に呼び出す
	/// </summary>
	void Finalize();

	/// <summary>
	/// テクスチャリソースを取得する
	/// </summary>
	/// <returns>リソースへの借用ポインタ。未読み込みの場合はnullptr</returns>
	ID3D12Resource *GetResource() const;

	/// <summary>
	/// テクスチャの幅を取得する
	/// </summary>
	/// <returns>幅（ピクセル）。未読み込みの場合は0</returns>
	UINT GetWidth() const;

	/// <summary>
	/// テクスチャの高さを取得する
	/// </summary>
	/// <returns>高さ（ピクセル）。未読み込みの場合は0</returns>
	UINT GetHeight() const;

  private:
	Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
};
