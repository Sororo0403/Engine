#include "gameplay/GameConfig.h"
#include "graphics/camera/Camera.h"
#include "platform/windows/FileUtils.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/StringConversion.h"
#include <fstream>
#include <iterator>
#include <json.hpp>
#include <stdexcept>
#include <utility>

namespace {
/// <summary>
/// JSONから有限な3成分の数値を読み込む
/// </summary>
/// <param name="value">数値の配列</param>
/// <returns>読み込んだ値。型が無効なら例外</returns>
DirectX::XMFLOAT3 ReadVector(const nlohmann::json &value) {
	if (!value.is_array() || value.size() != 3) {
		throw std::runtime_error("Camera vectors need three numbers.");
	}

	for (const auto &v : value) {
		if (!v.is_number()) {
			throw std::runtime_error("Camera values must be numbers.");
		}
	}

	return {value[0].get<float>(), value[1].get<float>(),
	        value[2].get<float>()};
}
} // namespace

bool GameConfig::IsValid() const {
	if (!lighting.IsValid() || !input.IsValid() || title.empty() ||
	    title.find('\0') != std::string::npos ||
	    StringConversion::Utf8ToWide(title).empty() || width < 1 ||
	    height < 1 || width > 8192 || height > 8192 || scenePath.empty() ||
	    scenePath.has_root_path() ||
	    scenePath.native().find(L'\0') != std::wstring::npos ||
	    !PathUtils::HasExtension(scenePath, ".json")) {
		return false;
	}

	for (const auto &part : scenePath) {
		if (part == L".." || part.native().find(L':') != std::wstring::npos) {
			return false;
		}
	}

	Camera camera;
	return camera.SetPose(fallbackCamera);
}

bool GameConfig::Serialize(std::string &text, std::string &error) const {
	try {
		error.clear();

		if (!IsValid()) {
			throw std::runtime_error("Game startup settings are invalid.");
		}

		auto vector = [](const auto &v) {
			return nlohmann::json::array({v.x, v.y, v.z});
		};
		nlohmann::json inputJson = nlohmann::json::object();

		for (const auto &action : GameInputBindings::kActions) {
			inputJson[action.name] =
			    GameInputBindings::GetName(input.*action.member);
		}

		const nlohmann::json data = {
		    {"format", "EngineGame"},
		    {"version", 4},
		    {"cameraRelativeMovement", cameraRelativeMovement},
		    {"input", inputJson},
		    {"lighting",
		     {{"direction", vector(lighting.direction)},
		      {"color", vector(lighting.color)},
		      {"intensity", lighting.intensity},
		      {"ambient", lighting.ambient}}},
		    {"title", title},
		    {"width", width},
		    {"height", height},
		    {"scene", PathUtils::ToUtf8(scenePath)},
		    {"camera",
		     {{"position", vector(fallbackCamera.position)},
		      {"target", vector(fallbackCamera.target)},
		      {"up", vector(fallbackCamera.up)}}}};
		std::string serialized = data.dump(2);
		serialized += '\n';
		text = std::move(serialized);
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

bool GameConfig::Save(const std::filesystem::path &path,
                      std::string &error) const {
	try {
		std::string text;

		if (!Serialize(text, error)) {
			return false;
		}

		if (path.empty()) {
			throw std::invalid_argument(
			    "Choose a game configuration destination.");
		}

		// 起動設定の相対パスは、Loadと同じ作業ディレクトリ基準を保持する。
		return FileUtils::WriteTextAtomically(std::filesystem::absolute(path),
		                                      text, error);
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

bool GameConfig::Load(const std::filesystem::path &path, std::string &error) {
	try {
		std::ifstream file(path, std::ios::binary);
		if (!file) {
			throw std::runtime_error("Could not open game.json.");
		}
		file.exceptions(std::ios::badbit);
		const std::string text{std::istreambuf_iterator<char>(file), {}};
		return Deserialize(text, error);
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

bool GameConfig::Deserialize(std::string_view text, std::string &error) {
	try {
		error.clear();
		const auto data = nlohmann::json::parse(text);

		if (data.at("format") != "EngineGame" ||
		    !data.at("version").is_number_integer() ||
		    (data.at("version") != 1 && data.at("version") != 2 &&
		     data.at("version") != 3 && data.at("version") != 4) ||
		    !data.at("width").is_number_integer() ||
		    !data.at("height").is_number_integer()) {
			throw std::runtime_error("Unsupported game configuration.");
		}

		if (data.at("width") < 1 || data.at("width") > 8192 ||
		    data.at("height") < 1 || data.at("height") > 8192) {
			throw std::runtime_error(
			    "Window dimensions are outside the supported range.");
		}

		GameConfig candidate;
		candidate.title = data.at("title").get<std::string>();
		candidate.width = data.at("width").get<int>();
		candidate.height = data.at("height").get<int>();
		candidate.scenePath =
		    PathUtils::FromUtf8(data.at("scene").get<std::string>());
		if (data.at("version") >= 2) {
			for (const auto &action : GameInputBindings::kActions) {
				const auto key = GameInputBindings::FromName(
				    data.at("input").at(action.name).get<std::string>());

				if (!key) {
					throw std::runtime_error("Unsupported gameplay key.");
				}

				candidate.input.*action.member = *key;
			}
		}

		if (data.at("version") >= 3) {
			if (!data.at("cameraRelativeMovement").is_boolean()) {
				throw std::runtime_error("Movement space must be a boolean.");
			}

			candidate.cameraRelativeMovement =
			    data.at("cameraRelativeMovement").get<bool>();
		}

		if (data.at("version") >= 4) {
			const auto &light = data.at("lighting");
			candidate.lighting.direction = ReadVector(light.at("direction"));
			candidate.lighting.color = ReadVector(light.at("color"));
			if (!light.at("intensity").is_number() ||
			    !light.at("ambient").is_number()) {
				throw std::runtime_error(
				    "Light intensity and ambient must be numbers.");
			}
			candidate.lighting.intensity = light.at("intensity").get<float>();
			candidate.lighting.ambient = light.at("ambient").get<float>();
		}

		const auto &camera = data.at("camera");
		candidate.fallbackCamera = {ReadVector(camera.at("position")),
		                            ReadVector(camera.at("target")),
		                            ReadVector(camera.at("up"))};

		if (!candidate.IsValid()) {
			throw std::runtime_error("Game startup settings are invalid.");
		}

		*this = std::move(candidate);
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}
