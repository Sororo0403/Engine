#include "assets/AssetUsage.h"

#include "assets/AssetDependencies.h"
#include "files/OutputDirectory.h"
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
#include <memory>
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
using PathMatch = std::function<bool(const fs::path &)>;
using ReferenceEdge = std::function<bool(const fs::path &, const fs::path &)>;

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

/// <summary>
/// 実行ファイル基準の参照や別名を、フォルダ配下のパスに揃える
/// </summary>
std::optional<fs::path> RelativeReference(const fs::path &root,
                                         const fs::path &path) {
	const auto absolute = PathUtils::ResolveFromExecutable(path);
	auto relative = RelativeWithin(root, absolute);

	if (!relative) {
		std::error_code error;
		const auto resolved = fs::weakly_canonical(absolute, error);

		if (!error) {
			relative = RelativeWithin(root, resolved);
		}
	}

	return relative;
}

/// <summary>
/// リンクやリダイレクトでない通常の項目を確認する
/// </summary>
void CheckOrdinaryAsset(const fs::path &path) {
	const auto attributes = GetFileAttributesW(path.c_str());

	if (attributes == INVALID_FILE_ATTRIBUTES ||
	    (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
		throw std::runtime_error(
		    "Cannot operate on a missing, linked or redirected item:\n" +
		    PathUtils::ToUtf8(path));
	}
}

/// <summary>
/// フォルダをまとめて操作する前に、配下にリンクがないことを確認する
/// </summary>
void ValidateFolderTree(const fs::path &folder) {
	CheckOrdinaryAsset(folder);

	for (const auto &entry : fs::recursive_directory_iterator(folder)) {
		CheckOrdinaryAsset(entry.path());
	}
}

/// <summary>
/// ファイル形式を保ったまま、同じフォルダで空いている複製名を探す
/// </summary>
fs::path DuplicateDestination(const fs::path &source, bool folder,
                              const fs::path &directory,
                              const AssetUsage::Paths &reserved) {
	const auto parent = directory.empty() ? source.parent_path() : directory;
	const auto available = [&](const fs::path &path) {
		return !fs::exists(fs::symlink_status(path)) &&
		       std::ranges::none_of(reserved, [&](const auto &other) {
			       return PathUtils::IsSameFile(path, other);
		       });
	};

	if (!directory.empty() && available(parent / source.filename())) {
		return parent / source.filename();
	}

	auto stem = folder ? source.filename() : source.stem();
	auto suffix = folder ? fs::path{} : source.extension();

	if (!folder && PathUtils::HasExtension(source, ".json")) {
		for (const auto kind : {".scene", ".prefab", ".settings", ".clip"}) {
			if (PathUtils::HasExtension(stem, kind)) {
				suffix = stem.extension().native() + suffix.native();
				stem = stem.stem();
				break;
			}
		}
	}

	for (std::size_t index = 1;; ++index) {
		auto name = stem.native() + L" Copy";

		if (index > 1) {
			name += L" " + std::to_wstring(index);
		}

		name += suffix.native();

		if (!PathUtils::IsValidFileName(name)) {
			throw std::runtime_error("The generated copy name is too long or invalid.");
		}

		auto candidate = parent / name;

		if (available(candidate)) {
			return candidate;
		}
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
std::vector<AssetUsage::Reference> Scan(
    const PathMatch &matches, const std::filesystem::path &searchRoot,
    const Scene &scene, const std::filesystem::path &scenePath,
    const std::filesystem::path &startup, const DocumentVisitor &visit = {},
    const ModelVisitor &visitModel = {}, const ReferenceEdge &recordEdge = {},
    const EditorHistory *history = nullptr) {
	std::vector<AssetUsage::Reference> usages;
	std::set<fs::path> scheduled;
	std::vector<std::pair<fs::path, Source>> pending;
	const auto check = [&](const std::string &label, const auto &operation) {
		try {
			operation();
		} catch (const std::exception &error) {
			usages.push_back({"Could not check " + label + "\n" + error.what()});
		}
	};

	const auto schedule = [&](const fs::path &path, Source source) {
		if (!recordEdge && matches(path) && (!visit || source == Source::MODEL)) {
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
	const auto scheduleReference = [&](const fs::path &path) {
		if (PrefabSerializer::IsPrefabPath(path)) {
			schedule(path, Source::PREFAB);
		} else if (PathUtils::HasExtension(path, ".json")) {
			schedule(path, Source::SCENE);
		} else if (PathUtils::IsModelFile(path)) {
			schedule(path, Source::MODEL);
		}
	};

	const auto inspectObject = [](const SceneObject &object, const std::string &label,
	                              const auto &reference) {
		SceneAssetReferences::Visit(object, reference);
		if (object.prefabLink && !object.prefabLink->asset.empty()) {
			reference(object.prefabLink->asset);
			Scene baseline;
			if (!SceneSerializer::Deserialize(baseline, object.prefabLink->baseline)) {
				throw std::runtime_error("Could not read Prefab baseline in " + label);
			}
			for (const auto &[sourceId, original] : baseline.GetObjects()) {
				(void)sourceId;
				SceneAssetReferences::Visit(original, reference);
			}
		}
	};

	const auto inspect = [&](const Scene &document, const std::string &label,
	                         const fs::path &source, const fs::path &openPath,
	                         const fs::path &sourcePath = {}) {
		for (const auto &[id, object] : document.GetObjects()) {
			const auto reference = [&](const fs::path &path) {
				if (matches(path)) {
					if (!visit && (!recordEdge || !recordEdge(source, path))) {
						usages.push_back({
						    std::format("{} / {} (ID {}) -> {}", label, object.name,
						                id, PathUtils::ToUtf8(path)),
						    openPath, id, source.empty(), false, sourcePath});
					}
				} else {
					scheduleReference(path);
				}
			};
			inspectObject(object, label, reference);
		}
	};

	if (!visit && matches(scenePath)) {
		usages.push_back({"Open scene: " + PathUtils::ToUtf8(scenePath)});
	}

	if (!visit && matches(startup)) {
		usages.push_back({"Project startup scene (project.json): " +
		                  PathUtils::ToUtf8(startup)});
	}

	const auto openLabel = scenePath.empty()
	                           ? "Open scene (unsaved)"
	                           : "Open scene: " + PathUtils::ToUtf8(scenePath);
	check(openLabel, [&] { inspect(scene, openLabel, {}, {}); });
	if (history) {
		const std::string label = "Undo / Redo history";
		check(label, [&] {
			std::set<fs::path> seen;
			history->VisitStoredObjects([&](const SceneObject &object) {
				inspectObject(object, label, [&](const auto &path) {
					if (!seen.insert(path).second) {
						return;
					}
					if (matches(path)) {
						usages.push_back({label + " -> " + PathUtils::ToUtf8(path),
						                  {}, 0, false, true});
					} else {
						scheduleReference(path);
					}
				});
			});
		});
	}

	if (!scenePath.empty()) {
		check(openLabel, [&] { schedule(scenePath, Source::SCENE); });
	}

	check(PathUtils::ToUtf8(searchRoot), [&] {
		const fs::recursive_directory_iterator end;

		for (fs::recursive_directory_iterator it(searchRoot); it != end; ++it) {
			const auto &path = it->path();

			if (!visit && !recordEdge && it->is_directory() && matches(path)) {
				it.disable_recursion_pending();
				continue;
			}

			if (!it->is_regular_file() || (!visit && !recordEdge && matches(path))) {
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
					if (matches(file) && !PathUtils::IsSameFile(path, file) &&
					    (!recordEdge || !recordEdge(path, file))) {
						usages.push_back({"Model: " + label + " -> " +
						                  PathUtils::ToUtf8(file), {}, 0, false,
						                  false, path});
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

			const bool canOpen =
			    !visit && source == Source::SCENE &&
			    (scenePath.empty() || !PathUtils::IsSameFile(path, scenePath));
			inspect(document, label, path, canOpen ? path : fs::path{},
			        source == Source::PREFAB ? path : fs::path{});

			if (visit) {
				visit(path, source, document, nextSourceId, std::move(original),
				      projectRoot);
			}
		});
	}

	std::ranges::sort(usages, {}, &AssetUsage::Reference::message);
	const auto duplicates =
	    std::ranges::unique(usages, {}, &AssetUsage::Reference::message);
	usages.erase(duplicates.begin(), duplicates.end());
	return usages;
}

/// <summary>
/// 直接参照とPrefabインスタンスの基準データに新しいパスを反映する
/// </summary>
bool RewriteObject(SceneObject &object, const PathMapping &map) {
	bool changed = false;
	const auto replace = [&](const fs::path &path) {
		auto updated = map(path);

		if (updated.empty()) {
			return path;
		}

		changed = true;
		return updated;
	};
	SceneAssetReferences::Rewrite(object, replace);

	if (object.prefabLink && !object.prefabLink->asset.empty()) {
		auto &link = *object.prefabLink;
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

bool Rewrite(Scene &scene, const PathMapping &map) {
	bool changed = false;
	for (const auto &[id, object] : scene.GetObjects()) {
		(void)object;
		changed |= RewriteObject(*scene.FindObject(id), map);
	}
	return changed;
}

struct Edit {
	fs::path path;
	std::string before;
	std::string after;
};
/// <summary>
/// Assets配下の通常項目を確認し、別名やルート自体の操作を防ぐ
/// </summary>
fs::path EditableAsset(const fs::path &source, const fs::path &root) {
	if (!source.is_absolute() || source.native().find(L'\0') != std::wstring::npos) {
		throw std::runtime_error("Choose an item inside editable Assets.");
	}

	CheckOrdinaryAsset(source);
	auto resolved = fs::canonical(source);
	const auto relative = PathUtils::RelativeInside(root, resolved);

	if (relative.empty() ||
	    PathUtils::RelativeInside(root, source.lexically_normal()) != relative) {
		throw std::runtime_error("Choose an item inside Assets without using a redirected path.");
	}

	return resolved;
}

using CopyEntry = std::function<void(const fs::path &, const fs::path &,
                                     OutputDirectory &)>;

void CopyAsset(const fs::path &source, const fs::path &destination,
               OutputDirectory &output, const CopyEntry &copyEntry) {
	output.OpenExisting(destination.parent_path());
	const auto name = destination.filename();

	if (!fs::is_directory(source)) {
		copyEntry(source, name, output);
		return;
	}

	output.CreateSubdirectory(name);

	for (const auto &entry : fs::recursive_directory_iterator(source)) {
		CheckOrdinaryAsset(entry.path());
		const auto copy = name / PathUtils::RelativeInside(source, entry.path());

		if (entry.is_directory()) {
			output.CreateSubdirectory(copy);
		} else {
			copyEntry(entry.path(), copy, output);
		}
	}
}

struct MoveEntry {
	fs::path from, to;
	bool folder;
};
/// <summary>
/// モデル内部の相対参照が、移動先または複製先で有効か確認する
/// </summary>
void CheckModelRelocation(const fs::path &model, const PathMapping &map,
                          bool copying) {
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
		    absolute ? copying || movedFile.empty()
		             : !relative.empty() &&
		                   PathUtils::IsSameFile(
		                       movedFile.empty() ? file : movedFile,
		                       newParent / relative);

		if (!resolves &&
		    (!copying || relative.empty() ||
		     !PathUtils::IsSameFile(file, newParent / relative))) {
			throw std::runtime_error(
			    "This operation would break an external dependency:\n" +
			    PathUtils::ToUtf8(file));
		}
	}
}

} // namespace

std::vector<AssetUsage::Reference> AssetUsage::Find(
    std::span<const fs::path> targets, const fs::path &searchRoot,
    const Scene &scene, const fs::path &scenePath, const fs::path &startup,
    Paths *ordered, const EditorHistory *history) {
	std::vector<MoveEntry> items;

	for (const auto &target : targets) {
		const bool folder = fs::is_directory(target);
		if (folder) {
			ValidateFolderTree(target);
		}

		items.push_back({fs::canonical(target), {}, folder});
	}

	const auto owner = [&](const fs::path &path) -> std::size_t {
		if (!path.empty()) {
			for (std::size_t i = 0; i < items.size(); ++i) {
				const auto &item = items[i];
				if (item.folder ? RelativeReference(item.from, path).has_value()
				                : PathUtils::IsSameFile(item.from, path)) {
					return i;
				}
			}
		}
		return items.size();
	};
	std::vector<std::set<std::size_t>> edges(items.size());
	const auto record = [&](const fs::path &source, const fs::path &target) {
		const auto from = owner(source);
		if (from == items.size()) {
			return false;
		}

		const auto to = owner(target);
		if (ordered && from != to && to < items.size()) {
			edges[from].insert(to);
		}
		return true;
	};

	auto issues = Scan([&](const auto &path) { return owner(path) < items.size(); },
	                   searchRoot, scene, scenePath, startup, {}, {},
	                   items.size() > 1 ? ReferenceEdge(record) : ReferenceEdge{}, history);
	if (!ordered) {
		return issues;
	}
	ordered->clear();
	if (!issues.empty()) {
		return issues;
	}

	std::vector<std::size_t> incoming(items.size());
	for (const auto &targetsOfItem : edges) {
		for (const auto target : targetsOfItem) {
			++incoming[target];
		}
	}

	// 途中で停止しても、残った項目が先に移動した素材を参照しない順にする。
	while (ordered->size() < items.size()) {
		const auto next = std::ranges::find(incoming, std::size_t{0});
		if (next == incoming.end()) {
			ordered->clear();
			return {{"The selected items reference each other in a cycle. "
			         "Clear the references before moving them to the Recycle Bin."}};
		}

		const auto index = static_cast<std::size_t>(next - incoming.begin());
		ordered->push_back(items[index].from);
		*next = items.size();
		for (const auto target : edges[index]) {
			--incoming[target];
		}
	}

	return {};
}

AssetUsage::Paths AssetUsage::Duplicate(std::span<const fs::path> sources,
                                        const fs::path &assetRoot,
                                        std::string &error) {
	return CopyTo(sources, {}, assetRoot, {}, error);
}

AssetUsage::Paths AssetUsage::CopyTo(std::span<const fs::path> sources,
                                    const fs::path &destination,
                                    const fs::path &assetRoot,
                                    const fs::path &projectRoot,
                                    std::string &error) {
	std::vector<std::unique_ptr<OutputDirectory>> outputs;

	try {
		if (!assetRoot.is_absolute() || sources.empty()) {
			throw std::runtime_error("Choose items inside editable Assets.");
		}

		const auto root = fs::canonical(assetRoot);
		fs::path directory;
		Paths originals, copies;

		if (!destination.empty()) {
			if (!destination.is_absolute() ||
			    destination.native().find(L'\0') != std::wstring::npos) {
				throw std::runtime_error("Choose a destination folder inside Assets.");
			}

			directory = fs::canonical(destination);
			if (!fs::is_directory(directory) ||
			    PathUtils::RelativeInside(root, destination.lexically_normal()) !=
			        PathUtils::RelativeInside(root, directory)) {
				throw std::runtime_error("Choose an ordinary destination folder.");
			}
		}

		for (const auto &source : sources) {
			originals.push_back(EditableAsset(source, root));
		}

		for (const auto &source : originals) {
			if (fs::is_directory(source) &&
			    std::ranges::any_of(originals, [&](const auto &other) {
				    return other != source && RelativeWithin(source, other).has_value();
			    })) {
				throw std::runtime_error("Select a folder or its contents, not both.");
			}
		}

		for (const auto &source : originals) {
			const bool folder = fs::is_directory(source);
			if (folder && !directory.empty() && RelativeWithin(source, directory)) {
				throw std::runtime_error("Choose a destination outside the source folder.");
			}

			copies.push_back(DuplicateDestination(source, folder, directory, copies));
		}

		const auto map = [&](const fs::path &path) -> fs::path {
			for (std::size_t i = 0; i < originals.size(); ++i) {
				if (fs::is_directory(originals[i])) {
					const auto relative = RelativeReference(originals[i], path);
					if (relative) {
						return relative->empty() ? copies[i] : copies[i] / *relative;
					}
				} else if (PathUtils::IsSameFile(originals[i], path)) {
					return copies[i];
				}
			}
			return {};
		};

		for (std::size_t i = 0; i < originals.size(); ++i) {
			const auto &source = originals[i];
			const auto &target = copies[i];
			const bool rebase = !directory.empty() &&
			                    !PathUtils::IsSameFile(source.parent_path(), target.parent_path());
			outputs.push_back(std::make_unique<OutputDirectory>());
			CopyAsset(source, target, *outputs.back(),
			          [&](const fs::path &file, const fs::path &relative,
			              OutputDirectory &output) {
				          const auto copiedFile = target.parent_path() / relative;
				          if (PathUtils::IsModelFile(file)) {
					          if (rebase) {
						          CheckModelRelocation(file, map, true);
					          }
				          } else if (PrefabSerializer::IsPrefabPath(file)) {
					          Scene prefab;
					          SceneObjectId nextId = 0, prefabRoot = 0;
					          std::string text, issue;
					          if (!PrefabSerializer::Load(file, prefab, issue, &nextId)) {
						          if (!rebase) {
							          output.Copy(file, relative);
							          return;
						          }
						          throw std::runtime_error("Could not copy Prefab: " + issue);
					          }
					          if (Rewrite(prefab, map) || rebase) {
						          if (!PrefabSerializer::Validate(prefab, prefabRoot, issue) ||
						              !PrefabSerializer::Serialize(prefab, prefabRoot,
						                                           copiedFile, text, issue, nextId)) {
							          throw std::runtime_error("Could not copy Prefab: " + issue);
						          }
						          output.Text(relative, text);
						          return;
					          }
				          } else if (PathUtils::HasExtension(file, ".json") && IsSceneFile(file)) {
					          Scene scene;
					          fs::path savedRoot;
					          std::string text;
					          if (!SceneSerializer::Deserialize(scene, ReadText(file), file, &savedRoot)) {
						          if (!rebase) {
							          output.Copy(file, relative);
							          return;
						          }
						          throw std::runtime_error("Could not copy scene: " + PathUtils::ToUtf8(file));
					          }
					          if (Rewrite(scene, map) || rebase) {
						          if (!SceneSerializer::SerializeForFile(
					                  scene, text, copiedFile,
					                  savedRoot.empty() ? projectRoot : savedRoot)) {
							          throw std::runtime_error("Could not copy scene: " + PathUtils::ToUtf8(file));
						          }
						          output.Text(relative, text);
						          return;
					          }
				          }

				          output.Copy(file, relative);
			          });
		}

		for (auto &output : outputs) {
			output->Complete();
		}

		error.clear();
		return copies;
	} catch (const std::exception &failure) {
		error = failure.what();

		for (auto it = outputs.rbegin(); it != outputs.rend(); ++it) {
			error += (*it)->Cleanup();
		}

		return {};
	}
}

bool AssetUsage::Relocate(const Moves &moves, const fs::path &assetRoot,
                          SceneDocument &document, ProjectDocument &project,
                          std::string &error) {
	std::vector<Edit> edits;
	std::vector<MoveEntry> items;
	std::size_t applied = 0, moved = 0;

	try {
		if (!assetRoot.is_absolute() || moves.empty()) {
			throw std::runtime_error("Choose items inside editable Assets.");
		}

		const auto assets = fs::canonical(assetRoot);

		for (const auto &[source, destination] : moves) {
			if (!destination.is_absolute() ||
			    destination.native().find(L'\0') != std::wstring::npos ||
			    !PathUtils::IsValidFileName(destination.filename())) {
				throw std::runtime_error("Choose a valid asset destination.");
			}

			auto from = EditableAsset(source, assets);
			const bool folder = fs::is_directory(from);
			const auto parent = fs::canonical(destination.parent_path());
			PathUtils::RelativeInside(assets, parent);

			if (!fs::is_directory(parent) ||
			    PathUtils::RelativeInside(assets, destination.parent_path().lexically_normal()) !=
			        PathUtils::RelativeInside(assets, parent)) {
				throw std::runtime_error("Choose an ordinary destination folder.");
			}

			auto to = parent / destination.filename();
			if (from == to) {
				continue;
			}

			if (folder) {
				ValidateFolderTree(from);
			} else {
				if (!PathUtils::HasExtension(to, PathUtils::ToUtf8(from.extension()))) {
					throw std::runtime_error("Keep the original file extension.");
				}

				for (const auto suffix : {".scene", ".prefab", ".settings", ".clip"}) {
					if (PathUtils::HasExtension(from.stem(), suffix) &&
					    !PathUtils::HasExtension(to.stem(), suffix)) {
						throw std::runtime_error("Keep the original file type suffix.");
					}
				}
			}

			if (fs::exists(fs::symlink_status(to)) ||
			    std::ranges::any_of(items, [&](const auto &item) {
				    return PathUtils::IsSameFile(item.to, to);
			    })) {
				throw std::runtime_error("A file or folder already uses that name: " +
				                         PathUtils::ToUtf8(to));
			}

			items.push_back({std::move(from), std::move(to), folder});
		}

		for (const auto &item : items) {
			for (const auto &other : items) {
				if ((&item != &other && PathUtils::IsSameFile(item.from, other.from)) ||
				    (other.folder &&
				     (RelativeWithin(other.from, item.to.parent_path()) ||
				      (&item != &other && RelativeWithin(other.from, item.from))))) {
					throw std::runtime_error("A selected folder contains another source or the destination.");
				}
			}
		}

		if (items.empty()) {
			error.clear();
			return true;
		}

		const auto map = [&](const fs::path &path) -> fs::path {
			if (path.empty()) {
				return {};
			}

			for (const auto &item : items) {
				if (item.folder) {
					const auto relative = RelativeReference(item.from, path);
					if (relative) {
						return relative->empty() ? item.to : item.to / *relative;
					}
				} else if (PathUtils::IsSameFile(item.from, path)) {
					return item.to;
				}
			}

			return {};
		};
		const auto checkModel = [&](const fs::path &model) {
			CheckModelRelocation(model, map, false);
		};

		const auto &projectRoot = project.GetRoot();
		const auto &root = projectRoot.empty() ? assets : projectRoot;
		const auto startup = project.GetStartupScene();
		const auto &scenePath = document.GetFilePath();
		auto movedOpen = map(scenePath);
		const auto movedStartup = map(startup);
		Scene live;
		live.CopyFrom(document.GetScene());
		Rewrite(live, map);
		auto history = document.GetHistory().PrepareAssetRewrite(
		    [&](SceneObject &object) { RewriteObject(object, map); });
		std::string liveSavedText;
		bool refreshSavedText = false;

		const auto issues = Scan(
		    [](const auto &) { return false; }, root, document.GetScene(),
		    scenePath, startup,
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
				error += "\n" + issue.message;
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

		for (const auto &item : items) {
			if (!MoveFileW(item.from.c_str(), item.to.c_str())) {
				throw std::runtime_error(std::format(
				    "Could not move {} (Windows error {}).",
				    PathUtils::ToUtf8(item.from), GetLastError()));
			}

			++moved;
		}

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
		                          refreshSavedText ? &liveSavedText : nullptr,
		                          *history);
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

	while (moved != 0) {
		const auto &item = items[--moved];
		if (!MoveFileW(item.to.c_str(), item.from.c_str())) {
			error += "\nCould not restore the original path. Item is at " +
			         PathUtils::ToUtf8(item.to);
		}
	}

	return false;
}
