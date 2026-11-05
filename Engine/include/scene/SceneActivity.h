#pragma once

#include "scene/SceneObjectId.h"
#include <map>

class Scene;

namespace SceneActivity {
using States = std::map<SceneObjectId, bool>;

/// <summary>
/// 自身とすべての祖先の設定から、各物体の実効的な有効状態を求める
/// </summary>
/// <param name="scene">変更しないシーン。子自身の設定は上書きしない</param>
/// <param name="states">成功時だけ置き換える全IDの状態。無効な親の子孫はfalse</param>
/// <returns>true:成功 false:親子関係が無効。失敗時は出力を保持する</returns>
bool BuildStates(const Scene &scene, States &states);

/// <summary>
/// 指定物体と祖先をたどり、実効的な有効状態を確認する
/// </summary>
/// <param name="scene">変更しないシーン</param>
/// <param name="id">調べる既存物体</param>
/// <returns>true:自身と全祖先が有効 false:無効、不在ID、または祖先の関係が不正</returns>
bool IsActive(const Scene &scene, SceneObjectId id);
} // namespace SceneActivity
