#pragma once

#include <DirectXMath.h>

class Keyboard;
struct Transform;

/// <summary>
/// WASDの押下状態を、斜め入力でも長さが1になる移動方向に変換する
/// </summary>
DirectX::XMFLOAT2 ReadMovementDirection(const Keyboard &keyboard);

/// <summary>
/// WASDの押下状態から、この更新で箱を動かす
/// </summary>
void MoveLessonBox(Transform &box, const Keyboard &keyboard, float seconds);
