#pragma once

#include "graphics/render/DebugLine.h"
#include "scene/SceneObjectId.h"
#include <array>
#include <span>

class SceneRenderer;
class Scene;
struct SceneObject;

/// <summary>
/// 衝突判定に使うワールド座標のボックスをSceneビューに表示する
/// </summary>
class ColliderBounds {
  public:
	/// <summary>
	/// Sceneビューへ表示する衝突形状の範囲
	/// </summary>
	enum class Mode {
		NONE,
		SELECTED,
		ALL,
	};

	using Lines = std::array<DebugLine, 12>;

	/// <summary>
	/// 指定範囲の衝突形状をScene描画へ登録する。非表示物体の形状も対象とする
	/// </summary>
	/// <param name="renderer">更新済みのScene描画先</param>
	/// <param name="scene">表示するシーン</param>
	/// <param name="selected">選択物体のID一覧。削除済みのIDは無視する</param>
	/// <param name="mode">表示する範囲</param>
	/// <returns>true:成功または対象なし false:表示範囲が無効、または線の登録失敗</returns>
	bool Update(SceneRenderer &renderer, const Scene &scene,
	            std::span<const SceneObjectId> selected, Mode mode) const;

	/// <summary>
	/// 衝突判定と同じワールド座標のボックスを、12本の線へ変換する
	/// </summary>
	/// <param name="object">表示する物体。表示状態やモデルの有無は問わない</param>
	/// <param name="lines">成功時に設定する線。対象なし・無効な場合は保持する</param>
	/// <returns>true:作成成功 false:衝突形状なし、またはワールド形状が無効</returns>
	bool BuildLines(const SceneObject &object, Lines &lines) const;

	/// <summary>
	/// 物体の衝突形状を緑の線で登録する
	/// </summary>
	/// <param name="renderer">更新済みの描画先</param>
	/// <param name="object">表示する物体</param>
	/// <returns>true:成功または形状なし false:線の登録失敗</returns>
	bool Update(SceneRenderer &renderer, const SceneObject &object) const;

  private:
	DirectX::XMFLOAT4 color_{0.2f, 1.0f, 0.3f, 1.0f};
};
