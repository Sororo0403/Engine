#pragma once

#include "graphics/lighting/FogSettings.h"
#include "graphics/lighting/LightingSettings.h"
#include "scene/SceneObject.h"
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
class Scene;

/// <summary>
/// Load SceneとScriptのPrefab参照で到達するシーン・Prefabを、循環を許し重複なく収集する
/// </summary>
class SceneDependencyGraph {
  public:
	struct Entry {
		std::filesystem::path source;
		std::map<SceneObjectId, SceneObject> objects;
		bool prefab = false;
		SceneObjectId nextSourceId = 0;
		std::optional<LightingSettings> lighting;
		bool legacyDirectionalLight = false;
		FogSettings fog;
	};

	using Resolve =
	    std::function<std::filesystem::path(const std::filesystem::path &)>;
	using Load = std::function<void(const std::filesystem::path &, Scene &)>;
	using LoadPrefab = std::function<void(const std::filesystem::path &,
	                                      Scene &, SceneObjectId &)>;
	using ReportIssue = std::function<void(std::string)>;
	static constexpr std::size_t kMaximumScenes = 256;

	/// <summary>
	/// 呼び出し側のパス検証・読み込み規則で全遷移先を収集し、遷移参照を正規化する
	/// </summary>
	/// <param name="scene">入口のシーン。変更しない</param>
	/// <param name="source">入口が既存ファイルなら、その検証済み絶対パス。編集中の入口は空</param>
	/// <param name="resolve">参照を検証済みの通常ファイルの絶対パスへ変換する処理。失敗は例外</param>
	/// <param name="load">参照先のシーンを読み込む処理。失敗は例外</param>
	/// <param name="loadPrefab">Prefabの安全な読み込みと元IDカウンターの取得。Prefab参照がある場合に必要</param>
	/// <param name="reportIssue">参照先の不在・不正を報告して収集を続ける処理。省略時は例外</param>
	/// <returns>入口を先頭にした到達順のシーン。件数超過は例外</returns>
	static std::vector<Entry> Read(const Scene &scene,
	                               const std::filesystem::path &source,
	                               const Resolve &resolve, const Load &load,
	                               const LoadPrefab &loadPrefab = {},
	                               const ReportIssue &reportIssue = {});
};
