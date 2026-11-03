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
	/// 更新日時・サイズに関係なく画像を読み直し、成功した場合だけ共有キャッシュを置き換える
	/// </summary>
	/// <param name="path">画像の絶対パスまたは実行ファイル基準の相対パス。空は指定できない</param>
	/// <returns>成功:新しい材質。失敗:空。以前のキャッシュと取得済み参照は保持する</returns>
	/// <remarks>GPU完了後、描画命令の記録前に呼び出す。次の更新で同じ画像の使用物体へ反映する</remarks>
	MaterialHandle ReloadTexture(const std::filesystem::path &path);

	/// <summary>
	/// キャッシュを解放する。GPU完了後、描画基盤の終了前に呼び出す
	/// </summary>
	void Finalize();

  private:
	/// <summary>
	/// 画像を読み込み、指定された場合は更新日時・サイズによるキャッシュ判定を省略する
	/// </summary>
	/// <param name="path">画像のパス。通常の読み込みだけ空を白として扱う</param>
	/// <param name="reload">true:必ず読み直す false:変更がない場合は共有する</param>
	/// <returns>成功:材質。失敗:強制再読み込みは空、通常は以前の有効な材質</returns>
	MaterialHandle LoadTextureFile(const std::filesystem::path &path,
	                               bool reload);

	struct Impl;
	std::unique_ptr<Impl> impl_;
};
