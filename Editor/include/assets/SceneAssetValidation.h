#pragma once

#include "gameplay/SceneValidation.h"
#include <vector>
class Scene;

namespace SceneAssetValidation {
/// <summary>
/// 全物体のモデルと明示的な材質割り当てについて、読み込み・依存ファイルの存在・明示割り当て画像の復号を検証する
/// </summary>
/// <param name="scene">変更しない編集シーン。非表示・Meshなし・未使用スロットも対象</param>
/// <returns>問題の物体IDと参照先を含む一覧。問題がなければ空。共有するアセットの読み込み結果は1回の検証内で再利用する</returns>
/// <remarks>COM初期化後に呼び出す。画像は取り込みと同じサイズ上限で先頭フレームの全行を検証する。外部ファイルは変更しない。GPU描画・出力先フォルダの検証は行わない</remarks>
std::vector<SceneValidationIssue> Inspect(const Scene &scene);
} // namespace SceneAssetValidation
