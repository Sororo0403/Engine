#pragma once

#include "graphics/render/DebugLine.h"
#include "physics/ColliderBounds.h"
#include "scene/SceneObjectId.h"
#include <array>
#include <optional>
#include <vector>

class Scene;
class SceneRenderer;
struct SceneObject;
struct RespawnSettings;

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
		std::optional<std::vector<DebugLine>> collider;
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
	/// <param name="object">親を持たない物体。親付きはscene・idのオーバーロードを使用する</param>
	/// <param name="lines">成功時に置き換える線。対象なしまたは無効な場合は保持する</param>
	/// <returns>true:作成成功 false:復帰設定なし、または設定・位置が無効</returns>
	bool BuildLines(const SceneObject &object, Lines &lines) const;

	/// <summary>
	/// 復帰先の目印とColliderを囲む、カメラへ収めるための8頂点を求める。
	/// 配置可否に依存せず、Colliderが無効な場合は位置の目印だけを囲む
	/// </summary>
	/// <param name="object">親を持たない物体。位置・回転・拡縮と復帰設定は変更しない</param>
	/// <param name="corners">成功時に設定するワールド座標の8頂点。落下判定の面は含めない</param>
	/// <returns>true:取得成功 false:復帰設定・位置・範囲が無効。失敗時は出力を保持する</returns>
	static bool GetFrameBounds(const SceneObject &object,
	                           std::array<DirectX::XMFLOAT3, 8> &corners);

	/// <summary>
	/// 復帰位置へ移動した場合のワールド座標の衝突形状を求める。移動設定や配置可否は問わない
	/// </summary>
	/// <param name="object">親を持たず、復帰設定とColliderを持つ物体</param>
	/// <param name="bounds">成功時に設定する衝突形状</param>
	/// <returns>true:取得成功 false:設定・変形・形状が無効。失敗時は出力を保持する</returns>
	static bool GetColliderBounds(const SceneObject &object,
	                              AxisAlignedBox &bounds);

	/// <summary>
	/// 復帰設定がある場合だけ、Scene描画にプレビューの線を登録する
	/// </summary>
	/// <param name="renderer">更新済みのScene描画先</param>
	/// <param name="scene">編集シーン。値は変更しない</param>
	/// <param name="id">表示する物体のID</param>
	/// <returns>true:登録成功または表示対象なし false:線の登録失敗</returns>
	bool Update(SceneRenderer &renderer, const Scene &scene,
	            SceneObjectId id) const;

	/// <summary>
	/// 親子合成後のワールド開始位置を使い、復帰位置と落下高度の線を作る
	/// </summary>
	/// <param name="scene">変更しない編集シーン</param>
	/// <param name="id">復帰設定を持つ物体ID</param>
	/// <param name="lines">成功時だけ置き換える線</param>
	/// <returns>true:成功 false:物体・関係・設定が無効</returns>
	bool BuildLines(const Scene &scene, SceneObjectId id, Lines &lines) const;

	/// <summary>
	/// 親子合成後の復帰位置と衝突形状を囲む8頂点を求める
	/// </summary>
	/// <param name="scene">変更しない編集シーン</param>
	/// <param name="id">復帰設定を持つ物体ID</param>
	/// <param name="corners">成功時だけ置き換える8頂点</param>
	/// <returns>true:成功 false:物体・関係・設定・範囲が無効</returns>
	static bool GetFrameBounds(const Scene &scene, SceneObjectId id,
	                           std::array<DirectX::XMFLOAT3, 8> &corners);

	/// <summary>
	/// 親子合成した線形成分を保ち、復帰先でのワールド衝突形状を取得する
	/// </summary>
	/// <param name="scene">変更しない編集シーン</param>
	/// <param name="id">復帰設定とColliderを持つ物体ID</param>
	/// <param name="bounds">成功時だけ置き換えるワールドAABB</param>
	/// <returns>true:成功 false:物体・関係・形状・設定が無効</returns>
	static bool GetColliderBounds(const Scene &scene, SceneObjectId id,
	                              AxisAlignedBox &bounds);

  private:
	/// <summary>
	/// ワールド位置と復帰設定から、原点の十字と落下判定面を組み立てる
	/// </summary>
	/// <param name="settings">検証済みの復帰設定</param>
	/// <param name="position">ワールド座標の復帰位置</param>
	/// <param name="lines">成功時だけ置き換える線</param>
	/// <returns>true:成功 false:位置が無効</returns>
	bool BuildLines(const RespawnSettings &settings,
	                const DirectX::XMFLOAT3 &position, Lines &lines) const;

	/// <summary>
	/// 復帰位置の目印と、指定時はColliderを囲む8頂点を作る
	/// </summary>
	/// <param name="position">ワールド復帰位置</param>
	/// <param name="body">有効な衝突形状。nullptrは目印だけ</param>
	/// <param name="corners">成功時だけ置き換える8頂点</param>
	/// <returns>true:成功 false:位置・範囲を表現できない</returns>
	static bool MakeFrameBounds(const DirectX::XMFLOAT3 &position,
	                            const AxisAlignedBox *body,
	                            std::array<DirectX::XMFLOAT3, 8> &corners);

	static constexpr float kMarkerRadius = 0.5f;
	static constexpr float kFallPlaneHalfExtent = 5.0f;
	DirectX::XMFLOAT4 invalidColor_{1.0f, 0.2f, 0.2f, 1.0f};
	DirectX::XMFLOAT4 positionColor_{0.2f, 0.85f, 1.0f, 1.0f};
	DirectX::XMFLOAT4 fallColor_{1.0f, 0.35f, 0.25f, 1.0f};
};
