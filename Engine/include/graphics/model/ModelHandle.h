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

  private:
	friend class ModelManager;
	friend class RenderWorld;

	// キャッシュと全ハンドルからの参照がなくなるまでモデルを保持する
	std::shared_ptr<const Model> model_;

	// 異なる描画基盤で作成されたモデルの使用を検出する
	const D3D12Graphics *graphics_ = nullptr;
};
