#pragma once

#include "graphics/material/MaterialHandle.h"
#include <filesystem>
#include <memory>
class D3D12Graphics;
struct MaterialAsset;

/// <summary>
/// モデルと独立した材質ファイルの読み込みとGPUリソースの共有を管理する
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
	/// 材質と画像を読み込む。同じファイルは共有し、ファイルの変更後は再読み込みする
	/// </summary>
	/// <param name="path">絶対パスまたは実行ファイル基準の相対パス</param>
	/// <returns>成功:材質。失敗:以前の有効な材質、または未設定</returns>
	/// <remarks>GPU完了後、描画命令の記録前に呼び出す。取得済み参照の値は変更しない</remarks>
	MaterialHandle Load(const std::filesystem::path &path);

	/// <summary>
	/// ファイルへ保存せず、パスのキャッシュと独立した材質を作成する
	/// </summary>
	/// <param name="asset">有効な材質の設定と相対画像パス</param>
	/// <param name="directory">画像パスの基準。相対指定は実行ファイル基準</param>
	/// <returns>成功:独立した材質。失敗:未設定。既存の共有材質は変更しない</returns>
	/// <remarks>GPU完了後、描画命令の記録前に呼び出す。参照がなくなるとリソースを解放する</remarks>
	MaterialHandle Create(const MaterialAsset &asset,
	                      const std::filesystem::path &directory = L".");

	/// <summary>
	/// キャッシュを解放する。GPU完了後、描画基盤の終了前に呼び出す
	/// </summary>
	void Finalize();

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
