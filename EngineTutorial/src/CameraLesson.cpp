#include "CameraLesson.h"

CameraPose MakeFollowCamera(const DirectX::XMFLOAT3 &target) {
	// 第4章の課題。注視点から見たカメラの高さと距離を変更する。
	constexpr DirectX::XMFLOAT3 offset{0.f, 5.5f, -8.f};
	return {{target.x + offset.x, target.y + offset.y, target.z + offset.z}, target};
}
