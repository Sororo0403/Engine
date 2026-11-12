#pragma once
#include <filesystem>
#include <functional>
class Scene;
struct SceneObject;

namespace SceneAssetReferences {
using ObjectRewrite = std::function<void(SceneObject &)>;

/// <summary>
/// 物体が直接参照する空でない素材・Prefab・遷移先を列挙する
/// </summary>
void Visit(const SceneObject &object,
           const std::function<void(const std::filesystem::path &)> &visit);

/// <summary>
/// 各物体の素材・Prefab・遷移先のパスを共通の規則で変換する
/// </summary>
/// <param name="scene">変換する作業用シーン。失敗時の破棄は呼び出し側で行う</param>
/// <param name="rewrite">空でない参照を変換する処理。失敗は例外</param>
void Rewrite(Scene &scene, const std::function<std::filesystem::path(
                               const std::filesystem::path &)> &rewrite);

/// <summary>
/// 物体1件の素材・遷移先参照を書き換える
/// </summary>
void Rewrite(SceneObject &object, const std::function<std::filesystem::path(
                                      const std::filesystem::path &)> &rewrite);
} // namespace SceneAssetReferences
