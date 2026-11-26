#pragma once

#include "graphics/render/DebugLine.h"
#include "physics/Collider.h"
#include "scene/SceneObjectId.h"
#include <array>
#include <span>
#include <vector>

class SceneRenderer;
class Scene;

/// <summary>
/// Colliderのローカル輪郭をワールド座標へ変換し、Sceneビューに表示する
/// </summary>
class ColliderBounds {
  public:
	/// <summary>
	/// Sceneビューへ表示する衝突形状の範囲
	/// </summary>
	enum class Mode {
		NONE,
		SELECTED,
	};

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
	/// 祖先の変形を含む衝突形状の8頂点を取得する
	/// </summary>
	/// <param name="scene">表示するシーン</param>
	/// <param name="id">Colliderを持つ物体ID</param>
	/// <param name="corners">成功時だけ置き換える8頂点</param>
	/// <returns>true:成功 false:物体・形状・親子関係が無効</returns>
	static bool GetFrameBounds(const Scene &scene, SceneObjectId id,
	                           std::array<DirectX::XMFLOAT3, 8> &corners);

  private:
	/// <summary>
	/// 祖先の変形を含むColliderを描画へ登録する
	/// </summary>
	/// <param name="renderer">線を登録する描画先</param>
	/// <param name="scene">参照するシーン</param>
	/// <param name="id">表示する物体ID</param>
	/// <returns>true:登録成功またはColliderなし false:物体・形状・関係が無効、または登録失敗</returns>
	bool Update(SceneRenderer &renderer, const Scene &scene,
	            SceneObjectId id) const;

	/// <summary>
	/// ローカルの箱・球・カプセルの輪郭を、合成行列で変換した線へ置き換える
	/// </summary>
	/// <param name="collider">表示する形状</param>
	/// <param name="matrix">有限で可逆なアフィン行列。編集途中の非一様な丸形状も輪郭として表示する</param>
	/// <param name="lines">成功時だけ設定する輪郭。箱12本、球96本、カプセル132本</param>
	/// <returns>true:作成成功 false:形状・行列・線の座標が無効</returns>
	bool BuildLines(const Collider &collider, const DirectX::XMFLOAT4X4 &matrix,
	                std::vector<DebugLine> &lines) const;

	static constexpr int kCircleSegments = 32;
	DirectX::XMFLOAT4 color_{0.2f, 1.0f, 0.3f, 1.0f};
};
