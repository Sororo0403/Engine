#pragma once

#include "scene/SceneObject.h"
#include <filesystem>
#include <functional>
#include <map>
#include <vector>
class Scene;

/// <summary>
/// Load Sceneで到達するシーンを、循環を許し重複なく収集する
/// </summary>
class SceneDependencyGraph {
  public:
	struct Entry {
		std::filesystem::path source;
		std::map<SceneObjectId, SceneObject> objects;
	};

	using Resolve =
	    std::function<std::filesystem::path(const std::filesystem::path &)>;
	using Load = std::function<void(const std::filesystem::path &, Scene &)>;
	static constexpr std::size_t kMaximumScenes = 256;

	/// <summary>
	/// 呼び出し側のパス検証・読み込み規則で全遷移先を収集し、遷移参照を正規化する
	/// </summary>
	/// <param name="scene">入口のシーン。変更しない</param>
	/// <param name="source">入口が既存ファイルなら、その検証済み絶対パス。編集中の入口は空</param>
	/// <param name="resolve">参照を検証済みの通常ファイルの絶対パスへ変換する処理。失敗は例外</param>
	/// <param name="load">参照先のシーンを読み込む処理。失敗は例外</param>
	/// <returns>入口を先頭にした到達順のシーン。未設定の参照は保持し、設定済み参照の不在・不正・件数超過は例外</returns>
	static std::vector<Entry> Read(const Scene &scene,
	                               const std::filesystem::path &source,
	                               const Resolve &resolve, const Load &load);
};
