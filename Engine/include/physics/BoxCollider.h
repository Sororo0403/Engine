#pragma once

#include <DirectXMath.h>

/// <summary>
/// 物体のローカル座標で定義する衝突用ボックス。描画の表示状態とは独立して使用する
/// </summary>
struct BoxCollider {
	DirectX::XMFLOAT3 center{};
	DirectX::XMFLOAT3 halfExtents{0.5f, 0.5f, 0.5f};

	/// <summary>
	/// 設定値が一致するか確認する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const BoxCollider &other) const {
		return center.x == other.center.x && center.y == other.center.y && center.z == other.center.z &&
		       halfExtents.x == other.halfExtents.x && halfExtents.y == other.halfExtents.y &&
		       halfExtents.z == other.halfExtents.z;
	}
};
