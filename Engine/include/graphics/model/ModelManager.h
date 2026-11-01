#pragma once

#include "graphics/model/ModelHandle.h"

#include <filesystem>
#include <array>
#include <DirectXMath.h>
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
	/// キャッシュを解放して終了する。GPUの利用完了後に呼び出す
	/// </summary>
	void Finalize();

	/// <summary>
	/// 読み込み済みモデルのローカル座標の外接ボックスを取得する
	/// </summary>
	/// <param name="model">このマネージャーの描画基盤で読み込んだモデル</param>
	/// <param name="corners">成功時に設定する8頂点</param>
	/// <returns>true:成功 false:ハンドルが無効、または頂点なし</returns>
	bool GetLocalBounds(const ModelHandle &model, std::array<DirectX::XMFLOAT3, 8> &corners) const;

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
