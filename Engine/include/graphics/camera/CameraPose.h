#pragma once

#include <DirectXMath.h>

/// <summary>
/// カメラの位置・注視点・上方向
/// </summary>
struct CameraPose {
	DirectX::XMFLOAT3 position{0.0f, 0.0f, -2.0f};
	DirectX::XMFLOAT3 target{0.0f, 0.0f, 0.0f};
	DirectX::XMFLOAT3 up{0.0f, 1.0f, 0.0f};
};
