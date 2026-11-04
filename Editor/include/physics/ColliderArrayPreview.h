#pragma once

#include "history/ObjectArraySettings.h"
#include "physics/AxisAlignedBox.h"
#include "scene/SceneObjectId.h"
#include <span>
#include <vector>

class Scene;
class SceneRenderer;

/// <summary>
/// 等間隔複製の下書きから、複製先のCollider群の外枠をSceneビューへ表示する
/// </summary>
class ColliderArrayPreview {
  public:
	/// <summary>
	/// 履歴と同じ位置計算で、追加する各組のColliderを囲むワールド座標の範囲を求める
	/// </summary>
	/// <param name="scene">変更しない元シーン。モデルの表示状態には依存しない</param>
	/// <param name="ids">重複せず、存在する元の物体ID。Colliderがない物体は範囲に含めない</param>
	/// <param name="settings">組数とワールド座標の間隔</param>
	/// <param name="bounds">成功時に組順で置き換える範囲。全対象にColliderがなければ空</param>
	/// <returns>true:計算成功 false:設定・対象・形状・座標精度が無効。失敗時は出力を保持する</returns>
	bool BuildBounds(const Scene &scene, std::span<const SceneObjectId> ids,
	                 const ObjectArraySettings &settings,
	                 std::vector<AxisAlignedBox> &bounds) const;

	/// <summary>
	/// 各複製組の外枠を水色の線として登録する。物体や描画モデルは作成しない
	/// </summary>
	/// <param name="renderer">Update成功後のScene描画先</param>
	/// <param name="scene">変更しない元シーン</param>
	/// <param name="ids">複製する元の物体ID</param>
	/// <param name="settings">表示中の下書き。無効な場合は線を登録しない</param>
	/// <returns>true:登録成功または表示対象なし false:線の登録失敗</returns>
	bool Update(SceneRenderer &renderer, const Scene &scene,
	            std::span<const SceneObjectId> ids,
	            const ObjectArraySettings &settings) const;

  private:
	DirectX::XMFLOAT4 color_{0.2f, 0.85f, 1.0f, 1.0f};
};
