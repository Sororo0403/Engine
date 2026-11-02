#pragma once

#include "graphics/render/DebugLine.h"
#include "scene/SceneObjectId.h"
#include <array>
#include <span>

class Scene;
class SceneRenderer;
struct SceneObject;
struct Ray;

/// <summary>
/// モデルを持たない物体の原点をScene上に表示し、目印の範囲で選択を判定する
/// </summary>
class ObjectMarkers {
  public:
	using Corners = std::array<DirectX::XMFLOAT3, 8>;
	using Lines = std::array<DebugLine, 12>;

	/// <summary>
	/// 原点を囲む固定サイズの目印の8頂点を作る。Colliderの大きさや表示状態には依存しない
	/// </summary>
	/// <param name="object">モデルを持たない編集物体</param>
	/// <param name="corners">成功時に設定する範囲。失敗時は保持する</param>
	/// <returns>true:取得成功 false:モデルあり、変形が無効、または範囲を表現できない</returns>
	static bool GetBounds(const SceneObject &object, Corners &corners);

	/// <summary>
	/// 目印をCamera・Collider・空の物体の色で組み立てる。選択中は強調色を使う
	/// </summary>
	/// <param name="object">モデルを持たない編集物体</param>
	/// <param name="selected">現在選択されているか</param>
	/// <param name="lines">成功時に設定する線。失敗時は保持する</param>
	/// <returns>true:作成成功 false:表示対象なし、または無効な範囲</returns>
	static bool BuildLines(const SceneObject &object, bool selected,
	                       Lines &lines);

	/// <summary>
	/// モデルより手前で最初に交差する目印を探す。同距離の目印では小さいIDを優先する
	/// </summary>
	/// <param name="scene">選択する編集シーン。値は変更しない</param>
	/// <param name="ray">正規化済みのワールド座標レイ。最大距離を超える物体は除く</param>
	/// <param name="modelDistance">モデルまでの距離。モデルに当たらない場合はレイの最大距離</param>
	/// <returns>最も手前の目印の物体ID。交差なし、または引数が無効な場合は0</returns>
	static SceneObjectId Pick(const Scene &scene, const Ray &ray,
	                          float modelDistance);

	/// <summary>
	/// 全ての有効な目印をScene描画へ登録する。Game描画には使用しない
	/// </summary>
	/// <param name="renderer">更新済みのScene描画先</param>
	/// <param name="scene">表示する編集シーン</param>
	/// <param name="selected">強調表示する物体ID一覧</param>
	/// <returns>true:登録成功または対象なし false:線の登録失敗</returns>
	bool Update(SceneRenderer &renderer, const Scene &scene,
	            std::span<const SceneObjectId> selected) const;

  private:
	static constexpr float kHalfExtent = 0.25f;
	static constexpr double kDirectionTolerance = 0.001;
};
