#include "settings/EditorPreferences.h"

#include "core/Logger.h"
#include "platform/windows/PathUtils.h"
#include "settings/RecentScenes.h"

#include <json.hpp>

#include <exception>
#include <algorithm>
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
		if (json.at("version") != 1 || !json.at("version").is_number_integer()) {
			throw std::runtime_error("未対応のEditor設定形式です。");
		}
		EditorPreferences loaded;
		loaded.showGrid = json.at("showGrid").get<bool>();
		loaded.showSelectionBounds = json.at("showSelectionBounds").get<bool>();
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
		if (!move.is_array() || move.size() != 3) {
			throw std::runtime_error("移動スナップには3成分が必要です。");
		}
		loaded.snap.translation = {move.at(0).get<float>(), move.at(1).get<float>(),
		                           move.at(2).get<float>()};
		loaded.snap.rotationDegrees = json.at("rotationStepDegrees").get<float>();
		loaded.snap.scaleRatio = json.at("scaleStepRatio").get<float>();
		TransformGizmo validator;
		if (!validator.SetSnapSettings(loaded.snap)) {
			throw std::runtime_error("Editor設定のスナップ間隔が無効です。");
		}
		// カメラ設定がない旧設定ファイルには初期値を使用する
		if (json.contains("camera")) {
			const auto &settings = json.at("camera");
			loaded.camera.moveSpeed = settings.at("moveSpeed").get<float>();
			loaded.camera.fastMultiplier = settings.at("fastMultiplier").get<float>();
			loaded.camera.mouseSensitivity = settings.at("mouseSensitivity").get<float>();
		}
		EditorCameraController cameraValidator;
		if (!cameraValidator.SetSettings(loaded.camera)) {
			throw std::runtime_error("Editor設定のカメラ操作設定が無効です。");
		}
		if (json.contains("recentScenes")) {
			const auto &paths = json.at("recentScenes");
			if (!paths.is_array()) {
				throw std::runtime_error("最近開いたシーンには配列が必要です。");
			}
			RecentScenes recent;
			const auto count = (std::min)(paths.size(), RecentScenes::kMaximumCount);
			for (std::size_t i = count; i > 0; --i) {
				if (!recent.Add(PathUtils::FromUtf8(paths.at(i - 1).get<std::string>()))) {
					throw std::runtime_error("最近開いたシーンのパスが無効です。");
				}
			}
			const auto normalized = recent.GetPaths();
			loaded.recentScenes.assign(normalized.begin(), normalized.end());
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
		if (!validator.SetSnapSettings(snap) || !validator.SetOperation(operation) ||
		    !cameraValidator.SetSettings(camera)) {
			throw std::runtime_error("保存するEditor設定が無効です。");
		}
		const auto path = PathUtils::ResolveFromExecutable(filePath);
		const char *tool = operation == TransformGizmo::Operation::TRANSLATE
		                       ? "move"
		                       : operation == TransformGizmo::Operation::ROTATE
		                             ? "rotate"
		                             : "scale";
		RecentScenes recent;
		const auto count = (std::min)(recentScenes.size(), RecentScenes::kMaximumCount);
		for (std::size_t i = count; i > 0; --i) {
			if (!recent.Add(recentScenes[i - 1])) {
				throw std::runtime_error("保存する最近のシーンのパスが無効です。");
			}
		}
		auto recentJson = nlohmann::json::array();
		for (const auto &recentPath : recent.GetPaths()) {
			recentJson.push_back(PathUtils::ToUtf8(recentPath));
		}
		const nlohmann::json json{
		    {"version", 1}, {"showGrid", showGrid},
		    {"showSelectionBounds", showSelectionBounds}, {"snapping", snapping},
		    {"tool", tool},
		    {"moveStep", {snap.translation.x, snap.translation.y, snap.translation.z}},
		    {"rotationStepDegrees", snap.rotationDegrees},
		    {"scaleStepRatio", snap.scaleRatio},
		    {"recentScenes", recentJson},
		    {"camera", {{"moveSpeed", camera.moveSpeed},
		                {"fastMultiplier", camera.fastMultiplier},
		                {"mouseSensitivity", camera.mouseSensitivity}}},
		};
		const std::string text = json.dump(2);
		std::filesystem::create_directories(path.parent_path());
		auto temporary = path;
		temporary += ".tmp";
		std::ofstream output;
		output.exceptions(std::ios::failbit | std::ios::badbit);
		output.open(temporary, std::ios::binary | std::ios::trunc);
		output << text << '\n';
		output.close();
		std::filesystem::rename(temporary, path);
		return true;
	} catch (const std::exception &error) {
		Logger::Error(std::format("EditorPreferences::Save: {}", error.what()));
		return false;
	}
}

bool EditorPreferences::operator==(const EditorPreferences &other) const {
	return recentScenes == other.recentScenes && showGrid == other.showGrid &&
	       showSelectionBounds == other.showSelectionBounds &&
	       snapping == other.snapping && operation == other.operation &&
	       snap.translation.x == other.snap.translation.x &&
	       snap.translation.y == other.snap.translation.y &&
	       snap.translation.z == other.snap.translation.z &&
	       snap.rotationDegrees == other.snap.rotationDegrees &&
	       snap.scaleRatio == other.snap.scaleRatio &&
	       camera.moveSpeed == other.camera.moveSpeed &&
	       camera.fastMultiplier == other.camera.fastMultiplier &&
	       camera.mouseSensitivity == other.camera.mouseSensitivity;
}
