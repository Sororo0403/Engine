#pragma once

#include <DirectXMath.h>

/// <summary>
/// ワールド座標の各軸に平行なボックス
/// </summary>
struct AxisAlignedBox {
	DirectX::XMFLOAT3 minimum{};
	DirectX::XMFLOAT3 maximum{};
};
