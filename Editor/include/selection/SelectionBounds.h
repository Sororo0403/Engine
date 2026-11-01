#pragma once

#include "scene/SceneObjectId.h"

#include <DirectXMath.h>
#include <array>
#include <cstddef>

class SceneRenderer;

/// <summary>
/// 選択物体の外接ボックスの表示を担当する
/// </summary>
class SelectionBounds {
  public:
	/// <summary>
	/// 選択物体を囲む12本の線を、今回の描画へ登録する
	/// </summary>
	/// <param name="renderer">最後の更新が完了したシーン描画機能</param>
	/// <param name="objectId">選択物体のID。0の場合は表示しない</param>
	/// <returns>true:成功、または表示対象なし false:線の登録失敗</returns>
	bool Update(SceneRenderer &renderer, SceneObjectId objectId) const;

  private:
	// ボックスの8頂点を結ぶ12辺
	static constexpr std::array<std::array<std::size_t, 2>, 12> kEdges{{
	    {0, 1},
	    {1, 2},
	    {2, 3},
	    {3, 0},
	    {4, 5},
	    {5, 6},
	    {6, 7},
	    {7, 4},
	    {0, 4},
	    {1, 5},
	    {2, 6},
	    {3, 7},
	}};

	DirectX::XMFLOAT4 color_{1.0f, 0.6f, 0.0f, 1.0f};
};
