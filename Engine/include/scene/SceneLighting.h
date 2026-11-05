#pragma once

#include "graphics/PointLightData.h"
#include "scene/SceneObjectId.h"
#include <string>
#include <vector>

class Scene;

namespace SceneLighting {
/// <summary>
/// 有効な親子の点光源をID順に集める。失敗時は出力配列を保持し、黙って上限で切り捨てない
/// </summary>
/// <param name="scene">ワールド位置と有効設定を解決するシーン</param>
/// <param name="lights">成功時に置き換える描画用配列</param>
/// <param name="error">成功時は空、失敗時は理由</param>
/// <param name="invalidObject">指定時は問題の物体ID、階層不正または成功時は0</param>
/// <returns>true:成功 false:階層・設定が無効、または同時に有効な光源数が上限を超える</returns>
bool CollectPointLights(const Scene &scene, std::vector<PointLightData> &lights,
                        std::string &error,
                        SceneObjectId *invalidObject = nullptr);
} // namespace SceneLighting
