#pragma once

#include "scene/SceneObjectId.h"
#include <filesystem>
#include <span>
#include <string>
#include <vector>
#include <utility>

class Scene;
class SceneDocument;
class ProjectDocument;
class EditorHistory;

namespace AssetUsage {
using Paths = std::vector<std::filesystem::path>;
using Moves = std::vector<std::pair<std::filesystem::path, std::filesystem::path>>;

/// <summary>
/// 参照元の表示と、シーン内で該当する物体を探すための情報
/// </summary>
struct Reference {
	std::string message;
	std::filesystem::path scenePath;
	SceneObjectId objectId = 0;
	bool editingScene = false;
	bool undoHistory = false;
	// シーンとして開けないPrefabやモデルの参照元ファイル
	std::filesystem::path sourcePath;
	bool startupScene = false;
};

/// <summary>
/// 選択した素材またはフォルダ配下を外部から参照する物体・ファイル・起動シーン設定を収集する
/// </summary>
/// <param name="targets">確認する素材ファイルまたはフォルダ</param>
/// <param name="searchRoot">プロジェクトまたはAssetsのルート</param>
/// <param name="scene">編集中のシーン</param>
/// <param name="scenePath">編集中のシーンの保存先。未保存なら空</param>
/// <param name="startup">プロジェクトの起動シーン。未指定なら空</param>
/// <param name="ordered">指定時は、ごみ箱へ移せる場合に参照元を先に並べた絶対パスを返す</param>
/// <param name="history">指定時は編集中のUndoとRedoに残る参照も確認する</param>
/// <returns>参照元と確認できなかったファイルの一覧。移動順を指定した場合は循環参照も報告する</returns>
std::vector<Reference>
Find(std::span<const std::filesystem::path> targets,
     const std::filesystem::path &searchRoot,
     const Scene &scene, const std::filesystem::path &scenePath,
     const std::filesystem::path &startup, Paths *ordered = nullptr,
     const EditorHistory *history = nullptr);

/// <summary>
/// Assets内でファイルまたはフォルダを同じ場所に複製する
/// </summary>
/// <returns>成功時に複製先を返す。失敗時は空で、errorに理由を返す</returns>
Paths Duplicate(std::span<const std::filesystem::path> sources,
                const std::filesystem::path &assetRoot, std::string &error);

/// <summary>
/// 指定フォルダに素材をコピーし、同名項目には複製名を割り当てる
/// </summary>
Paths CopyTo(std::span<const std::filesystem::path> sources,
             const std::filesystem::path &destination,
             const std::filesystem::path &assetRoot,
             const std::filesystem::path &projectRoot, std::string &error);

/// <summary>
/// Assets内で素材を移動または改名し、保存済みと編集中の参照を更新する
/// </summary>
bool Relocate(const Moves &moves, const std::filesystem::path &assetRoot,
              SceneDocument &document, ProjectDocument &project,
              std::string &error);
} // namespace AssetUsage
