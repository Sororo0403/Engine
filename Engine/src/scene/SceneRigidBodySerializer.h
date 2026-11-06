#pragma once
#include <json.hpp>
struct SceneObject;

/// <summary>
/// 剛体コンポーネントの保存と、厳密な型・値の読み込みを担当する
/// </summary>
namespace SceneRigidBodySerializer {
/// <summary>
/// 剛体設定がある場合だけ、物体のJSONへ追記する
/// </summary>
/// <param name="item">保存する物体</param>
/// <param name="object">設定の参照元</param>
void Write(nlohmann::json &item, const SceneObject &object);

/// <summary>
/// version 32以降の剛体設定を読み込む。無効な型や値では例外を返す
/// </summary>
/// <param name="item">物体のJSON</param>
/// <param name="object">読み込んだ設定の反映先</param>
/// <param name="version">シーン形式</param>
void Read(const nlohmann::json &item, SceneObject &object, int version);
} // namespace SceneRigidBodySerializer
