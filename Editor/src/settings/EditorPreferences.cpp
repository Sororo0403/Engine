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
#include <utility>

namespace {
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

			if (settings.contains("input")) {
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

			if (settings.contains("lighting")) {
				std::string text, error;
				if (!loaded.gameExport.Serialize(text, error)) {
					throw std::runtime_error(error);
				}
				auto configuration = nlohmann::json::parse(text);
				configuration["lighting"] = settings.at("lighting");
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
				const auto &position = bookmark.at("position");
				if (!position.is_array() || position.size() != 3 ||
				    !position[0].is_number() || !position[1].is_number() ||
				    !position[2].is_number() ||
				    !bookmark.at("yaw").is_number() ||
				    !bookmark.at("pitch").is_number() ||
				    !loaded.cameraBookmarks.Set(
				        i,
				        {{position[0].get<float>(), position[1].get<float>(),
				          position[2].get<float>()},
				         bookmark.at("yaw").get<float>(),
				         bookmark.at("pitch").get<float>()},
				        projection)) {
					throw std::runtime_error(
					    "カメラのブックマークが無効です。");
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

		nlohmann::json inputJson = nlohmann::json::object();

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
		const auto lighting = nlohmann::json::parse(gameText).at("lighting");

		auto bookmarks = nlohmann::json::array();
		for (std::size_t i = 0; i < EditorCameraBookmarks::kSlotCount; ++i) {
			if (const auto *bookmark = cameraBookmarks.Get(i)) {
				nlohmann::json saved{
				    {"position",
				     {bookmark->position.x, bookmark->position.y,
				      bookmark->position.z}},
				    {"yaw", bookmark->yaw},
				    {"pitch", bookmark->pitch}};
				if (const auto *projection = cameraBookmarks.GetProjection(i)) {
					saved["projection"] = ProjectionJson(*projection);
				}
				bookmarks.push_back(std::move(saved));
			} else {
				bookmarks.push_back(nullptr);
			}
		}

		const nlohmann::json json{
		    {"version", 1},
		    {"cameraBookmarks", bookmarks},
		    {"assetDirectory", PathUtils::ToUtf8(assets.GetDirectory())},
		    {"assetDirectories", assetDirectories},
		    {"gameExport",
		     {{"lighting", lighting},
		      {"title", gameExport.title},
		      {"width", gameExport.width},
		      {"height", gameExport.height},
		      {"input", inputJson},
		      {"cameraRelativeMovement", gameExport.cameraRelativeMovement}}},
		    {"showGrid", showGrid},
		    {"showSelectionBounds", showSelectionBounds},
		    {"showCameraGuides", showCameraGuides},
		    {"showRespawnGuides", showRespawnGuides},
		    {"showJumpGuide", showJumpGuide},
		    {"showMotionTrail", showMotionTrail},
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
	       recentScenes == other.recentScenes && assets == other.assets &&
	       gameExport.title == other.gameExport.title &&
	       gameExport.width == other.gameExport.width &&
	       gameExport.height == other.gameExport.height &&
	       gameExport.input == other.gameExport.input &&
	       gameExport.lighting == other.gameExport.lighting &&
	       gameExport.cameraRelativeMovement ==
	           other.gameExport.cameraRelativeMovement &&
	       showGrid == other.showGrid &&
	       showSelectionBounds == other.showSelectionBounds &&
	       showCameraGuides == other.showCameraGuides &&
	       showRespawnGuides == other.showRespawnGuides &&
	       showJumpGuide == other.showJumpGuide &&
	       showMotionTrail == other.showMotionTrail &&
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
