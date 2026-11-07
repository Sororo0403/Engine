#pragma once

#include "math/MathUtils.h"
#include <cstddef>

/// <summary>
/// 選択した物体の組を、ワールド座標の行と列に並べて複製する設定
/// </summary>
struct ObjectArraySettings {
	static constexpr int kMaximumCopies = 100;
	static constexpr std::size_t kMaximumNewObjects = 10000;
	int copies = 1;
	DirectX::XMFLOAT3 offset{2, 0, 0};
	int rows = 1;
	DirectX::XMFLOAT3 rowOffset{0, 0, 2};

	/// <summary>
	/// 行と列を合わせた追加の組数を取得する
	/// </summary>
	/// <returns>元の組を除く組数。行または列の範囲が無効なら0</returns>
	int GetCopyCount() const {
		if (copies < 0 || copies > kMaximumCopies || rows < 1 ||
		    rows > kMaximumCopies + 1)
			return 0;
		return (copies + 1) * rows - 1;
	}

	/// <summary>
	/// 行優先で並べた指定の組のワールド座標を求める
	/// </summary>
	/// <param name="position">元の組の物体のワールド座標</param>
	/// <param name="copy">検証済み設定の組番号。0は元の組</param>
	/// <returns>行・列の移動を加えた座標</returns>
	DirectX::XMFLOAT3 GetPosition(const DirectX::XMFLOAT3 &position,
	                              int copy) const {
		const auto column = copy % (copies + 1);
		const auto row = copy / (copies + 1);
		return {
		    static_cast<float>(double(position.x) + double(offset.x) * column +
		                       double(rowOffset.x) * row),
		    static_cast<float>(double(position.y) + double(offset.y) * column +
		                       double(rowOffset.y) * row),
		    static_cast<float>(double(position.z) + double(offset.z) * column +
		                       double(rowOffset.z) * row)};
	}

	/// <summary>
	/// 複製数・間隔と、1操作で追加する物体数を検証する
	/// </summary>
	/// <param name="sourceCount">選択した元の物体数</param>
	/// <returns>true:1組以上・100組以下かつ追加物体数10000以下で行・列の間隔が有限 false:無効</returns>
	bool IsValid(std::size_t sourceCount) const {
		const auto count = GetCopyCount();
		return count > 0 && count <= kMaximumCopies && sourceCount > 0 &&
		       sourceCount <=
		           kMaximumNewObjects / static_cast<std::size_t>(count) &&
		       MathUtils::IsFinite(offset) && MathUtils::IsFinite(rowOffset);
	}
};
