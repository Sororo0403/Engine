#include "assets/AssetUsage.h"

#include "assets/AssetDependencies.h"
#include "platform/windows/PathUtils.h"
#include "scene/PrefabSerializer.h"
#include "scene/Scene.h"
#include "scene/SceneAssetReferences.h"
#include "scene/SceneSerializer.h"

#include <format>
#include <fstream>
#include <json.hpp>
#include <set>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
enum class Source { SCENE, PREFAB, MODEL };

/// <summary>
/// 任意の名前で保存したシーンを通常のJSON設定ファイルと区別する
/// </summary>
bool IsSceneFile(const fs::path &path) {
	if (PathUtils::HasExtension(path.stem(), ".scene")) {
		return true;
	}

	std::ifstream input(path, std::ios::binary);

	if (!input) {
		throw std::runtime_error("Could not read " + PathUtils::ToUtf8(path));
	}

	const auto data = nlohmann::json::parse(input, nullptr, false);
	return data.is_object() && data.contains("format") &&
	       data.at("format") == "EngineScene";
}
} // namespace

std::vector<std::string> AssetUsage::Find(
    const std::filesystem::path &target, const std::filesystem::path &searchRoot,
    const Scene &scene, const std::filesystem::path &scenePath,
    const std::filesystem::path &startup) {
	const auto matches = [&](const fs::path &path) {
		return !path.empty() && PathUtils::IsSameFile(target, path);
	};
	std::set<std::string> usages;
	std::set<fs::path> scheduled;
	std::vector<std::pair<fs::path, Source>> pending;
	const auto check = [&](const std::string &label, const auto &operation) {
		try {
			operation();
		} catch (const std::exception &error) {
			usages.insert("Could not check " + label + "\n" + error.what());
		}
	};

	const auto schedule = [&](const fs::path &path, Source source) {
		if (matches(path)) {
			return;
		}

		const auto absolute = PathUtils::ResolveFromExecutable(path);

		if (!fs::is_regular_file(absolute)) {
			return;
		}

		auto file = fs::canonical(absolute);

		if (scheduled.insert(file).second) {
			pending.emplace_back(std::move(file), source);
		}
	};

	const auto inspect = [&](const Scene &document, const std::string &label) {
		for (const auto &[id, object] : document.GetObjects()) {
			const auto reference = [&](const fs::path &path) {
				if (matches(path)) {
					usages.insert(std::format("{} / {} (ID {})", label,
					                          object.name, id));
				} else if (PrefabSerializer::IsPrefabPath(path)) {
					schedule(path, Source::PREFAB);
				} else if (PathUtils::HasExtension(path, ".json")) {
					schedule(path, Source::SCENE);
				} else if (PathUtils::IsModelFile(path)) {
					schedule(path, Source::MODEL);
				}
			};
			SceneAssetReferences::Visit(object, reference);

			if (object.prefabLink && !object.prefabLink->asset.empty()) {
				reference(object.prefabLink->asset);
				Scene baseline;

				if (!SceneSerializer::Deserialize(baseline,
				                                  object.prefabLink->baseline)) {
					throw std::runtime_error("Could not read Prefab baseline in " +
					                         label);
				}

				for (const auto &[source, original] : baseline.GetObjects()) {
					(void)source;
					SceneAssetReferences::Visit(original, reference);
				}
			}
		}
	};

	if (matches(scenePath)) {
		usages.insert("Open scene");
	}

	if (matches(startup)) {
		usages.insert("Project startup scene (project.json)");
	}

	const auto openLabel = scenePath.empty()
	                           ? "Open scene (unsaved)"
	                           : "Open scene: " + PathUtils::ToUtf8(scenePath);
	check(openLabel, [&] { inspect(scene, openLabel); });

	if (!scenePath.empty()) {
		check(openLabel, [&] { schedule(scenePath, Source::SCENE); });
	}

	check(PathUtils::ToUtf8(searchRoot), [&] {
		for (const auto &entry : fs::recursive_directory_iterator(searchRoot)) {
			const auto &path = entry.path();

			if (!entry.is_regular_file() || matches(path)) {
				continue;
			}

			check(PathUtils::ToUtf8(path), [&] {
				if (PrefabSerializer::IsPrefabPath(path)) {
					schedule(path, Source::PREFAB);
				} else if (PathUtils::IsModelFile(path)) {
					schedule(path, Source::MODEL);
				} else if (PathUtils::HasExtension(path, ".json") &&
				           IsSceneFile(path)) {
					schedule(path, Source::SCENE);
				}
			});
		}
	});

	for (std::size_t i = 0; i < pending.size(); ++i) {
		const auto [path, source] = pending[i];
		const auto label = PathUtils::ToUtf8(path);

		check(label, [&] {
			if (source == Source::MODEL) {
				for (const auto &file : AssetDependencies::ModelFiles(path)) {
					if (matches(file)) {
						usages.insert("Model: " + label);
					}
				}

				return;
			}

			Scene document;
			std::string error;
			const bool loaded = source == Source::PREFAB
			                        ? PrefabSerializer::Load(path, document, error)
			                        : SceneSerializer::Load(document, path);

			if (!loaded) {
				throw std::runtime_error(
				    error.empty() ? "Could not read the scene." : error);
			}

			inspect(document, label);
		});
	}

	return {usages.begin(), usages.end()};
}
