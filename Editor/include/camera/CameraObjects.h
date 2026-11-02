#pragma once

#include "graphics/camera/CameraPose.h"
#include "graphics/camera/CameraSettings.h"
#include "scene/SceneObject.h"
#include <optional>

namespace CameraObjects {
/// <summary>
/// 指定した位置と視線方向をTransformへ反映した、固定Cameraの物体データを作る。
/// シーンへの登録は行わず、Primaryの初期値はtrueとする。排他制御は登録側で行う
/// </summary>
/// <param name="pose">作成元の視点。注視距離ではなく位置・向き・上方向を使用する</param>
/// <param name="settings">縦視野角と表示距離</param>
/// <returns>有効なCameraの物体データ。姿勢または投影設定が無効ならnullopt</returns>
std::optional<SceneObject>
CreateFromView(const CameraPose &pose,
               const CameraSettings &settings = CameraSettings{});
} // namespace CameraObjects
