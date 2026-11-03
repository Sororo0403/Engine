#pragma once

#include <DirectXMath.h>
#include <string>

/// <summary>
/// 新しく作るBoxの名前・ワールド座標の中心・各軸の実寸
/// </summary>
struct BoxCreationSettings {
	static constexpr double kBoundsRelativeTolerance = 0.0001;
	std::string name = "Box";
	DirectX::XMFLOAT3 center{};
	DirectX::XMFLOAT3 size{2.0f, 2.0f, 2.0f};
};
