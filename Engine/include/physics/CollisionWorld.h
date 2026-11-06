#pragma once

#include "physics/AxisAlignedBox.h"
#include "physics/CollisionFilter.h"
#include "physics/CollisionMoveResult.h"
#include "physics/CollisionSweepResult.h"
#include "physics/RigidBodyWorld.h"
#include "scene/SceneObjectId.h"

#include <optional>
#include <span>
#include <vector>

class Scene;

/// <summary>
/// シーンの衝突用ボックスを静的な障害物として保持し、移動と滑りを判定する
/// </summary>
class CollisionWorld {
  public:
	/// <summary>
	/// Trigger以外の衝突形状からワールドを作る。Triggerも形状を検証し、描画の表示状態には依存しない
	/// </summary>
	/// <param name="scene">静的な障害物の設定を持つシーン</param>
	/// <param name="ignoredObject">主人公など、障害物から除くID。0は除外なし</param>
	/// <param name="invalidObject">指定時は無効な形状の物体IDを設定する。成功時は0</param>
	/// <param name="rigidBodies">指定時はコピーした剛体形状で移動・重なりを判定する</param>
	/// <returns>true:成功 false:形状が無効。失敗時は現在の障害物を保持する</returns>
	bool Rebuild(const Scene &scene, SceneObjectId ignoredObject = 0,
	             SceneObjectId *invalidObject = nullptr,
	             const RigidBodyWorld *rigidBodies = nullptr);

	/// <summary>
	/// 複数の物体とTriggerを障害物から除外してワールドを作る。描画の表示状態には依存しない
	/// </summary>
	/// <param name="scene">障害物の設定を持つシーン</param>
	/// <param name="ignoredObjects">除外するID一覧。重複や不在IDは無視する</param>
	/// <param name="invalidObject">指定時は無効な形状のIDを設定する。成功時は0</param>
	/// <param name="rigidBodies">指定時はコピーした剛体形状で移動・重なりを判定する</param>
	/// <returns>true:成功 false:形状が無効。失敗時は現在の障害物を保持する</returns>
	bool Rebuild(const Scene &scene,
	             std::span<const SceneObjectId> ignoredObjects,
	             SceneObjectId *invalidObject = nullptr,
	             const RigidBodyWorld *rigidBodies = nullptr);

	/// <summary>
	/// 有効なボックスを障害物と重ならず配置できるか確認する。面への接触は許可する
	/// </summary>
	/// <param name="body">配置するワールド座標のボックス</param>
	/// <param name="filter">問い合わせ元の衝突設定。未指定はマスクを無視して非Trigger形状を調べる</param>
	/// <returns>true:配置可能 false:値が無効、または障害物と重なる</returns>
	bool
	CanPlace(const AxisAlignedBox &body,
	         const std::optional<CollisionFilter> &filter = std::nullopt) const;

	/// <summary>
	/// 指定したボックスと体積が重なるColliderの物体IDをシーンから検索する。
	/// 面・辺・頂点への接触は重なりに含めず、表示状態には依存しない
	/// </summary>
	/// <param name="scene">衝突形状を調べるシーン。内容は変更しない</param>
	/// <param name="body">検索する有効なワールド座標のボックス</param>
	/// <param name="objects">成功時に置き換えるID昇順の結果。重複なし。該当なしは空</param>
	/// <param name="ignoredObject">検索から除く物体ID。0は除外なし</param>
	/// <param name="filter">問い合わせ元の衝突設定。未指定は幾何的な重なりを検索する</param>
	/// <param name="includeTriggers">true:Triggerも含む幾何的な検索 false:移動を止めるColliderだけを検索</param>
	/// <returns>true:検索成功 false:ボックスや障害物の形状が無効。失敗や例外時は結果を保持する</returns>
	static bool
	FindOverlaps(const Scene &scene, const AxisAlignedBox &body,
	             std::vector<SceneObjectId> &objects,
	             SceneObjectId ignoredObject = 0,
	             const std::optional<CollisionFilter> &filter = std::nullopt,
	             bool includeTriggers = true);

	/// <summary>
	/// ボックスを直線移動したとき、最初に体積が重なり始めるColliderと移動割合を調べる
	/// </summary>
	/// <param name="body">開始位置の有効なワールド座標のボックス</param>
	/// <param name="displacement">調べるワールド座標の移動量。ゼロも指定できる</param>
	/// <param name="result">成功時に置き換える接触結果。同時刻の接触では小さい物体IDを優先する</param>
	/// <param name="filter">問い合わせ元の衝突設定。未指定はすべての非Trigger形状を調べる</param>
	/// <returns>true:問い合わせ成功 false:ボックスや移動量が無効。失敗時は出力を保持する</returns>
	/// <remarks>開始時の重なりは割合0として返す。面に沿う移動や辺を掠めるだけの接触は除く。終点での接触は含む</remarks>
	bool
	Sweep(const AxisAlignedBox &body, const DirectX::XMFLOAT3 &displacement,
	      CollisionSweepResult &result,
	      const std::optional<CollisionFilter> &filter = std::nullopt) const;

	/// <summary>
	/// 移動経路で最初に接触する位置を調べ、障害物に沿った滑りを反映する
	/// </summary>
	/// <param name="body">開始位置のワールド座標のボックス</param>
	/// <param name="displacement">要求するワールド座標の移動量</param>
	/// <param name="result">成功時に設定する移動量と接触状態</param>
	/// <param name="filter">移動する形状の衝突設定。未指定はすべての非Trigger形状と衝突する</param>
	/// <returns>true:成功 false:開始状態や移動結果が無効。失敗時は出力を保持する</returns>
	bool
	Move(const AxisAlignedBox &body, const DirectX::XMFLOAT3 &displacement,
	     CollisionMoveResult &result,
	     const std::optional<CollisionFilter> &filter = std::nullopt) const;

  private:
	/// <summary>
	/// 剛体の面の法線で滑り方向を決め、回転した箱との移動を判定する
	/// </summary>
	bool MoveRigid(const AxisAlignedBox &body,
	               const DirectX::XMFLOAT3 &displacement,
	               CollisionMoveResult &result,
	               const std::optional<CollisionFilter> &filter) const;
	std::optional<RigidBodyWorld> rigidBodies_;
	std::vector<SceneObjectId> ignoredBodies_;

	/// <summary>
	/// 衝突形状と、それを持つ物体の対応
	/// </summary>
	struct Obstacle {
		SceneObjectId objectId;
		AxisAlignedBox bounds;
		CollisionFilter filter;
	};

	std::vector<Obstacle> obstacles_;
	static constexpr double kSkinWidth = 0.0001;
	static constexpr int kMaximumSlideIterations = 4;
};
