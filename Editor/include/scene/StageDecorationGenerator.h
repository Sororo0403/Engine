#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

class ModelManager;
class Scene;
class SceneRenderer;

namespace StageDecorationGenerator {
/// <summary>
/// プロジェクトのテーマとシードを使い、元シーンに装飾を加えたシーンを作る
/// </summary>
/// <param name="source">現在の編集シーン。変更しない</param>
/// <param name="projectRoot">テーマと素材の参照を解決するプロジェクトのルート</param>
/// <param name="seed">同じシーンとテーマから配置を再現するシード</param>
/// <param name="models">装飾モデルの寸法を取得するマネージャー</param>
/// <param name="renderer">元シーンの既存モデルの境界を取得する描画処理</param>
/// <param name="output">成功時だけ置き換えるシーン</param>
/// <param name="error">失敗理由。成功時は空</param>
/// <returns>追加した装飾の数。失敗時は0</returns>
std::size_t Generate(const Scene &source, const std::filesystem::path &projectRoot,
                     std::uint32_t seed, ModelManager &models,
                     const SceneRenderer &renderer, Scene &output,
                     std::string &error);
} // namespace StageDecorationGenerator
