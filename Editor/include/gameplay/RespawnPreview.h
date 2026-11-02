#pragma once

#include "graphics/render/DebugLine.h"
#include "physics/ColliderBounds.h"
#include "scene/SceneObjectId.h"
#include <array>
#include <optional>

class Scene;
class SceneRenderer;
struct SceneObject;

/// <summary>
/// 選択した物体の落下復帰位置と判定高度をSceneビューに表示する
/// </summary>
class RespawnPreview {
  public:
	using Lines = std::array<DebugLine, 10>;

	/// <summary>
	/// 復帰位置・判定高度の目印と、復帰先での主人公の衝突形状
	/// </summary>
	struct Geometry {
		Lines markers{};
		std::optional<ColliderBounds::Lines> collider;
		// 復帰先の高さ・移動設定・衝突形状・障害物との重なりだけを確認する。シーン全体のPlay可否ではない。
		bool canRespawn = false;
	};

	/// <summary>
	/// 復帰先の衝突形状を作り、Playと同じ障害物で配置を確認する。
	/// 配置可能な目印と形状は水色、配置不可は赤とする
	/// </summary>
	/// <param name="scene">編集シーン。モデルの表示状態には依存しない</param>
	/// <param name="id">確認する物体。自身のColliderは障害物から除く</param>
	/// <param name="geometry">成功時に設定する表示データ。形状を表現できない場合も赤い位置の目印は表示する</param>
	/// <returns>true:作成成功 false:対象なし、または復帰設定・位置の目印が無効。失敗時は出力を保持する</returns>
	bool BuildGeometry(const Scene &scene, SceneObjectId id,
	                   Geometry &geometry) const;

	/// <summary>
	/// 復帰位置の十字と、判定高度の局所的な水平面をワールド座標で組み立てる
	/// </summary>
	/// <param name="object">編集シーンの物体。開始位置は現在のTransformから取得する</param>
	/// <param name="lines">成功時に置き換える線。対象なしまたは無効な場合は保持する</param>
	/// <returns>true:作成成功 false:復帰設定なし、または設定・位置が無効</returns>
	bool BuildLines(const SceneObject &object, Lines &lines) const;

	/// <summary>
	/// 復帰先の目印とColliderを囲む、カメラへ収めるための8頂点を求める。
	/// 配置可否に依存せず、Colliderが無効な場合は位置の目印だけを囲む
	/// </summary>
	/// <param name="object">表示する編集物体。位置・回転・拡縮と復帰設定は変更しない</param>
	/// <param name="corners">成功時に設定するワールド座標の8頂点。落下判定の面は含めない</param>
	/// <returns>true:取得成功 false:復帰設定・位置・範囲が無効。失敗時は出力を保持する</returns>
	static bool GetFrameBounds(const SceneObject &object,
	                           std::array<DirectX::XMFLOAT3, 8> &corners);

	/// <summary>
	/// 復帰設定がある場合だけ、Scene描画にプレビューの線を登録する
	/// </summary>
	/// <param name="renderer">更新済みのScene描画先</param>
	/// <param name="scene">編集シーン。値は変更しない</param>
	/// <param name="id">表示する物体のID</param>
	/// <returns>true:登録成功または表示対象なし false:線の登録失敗</returns>
	bool Update(SceneRenderer &renderer, const Scene &scene,
	            SceneObjectId id) const;

  private:
	static constexpr float kMarkerRadius = 0.5f;
	static constexpr float kFallPlaneHalfExtent = 5.0f;
	DirectX::XMFLOAT4 invalidColor_{1.0f, 0.2f, 0.2f, 1.0f};
	DirectX::XMFLOAT4 positionColor_{0.2f, 0.85f, 1.0f, 1.0f};
	DirectX::XMFLOAT4 fallColor_{1.0f, 0.35f, 0.25f, 1.0f};
};
