#pragma once

#include <cstdint>

/// <summary>
/// 衝突形状の所属レイヤーと、相互に衝突を許可するレイヤーの組み合わせを保持する
/// </summary>
struct CollisionFilter {
	static constexpr std::uint32_t kLayerCount = 32;
	static constexpr std::uint32_t kAllLayers = UINT32_MAX;

	// 0はDefault。親の設定は継承せず、各Colliderの所属を使用する
	std::uint32_t layer = 0;
	// ビットが立っているレイヤーとの衝突を許可する。0はすべて無視する
	std::uint32_t mask = kAllLayers;

	/// <summary>
	/// 所属レイヤーが有効範囲内か確認する。マスクには任意の32ビット値を指定できる
	/// </summary>
	/// <returns>true:有効 false:所属レイヤーが範囲外</returns>
	constexpr bool IsValid() const {
		return layer < kLayerCount;
	}

	/// <summary>
	/// 両方のマスクが相手の所属レイヤーを許可しているか確認する
	/// </summary>
	/// <param name="other">相手の衝突設定</param>
	/// <returns>true:相互に許可 false:片方が無視する、または設定が無効</returns>
	constexpr bool CanCollide(const CollisionFilter &other) const {
		return IsValid() && other.IsValid() &&
		       (mask & (std::uint32_t{1} << other.layer)) != 0 &&
		       (other.mask & (std::uint32_t{1} << layer)) != 0;
	}

	/// <summary>
	/// 所属レイヤーとマスクが一致するか確認する
	/// </summary>
	/// <param name="other">比較対象</param>
	/// <returns>true:一致 false:不一致</returns>
	bool operator==(const CollisionFilter &other) const = default;
};
