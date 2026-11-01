#include "gameplay/GameConfig.h"
#include "graphics/camera/Camera.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/StringConversion.h"
#include <fstream>
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
	if (title.empty() || title.find('\0') != std::string::npos ||
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

bool GameConfig::Save(const std::filesystem::path &path,
                      std::string &error) const {
	try {
		error.clear();

		if (!IsValid()) {
			throw std::runtime_error("Game startup settings are invalid.");
		}

		auto vector = [](const auto &v) {
			return nlohmann::json::array({v.x, v.y, v.z});
		};
		const nlohmann::json data = {
		    {"format", "EngineGame"},
		    {"version", 1},
		    {"title", title},
		    {"width", width},
		    {"height", height},
		    {"scene", PathUtils::ToUtf8(scenePath)},
		    {"camera",
		     {{"position", vector(fallbackCamera.position)},
		      {"target", vector(fallbackCamera.target)},
		      {"up", vector(fallbackCamera.up)}}}};
		const auto text = data.dump(2);
		std::ofstream file;
		file.exceptions(std::ios::failbit | std::ios::badbit);
		file.open(path, std::ios::binary | std::ios::trunc);
		file << text << '\n';
		file.close();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

bool GameConfig::Load(const std::filesystem::path &path, std::string &error) {
	try {
		error.clear();
		std::ifstream file(path, std::ios::binary);

		if (!file) {
			throw std::runtime_error("Could not open game.json.");
		}

		const auto data = nlohmann::json::parse(file);

		if (data.at("format") != "EngineGame" ||
		    !data.at("version").is_number_integer() ||
		    data.at("version") != 1 || !data.at("width").is_number_integer() ||
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
