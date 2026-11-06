#pragma once
#include <json.hpp>
struct SceneObject;

namespace SceneUISerializer {
/// <summary>
/// UIコンポーネントの保存値を検証し、物体のJSONへ追加する
/// </summary>
/// <param name="item">他の保存値を保持する物体JSON</param>
/// <param name="object">保存する物体</param>
/// <remarks>設定が無効な場合は例外。親Canvasなど、再生時の構成条件はここでは要求しない</remarks>
void Write(nlohmann::json &item, const SceneObject &object);
/// <summary>
/// version 30のUIコンポーネントを、型と範囲を確認して読み込む
/// </summary>
/// <param name="item">物体JSON</param>
/// <param name="object">読み込み先の作業用物体</param>
/// <param name="version">入力シーン形式。29以前ではUIを追加しない</param>
/// <remarks>型・設定が無効な場合は例外</remarks>
void Read(const nlohmann::json &item, SceneObject &object, int version);
} // namespace SceneUISerializer
