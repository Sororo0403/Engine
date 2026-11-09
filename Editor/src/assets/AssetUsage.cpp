#include "assets/AssetUsage.h"

#include "assets/AssetDependencies.h"
#include "platform/windows/FileUtils.h"
#include "platform/windows/PathUtils.h"
#include "scene/PrefabSerializer.h"
#include "scene/ProjectDocument.h"
#include "scene/Scene.h"
#include "scene/SceneAssetReferences.h"
#include "scene/SceneDocument.h"
#include "scene/SceneSerializer.h"

#include <Windows.h>
#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <json.hpp>
#include <optional>
#include <set>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
enum class Source { SCENE, PREFAB, MODEL };
using DocumentVisitor = std::function<void(
    const fs::path &, Source, Scene &, SceneObjectId, std::string,
    const fs::path &)>;
using ModelVisitor = std::function<void(const fs::path &)>;
using PathMapping = std::function<fs::path(const fs::path &)>;

/// <summary>
/// ルートと同じ場所または配下の場合にだけ、ルート基準のパスを返す
/// </summary>
std::optional<fs::path> RelativeWithin(const fs::path &root,
                                      const fs::path &path) {
	try {
		return PathUtils::RelativeInside(root, path);
	} catch (const std::runtime_error &) {
		return std::nullopt;
	}
}

std::string ReadText(const fs::path &path) {
	std::ifstream input(path, std::ios::binary);

	if (!input) {
		throw std::runtime_error("Could not read " + PathUtils::ToUtf8(path));
	}

	input.exceptions(std::ios::badbit);
	return {std::istreambuf_iterator<char>(input), {}};
}

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
std::vector<std::string> Scan(
    const std::filesystem::path &target, const std::filesystem::path &searchRoot,
    const Scene &scene, const std::filesystem::path &scenePath,
    const std::filesystem::path &startup, const DocumentVisitor &visit = {},
    const ModelVisitor &visitModel = {}) {
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
		if (matches(path) && (!visit || source == Source::MODEL)) {
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
					if (!visit) {
						usages.insert(std::format("{} / {} (ID {})", label,
						                          object.name, id));
					}
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

	if (!visit && matches(scenePath)) {
		usages.insert("Open scene");
	}

	if (!visit && matches(startup)) {
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

			if (!entry.is_regular_file() || (!visit && matches(path))) {
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
				if (visitModel) {
					visitModel(path);
					return;
				}

				for (const auto &file : AssetDependencies::ModelFiles(path)) {
					if (matches(file)) {
						usages.insert("Model: " + label);
					}
				}

				return;
			}

			Scene document;
			std::string error;
			auto original = ReadText(path);
			SceneObjectId nextSourceId = 0;
			fs::path projectRoot;
			const bool loaded = source == Source::PREFAB
			                        ? PrefabSerializer::Deserialize(
			                              original, path, document, error,
			                              &nextSourceId)
			                        : SceneSerializer::Deserialize(
			                              document, original, path, &projectRoot);

			if (!loaded) {
				throw std::runtime_error(
				    error.empty() ? "Could not read the scene." : error);
			}

			inspect(document, label);

			if (visit) {
				visit(path, source, document, nextSourceId, std::move(original),
				      projectRoot);
			}
		});
	}

	return {usages.begin(), usages.end()};
}

/// <summary>
/// 直接参照とPrefabインスタンスの基準データに新しいパスを反映する
/// </summary>
bool Rewrite(Scene &scene, const PathMapping &map) {
	bool changed = false;
	const auto replace = [&](const fs::path &path) {
		auto updated = map(path);

		if (updated.empty()) {
			return path;
		}

		changed = true;
		return updated;
	};
	SceneAssetReferences::Rewrite(scene, replace);

	for (const auto &[id, object] : scene.GetObjects()) {
		if (!object.prefabLink || object.prefabLink->asset.empty()) {
			continue;
		}

		auto &link = *scene.FindObject(id)->prefabLink;
		link.asset = replace(link.asset);
		Scene baseline;

		if (!SceneSerializer::Deserialize(baseline, link.baseline)) {
			throw std::runtime_error("Could not read a Prefab baseline.");
		}

		const bool earlier = changed;
		changed = false;
		SceneAssetReferences::Rewrite(baseline, replace);

		if (changed && !SceneSerializer::Serialize(baseline, link.baseline)) {
			throw std::runtime_error("Could not update a Prefab baseline.");
		}

		changed |= earlier;
	}

	return changed;
}

struct Edit {
	fs::path path;
	std::string before;
	std::string after;
};
} // namespace

std::vector<std::string> AssetUsage::Find(
    const fs::path &target, const fs::path &searchRoot, const Scene &scene,
    const fs::path &scenePath, const fs::path &startup) {
	return Scan(target, searchRoot, scene, scenePath, startup);
}

bool AssetUsage::Relocate(const fs::path &source, const fs::path &destination,
                        const fs::path &assetRoot, SceneDocument &document,
                        ProjectDocument &project, std::string &error) {
	std::vector<Edit> edits;
	std::size_t applied = 0;
	fs::path from, to;
	bool moved = false;

	try {
		if (!assetRoot.is_absolute() || !source.is_absolute() ||
		    !destination.is_absolute() ||
		    source.native().find(L'\0') != std::wstring::npos ||
		    destination.native().find(L'\0') != std::wstring::npos ||
		    !PathUtils::IsValidFileName(destination.filename())) {
			throw std::runtime_error("Choose a valid destination for an editable asset.");
		}

		const auto status = fs::symlink_status(source);
		const bool folder = fs::is_directory(status);

		if (!folder && !fs::is_regular_file(status)) {
			throw std::runtime_error("Choose a file or folder inside Assets.");
		}

		from = fs::canonical(source);
		const auto assets = fs::canonical(assetRoot);
		const auto parent = fs::canonical(destination.parent_path());
		PathUtils::RelativeInside(assets, parent);
		to = parent / destination.filename();

		if (PathUtils::RelativeInside(assets, from).empty() ||
		    !fs::is_directory(parent) || (folder && RelativeWithin(from, parent))) {
			throw std::runtime_error("Choose a destination outside the source folder.");
		}

		if (folder) {
			const auto check = [](const fs::path &path) {
				const auto attributes = GetFileAttributesW(path.c_str());
				if (attributes == INVALID_FILE_ATTRIBUTES ||
				    (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
					throw std::runtime_error("Cannot relocate a folder containing a link or redirected item:\n" +
					                         PathUtils::ToUtf8(path));
				}
			};
			check(source);
			for (const auto &entry : fs::recursive_directory_iterator(from)) {
				check(entry.path());
			}
		} else {
			if (!PathUtils::HasExtension(to, PathUtils::ToUtf8(from.extension()))) {
				throw std::runtime_error("Keep the original file extension.");
			}

			for (const auto suffix : {".scene", ".prefab", ".settings"}) {
				if (PathUtils::HasExtension(from.stem(), suffix) &&
				    !PathUtils::HasExtension(to.stem(), suffix)) {
					throw std::runtime_error("Keep the original file type suffix.");
				}
			}
		}

		if (fs::exists(fs::symlink_status(to))) {
			throw std::runtime_error("A file or folder already uses that name.");
		}

		const auto map = [&](const fs::path &path) -> fs::path {
			if (path.empty()) {
				return {};
			}

			if (!folder) {
				return PathUtils::IsSameFile(from, path) ? to : fs::path{};
			}

			auto absolute = PathUtils::ResolveFromExecutable(path);
			auto relative = RelativeWithin(from, absolute);

			if (!relative) {
				std::error_code issue;
				absolute = fs::weakly_canonical(absolute, issue);
				if (!issue) {
					relative = RelativeWithin(from, absolute);
				}
			}

			return !relative ? fs::path{} : relative->empty() ? to : to / *relative;
		};
		const auto checkModel = [&](const fs::path &model) {
			const auto movedModel = map(model);
			const auto newParent =
			    movedModel.empty() ? model.parent_path() : movedModel.parent_path();
			std::vector<fs::path> absoluteReferences;

			for (const auto &file :
			     AssetDependencies::ModelFiles(model, &absoluteReferences)) {
				if (PathUtils::IsSameFile(model, file)) {
					continue;
				}

				const auto movedFile = map(file);
				if (movedFile.empty() && newParent == model.parent_path()) {
					continue;
				}

				const auto relative =
				    file.lexically_normal().lexically_relative(model.parent_path());
				const bool absolute = std::ranges::find(absoluteReferences, file) !=
				                      absoluteReferences.end();

				const bool resolves =
				    absolute ? movedFile.empty()
				             : !relative.empty() &&
				                   PathUtils::IsSameFile(
				                       movedFile.empty() ? file : movedFile,
				                       newParent / relative);

				if (!resolves) {
					throw std::runtime_error(
					    "This operation would break an external dependency:\n" +
					    PathUtils::ToUtf8(file));
				}
			}
		};

		if (!folder && parent != from.parent_path() && PathUtils::IsModelFile(from)) {
			checkModel(from);
		}

		const auto &projectRoot = project.GetRoot();
		const auto &root = projectRoot.empty() ? assets : projectRoot;
		const auto startup = project.GetStartupScene();
		const auto &scenePath = document.GetFilePath();
		auto movedOpen = map(scenePath);
		const auto movedStartup = map(startup);
		Scene live;
		live.CopyFrom(document.GetScene());
		Rewrite(live, map);
		std::string liveSavedText;
		bool refreshSavedText = false;

		const auto issues = Scan(
		    from, root, document.GetScene(), scenePath, startup,
		    [&](const fs::path &path, Source kind, Scene &scene,
		        SceneObjectId nextId, std::string original,
		        const fs::path &savedRoot) {
			    const bool changed = Rewrite(scene, map);
			    const auto movedPath = map(path);
			    const bool rebase = !movedPath.empty() &&
			                        movedPath.parent_path() != path.parent_path();
			    const bool open =
			        !scenePath.empty() && PathUtils::IsSameFile(scenePath, path);

			    if ((changed || !movedOpen.empty()) && open &&
			        !document.MatchesSavedText(original)) {
				    throw std::runtime_error("The open scene changed outside the editor.");
			    }

			    if (!changed && !rebase) {
				    return;
			    }

			    PathUtils::RelativeInside(root, path);
			    const auto &target = movedPath.empty() ? path : movedPath;
			    std::string updated, issue;
			    SceneObjectId prefabRoot = 0;
			    const bool serialized =
			        kind == Source::PREFAB
			            ? PrefabSerializer::Validate(scene, prefabRoot, issue) &&
			                  PrefabSerializer::Serialize(scene, prefabRoot, target,
			                                              updated, issue, nextId)
			            : SceneSerializer::SerializeForFile(
			                  scene, updated, target,
			                  savedRoot.empty() ? projectRoot : savedRoot);

			    if (!serialized) {
				    throw std::runtime_error("Could not update references. " + issue);
			    }

			    if (open) {
				    liveSavedText = updated;
				    refreshSavedText = true;
			    }

			    edits.push_back({path, std::move(original), std::move(updated)});
		    }, checkModel);

		if (!issues.empty()) {
			error = "Could not move or rename the asset.";
			for (const auto &issue : issues) {
				error += "\n" + issue;
			}
			return false;
		}

		if (movedOpen.empty()) {
			movedOpen = scenePath;
		}

		for (auto &edit : edits) {
			if (ReadText(edit.path) != edit.before) {
				throw std::runtime_error("A referencing file changed during the operation.");
			}

			if (auto movedPath = map(edit.path); !movedPath.empty()) {
				edit.path = std::move(movedPath);
			}
		}

		if (!MoveFileW(from.c_str(), to.c_str())) {
			throw std::runtime_error(std::format("Could not move the asset (Windows error {}).",
			                                     GetLastError()));
		}

		moved = true;

		for (const auto &edit : edits) {
			if (ReadText(edit.path) != edit.before ||
			    !FileUtils::WriteTextAtomically(edit.path, edit.after, error)) {
				throw std::runtime_error("Could not update " +
				                         PathUtils::ToUtf8(edit.path) + "\n" + error);
			}

			++applied;
		}

		if (!movedStartup.empty() &&
		    !project.Save(project.GetGameConfig(), movedStartup, error)) {
			throw std::runtime_error("Could not update the startup scene.\n" + error);
		}

		document.ApplyAssetRename(live, std::move(movedOpen),
		                          refreshSavedText ? &liveSavedText : nullptr);
		error.clear();
		return true;
	} catch (const std::exception &failure) {
		error = failure.what();
	}

	while (applied != 0) {
		const auto &edit = edits[--applied];
		try {
			std::string issue;
			if (ReadText(edit.path) != edit.after ||
			    !FileUtils::WriteTextAtomically(edit.path, edit.before, issue)) {
				error += "\nCould not restore " + PathUtils::ToUtf8(edit.path) +
				         " " + issue;
			}
		} catch (const std::exception &failure) {
			error += "\n" + std::string(failure.what());
		}
	}

	if (moved && !MoveFileW(to.c_str(), from.c_str())) {
		error += "\nCould not restore the original file path. File is at " +
		         PathUtils::ToUtf8(to);
	}

	return false;
}
