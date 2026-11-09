#pragma once

#include <filesystem>
#include <string>
#include <vector>

class Scene;

namespace AssetUsage {
/// <summary>
/// 選択ファイルを参照する編集中の物体・保存済みファイル・起動シーン設定を収集する
/// </summary>
/// <param name="target">確認する素材ファイル</param>
/// <param name="searchRoot">プロジェクトまたはAssetsのルート</param>
/// <param name="scene">編集中のシーン</param>
/// <param name="scenePath">編集中のシーンの保存先。未保存なら空</param>
/// <param name="startup">プロジェクトの起動シーン。未指定なら空</param>
/// <returns>参照元と確認できなかったファイルの一覧。空の場合のみ移動できる</returns>
std::vector<std::string>
Find(const std::filesystem::path &target, const std::filesystem::path &searchRoot,
     const Scene &scene, const std::filesystem::path &scenePath,
     const std::filesystem::path &startup);
} // namespace AssetUsage
