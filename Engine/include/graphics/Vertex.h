#pragma once

#include <DirectXMath.h>

/// <summary>
/// 位置・色・テクスチャ座標を持つ頂点
/// </summary>
struct Vertex {
	DirectX::XMFLOAT3 position;
	DirectX::XMFLOAT4 color;
	DirectX::XMFLOAT2 texcoord;
};
