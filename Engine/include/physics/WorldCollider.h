#pragma once
#include "physics/AxisAlignedBox.h"
#include "physics/Collider.h"
#include "physics/ColliderCastHit.h"
#include <memory>
#include <optional>
#include <string>

/// <summary>
/// ワールド座標の変更不可の凸形状。コピーは形状を共有し、剛体を作らずに重なり・通過を調べる
/// </summary>
class WorldCollider {
  public:
	static constexpr float kMaximumCoordinate = 1000000;
	static constexpr float kMinimumExtent = .0001f;
	static constexpr float kGeometryTolerance = .00001f;

	/// <summary>
	/// ローカルの形状と親子合成した行列を準備し、成功後だけ結果を置き換える
	/// </summary>
	/// <param name="collider">有効な箱・球・カプセル。Triggerとマスクは呼び出し側で判定する</param>
	/// <param name="world">有限なアフィン行列。箱はせん断・反転に対応し、球・カプセルは一様拡縮と直交する軸を必要とする</param>
	/// <param name="result">成功時だけ設定する共有形状</param>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:成功 false:特異行列・非対応の変形・範囲外・形状作成失敗</returns>
	static bool Create(const Collider &collider,
	                   const DirectX::XMFLOAT4X4 &world, WorldCollider &result,
	                   std::string &error);

	/// <summary>
	/// 実際のワールド形状を囲むAABBを取得する。判定用の形状自体は変更しない
	/// </summary>
	/// <param name="bounds">成功時だけ設定する有限な範囲</param>
	/// <returns>true:取得成功 false:未作成</returns>
	bool GetBounds(AxisAlignedBox &bounds) const;

	/// <summary>
	/// 実形状の重なりを調べる。マスクやTrigger設定は確認しない
	/// </summary>
	/// <param name="other">もう一方の準備済み形状</param>
	/// <param name="minimumDepth">この深さより大きい重なりを検知する。0は正の体積、負の値は近接接触を含む。絶対値の上限は0.01m</param>
	/// <param name="overlap">成功時だけ設定する結果</param>
	/// <returns>true:検索成功 false:未作成・深さが無効</returns>
	bool Overlaps(const WorldCollider &other, float minimumDepth,
	              bool &overlap) const;

	/// <summary>
	/// 姿勢と寸法を固定して直線移動し、最初の接触を調べる。接するだけの移動も含む
	/// </summary>
	/// <param name="other">移動しない障害物</param>
	/// <param name="displacement">ワールド移動量。各成分の絶対値は2000000以下</param>
	/// <param name="hit">成功時だけ設定する最初の接触。接触なしはnullopt</param>
	/// <returns>true:検索成功 false:未作成・移動量が無効</returns>
	bool Cast(const WorldCollider &other, const DirectX::XMFLOAT3 &displacement,
	          std::optional<ColliderCastHit> &hit) const;

	/// <summary>
	/// 直線移動の途中で正の体積を共有するか調べ、面・辺・頂点をかすめるだけの移動を除く
	/// </summary>
	/// <param name="other">移動しない障害物。両方が動く場合は相対移動を渡す</param>
	/// <param name="displacement">ワールド移動量。回転と寸法は固定する</param>
	/// <param name="minimumDepth">検知する深さの下限。0以上0.01m以下。丸め誤差を除く場合は0.000001mなどを指定する</param>
	/// <param name="crosses">成功時だけ設定する途中または端点の重なり</param>
	/// <returns>true:検索成功 false:未作成・引数が無効</returns>
	bool Crosses(const WorldCollider &other,
	             const DirectX::XMFLOAT3 &displacement, float minimumDepth,
	             bool &crosses) const;

  private:
	friend class RigidBodyWorld;
	bool DepthAt(const WorldCollider &other, const DirectX::XMFLOAT3 &offset,
	             float minimumDepth, float &depth) const;
	bool OverlapAt(const WorldCollider &other, const DirectX::XMFLOAT3 &offset,
	               float minimumDepth, bool &overlap) const;
	bool CastAt(const WorldCollider &other, const DirectX::XMFLOAT3 &offset,
	            const DirectX::XMFLOAT3 &displacement,
	            std::optional<ColliderCastHit> &hit) const;
	struct Impl;
	std::shared_ptr<const Impl> impl_;
};
