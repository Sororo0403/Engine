#pragma once

#include "graphics/camera/CameraPose.h"

/// <summary>
/// 箱の位置を注視点とする追従カメラを作る
/// </summary>
CameraPose MakeFollowCamera(const DirectX::XMFLOAT3 &target);
