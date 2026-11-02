#pragma once

#include "scene/SceneObjectId.h"
#include <cstddef>
#include <span>
#include <vector>

class Scene;
class EditorSelection;

namespace FollowCameraUsage {
/// <summary>
/// 指定した物体を直接の追従先とする物体を、非表示・未完成のFollow Cameraも含めて検索する
/// </summary>
/// <param name="scene">参照を調べるシーン。内容は変更しない</param>
/// <param name="targets">検索する追従先ID。重複・0・シーンにないIDは無視する</param>
/// <returns>ID昇順で重複しない検索結果。追従の連鎖を再帰的には検索しない</returns>
std::vector<SceneObjectId> FindObjects(const Scene &scene,
                                       std::span<const SceneObjectId> targets);

/// <summary>
/// 現在の選択物体を追従先とするFollow Cameraをまとめて選択する。
/// アクティブ物体が結果に含まれる場合は維持する
/// </summary>
/// <param name="scene">参照を調べるシーン</param>
/// <param name="selection">検索元と置き換える選択。検索失敗時は保持し、該当なしの場合は解除する</param>
/// <returns>選択した物体数。結果が無効な場合は例外</returns>
std::size_t SelectObjects(const Scene &scene, EditorSelection &selection);
} // namespace FollowCameraUsage
