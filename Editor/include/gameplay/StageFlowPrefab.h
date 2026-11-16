#pragma once

#include "gameplay/StageFlowBuilder.h"
#include "scene/SceneObjectId.h"

#include <string>
#include <string_view>

class Scene;

namespace StageFlow {
/// <summary>
/// Prefab階層から入口・出口のマーカーと有効な衝突範囲を取得する
/// </summary>
/// <param name="prefab">PrefabSerializerで読み込んだ単独階層</param>
/// <param name="id">カタログ内で区間Prefabを識別するID</param>
/// <param name="sectionType">フローグラフで選ぶ区間の種類</param>
/// <param name="module">成功時だけ置き換える区間の情報</param>
/// <param name="error">失敗理由。成功時は空</param>
bool ReadModule(const Scene &prefab, std::string_view id, std::string_view sectionType,
                Module &module, std::string &error);

/// <summary>
/// ビルダーの位置と向きを適用し、Prefab内の参照を新しい物体IDに付け替えて配置する
/// </summary>
/// <returns>追加した区間の根のID。失敗時は0で配置先を保持する</returns>
SceneObjectId PlaceModule(const Scene &prefab, const Pose &pose, Scene &destination,
                          std::string &error);
} // namespace StageFlow
