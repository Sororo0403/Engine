#pragma once

#include <memory>

class D3D12Graphics;
class Model;
class ModelManager;
class RenderWorld;

/// <summary>
/// 読み込み済みモデルへの共有参照を保持する
/// </summary>
class ModelHandle {
  public:
	ModelHandle() = default;

	/// <summary>
	/// モデルを参照しているか取得する
	/// </summary>
	/// <returns>true:参照あり false:空のハンドル</returns>
	explicit operator bool() const {
		return model_ != nullptr;
	}

	/// <summary>
	/// 同じ描画基盤の、同じ読み込み済みモデルを参照しているか比較する
	/// </summary>
	/// <param name="other">比較する共有参照</param>
	/// <returns>true:同じモデル false:別のモデルまたは描画基盤</returns>
	bool operator==(const ModelHandle &other) const = default;

  private:
	friend class ModelManager;
	friend class RenderWorld;

	// キャッシュと全ハンドルからの参照がなくなるまでモデルを保持する
	std::shared_ptr<const Model> model_;

	// 異なる描画基盤で作成されたモデルの使用を検出する
	const D3D12Graphics *graphics_ = nullptr;
};
