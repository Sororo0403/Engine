#pragma once
#include "scene/SceneObjectId.h"
#include <span>
#include <string>
class Scene;

namespace SceneDestruction {
/// <summary>
/// 再生用シーンから指定した根と子孫を削除し、残る物体の参照を整理する
/// </summary>
/// <param name="scene">候補シーン。失敗時は物体と発行済みIDの上限を保持する</param>
/// <param name="roots">存在する根のID。重複・祖先と子孫の同時指定を許可する</param>
/// <param name="error">成功時は空、失敗時は理由</param>
/// <returns>true:削除成功または指定なし false:不在の根・不正な階層や設定</returns>
/// <remarks>名前付き参照は未設定にする。対象を失うFollow CameraとTrigger Actionは再生用複製から取り除く。描画・物理・音声の同期は所有側が行う</remarks>
bool Apply(Scene &scene, std::span<const SceneObjectId> roots,
           std::string &error);
} // namespace SceneDestruction
