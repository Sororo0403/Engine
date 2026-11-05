#pragma once

#include "scene/SceneObjectId.h"
#include <map>
#include <span>

struct SceneObject;

namespace SceneObjectReferences {
/// <summary>
/// 親子関係以外のコンポーネント参照を、対応表にある新しいIDへ付け替える
/// </summary>
/// <param name="object">Follow CameraとTrigger Actionsを含む変更対象</param>
/// <param name="remap">旧IDと新IDの対応。対応表にない参照は保持する</param>
void Remap(SceneObject &object,
           const std::map<SceneObjectId, SceneObjectId> &remap);

/// <summary>
/// 許可したID以外へのコンポーネント参照があるか確認する
/// </summary>
/// <param name="object">変更しない物体</param>
/// <param name="allowed">保持できるID一覧。0は常に未設定として許可する</param>
/// <returns>true:範囲外の参照あり false:範囲外の参照なし</returns>
bool HasExternal(const SceneObject &object,
                 std::span<const SceneObjectId> allowed) noexcept;

/// <summary>
/// 許可したID以外への参照だけを未設定にする。親IDは変更しない
/// </summary>
/// <param name="object">変更する物体</param>
/// <param name="allowed">保持できるID一覧</param>
void ClearExternal(SceneObject &object,
                   std::span<const SceneObjectId> allowed) noexcept;

/// <summary>
/// 削除するIDへのコンポーネント参照を未設定にする。親IDは変更しない
/// </summary>
/// <param name="object">変更する物体</param>
/// <param name="removed">削除対象ID</param>
void ClearTargets(SceneObject &object,
                  std::span<const SceneObjectId> removed) noexcept;
} // namespace SceneObjectReferences
