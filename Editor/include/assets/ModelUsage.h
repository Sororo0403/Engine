#pragma once

#include "scene/SceneObjectId.h"

#include <filesystem>
#include <vector>

class Scene;

namespace ModelUsage {

/// <summary>
/// 指定モデルを参照する物体を、非表示の物体も含めて検索する
/// </summary>
/// <param name="scene">参照を調べるシーン。内容は変更しない</param>
/// <param name="modelPath">モデルの絶対パス、または実行ファイル基準の相対パス</param>
/// <returns>IDの昇順の検索結果。検索先が無効な場合は例外。不正な参照パスは対象外</returns>
std::vector<SceneObjectId> FindObjects(const Scene &scene,
                                       const std::filesystem::path &modelPath);

} // namespace ModelUsage
