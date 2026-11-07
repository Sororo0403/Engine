#pragma once

#include "scene/BoxCreationSettings.h"
#include "scene/SceneObject.h"

namespace PrimitiveObjects {
/// <summary>
/// 組み込みモデルと一致するColliderを持つ、各辺2のBoxを原点に作る。
/// シーンへの登録や描画リソースの読み込みは行わない
/// </summary>
/// <returns>拡大率1、回転なしの通常の編集用物体</returns>
SceneObject CreateBox();
/// <summary>
/// モデルのローカル範囲から拡大率と原点を求め、指定した中心・実寸のBoxを作る
/// </summary>
/// <param name="settings">名前・有限な中心・正の実寸</param>
/// <param name="bounds">実モデルから求めたローカルのBox。中心が原点でなくてもよい</param>
/// <param name="object">成功時だけ置き換える物体。シーンには登録しない</param>
/// <param name="error">失敗理由。成功時は空</param>
/// <returns>true:成功 false:名前・寸法・形状が無効、または実寸を表現できない</returns>
bool TryCreateBox(const BoxCreationSettings &settings, const Collider &bounds,
                  SceneObject &object, std::string &error);
} // namespace PrimitiveObjects
