#pragma once
#include "scene/SceneObjectId.h"
#include <string>
class EditorHistory;
class Scene;

namespace UIObjects {
enum class Kind { CANVAS, TEXT, IMAGE };
/// <summary>
/// UI物体を1操作として作る。文字・画像の配置先にCanvasがなければ同時に作成する
/// </summary>
/// <param name="scene">親候補とCanvasを探す編集シーン</param>
/// <param name="history">同じシーンの物体追加を記録する履歴</param>
/// <param name="kind">追加するUI</param>
/// <param name="parent">選択した親の候補。Canvas配下でなければ既存または新規Canvasを使う</param>
/// <param name="error">失敗理由。成功時は空</param>
/// <returns>新しい表示物体ID。失敗時は0で、シーンと履歴を保持する</returns>
SceneObjectId Create(const Scene &scene, EditorHistory &history, Kind kind,
                     SceneObjectId parent, std::string &error);
} // namespace UIObjects
