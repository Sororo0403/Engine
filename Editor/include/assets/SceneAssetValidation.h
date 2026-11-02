#pragma once

#include "gameplay/SceneValidation.h"
#include <vector>
class Scene;

namespace SceneAssetValidation {
/// <summary>
/// 全物体のモデルと明示的な材質割り当てについて、読み込みと依存ファイルの存在を検証する
/// </summary>
/// <param name="scene">変更しない編集シーン。非表示・Meshなし・未使用スロットも対象</param>
/// <returns>問題の物体IDと参照先を含む一覧。問題がなければ空。共有するアセットの読み込み結果は1回の検証内で再利用する</returns>
/// <remarks>外部ファイルは変更しない。画像の復号・GPU描画・出力先フォルダの検証は行わない</remarks>
std::vector<SceneValidationIssue> Inspect(const Scene &scene);
} // namespace SceneAssetValidation
