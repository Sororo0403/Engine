#pragma once

#include "scene/SceneObjectId.h"
#include <span>
#include <vector>

class Scene;
class EditorHistory;

namespace SceneObjects {
/// <summary>
/// 選択物体を独立したシーンへコピーし、範囲外の物体への追従参照を解除する
/// </summary>
/// <param name="source">変更しない元のシーン</param>
/// <param name="ids">空でない、重複しない物体ID一覧</param>
/// <param name="snapshot">成功時だけ置き換える選択シーン。物体IDと変形・素材参照は保持する</param>
/// <returns>true:成功 false:対象なし、重複、または保存できない設定。ファイル操作は行わない</returns>
bool Capture(const Scene &source, std::span<const SceneObjectId> ids,
             Scene &snapshot);

/// <summary>
/// 別シーンの全物体を新規IDで追加し、内部の追従参照を付け替えて1回のUndo操作にまとめる
/// </summary>
/// <param name="source">変更しない追加元。範囲外への追従参照は解除し、元の配置を保持する</param>
/// <param name="history">追加先の編集履歴。既存のPrimary Cameraと他の物体を保持する</param>
/// <returns>追加順のID一覧。空シーンまたは無効なデータは空を返し、追加しない</returns>
std::vector<SceneObjectId> Append(const Scene &source, EditorHistory &history);
} // namespace SceneObjects
