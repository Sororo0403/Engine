#pragma once

#include <DirectXMath.h>

/// <summary>
/// 位置と色を持つ頂点
/// </summary>
struct Vertex {
	DirectX::XMFLOAT3 position;
	DirectX::XMFLOAT4 color;
};
