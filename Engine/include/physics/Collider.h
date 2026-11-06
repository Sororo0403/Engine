#pragma once
#include "physics/BoxCollider.h"

/// <summary>
/// ローカルの箱・球・Y軸カプセルと、共通の中心・衝突レイヤーを保持する
/// </summary>
struct Collider {
	enum class Shape { BOX, SPHERE, CAPSULE };
	DirectX::XMFLOAT3 center{};
	DirectX::XMFLOAT3 halfExtents{.5f, .5f, .5f};
	CollisionFilter filter{};
	bool isTrigger = false;
	Shape shape = Shape::BOX;
	float radius = .5f;
	// カプセルの円柱部分の半分。端の半球は含めない。全高は2*(halfHeight+radius)
	float halfHeight = .5f;

	Collider() = default;

	/// <summary>
	/// 既存の箱の中心・半径・レイヤー・Trigger設定をそのまま引き継ぐ
	/// </summary>
	/// <param name="box">読み取り元の箱。参照は保持しない</param>
	Collider(const BoxCollider &box);

	/// <summary>
	/// 中心と各軸の半径から箱を作る
	/// </summary>
	/// <param name="center">ローカル中心</param>
	/// <param name="halfExtents">箱の正の半径</param>
	/// <param name="filter">衝突の所属とマスク</param>
	/// <param name="isTrigger">移動を止めずイベントだけを発生させる設定</param>
	Collider(const DirectX::XMFLOAT3 &center,
	         const DirectX::XMFLOAT3 &halfExtents,
	         const CollisionFilter &filter = {}, bool isTrigger = false);

	/// <summary>
	/// 中心・レイヤーと、選択した形状の寸法を確認する
	/// </summary>
	/// <returns>true:有効 false:無効な形状、非有限値、または潰れた寸法</returns>
	bool IsValid() const;

	/// <summary>
	/// 選択した形状と、保持する全寸法・中心・衝突設定が一致するか確認する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const Collider &other) const;

	/// <summary>
	/// 選択した形状を囲むローカル座標の半径を取得する
	/// </summary>
	/// <returns>箱の半径、球の半径、またはカプセルの円柱と半球を含む半径。無効な形状は0</returns>
	DirectX::XMFLOAT3 GetHalfExtents() const;
};
