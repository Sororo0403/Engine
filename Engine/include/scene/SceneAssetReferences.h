#pragma once
#include <filesystem>
#include <functional>
class Scene;

namespace SceneAssetReferences {
/// <summary>
/// 各物体の素材・Prefab・遷移先のパスを共通の規則で変換する
/// </summary>
/// <param name="scene">変換する作業用シーン。失敗時の破棄は呼び出し側で行う</param>
/// <param name="rewrite">空でない参照を変換する処理。失敗は例外</param>
void Rewrite(Scene &scene, const std::function<std::filesystem::path(
                               const std::filesystem::path &)> &rewrite);
} // namespace SceneAssetReferences
