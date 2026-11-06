#pragma once

#include "audio/AudioSettings.h"
#include <cstdint>
#include <span>

namespace SpatialAudio {
/// <summary>
/// 音源と受信位置の差を、空間計算で有限に扱えるか確認する
/// </summary>
/// <param name="listener">有効な受信姿勢</param>
/// <param name="emitter">有効な音源設定</param>
/// <returns>true:差と距離を表現可能 false:無効または距離が大きすぎる</returns>
bool CanCalculate(const AudioListenerPose &listener,
                  const AudioEmitterSettings &emitter);

/// <summary>
/// Windowsのスピーカー構成に合わせてモノラル音源の配分を計算する。出力は全件成功時だけ置き換える
/// </summary>
/// <param name="listener">左手座標系の受信姿勢</param>
/// <param name="emitter">全方向音源の位置と減衰距離</param>
/// <param name="speakerMask">出力デバイスのスピーカー配置</param>
/// <param name="coefficients">スピーカー数と同じ要素数の出力先</param>
/// <returns>true:成功 false:姿勢・距離・スピーカー構成または出力サイズが無効</returns>
bool Calculate(const AudioListenerPose &listener,
               const AudioEmitterSettings &emitter, std::uint32_t speakerMask,
               std::span<float> coefficients);
} // namespace SpatialAudio
