#pragma once

#include "graphics/render/DebugLine.h"
#include <array>

class SceneRenderer;
struct SceneObject;

/// <summary>
/// 選択した物体の落下復帰位置と判定高度をSceneビューに表示する
/// </summary>
class RespawnPreview {
  public:
	using Lines = std::array<DebugLine, 10>;

	/// <summary>
	/// 復帰位置の十字と、判定高度の局所的な水平面をワールド座標で組み立てる
	/// </summary>
	/// <param name="object">編集シーンの物体。開始位置は現在のTransformから取得する</param>
	/// <param name="lines">成功時に置き換える線。対象なしまたは無効な場合は保持する</param>
	/// <returns>true:作成成功 false:復帰設定なし、または設定・位置が無効</returns>
	bool BuildLines(const SceneObject &object, Lines &lines) const;

	/// <summary>
	/// 復帰設定がある場合だけ、Scene描画にプレビューの線を登録する
	/// </summary>
	/// <param name="renderer">更新済みのScene描画先</param>
	/// <param name="object">表示する物体。値は変更しない</param>
	/// <returns>true:登録成功または表示対象なし false:線の登録失敗</returns>
	bool Update(SceneRenderer &renderer, const SceneObject &object) const;

  private:
	static constexpr float kMarkerRadius = 0.5f;
	static constexpr float kFallPlaneHalfExtent = 5.0f;
	DirectX::XMFLOAT4 positionColor_{0.2f, 0.85f, 1.0f, 1.0f};
	DirectX::XMFLOAT4 fallColor_{1.0f, 0.35f, 0.25f, 1.0f};
};
