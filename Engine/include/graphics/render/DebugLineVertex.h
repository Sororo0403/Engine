#pragma once

#include <DirectXMath.h>

/// <summary>
/// 線の描画に使用する頂点
/// </summary>
struct DebugLineVertex {
	DirectX::XMFLOAT3 position{};
	DirectX::XMFLOAT4 color{};
};
