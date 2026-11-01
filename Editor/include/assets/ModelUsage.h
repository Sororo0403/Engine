#pragma once

#include "scene/SceneObjectId.h"

#include <cstddef>
#include <filesystem>
#include <vector>

class Scene;
class EditorSelection;

namespace ModelUsage {

/// <summary>
/// 指定モデルを参照する物体を、非表示の物体も含めて検索する
/// </summary>
/// <param name="scene">参照を調べるシーン。内容は変更しない</param>
/// <param name="modelPath">モデルの絶対パス、または実行ファイル基準の相対パス</param>
/// <returns>IDの昇順の検索結果。検索先が無効な場合は例外。不正な参照パスは対象外</returns>
std::vector<SceneObjectId> FindObjects(const Scene &scene,
                                       const std::filesystem::path &modelPath);

/// <summary>
/// 使用物体をまとめて選択する。現在のアクティブ物体が結果に含まれる場合は維持する
/// </summary>
/// <param name="scene">参照を調べるシーン</param>
/// <param name="modelPath">検索するモデルのパス</param>
/// <param name="selection">置き換える選択。検索失敗時は保持し、該当なしの場合は解除する</param>
/// <returns>選択した物体数。検索先や結果が無効な場合は例外</returns>
std::size_t SelectObjects(const Scene &scene,
                          const std::filesystem::path &modelPath,
                          EditorSelection &selection);

} // namespace ModelUsage
