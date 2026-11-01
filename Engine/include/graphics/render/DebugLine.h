#pragma once

#include <DirectXMath.h>

/// <summary>
/// ワールド座標で描画する線の情報
/// </summary>
struct DebugLine {
	DirectX::XMFLOAT3 start{};
	DirectX::XMFLOAT3 end{};
	DirectX::XMFLOAT4 color{1.0f, 1.0f, 1.0f, 1.0f};
};
