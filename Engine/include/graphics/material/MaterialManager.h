#pragma once

#include "graphics/material/MaterialHandle.h"
#include <filesystem>
#include <memory>
class D3D12Graphics;

/// <summary>
/// モデルと独立した画像テクスチャの読み込みとGPUリソースの共有を管理する
/// </summary>
class MaterialManager {
  public:
	/// <summary>
	/// 使用する描画基盤を保持する。GPUリソースは作成しない
	/// </summary>
	/// <param name="graphics">この管理機能より長く保持する描画基盤</param>
	explicit MaterialManager(D3D12Graphics &graphics);
	~MaterialManager();
	MaterialManager(const MaterialManager &) = delete;
	MaterialManager &operator=(const MaterialManager &) = delete;

	/// <summary>
	/// 画像からGPU用の材質を作成する。同じ画像は共有し、ファイルの変更後は再読み込みする
	/// </summary>
	/// <param name="path">画像の絶対パスまたは実行ファイル基準の相対パス。空は白</param>
	/// <returns>成功:材質。失敗:以前の有効な材質、または未設定</returns>
	/// <remarks>GPU完了後、描画命令の記録前に呼び出す。取得済み参照の値は変更しない</remarks>
	MaterialHandle LoadTexture(const std::filesystem::path &path);

	/// <summary>
	/// キャッシュを解放する。GPU完了後、描画基盤の終了前に呼び出す
	/// </summary>
	void Finalize();

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
