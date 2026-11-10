#pragma once

#include "audio/AudioSettings.h"
#include "graphics/camera/CameraPose.h"
#include "scene/AudioSourceComponent.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"
#include "scene/SceneObjectId.h"
#include <string>
#include <vector>

class Scene;

/// <summary>
/// 有効な音源の編集設定と、親子合成後の再生位置
/// </summary>
struct SceneAudioSource {
	SceneObjectId id = 0;
	AudioSourceComponent component;
	AudioPlaybackSettings playback;
};

/// <summary>
/// 1シーンから同じ時点で収集した、リスナー姿勢と有効な音源
/// </summary>
struct SceneAudioFrame {
	SceneObjectId listenerObject = 0;
	AudioListenerPose listener;
	std::vector<SceneAudioSource> sources;
};

namespace SceneAudio {
/// <summary>
/// 有効な音源とリスナーをワールド座標で収集する。無効な枝と未割り当て音源は再生しない
/// </summary>
/// <param name="scene">収集するシーン</param>
/// <param name="camera">明示したリスナーがない場合に使う描画カメラ</param>
/// <param name="frame">全件成功時にだけ置き換える結果</param>
/// <param name="error">成功時は空、失敗時は理由</param>
/// <param name="invalidObject">指定時は問題のID。階層・予備姿勢の不正や成功時は0</param>
/// <returns>true:収集成功 false:階層・姿勢・設定・距離・有効リスナー数が無効</returns>
bool Collect(const Scene &scene, const CameraPose &camera,
             SceneAudioFrame &frame, std::string &error,
             SceneObjectId *invalidObject = nullptr);

/// <summary>
/// 同じシーンから計算済みのワールド変形と有効状態を使って音源を収集する
/// </summary>
bool Collect(const Scene &scene, const CameraPose &camera,
             const SceneHierarchy::WorldTransforms &transforms,
             const SceneActivity::States &active, SceneAudioFrame &frame,
             std::string &error, SceneObjectId *invalidObject = nullptr);
} // namespace SceneAudio
