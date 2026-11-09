#pragma once

#include "graphics/lighting/DirectionalLightData.h"
#include "graphics/lighting/LightingSettings.h"
#include "graphics/lighting/PointLightData.h"
#include "scene/SceneActivity.h"
#include "scene/SceneHierarchy.h"
#include "graphics/lighting/SpotLightData.h"
#include "scene/SceneObjectId.h"
#include <string>
#include <vector>

class Scene;
struct SceneObject;

namespace SceneLighting {
/// <summary>
/// 照明設定と同じ向き・色・強さ・影を持つ平行光オブジェクトを作る
/// </summary>
SceneObject CreateDirectionalLight(const LightingSettings &settings);

/// <summary>
/// 有効な平行光を集める。旧形式のシーンだけ従来の平行光にフォールバックする
/// </summary>
bool CollectDirectionalLights(const Scene &scene,
                              const SceneHierarchy::WorldTransforms &transforms,
                              const SceneActivity::States &active,
                              const LightingSettings &fallback,
                              DirectionalLightsData &lights, ShadowSettings &shadows,
                              std::string &error,
                              SceneObjectId *invalidObject = nullptr);


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

/// <summary>
/// 有効なスポットライトをID順に集める。方向は親子の回転だけでローカル+Zから作る
/// </summary>
/// <param name="scene">ワールド位置・回転・有効設定を解決するシーン</param>
/// <param name="lights">成功時に置き換える描画用配列</param>
/// <param name="error">成功時は空、失敗時は理由</param>
/// <param name="invalidObject">指定時は問題の物体ID。階層不正または成功時は0</param>
/// <returns>true:成功 false:階層・設定が無効、または有効な光源数が上限を超える。失敗時は出力を保持する</returns>
bool CollectSpotLights(const Scene &scene, std::vector<SpotLightData> &lights,
                       std::string &error,
                       SceneObjectId *invalidObject = nullptr);
} // namespace SceneLighting
