#pragma once

#include <DirectXMath.h>

class SceneRenderer;
struct SceneObject;

/// <summary>
/// 衝突判定に使うワールド座標のボックスをSceneビューに表示する
/// </summary>
class ColliderBounds {
  public:
	/// <summary>
	/// 選択物体の衝突形状を緑の線で登録する
	/// </summary>
	/// <param name="renderer">更新済みの描画先</param>
	/// <param name="object">表示する物体</param>
	/// <returns>true:成功または形状なし false:線の登録失敗</returns>
	bool Update(SceneRenderer &renderer, const SceneObject &object) const;

  private:
	DirectX::XMFLOAT4 color_{0.2f, 1.0f, 0.3f, 1.0f};
};
