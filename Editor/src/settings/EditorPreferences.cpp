#include "settings/EditorPreferences.h"

#include "core/Logger.h"
#include "platform/windows/PathUtils.h"

#include <json.hpp>

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
		if (!validator.SetSnapSettings(snap) || !validator.SetOperation(operation)) {
			throw std::runtime_error("保存するEditor設定が無効です。");
		}
		const auto path = PathUtils::ResolveFromExecutable(filePath);
		const char *tool = operation == TransformGizmo::Operation::TRANSLATE
		                       ? "move"
		                       : operation == TransformGizmo::Operation::ROTATE
		                             ? "rotate"
		                             : "scale";
		const nlohmann::json json{
		    {"version", 1}, {"showGrid", showGrid},
		    {"showSelectionBounds", showSelectionBounds}, {"snapping", snapping},
		    {"tool", tool},
		    {"moveStep", {snap.translation.x, snap.translation.y, snap.translation.z}},
		    {"rotationStepDegrees", snap.rotationDegrees},
		    {"scaleStepRatio", snap.scaleRatio},
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
	return showGrid == other.showGrid &&
	       showSelectionBounds == other.showSelectionBounds &&
	       snapping == other.snapping && operation == other.operation &&
	       snap.translation.x == other.snap.translation.x &&
	       snap.translation.y == other.snap.translation.y &&
	       snap.translation.z == other.snap.translation.z &&
	       snap.rotationDegrees == other.snap.rotationDegrees &&
	       snap.scaleRatio == other.snap.scaleRatio;
}
