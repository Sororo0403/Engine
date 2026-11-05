#pragma once

#include "graphics/camera/CameraPose.h"
#include "scene/SceneObjectId.h"
#include <string>
#include <vector>

class Scene;

/// <summary>
/// 再生に必要なコンポーネント設定や初期配置の検証結果
/// </summary>
struct SceneValidationIssue {
	SceneObjectId objectId = 0;
	std::string message;
};

namespace SceneValidation {
/// <summary>
/// 再生時と共通の設定・追従姿勢・衝突処理で、編集シーンの問題を列挙する
/// </summary>
/// <param name="scene">変更しない検証対象。空のシーンも有効</param>
/// <param name="fallbackPose">Primary Cameraがない場合の描画姿勢</param>
/// <param name="resolveInitialPose">true:開始時の追従姿勢と復帰配置を検証 false:現在のワールド姿勢と明示的な復帰先を検証。保持中の開始位置は呼び出し側で確認する</param>
/// <returns>検出した問題。空なら再生設定は有効。リソースファイルの有無やGPU描画は検証しない</returns>
/// <remarks>前提となる設定が不正な場合、その設定に依存する衝突検証は修正後に再実行する</remarks>
std::vector<SceneValidationIssue>
Inspect(const Scene &scene, const CameraPose &fallbackPose = CameraPose{},
        bool resolveInitialPose = true);
} // namespace SceneValidation
