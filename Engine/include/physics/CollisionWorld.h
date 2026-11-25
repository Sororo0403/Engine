#pragma once

#include "physics/AxisAlignedBox.h"
#include "physics/CollisionFilter.h"
#include "physics/CollisionMoveResult.h"
#include "physics/CollisionSweepResult.h"
#include "physics/RigidBodyWorld.h"
#include "physics/WorldCollider.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"
#include "scene/SceneObjectId.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

class Scene;

/// <summary>
/// シーンのColliderを実形状の静的な障害物として保持し、移動と滑りを判定する
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
	/// 同じシーンから計算済みのワールド変形と有効状態を使って障害物を作る
	/// </summary>
	bool Rebuild(const Scene &scene,
	             const SceneHierarchy::WorldTransforms &transforms,
	             const SceneActivity::States &active,
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
	/// 指定物体のColliderの実形状と重なる、有効な他のColliderをID順に列挙する
	/// </summary>
	/// <param name="scene">形状と合成姿勢の参照元</param>
	/// <param name="objectId">問い合わせ元のCollider。自身は結果から除外する</param>
	/// <param name="objects">成功時だけ置き換えるID一覧。マスクとTrigger設定は絞り込まない</param>
	/// <returns>true:検索成功 false:物体・階層・形状が無効</returns>
	static bool FindColliderOverlaps(const Scene &scene, SceneObjectId objectId,
	                                 std::vector<SceneObjectId> &objects);

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
	     const std::optional<CollisionFilter> &filter = std::nullopt,
	     const DirectX::XMFLOAT3 &up = {0, 1, 0}) const;

	/// <summary>
	/// 箱は従来のAABB、球とカプセルは実形状で配置できるか調べる
	/// </summary>
	/// <param name="collider">問い合わせ元の形状とマスク</param>
	/// <param name="matrix">ワールド姿勢</param>
	/// <returns>true:配置可能 false:重なり、または形状・姿勢が無効</returns>
	bool CanPlace(const Collider &collider,
	              const DirectX::XMFLOAT4X4 &matrix) const;

	/// <summary>
	/// 形状と姿勢を保って直線移動したときの最初の接触を調べる
	/// </summary>
	/// <param name="collider">問い合わせ元の形状とマスク</param>
	/// <param name="matrix">移動前のワールド姿勢</param>
	/// <param name="displacement">ワールド移動量</param>
	/// <param name="result">成功時だけ設定する接触</param>
	/// <returns>true:検索成功 false:形状や入力が無効</returns>
	bool Sweep(const Collider &collider, const DirectX::XMFLOAT4X4 &matrix,
	           const DirectX::XMFLOAT3 &displacement,
	           CollisionSweepResult &result) const;

	/// <summary>
	/// ワールド形状を直線移動し、実形状が最初に接触する位置を調べる
	/// </summary>
	bool Sweep(const WorldCollider &body, const DirectX::XMFLOAT3 &displacement,
	           CollisionSweepResult &result,
	           const std::optional<CollisionFilter> &filter) const;

	/// <summary>
	/// 実行時の形状に合わせて、衝突と滑りを反映した移動量を求める
	/// </summary>
	/// <param name="collider">移動する形状とマスク</param>
	/// <param name="matrix">移動前のワールド姿勢</param>
	/// <param name="displacement">要求するワールド移動量</param>
	/// <param name="result">成功時だけ設定する移動量と接触状態</param>
	/// <returns>true:成功 false:配置できないか、形状や入力が無効</returns>
	bool Move(const Collider &collider, const DirectX::XMFLOAT4X4 &matrix,
	          const DirectX::XMFLOAT3 &displacement,
	          CollisionMoveResult &result,
	          const DirectX::XMFLOAT3 &up = {0, 1, 0}) const;

	/// <summary>
	/// 指定姿勢の移動用形状と重なる有効な固体を、衝突マスクに従って列挙する
	/// </summary>
	/// <param name="scene">固体を取得するシーン</param>
	/// <param name="collider">問い合わせ元の形状とマスク</param>
	/// <param name="matrix">問い合わせ元のワールド姿勢</param>
	/// <param name="objects">成功時だけ設定する物体ID一覧</param>
	/// <returns>true:検索成功 false:姿勢や形状が無効</returns>
	static bool FindOverlaps(const Scene &scene, const Collider &collider,
	                         const DirectX::XMFLOAT4X4 &matrix,
	                         std::vector<SceneObjectId> &objects);

	/// <summary>
	/// ワールド座標の球と重なるColliderを物体ID順に列挙する
	/// </summary>
	/// <param name="scene">有効なColliderを取得するシーン</param>
	/// <param name="center">球の中心</param>
	/// <param name="radius">球の半径</param>
	/// <param name="objects">成功時だけ設定する物体ID一覧</param>
	/// <param name="layerMask">対象の所属レイヤー。対象自身の衝突マスクには依存しない</param>
	/// <param name="includeTriggers">Triggerも対象にするか</param>
	/// <param name="ignoredObject">除外する物体ID。0は除外なし</param>
	/// <returns>true:検索成功 false:形状や姿勢が無効</returns>
	static bool
	FindSphereOverlaps(const Scene &scene, const DirectX::XMFLOAT3 &center,
	                   float radius, std::vector<SceneObjectId> &objects,
	                   std::uint32_t layerMask, bool includeTriggers,
	                   SceneObjectId ignoredObject);

  private:
	bool CanPlace(const WorldCollider &body,
	              const std::optional<CollisionFilter> &filter) const;
	static bool FindOverlaps(const Scene &scene, const WorldCollider &body,
	                         std::vector<SceneObjectId> &objects,
	                         SceneObjectId ignoredObject,
	                         const std::optional<CollisionFilter> &filter,
	                         bool includeTriggers,
	                         std::uint32_t layerMask = UINT32_MAX);
	/// <summary>
	/// 実形状の面の法線で滑り方向を決め、移動を判定する
	/// </summary>
	template <class Body>
	bool MoveAlongSurfaces(const Body &body,
	                       const DirectX::XMFLOAT3 &displacement,
	                       CollisionMoveResult &result,
	                       const std::optional<CollisionFilter> &filter,
	                       const DirectX::XMFLOAT3 &up) const;
	std::optional<RigidBodyWorld> rigidBodies_;
	std::vector<SceneObjectId> ignoredBodies_;

	/// <summary>
	/// 衝突形状と、それを持つ物体の対応
	/// </summary>
	struct Obstacle {
		SceneObjectId objectId;
		AxisAlignedBox bounds;
		CollisionFilter filter;
		WorldCollider geometry;
		Collider collider;
		DirectX::XMFLOAT4X4 matrix;
	};

	std::vector<Obstacle> obstacles_;
	static constexpr double kSkinWidth = 0.0001;
	static constexpr float kOverlapTolerance = 0.00001f;
	static constexpr int kMaximumSlideIterations = 4;
};
