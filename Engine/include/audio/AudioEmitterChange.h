#pragma once

#include "audio/AudioPlaybackHandle.h"
#include "audio/AudioSettings.h"

/// <summary>
/// 受信姿勢と同じ更新で確定する、1つの空間音源の変更
/// </summary>
struct AudioEmitterChange {
	AudioPlaybackHandle handle;
	AudioEmitterSettings emitter;
};
