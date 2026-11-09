#pragma once

#include <filesystem>
#include <string>
#include <vector>

class Scene;
class SceneDocument;
class ProjectDocument;

namespace AssetUsage {
/// <summary>
/// 選択した素材またはフォルダ配下を外部から参照する物体・ファイル・起動シーン設定を収集する
/// </summary>
/// <param name="target">確認する素材ファイルまたはフォルダ</param>
/// <param name="searchRoot">プロジェクトまたはAssetsのルート</param>
/// <param name="scene">編集中のシーン</param>
/// <param name="scenePath">編集中のシーンの保存先。未保存なら空</param>
/// <param name="startup">プロジェクトの起動シーン。未指定なら空</param>
/// <returns>参照元と確認できなかったファイルの一覧。空の場合のみ移動できる</returns>
std::vector<std::string>
Find(const std::filesystem::path &target, const std::filesystem::path &searchRoot,
     const Scene &scene, const std::filesystem::path &scenePath,
     const std::filesystem::path &startup);

/// <summary>
/// Assets内で素材を移動または改名し、保存済みと編集中の参照を更新する
/// </summary>
bool Relocate(const std::filesystem::path &source,
            const std::filesystem::path &destination,
            const std::filesystem::path &assetRoot, SceneDocument &document,
            ProjectDocument &project, std::string &error);
} // namespace AssetUsage
