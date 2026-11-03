#pragma once

#include "graphics/model/ModelHandle.h"

#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>

class D3D12Graphics;

/// <summary>
/// モデルの読み込み・キャッシュ・共有リソースを管理する
/// </summary>
class ModelManager {
  public:
	/// <summary>
	/// 描画基盤への参照を保持する。GPUリソースは作成しない
	/// </summary>
	/// <param name="graphics">このマネージャーより長く保持する描画基盤</param>
	explicit ModelManager(D3D12Graphics &graphics);

	~ModelManager();

	ModelManager(const ModelManager &) = delete;
	ModelManager &operator=(const ModelManager &) = delete;

	/// <summary>
	/// モデルを読み込む。同じ正規化パスはキャッシュを共有する
	/// </summary>
	/// <param name="filePath">モデルの絶対パス、または実行ファイルのフォルダを基準にした相対パス</param>
	/// <returns>成功:モデルの共有参照 失敗:空のハンドル</returns>
	ModelHandle Load(const std::filesystem::path &filePath);

	/// <summary>
	/// 読み込み済みの共有参照だけを取得する。ファイル読み込み・GPUリソース作成・エラー出力は行わない
	/// </summary>
	/// <param name="filePath">モデルの絶対パスまたは実行ファイル基準の相対パス</param>
	/// <returns>読み込み済み:共有参照。それ以外・無効なパス・終了済み:空</returns>
	ModelHandle FindLoaded(const std::filesystem::path &filePath) const;

	/// <summary>
	/// ファイルとGPUリソースを読み直し、成功した場合だけキャッシュを置き換える
	/// </summary>
	/// <param name="filePath">モデルの絶対パスまたは実行ファイル基準の相対パス</param>
	/// <returns>成功:新しい共有参照 失敗:空。以前のキャッシュと取得済み参照は保持する</returns>
	/// <remarks>GPU利用完了後、描画命令の記録前に呼び出す。既存ハンドルの参照先は変更しない</remarks>
	ModelHandle Reload(const std::filesystem::path &filePath);

	/// <summary>
	/// 成功した再読み込みの連番を取得する。通常のキャッシュ追加では変化しない
	/// </summary>
	/// <returns>再読み込みによる変更の連番</returns>
	std::uint64_t GetReloadRevision() const;

	/// <summary>
	/// 新規読み込み・再読み込みの成功で変化するキャッシュの連番を取得する
	/// </summary>
	/// <returns>共有キャッシュの変更連番。キャッシュ取得と読み込み失敗では変化しない</returns>
	std::uint64_t GetCacheRevision() const;

	/// <summary>
	/// キャッシュを解放して終了する。GPUの利用完了後に呼び出す
	/// </summary>
	void Finalize();

	/// <summary>
	/// モデルの材質スロット数を取得する
	/// </summary>
	/// <param name="model">この描画基盤で読み込んだモデル</param>
	/// <returns>有効なスロット数。無効なハンドルは0</returns>
	std::size_t GetMaterialCount(const ModelHandle &model) const;

	/// <summary>
	/// 読み込み済みモデルのローカル座標の外接ボックスを取得する
	/// </summary>
	/// <param name="model">このマネージャーの描画基盤で読み込んだモデル</param>
	/// <param name="corners">成功時に設定する8頂点</param>
	/// <returns>true:成功 false:ハンドルが無効、または頂点なし</returns>
	bool GetLocalBounds(const ModelHandle &model,
	                    std::array<DirectX::XMFLOAT3, 8> &corners) const;

  private:
	/// <summary>
	/// 共通の読み込みを行い、必要な場合だけ既存キャッシュを使用する
	/// </summary>
	/// <param name="filePath">読み込むモデル</param>
	/// <param name="reload">true:キャッシュを使用せず読み直す false:読み込み済みなら共有する</param>
	/// <returns>成功:共有参照 失敗:空</returns>
	ModelHandle LoadFile(const std::filesystem::path &filePath, bool reload);

	struct Impl;
	std::unique_ptr<Impl> impl_;
};
