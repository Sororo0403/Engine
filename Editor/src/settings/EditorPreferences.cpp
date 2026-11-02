#include "settings/EditorPreferences.h"

#include "core/Logger.h"
#include "platform/windows/FileUtils.h"
#include "platform/windows/PathUtils.h"
#include "settings/RecentScenes.h"

#include <json.hpp>

#include <algorithm>
#include <exception>
#include <format>
#include <fstream>
#include <stdexcept>

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
		loaded.showObjectMarkers = json.value("showObjectMarkers", true);

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

			if (!loaded.gameExport.IsValid()) {
				throw std::runtime_error(
				    "ゲーム出力のタイトル・画面サイズが無効です。");
			}
		}

		*this = loaded;
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
		    !gridValidator.SetSettings(grid) || !gameExport.IsValid() ||
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

		const nlohmann::json json{
		    {"version", 1},
		    {"gameExport",
		     {{"title", gameExport.title},
		      {"width", gameExport.width},
		      {"height", gameExport.height}}},
		    {"showGrid", showGrid},
		    {"showSelectionBounds", showSelectionBounds},
		    {"showCameraGuides", showCameraGuides},
		    {"showRespawnGuides", showRespawnGuides},
		    {"showObjectMarkers", showObjectMarkers},
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
		    {"grid",
		     {{"spacing", grid.spacing},
		      {"halfLineCount", grid.halfLineCount},
		      {"majorLineInterval", grid.majorLineInterval}}},
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
	return colliderDisplay == other.colliderDisplay &&
	       pivotMode == other.pivotMode && grid == other.grid &&
	       recentScenes == other.recentScenes &&
	       gameExport.title == other.gameExport.title &&
	       gameExport.width == other.gameExport.width &&
	       gameExport.height == other.gameExport.height &&
	       showGrid == other.showGrid &&
	       showSelectionBounds == other.showSelectionBounds &&
	       showCameraGuides == other.showCameraGuides &&
	       showRespawnGuides == other.showRespawnGuides &&
	       showObjectMarkers == other.showObjectMarkers &&
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
