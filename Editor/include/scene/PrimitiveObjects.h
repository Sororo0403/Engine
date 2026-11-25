#pragma once

#include "scene/SceneObject.h"

namespace PrimitiveObjects {
/// <summary>
/// 組み込みモデルと一致するColliderを持つ、各辺2のBoxを原点に作る。
/// シーンへの登録や描画リソースの読み込みは行わない
/// </summary>
/// <returns>拡大率1、回転なしの通常の編集用物体</returns>
SceneObject CreateBox();
} // namespace PrimitiveObjects
