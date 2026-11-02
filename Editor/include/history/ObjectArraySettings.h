#pragma once

#include "math/MathUtils.h"
#include <cstddef>

/// <summary>
/// 選択した物体の組を、ワールド座標の一定間隔で繰り返し複製する設定
/// </summary>
struct ObjectArraySettings {
	static constexpr int kMaximumCopies = 100;
	static constexpr std::size_t kMaximumNewObjects = 10000;
	int copies = 1;
	DirectX::XMFLOAT3 offset{2, 0, 0};

	/// <summary>
	/// 複製数・間隔と、1操作で追加する物体数を検証する
	/// </summary>
	/// <param name="sourceCount">選択した元の物体数</param>
	/// <returns>true:1組以上・100組以下かつ追加物体数10000以下で間隔が有限 false:無効</returns>
	bool IsValid(std::size_t sourceCount) const {
		return copies > 0 && copies <= kMaximumCopies && sourceCount > 0 &&
		       sourceCount <=
		           kMaximumNewObjects / static_cast<std::size_t>(copies) &&
		       MathUtils::IsFinite(offset);
	}
};
