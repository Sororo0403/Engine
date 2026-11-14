#include "settings/EditorPreferences.h"

#include "core/Logger.h"
#include "platform/windows/FileUtils.h"
#include "platform/windows/PathUtils.h"
#include "settings/RecentScenes.h"

#include <json.hpp>

#include <algorithm>
#include <climits>
#include <exception>
#include <format>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace {
using ViewState = EditorCameraController::ViewState;
constexpr std::size_t kMaximumRecentProjects = 8;
constexpr std::size_t kMaximumRecentGameExports = 8;

/// <summary>
/// 保存した位置と向きを読み、Sceneビューで復元できるか検証する
/// </summary>
std::optional<ViewState> ReadCameraView(const nlohmann::json &json) {
	if (!json.is_object()) {
		return std::nullopt;
	}

	const auto position = json.find("position");
	const auto yaw = json.find("yaw");
	const auto pitch = json.find("pitch");

	if (position == json.end() || !position->is_array() ||
	    position->size() != 3 ||
	    !std::ranges::all_of(*position, [](const auto &value) {
		    return value.is_number();
	    }) ||
	    yaw == json.end() || !yaw->is_number() ||
	    pitch == json.end() || !pitch->is_number()) {
		return std::nullopt;
	}

	const auto &values = *position;
	ViewState view{{values[0].get<float>(), values[1].get<float>(),
	                values[2].get<float>()},
	               yaw->get<float>(), pitch->get<float>()};
	EditorCameraController validator;
	return validator.SetViewState(view) ? std::optional<ViewState>{view}
	                                    : std::nullopt;
}

/// <summary>
/// 位置と向きをカメラ設定用のJSONに変換する
/// </summary>
nlohmann::json CameraViewJson(const ViewState &view) {
	return {{"position", {view.position.x, view.position.y, view.position.z}},
	        {"yaw", view.yaw},
	        {"pitch", view.pitch}};
}

/// <summary>
/// 保存済みシーンの絶対パスを、視点の記録に使うパスへ揃える
/// </summary>
std::filesystem::path SceneViewKey(const std::filesystem::path &path,
                                  bool allowMissing = false) {
	if (!path.is_absolute() || path.native().find(L'\0') != std::wstring::npos) {
		return {};
	}

	std::error_code error;
	auto key = std::filesystem::canonical(path, error);
	if (allowMissing && error == std::errc::no_such_file_or_directory) {
		key = path.lexically_normal();
		return key.make_preferred();
	}
	return !error && std::filesystem::is_regular_file(key, error)
	           ? key
	           : std::filesystem::path{};
}

bool IsProjectScenePath(const std::filesystem::path &path) {
	return !path.empty() && !path.has_root_path() &&
	       PathUtils::HasExtension(path, ".json") &&
	       std::ranges::all_of(path, PathUtils::IsValidFileName);
}

/// <summary>
/// Sceneとブックマークに共通する投影設定を読み、数値型と範囲を検証する
/// </summary>
/// <param name="json">縦視野角とNear／Far Clipを持つJSONオブジェクト</param>
/// <returns>有効な投影設定。形式や値が無効な場合は例外</returns>
CameraSettings ReadProjection(const nlohmann::json &json) {
	if (!json.is_object() || !json.at("verticalFovDegrees").is_number() ||
	    !json.at("nearClip").is_number() || !json.at("farClip").is_number()) {
		throw std::runtime_error("Sceneカメラの投影設定には数値が必要です。");
	}

	CameraSettings settings{json.at("verticalFovDegrees").get<float>(),
	                        json.at("nearClip").get<float>(),
	                        json.at("farClip").get<float>()};
	if (!settings.IsValid()) {
		throw std::runtime_error("Sceneカメラの投影設定が無効です。");
	}

	return settings;
}

/// <summary>
/// 検証済みの投影設定を保存用のJSONオブジェクトへ変換する
/// </summary>
/// <param name="settings">保存前にIsValidを確認した投影設定</param>
/// <returns>縦視野角とNear／Far Clipを持つオブジェクト</returns>
nlohmann::json ProjectionJson(const CameraSettings &settings) {
	return {{"verticalFovDegrees", settings.verticalFovDegrees},
	        {"nearClip", settings.nearClip},
	        {"farClip", settings.farClip}};
}
} // namespace

void EditorPreferences::RememberSceneView(const std::filesystem::path &scene,
                                          const ViewState &view) {
	auto key = SceneViewKey(scene);
	EditorCameraController validator;

	if (!key.empty() && validator.SetViewState(view)) {
		sceneViews.insert_or_assign(std::move(key), view);
	}
}

const ViewState *
EditorPreferences::FindSceneView(const std::filesystem::path &scene) const {
	const auto found = sceneViews.find(SceneViewKey(scene, true));
	return found == sceneViews.end() ? nullptr : &found->second;
}

void EditorPreferences::RememberSceneSelection(
    const std::filesystem::path &scene, std::span<const SceneObjectId> selection) {
	const auto key = SceneViewKey(scene);
	if (key.empty()) {
		return;
	}

	if (selection.empty()) {
		sceneSelections_.erase(key);
	} else {
		sceneSelections_.insert_or_assign(
		    key, std::vector<SceneObjectId>(selection.begin(), selection.end()));
	}
}

std::span<const SceneObjectId>
EditorPreferences::LastSceneSelection(const std::filesystem::path &scene) const {
	const auto found = sceneSelections_.find(SceneViewKey(scene, true));
	return found == sceneSelections_.end() ? std::span<const SceneObjectId>{}
	                                       : std::span<const SceneObjectId>{found->second};
}

void EditorPreferences::CopySceneViewAndSelection(
    const std::filesystem::path &source, const std::filesystem::path &destination) {
	const auto key = SceneViewKey(destination, true);
	if (key.empty() || sceneViews.contains(key) || sceneSelections_.contains(key)) {
		return;
	}

	if (const auto *view = FindSceneView(source)) {
		sceneViews.try_emplace(key, *view);
	}
	const auto selection = LastSceneSelection(source);
	if (!selection.empty()) {
		sceneSelections_.try_emplace(key, selection.begin(), selection.end());
	}
}

void EditorPreferences::RelocateScenes(
    std::span<const std::pair<std::filesystem::path, std::filesystem::path>> moves) {
	namespace fs = std::filesystem;
	const auto relocate = [&](const fs::path &scene) {
		const auto moved = PathUtils::AfterMove(scene, moves);
		if (moved != scene) {
			const auto target = SceneViewKey(moved);
			if (!target.empty()) {
				return target;
			}
		}
		return scene;
	};

	const auto relocateKeys = [&](auto &entries) {
		auto before = entries;
		for (auto &[path, value] : before) {
			const auto target = relocate(path);
			if (target != path) {
				entries.erase(path);
				entries.insert_or_assign(target, std::move(value));
			}
		}
	};
	relocateKeys(sceneViews);
	relocateKeys(sceneSelections_);

	RecentScenes recent;
	for (auto it = recentScenes.rbegin(); it != recentScenes.rend(); ++it) {
		recent.Add(relocate(*it));
	}
	const auto paths = recent.GetPaths();
	recentScenes.assign(paths.begin(), paths.end());

	for (auto &[root, path] : projectScenes) {
		const auto original = root / path;
		const auto target = relocate(original);
		if (target != original) {
			try {
				path = PathUtils::RelativeInside(root, target);
			} catch (const std::runtime_error &) {
				// プロジェクト外への移動はプロジェクト内の履歴へ登録しない。
			}
		}
	}
	for (auto &[key, sources] : exportSceneSources_) {
		for (auto &source : sources) {
			source.sourcePath = PathUtils::AfterMove(source.sourcePath, moves);
		}
	}
}

void EditorPreferences::RememberProject(const std::filesystem::path &root) {
	if (!root.is_absolute() || root.native().find(L'\0') != std::wstring::npos) {
		return;
	}

	auto normalized = root.lexically_normal();
	if (!recentProjects.empty() && recentProjects.front() == normalized) {
		return;
	}

	std::erase_if(recentProjects, [&](const auto &path) {
		return PathUtils::IsSameFile(path, normalized);
	});
	recentProjects.insert(recentProjects.begin(), std::move(normalized));
	if (recentProjects.size() > kMaximumRecentProjects) {
		recentProjects.resize(kMaximumRecentProjects);
	}
}

bool EditorPreferences::RemoveMissingProjects(const std::filesystem::path &activeRoot) {
	const auto missing = [&](const std::filesystem::path &root) {
		if (root.empty() ||
		    (!activeRoot.empty() && PathUtils::IsSameFile(root, activeRoot))) {
			return false;
		}

		std::error_code error;
		const auto folder = std::filesystem::symlink_status(root, error);
		if (folder.type() == std::filesystem::file_type::not_found) {
			return true;
		}
		if (!std::filesystem::is_directory(folder)) {
			return false;
		}

		return std::filesystem::symlink_status(root / L"project.json", error).type() ==
		       std::filesystem::file_type::not_found;
	};

	const bool removed = std::erase_if(recentProjects, missing) != 0;
	if (missing(lastProject)) {
		lastProject.clear();
		return true;
	}
	return removed;
}

void EditorPreferences::RememberGameExport(
    const std::filesystem::path &root, const std::filesystem::path &directory) {
	if (RememberOpenedGameExport(root, directory))
		lastGameExports_.insert_or_assign(root.lexically_normal(),
		                                  directory.lexically_normal());
}

void EditorPreferences::RememberExportSceneSources(
    const std::filesystem::path &root, const std::filesystem::path &directory,
    std::vector<GameExporter::SceneSource> sources) {
	const auto key = std::pair(root.lexically_normal(), directory.lexically_normal());
	const auto recent = RecentGameExports(root);
	const auto last = LastGameExport(root);
	if (!key.second.is_absolute() ||
	    (std::ranges::find(recent, key.second) == recent.end() && last != key.second)) {
		return;
	}
	std::erase_if(sources, [](const auto &source) {
		return source.packagedPath.empty() || source.packagedPath.has_root_path() ||
		       !PathUtils::HasExtension(source.packagedPath, ".json") ||
		       !std::ranges::all_of(source.packagedPath, PathUtils::IsValidFileName) ||
		       !source.sourcePath.is_absolute() || !PathUtils::HasExtension(source.sourcePath, ".json") ||
		       source.sourcePath.native().find(L'\0') != std::wstring::npos;
	});
	for (auto &source : sources) {
		source.packagedPath.make_preferred();
		source.sourcePath.make_preferred();
	}
	std::ranges::sort(sources, {}, &GameExporter::SceneSource::packagedPath);
	if (sources.empty()) {
		exportSceneSources_.erase(key);
	} else {
		exportSceneSources_.insert_or_assign(key, std::move(sources));
	}
	std::erase_if(exportSceneSources_, [&](const auto &entry) {
		const auto &[project, output] = entry.first;
		return project == key.first && output != last &&
		       std::ranges::find(recent, output) == recent.end();
	});
}

std::vector<GameExporter::SceneSource> EditorPreferences::ExportSceneSources(
    const std::filesystem::path &root, const std::filesystem::path &directory) const {
	const auto found = exportSceneSources_.find({root.lexically_normal(), directory.lexically_normal()});
	return found == exportSceneSources_.end() ? std::vector<GameExporter::SceneSource>{} : found->second;
}

bool EditorPreferences::RememberOpenedGameExport(
    const std::filesystem::path &root, const std::filesystem::path &directory,
    const std::filesystem::path &previousDirectory) {
	if ((!root.empty() && !root.is_absolute()) ||
	    root.native().find(L'\0') != std::wstring::npos ||
	    !directory.is_absolute() ||
	    directory.native().find(L'\0') != std::wstring::npos) {
		return false;
	}

	auto normalized = directory.lexically_normal();
	const auto previous = previousDirectory.lexically_normal();
	const auto key = root.lexically_normal();
	auto &recent = recentGameExports_[key];
	if (!previous.empty()) {
		std::erase(recent, previous);
		const auto last = lastGameExports_.find(key);
		if (last != lastGameExports_.end() && last->second == previous) {
			last->second = normalized;
		}
	}
	std::erase(recent, normalized);
	recent.insert(recent.begin(), std::move(normalized));
	if (recent.size() > kMaximumRecentGameExports) {
		recent.resize(kMaximumRecentGameExports);
	}
	return true;
}

std::filesystem::path EditorPreferences::LastGameExport(
    const std::filesystem::path &root) const {
	const auto last = lastGameExports_.find(root.lexically_normal());
	return last == lastGameExports_.end() ? std::filesystem::path{} : last->second;
}

std::span<const std::filesystem::path> EditorPreferences::RecentGameExports(
    const std::filesystem::path &root) const {
	const auto found = recentGameExports_.find(root.lexically_normal());
	if (found == recentGameExports_.end()) {
		return {};
	}
	return found->second;
}

bool EditorPreferences::RemoveMissingGameExports(const std::filesystem::path &root) {
	const auto key = root.lexically_normal();
	const auto missing = [](const std::filesystem::path &directory) {
		std::error_code error;
		return std::filesystem::status(directory, error).type() ==
		       std::filesystem::file_type::not_found;
	};
	bool changed = false;
	if (const auto recent = recentGameExports_.find(key); recent != recentGameExports_.end()) {
		changed = std::erase_if(recent->second, missing) != 0;
		if (recent->second.empty())
			recentGameExports_.erase(recent);
	}
	if (const auto last = lastGameExports_.find(key);
	    last != lastGameExports_.end() && missing(last->second)) {
		lastGameExports_.erase(last);
		changed = true;
	}
	changed |= std::erase_if(exportSceneSources_, [&](const auto &entry) {
		return entry.first.first == key && missing(entry.first.second);
	}) != 0;
	return changed;
}

void EditorPreferences::RememberProjectScene(const std::filesystem::path &root,
                                              const std::filesystem::path &scene) {
	if (root.empty() || scene.empty()) {
		return;
	}

	std::error_code error;
	const auto resolved = std::filesystem::canonical(scene, error);

	if (!error && std::filesystem::is_regular_file(resolved, error)) {
		try {
			auto relative = PathUtils::RelativeInside(root, resolved);

			if (IsProjectScenePath(relative)) {
				projectScenes.insert_or_assign(root, std::move(relative));
			}
		} catch (const std::runtime_error &) {
			// プロジェクト外のシーンは、再開先の記録を変更しない。
		}
	}
}

void EditorPreferences::RememberProjectAssetFolder(
    const std::filesystem::path &root, const std::filesystem::path &folder) {
	if (!root.is_absolute() || root.native().find(L'\0') != std::wstring::npos ||
	    folder.has_root_path() ||
	    !std::ranges::all_of(folder, PathUtils::IsValidFileName)) {
		return;
	}

	projectAssetFolders_.insert_or_assign(root.lexically_normal(), folder);
}

std::filesystem::path EditorPreferences::LastProjectAssetFolder(
    const std::filesystem::path &root, const std::filesystem::path &previousRoot) const {
	auto found = projectAssetFolders_.find(root.lexically_normal());
	if (found == projectAssetFolders_.end() && !previousRoot.empty()) {
		found = projectAssetFolders_.find(previousRoot.lexically_normal());
	}
	return found == projectAssetFolders_.end() ? std::filesystem::path{}
	                                           : found->second;
}

std::filesystem::path EditorPreferences::LastProjectScene(
    const std::filesystem::path &root, std::filesystem::path *unavailable,
    const std::filesystem::path &previousRoot) const {
	if (unavailable) {
		unavailable->clear();
	}
	auto saved = projectScenes.find(root);
	if (saved == projectScenes.end() && !previousRoot.empty()) {
		saved = projectScenes.find(previousRoot);
	}

	if (saved == projectScenes.end() || !IsProjectScenePath(saved->second)) {
		return {};
	}

	const auto remembered = root / saved->second;
	std::error_code error;
	const auto path = std::filesystem::canonical(remembered, error);

	if (error || !std::filesystem::is_regular_file(path, error)) {
		if (unavailable) {
			*unavailable = remembered;
		}
		return {};
	}

	try {
		PathUtils::RelativeInside(root, path);
		return path;
	} catch (const std::runtime_error &) {
		if (unavailable) {
			*unavailable = remembered;
		}
		return {};
	}
}

bool EditorPreferences::Load(const std::filesystem::path &filePath) {
	try {
		const auto path = PathUtils::ResolveFromExecutable(filePath);

		if (!std::filesystem::exists(path)) {
			return true;
		}

		std::ifstream input(path, std::ios::binary);

		if (!input) {
			throw std::runtime_error("Editor設定ファイルを開けませんでした。");
		}

		input.exceptions(std::ios::badbit);
		const auto json = nlohmann::json::parse(input);

		if (json.at("version") != 1 ||
		    !json.at("version").is_number_integer()) {
			throw std::runtime_error("未対応のEditor設定形式です。");
		}

		EditorPreferences loaded;
		loaded.showGrid = json.at("showGrid").get<bool>();
		loaded.showSelectionBounds = json.at("showSelectionBounds").get<bool>();
		// 補助線の表示設定がない旧ファイルでは、従来どおり表示する。
		loaded.showCameraGuides = json.value("showCameraGuides", true);
		loaded.showRespawnGuides = json.value("showRespawnGuides", true);
		loaded.showJumpGuide = json.value("showJumpGuide", false);
		loaded.showMotionTrail = json.value("showMotionTrail", false);
		loaded.maximizeGameOnPlay = json.value("maximizeGameOnPlay", false);
		loaded.startPlayPaused = json.value("startPlayPaused", false);
		loaded.searchProjectScenes = json.value("searchProjectScenes", false);
		loaded.showObjectMarkers = json.value("showObjectMarkers", true);
		loaded.showMeasurements = json.value("showMeasurements", false);

		// 旧設定には従来と同じ選択物体のみの表示を適用する。
		if (json.contains("colliderDisplay")) {
			const auto mode = json.at("colliderDisplay").get<std::string>();

			if (mode == "none") {
				loaded.colliderDisplay = ColliderBounds::Mode::NONE;
			} else if (mode == "selected") {
				loaded.colliderDisplay = ColliderBounds::Mode::SELECTED;
			} else if (mode == "all") {
				loaded.colliderDisplay = ColliderBounds::Mode::ALL;
			} else {
				throw std::runtime_error("衝突形状の表示範囲が無効です。");
			}
		}

		loaded.snapping = json.at("snapping").get<bool>();
		const auto tool = json.at("tool").get<std::string>();

		if (tool == "move") {
			loaded.operation = TransformGizmo::Operation::TRANSLATE;
		} else if (tool == "rotate") {
			loaded.operation = TransformGizmo::Operation::ROTATE;
		} else if (tool == "scale") {
			loaded.operation = TransformGizmo::Operation::SCALE;
		} else {
			throw std::runtime_error("Editor設定のツールが無効です。");
		}

		const auto &move = json.at("moveStep");

		if (json.contains("pivotMode")) {
			const auto name = json.at("pivotMode").get<std::string>();

			if (name != "center" && name != "active") {
				throw std::runtime_error("Editor設定の支点が無効です。");
			}

			loaded.pivotMode = name == "active" ? SelectionPivot::Mode::ACTIVE
			                                    : SelectionPivot::Mode::CENTER;
		}

		if (json.contains("space")) {
			const auto spaceName = json.at("space").get<std::string>();

			if (spaceName != "local" && spaceName != "world") {
				throw std::runtime_error("Editor設定の座標系が無効です。");
			}

			loaded.space = spaceName == "world" ? TransformGizmo::Space::WORLD
			                                    : TransformGizmo::Space::LOCAL;
		}

		if (!move.is_array() || move.size() != 3) {
			throw std::runtime_error("移動スナップには3成分が必要です。");
		}

		loaded.snap.translation = {move.at(0).get<float>(),
		                           move.at(1).get<float>(),
		                           move.at(2).get<float>()};
		loaded.snap.rotationDegrees =
		    json.at("rotationStepDegrees").get<float>();
		loaded.snap.scaleRatio = json.at("scaleStepRatio").get<float>();
		TransformGizmo validator;

		if (!validator.SetSnapSettings(loaded.snap)) {
			throw std::runtime_error("Editor設定のスナップ間隔が無効です。");
		}

		// カメラ設定がない旧設定ファイルには初期値を使用する
		if (json.contains("camera")) {
			const auto &settings = json.at("camera");
			loaded.camera.moveSpeed = settings.at("moveSpeed").get<float>();
			loaded.camera.fastMultiplier =
			    settings.at("fastMultiplier").get<float>();
			loaded.camera.mouseSensitivity =
			    settings.at("mouseSensitivity").get<float>();

			if (settings.contains("wheelDistance")) {
				loaded.camera.wheelDistance =
				    settings.at("wheelDistance").get<float>();
			}

			if (settings.contains("panSensitivity")) {
				loaded.camera.panSensitivity =
				    settings.at("panSensitivity").get<float>();
			}
		}

		// 投影設定がない旧ファイルでは、従来のSceneビューの初期値を使う。
		if (json.contains("sceneProjection")) {
			loaded.sceneProjection = ReadProjection(json.at("sceneProjection"));
		}

		EditorCameraController cameraValidator;

		if (!cameraValidator.SetSettings(loaded.camera)) {
			throw std::runtime_error("Editor設定のカメラ操作設定が無効です。");
		}

		if (json.contains("recentScenes")) {
			const auto &paths = json.at("recentScenes");

			if (!paths.is_array()) {
				throw std::runtime_error(
				    "最近開いたシーンには配列が必要です。");
			}

			RecentScenes recent;
			const auto count =
			    (std::min)(paths.size(), RecentScenes::kMaximumCount);

			for (std::size_t i = count; i > 0; --i) {
				if (!recent.Add(PathUtils::FromUtf8(
				        paths.at(i - 1).get<std::string>()))) {
					throw std::runtime_error(
					    "最近開いたシーンのパスが無効です。");
				}
			}

			const auto normalized = recent.GetPaths();
			loaded.recentScenes.assign(normalized.begin(), normalized.end());
		}

		if (json.contains("lastProject") && json.at("lastProject").is_string()) {
			const auto projectPath =
			    PathUtils::FromUtf8(json.at("lastProject").get<std::string>());

			if (projectPath.is_absolute() &&
			    projectPath.native().find(L'\0') == std::wstring::npos) {
				loaded.lastProject = projectPath.lexically_normal();
			}
		}

		const auto recentProjectPaths = json.find("recentProjects");
		if (recentProjectPaths != json.end() && recentProjectPaths->is_array()) {
			const auto count =
			    (std::min)(recentProjectPaths->size(), kMaximumRecentProjects);
			for (std::size_t i = count; i > 0; --i) {
				const auto &entry = (*recentProjectPaths)[i - 1];
				if (entry.is_string()) {
					const auto &text = entry.get_ref<const std::string &>();
					if (text.find('\0') == std::string::npos) {
						loaded.RememberProject(PathUtils::FromUtf8(text));
					}
				}
			}
		} else {
			loaded.RememberProject(loaded.lastProject);
		}

		const auto exports = json.find("lastGameExports");
		if (exports != json.end() && exports->is_object()) {
			for (const auto &[key, value] : exports->items()) {
				if (!value.is_string() || key.find('\0') != std::string::npos) {
					continue;
				}
				const auto &directory = value.get_ref<const std::string &>();
				if (directory.find('\0') == std::string::npos) {
					loaded.RememberGameExport(PathUtils::FromUtf8(key),
					                          PathUtils::FromUtf8(directory));
				}
			}
		}

		if (const auto drafts = json.find("gameExportDrafts");
		    drafts != json.end() && drafts->is_object()) {
			for (const auto &[key, value] : drafts->items()) {
				if (key.find('\0') != std::string::npos || !value.is_object()) {
					continue;
				}
				const auto root = PathUtils::FromUtf8(key);
				const auto scene = value.find("scene");
				const auto output = value.find("output");
				const auto title = value.find("title");
				const auto width = value.find("width");
				const auto height = value.find("height");
				const auto recheck = value.find("recheck");
				if (!root.is_absolute() || scene == value.end() || !scene->is_string() ||
				    output == value.end() || !output->is_string() ||
				    title == value.end() || !title->is_string() ||
				    width == value.end() || !width->is_number_integer() ||
				    height == value.end() || !height->is_number_integer()) {
					continue;
				}
				const auto draftWidth = width->get<double>();
				const auto draftHeight = height->get<double>();
				if (draftWidth < INT_MIN || draftWidth > INT_MAX ||
				    draftHeight < INT_MIN || draftHeight > INT_MAX) {
					continue;
				}
				const auto &sceneText = scene->get_ref<const std::string &>();
				if (sceneText.find('\0') != std::string::npos) {
					continue;
				}
				auto startupPath = PathUtils::FromUtf8(sceneText);
				if (startupPath.is_absolute()) {
					loaded.gameExportDrafts.emplace(
					    root.lexically_normal(), GameExportDraft{std::move(startupPath),
					        output->get<std::string>(), title->get<std::string>(),
					        static_cast<int>(draftWidth), static_cast<int>(draftHeight),
					        recheck != value.end() && recheck->is_boolean() && recheck->get<bool>()});
				}
			}
		}

		const auto recentExports = json.find("recentGameExports");
		if (recentExports != json.end() && recentExports->is_object()) {
			for (const auto &[key, values] : recentExports->items()) {
				if (!values.is_array() || key.find('\0') != std::string::npos) {
					continue;
				}
				const auto root = PathUtils::FromUtf8(key);
				for (auto it = values.rbegin(); it != values.rend(); ++it) {
					if (it->is_string()) {
						const auto &directory = it->get_ref<const std::string &>();
						if (directory.find('\0') == std::string::npos) {
							loaded.RememberOpenedGameExport(root, PathUtils::FromUtf8(directory));
						}
					}
				}
			}
		}

		if (const auto savedSources = json.find("exportSceneSources");
		    savedSources != json.end() && savedSources->is_object()) {
			for (const auto &[project, outputs] : savedSources->items()) {
				if (!outputs.is_object() || project.find('\0') != std::string::npos) {
					continue;
				}
				const auto root = PathUtils::FromUtf8(project);
				for (const auto &[directory, entries] : outputs.items()) {
					if (!entries.is_object() || directory.find('\0') != std::string::npos) {
						continue;
					}
					std::vector<GameExporter::SceneSource> sources;
					for (const auto &[packaged, source] : entries.items()) {
						if (source.is_string() && packaged.find('\0') == std::string::npos) {
							sources.push_back({PathUtils::FromUtf8(packaged),
							                   PathUtils::FromUtf8(source.get_ref<const std::string &>())});
						}
					}
					loaded.RememberExportSceneSources(root, PathUtils::FromUtf8(directory), std::move(sources));
				}
			}
		}

		if (json.contains("projectScenes") && json.at("projectScenes").is_object()) {
			for (const auto &[key, value] : json.at("projectScenes").items()) {
				const auto root = PathUtils::FromUtf8(key);

				if (!root.is_absolute() ||
				    root.native().find(L'\0') != std::wstring::npos ||
				    !value.is_string()) {
					continue;
				}

				auto scene = PathUtils::FromUtf8(value.get<std::string>());

				if (IsProjectScenePath(scene)) {
					loaded.projectScenes.insert_or_assign(root.lexically_normal(),
					                                      std::move(scene));
				}
			}
		}

		const auto assetFolders = json.find("projectAssetFolders");
		if (assetFolders != json.end() && assetFolders->is_object()) {
			for (const auto &[key, value] : assetFolders->items()) {
				if (!value.is_string() || key.find('\0') != std::string::npos) {
					continue;
				}
				const auto &folder = value.get_ref<const std::string &>();
				if (folder.find('\0') == std::string::npos) {
					loaded.RememberProjectAssetFolder(PathUtils::FromUtf8(key),
					                                  PathUtils::FromUtf8(folder));
				}
			}
		}

		// グリッド設定がない旧ファイルでは初期値を使う
		if (json.contains("grid")) {
			const auto &settings = json.at("grid");
			const auto &count = settings.at("halfLineCount");
			const auto &major = settings.at("majorLineInterval");

			if (!count.is_number_integer() || count < 1 ||
			    count > SceneGrid::kMaximumHalfLineCount ||
			    !major.is_number_integer() || major < 1 ||
			    major > SceneGrid::kMaximumMajorLineInterval) {
				throw std::runtime_error("グリッドのセル数・周期が無効です。");
			}

			loaded.grid.spacing = settings.at("spacing").get<float>();
			loaded.grid.height = settings.value("height", 0.0f);
			loaded.grid.halfLineCount = count.get<int>();
			loaded.grid.majorLineInterval = major.get<int>();
		}

		SceneGrid gridValidator;

		if (!gridValidator.SetSettings(loaded.grid)) {
			throw std::runtime_error("グリッドの設定が無効です。");
		}

		// 出力設定がない旧ファイルでは初期値を使う
		if (json.contains("gameExport")) {
			const auto &settings = json.at("gameExport");
			const auto &width = settings.at("width");
			const auto &height = settings.at("height");

			if (!width.is_number_integer() || !height.is_number_integer() ||
			    width < 1 || width > 8192 || height < 1 || height > 8192) {
				throw std::runtime_error("ゲーム出力の画面サイズが無効です。");
			}

			loaded.gameExport.title = settings.at("title").get<std::string>();
			loaded.gameExport.width = width.get<int>();
			loaded.gameExport.height = height.get<int>();
			loaded.gameExport.cameraRelativeMovement =
			    settings.value("cameraRelativeMovement", false);
			loaded.gameExport.gamepadEnabled =
			    settings.value("gamepadEnabled", true);

			if (settings.contains("input")) {
				loaded.gameExport.input.arrowMovement =
				    settings.at("input").value("arrowMovement", false);

				for (const auto &action : GameInputBindings::kActions) {
					const auto key =
					    GameInputBindings::FromName(settings.at("input")
					                                    .at(action.name)
					                                    .get<std::string>());

					if (!key) {
						throw std::runtime_error(
						    "ゲームの操作キーが未対応です。");
					}

					loaded.gameExport.input.*action.member = *key;
				}
			}

			if (settings.contains("lighting") || settings.contains("actions")) {
				std::string text, error;

				if (!loaded.gameExport.Serialize(text, error)) {
					throw std::runtime_error(error);
				}

				auto configuration = nlohmann::json::parse(text);

				if (settings.contains("lighting")) {
					configuration["lighting"] = settings.at("lighting");
					if (!settings.at("lighting").contains("shadows"))
						configuration["version"] = 7;
				}

				if (settings.contains("actions")) {
					auto &actions = configuration["actions"];
					actions = settings.at("actions");
					// 旧Editor設定の操作にはゲームパッド軸の欄がない。
					if (actions.is_array()) {
						for (auto &action : actions)
							if (action.is_object() &&
							    !action.contains("gamepadAxis"))
								action["gamepadAxis"] = nullptr;
					}
				}

				if (!loaded.gameExport.Deserialize(configuration.dump(),
				                                   error)) {
					throw std::runtime_error(error);
				}
			}

			if (!loaded.gameExport.IsValid()) {
				throw std::runtime_error(
				    "ゲーム出力のタイトル・画面サイズが無効です。");
			}
		}

		if (json.contains("assetDirectories")) {
			const auto &directories = json.at("assetDirectories");

			if (!directories.is_array()) {
				throw std::runtime_error("素材の取得元には配列が必要です。");
			}

			for (const auto &directory : directories) {
				loaded.assets.Register(
				    PathUtils::FromUtf8(directory.get<std::string>()));
			}
		}

		if (json.contains("assetDirectory")) {
			loaded.assets.Select(PathUtils::FromUtf8(
			    json.at("assetDirectory").get<std::string>()));
		}

		if (json.contains("cameraBookmarks")) {
			const auto &bookmarks = json.at("cameraBookmarks");

			if (!bookmarks.is_array() ||
			    bookmarks.size() != EditorCameraBookmarks::kSlotCount) {
				throw std::runtime_error("カメラのブックマーク数が無効です。");
			}

			for (std::size_t i = 0; i < bookmarks.size(); ++i) {
				const auto &bookmark = bookmarks[i];

				if (bookmark.is_null()) {
					continue;
				}

				std::optional<CameraSettings> projection;

				if (bookmark.contains("projection")) {
					projection = ReadProjection(bookmark.at("projection"));
				}

				const auto view = ReadCameraView(bookmark);

				if (!view || !loaded.cameraBookmarks.Set(i, *view, projection)) {
					throw std::runtime_error(
					    "カメラのブックマークが無効です。");
				}
			}
		}

		if (json.contains("sceneViews") && json.at("sceneViews").is_object()) {
			for (const auto &[key, value] : json.at("sceneViews").items()) {
				const auto scene = PathUtils::FromUtf8(key);
				const auto view = ReadCameraView(value);

				if (scene.is_absolute() &&
				    scene.native().find(L'\0') == std::wstring::npos && view) {
					loaded.sceneViews.insert_or_assign(scene.lexically_normal(), *view);
				}
			}
		}

		if (json.contains("sceneSelections") && json.at("sceneSelections").is_object()) {
			for (const auto &[key, value] : json.at("sceneSelections").items()) {
				const auto scene = PathUtils::FromUtf8(key);
				if (!scene.is_absolute() || scene.native().find(L'\0') != std::wstring::npos ||
				    !value.is_array()) {
					continue;
				}

				std::vector<SceneObjectId> selection;
				std::unordered_set<SceneObjectId> seen;
				for (const auto &item : value) {
					if (!item.is_number_unsigned()) {
						continue;
					}
					const auto id = item.get<SceneObjectId>();
					if (id != 0 && seen.insert(id).second) {
						selection.push_back(id);
					}
				}
				if (!selection.empty()) {
					loaded.sceneSelections_.insert_or_assign(scene.lexically_normal(),
					                                         std::move(selection));
				}
			}
		}

		*this = std::move(loaded);
		return true;
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorPreferences::Load: {}", error.what()));
		return false;
	}
}

bool EditorPreferences::Save(const std::filesystem::path &filePath) const {
	try {
		TransformGizmo validator;
		EditorCameraController cameraValidator;
		SceneGrid gridValidator;

		if (!validator.SetSnapSettings(snap) ||
		    !validator.SetOperation(operation) || !validator.SetSpace(space) ||
		    !validator.SetPivotMode(pivotMode) ||
		    !cameraValidator.SetSettings(camera) ||
		    !sceneProjection.IsValid() || !cameraBookmarks.IsValid() ||
		    !gridValidator.SetSettings(grid) || !gameExport.IsValid() ||
		    (!lastProject.empty() &&
		     (!lastProject.is_absolute() ||
		      lastProject.native().find(L'\0') != std::wstring::npos)) ||
		    (colliderDisplay != ColliderBounds::Mode::NONE &&
		     colliderDisplay != ColliderBounds::Mode::SELECTED &&
		     colliderDisplay != ColliderBounds::Mode::ALL)) {
			throw std::runtime_error("保存するEditor設定が無効です。");
		}

		const auto path = PathUtils::ResolveFromExecutable(filePath);
		const char *tool =
		    operation == TransformGizmo::Operation::TRANSLATE ? "move"
		    : operation == TransformGizmo::Operation::ROTATE  ? "rotate"
		                                                      : "scale";
		RecentScenes recent;
		const auto count =
		    (std::min)(recentScenes.size(), RecentScenes::kMaximumCount);

		for (std::size_t i = count; i > 0; --i) {
			if (!recent.Add(recentScenes[i - 1])) {
				throw std::runtime_error(
				    "保存する最近のシーンのパスが無効です。");
			}
		}

		auto recentJson = nlohmann::json::array();

		for (const auto &recentPath : recent.GetPaths()) {
			recentJson.push_back(PathUtils::ToUtf8(recentPath));
		}

		nlohmann::json inputJson{{"arrowMovement", gameExport.input.arrowMovement}};

		for (const auto &action : GameInputBindings::kActions) {
			inputJson[action.name] =
			    GameInputBindings::GetName(gameExport.input.*action.member);
		}

		auto assetDirectories = nlohmann::json::array();

		for (const auto &directory : assets.GetDirectories()) {
			assetDirectories.push_back(PathUtils::ToUtf8(directory));
		}

		std::string gameText, gameError;

		if (!gameExport.Serialize(gameText, gameError)) {
			throw std::runtime_error(gameError);
		}

		const auto gameJson = nlohmann::json::parse(gameText);
		const auto lighting = gameJson.at("lighting");

		auto bookmarks = nlohmann::json::array();

		for (std::size_t i = 0; i < EditorCameraBookmarks::kSlotCount; ++i) {
			if (const auto *bookmark = cameraBookmarks.Get(i)) {
				auto saved = CameraViewJson(*bookmark);
				if (const auto *projection = cameraBookmarks.GetProjection(i)) {
					saved["projection"] = ProjectionJson(*projection);
				}

				bookmarks.push_back(std::move(saved));
			} else {
				bookmarks.push_back(nullptr);
			}
		}

		auto projectScenesJson = nlohmann::json::object();
		auto recentProjectsJson = nlohmann::json::array();
		for (const auto &root : recentProjects) {
			recentProjectsJson.push_back(PathUtils::ToUtf8(root));
		}
		for (const auto &[root, scene] : projectScenes) {
			projectScenesJson[PathUtils::ToUtf8(root)] = PathUtils::ToUtf8(scene);
		}

		auto lastGameExportsJson = nlohmann::json::object();
		for (const auto &[root, directory] : lastGameExports_) {
			lastGameExportsJson[PathUtils::ToUtf8(root)] = PathUtils::ToUtf8(directory);
		}
		auto gameExportDraftsJson = nlohmann::json::object();
		for (const auto &[root, draft] : gameExportDrafts) {
			gameExportDraftsJson[PathUtils::ToUtf8(root)] = {
			    {"scene", PathUtils::ToUtf8(draft.startupScenePath)},
			    {"output", draft.output}, {"title", draft.title},
			    {"width", draft.width}, {"height", draft.height}, {"recheck", draft.recheck}};
		}
		auto recentGameExportsJson = nlohmann::json::object();
		for (const auto &[root, directories] : recentGameExports_) {
			if (directories.empty()) {
				continue;
			}
			const auto key = PathUtils::ToUtf8(root);
			auto entries = nlohmann::json::array();
			for (const auto &directory : directories) {
				entries.push_back(PathUtils::ToUtf8(directory));
			}
			recentGameExportsJson[key] = std::move(entries);
		}
		auto projectAssetFoldersJson = nlohmann::json::object();
		auto exportSceneSourcesJson = nlohmann::json::object();
		for (const auto &[key, sources] : exportSceneSources_) {
			const auto &[root, directory] = key;
			auto &entries = exportSceneSourcesJson[PathUtils::ToUtf8(root)][PathUtils::ToUtf8(directory)];
			for (const auto &[packaged, source] : sources) {
				entries[PathUtils::ToUtf8(packaged)] = PathUtils::ToUtf8(source);
			}
		}
		for (const auto &[root, folder] : projectAssetFolders_) {
			projectAssetFoldersJson[PathUtils::ToUtf8(root)] = PathUtils::ToUtf8(folder);
		}

		auto sceneViewsJson = nlohmann::json::object();
		for (const auto &[scene, view] : sceneViews) {
			sceneViewsJson[PathUtils::ToUtf8(scene)] = CameraViewJson(view);
		}
		auto sceneSelectionsJson = nlohmann::json::object();
		for (const auto &[scene, selection] : sceneSelections_) {
			sceneSelectionsJson[PathUtils::ToUtf8(scene)] = selection;
		}

		const nlohmann::json json{
		    {"version", 1},
		    {"cameraBookmarks", bookmarks},
		    {"assetDirectory", PathUtils::ToUtf8(assets.GetDirectory())},
		    {"assetDirectories", assetDirectories},
		    {"gameExport",
		     {{"lighting", lighting},
		      {"actions", gameJson.at("actions")},
		      {"title", gameExport.title},
		      {"width", gameExport.width},
		      {"height", gameExport.height},
		      {"input", inputJson},
		      {"gamepadEnabled", gameExport.gamepadEnabled},
		      {"cameraRelativeMovement", gameExport.cameraRelativeMovement}}},
		    {"showGrid", showGrid},
		    {"showSelectionBounds", showSelectionBounds},
		    {"showCameraGuides", showCameraGuides},
		    {"showRespawnGuides", showRespawnGuides},
		    {"showJumpGuide", showJumpGuide},
		    {"showMotionTrail", showMotionTrail},
		    {"maximizeGameOnPlay", maximizeGameOnPlay},
		    {"startPlayPaused", startPlayPaused},
		    {"searchProjectScenes", searchProjectScenes},
		    {"showObjectMarkers", showObjectMarkers},
		    {"showMeasurements", showMeasurements},
		    {"colliderDisplay",
		     colliderDisplay == ColliderBounds::Mode::NONE  ? "none"
		     : colliderDisplay == ColliderBounds::Mode::ALL ? "all"
		                                                    : "selected"},
		    {"snapping", snapping},
		    {"tool", tool},
		    {"space",
		     space == TransformGizmo::Space::WORLD ? "world" : "local"},
		    {"pivotMode",
		     pivotMode == SelectionPivot::Mode::ACTIVE ? "active" : "center"},
		    {"moveStep",
		     {snap.translation.x, snap.translation.y, snap.translation.z}},
		    {"rotationStepDegrees", snap.rotationDegrees},
		    {"scaleStepRatio", snap.scaleRatio},
		    {"recentScenes", recentJson},
		    {"lastProject", PathUtils::ToUtf8(lastProject)},
		    {"recentProjects", std::move(recentProjectsJson)},
		    {"projectScenes", std::move(projectScenesJson)},
		    {"lastGameExports", std::move(lastGameExportsJson)},
		    {"gameExportDrafts", std::move(gameExportDraftsJson)},
		    {"recentGameExports", std::move(recentGameExportsJson)},
		    {"exportSceneSources", std::move(exportSceneSourcesJson)},
		    {"projectAssetFolders", std::move(projectAssetFoldersJson)},
		    {"sceneViews", std::move(sceneViewsJson)},
		    {"sceneSelections", std::move(sceneSelectionsJson)},
		    {"grid",
		     {{"spacing", grid.spacing},
		      {"height", grid.height},
		      {"halfLineCount", grid.halfLineCount},
		      {"majorLineInterval", grid.majorLineInterval}}},
		    {"sceneProjection", ProjectionJson(sceneProjection)},
		    {"camera",
		     {{"moveSpeed", camera.moveSpeed},
		      {"fastMultiplier", camera.fastMultiplier},
		      {"mouseSensitivity", camera.mouseSensitivity},
		      {"wheelDistance", camera.wheelDistance},
		      {"panSensitivity", camera.panSensitivity}}},
		};
		const std::string text = json.dump(2) + '\n';
		std::filesystem::create_directories(path.parent_path());
		std::string saveError;

		if (!FileUtils::WriteTextAtomically(path, text, saveError)) {
			throw std::runtime_error(saveError);
		}

		return true;
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorPreferences::Save: {}", error.what()));
		return false;
	}
}

bool EditorPreferences::operator==(const EditorPreferences &other) const {
	return sceneProjection == other.sceneProjection &&
	       cameraBookmarks == other.cameraBookmarks &&
	       colliderDisplay == other.colliderDisplay &&
	       pivotMode == other.pivotMode && grid == other.grid &&
	       recentScenes == other.recentScenes && lastProject == other.lastProject &&
	       recentProjects == other.recentProjects &&
	       lastGameExports_ == other.lastGameExports_ &&
	       gameExportDrafts == other.gameExportDrafts &&
	       recentGameExports_ == other.recentGameExports_ &&
	       exportSceneSources_ == other.exportSceneSources_ &&
	       projectAssetFolders_ == other.projectAssetFolders_ &&
	       projectScenes == other.projectScenes && sceneViews == other.sceneViews &&
	       sceneSelections_ == other.sceneSelections_ &&
	       assets == other.assets &&
	       gameExport.title == other.gameExport.title &&
	       gameExport.width == other.gameExport.width &&
	       gameExport.height == other.gameExport.height &&
	       gameExport.input == other.gameExport.input &&
	       gameExport.actions == other.gameExport.actions &&
	       gameExport.lighting == other.gameExport.lighting &&
	       gameExport.gamepadEnabled == other.gameExport.gamepadEnabled &&
	       gameExport.cameraRelativeMovement ==
	           other.gameExport.cameraRelativeMovement &&
	       showGrid == other.showGrid &&
	       showSelectionBounds == other.showSelectionBounds &&
	       showCameraGuides == other.showCameraGuides &&
	       showRespawnGuides == other.showRespawnGuides &&
	       showJumpGuide == other.showJumpGuide &&
	       showMotionTrail == other.showMotionTrail &&
	       maximizeGameOnPlay == other.maximizeGameOnPlay &&
	       startPlayPaused == other.startPlayPaused &&
	       searchProjectScenes == other.searchProjectScenes &&
	       showObjectMarkers == other.showObjectMarkers &&
	       showMeasurements == other.showMeasurements &&
	       snapping == other.snapping && operation == other.operation &&
	       space == other.space &&
	       snap.translation.x == other.snap.translation.x &&
	       snap.translation.y == other.snap.translation.y &&
	       snap.translation.z == other.snap.translation.z &&
	       snap.rotationDegrees == other.snap.rotationDegrees &&
	       snap.scaleRatio == other.snap.scaleRatio &&
	       camera.moveSpeed == other.camera.moveSpeed &&
	       camera.fastMultiplier == other.camera.fastMultiplier &&
	       camera.mouseSensitivity == other.camera.mouseSensitivity &&
	       camera.wheelDistance == other.camera.wheelDistance &&
	       camera.panSensitivity == other.camera.panSensitivity;
}
