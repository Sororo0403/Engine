#pragma once
#include "scene/SceneObjectId.h"
#include <DirectXMath.h>
#include <cstddef>
#include <string>
class Scene;

namespace PrefabInstantiation {
static constexpr std::size_t kMaximumObjects = 4096;

/// <summary>
/// 検証済みの単独階層を新IDで追加し、内部参照を付け替えて指定位置へ配置する
/// </summary>
/// <param name="prefab">PrefabSerializerで読み込んだ、範囲外を参照しない単独階層。変更しない</param>
/// <param name="scene">配置先。ファイル・描画・再生状態・編集履歴は操作しない</param>
/// <param name="position">根の配置先の有限なワールド位置。回転・拡縮・有効設定はPrefabから保持する</param>
/// <param name="error">成功時は空、失敗時は理由</param>
/// <returns>追加した根のID。失敗は0で、配置先の物体とIDカウンターを保持する</returns>
/// <remarks>親子と全内部物体参照を付け替える。Respawnのワールド座標も移す。結果のコンポーネント同士の実行可能性はSceneSimulationで検証する</remarks>
SceneObjectId Instantiate(const Scene &prefab, Scene &scene,
                          const DirectX::XMFLOAT3 &position,
                          std::string &error);
} // namespace PrefabInstantiation
