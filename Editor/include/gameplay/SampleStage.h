#pragma once
#include "scene/Scene.h"
#include <map>

namespace SampleStage {
/// <summary>
/// 通常の編集可能なコンポーネントだけで、小さな床・壁・主人公・カメラ・平行光を構成する
/// </summary>
/// <returns>シーンへ読み込む物体データ。ゲーム固有の実行処理は含まない</returns>
std::map<SceneObjectId, SceneObject> Create();
} // namespace SampleStage
