#include "graphics/material/MaterialAsset.h"
#include "platform/windows/FileUtils.h"
#include "platform/windows/PathUtils.h"
#include <fstream>
#include <json.hpp>
#include <stdexcept>
#include <vector>

bool MaterialAsset::IsValid() const {
	if (!data.IsValid() || baseColorTexture.has_root_path()) {
		return false;
	}

	const auto &text = baseColorTexture.native();
	return text.find(L'\0') == std::wstring::npos &&
	       text.find(L':') == std::wstring::npos;
}

bool MaterialSerializer::Serialize(const MaterialAsset &asset,
                                   std::string &text, std::string &error) {
	try {
		if (!asset.IsValid()) {
			throw std::runtime_error(
			    "Invalid material parameters or texture path.");
		}

		const auto &v = asset.data;
		nlohmann::json json{
		    {"format", "EngineMaterial"},
		    {"version", 2},
		    {"shading", v.shadingMode == MaterialData::ShadingMode::UNLIT
		                    ? "Unlit"
		                    : "Lit"},
		    {"baseColor",
		     {v.baseColor.x, v.baseColor.y, v.baseColor.z, v.baseColor.w}},
		    {"uvScale", {v.uvScale.x, v.uvScale.y}},
		    {"uvOffset", {v.uvOffset.x, v.uvOffset.y}},
		    {"baseColorTexture", PathUtils::ToUtf8(asset.baseColorTexture)}};
		auto serialized = json.dump(2) + '\n';
		text.swap(serialized);
		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

bool MaterialSerializer::Load(const std::filesystem::path &path,
                              MaterialAsset &asset, std::string &error) {
	try {
		std::ifstream file(PathUtils::ResolveFromExecutable(path),
		                   std::ios::binary);
		const auto json = nlohmann::json::parse(file);

		if (json.at("format") != "EngineMaterial" ||
		    !json.at("version").is_number_integer() ||
		    (json.at("version") != 1 && json.at("version") != 2)) {
			throw std::runtime_error("Unsupported material format or version.");
		}

		auto values = [&json](const char *key, std::size_t count) {
			const auto &array = json.at(key);

			if (!array.is_array() || array.size() != count) {
				throw std::runtime_error("Invalid material parameter array.");
			}

			std::vector<float> result;

			for (const auto &value : array) {
				if (!value.is_number()) {
					throw std::runtime_error(
					    "Material parameters must be numbers.");
				}

				result.push_back(value.get<float>());
			}

			return result;
		};
		const auto color = values("baseColor", 4);
		const auto scale = values("uvScale", 2);
		const auto offset = values("uvOffset", 2);
		MaterialAsset loaded;

		if (json.at("version") == 2) {
			const auto shading = json.at("shading").get<std::string>();

			if (shading == "Lit") {
				loaded.data.shadingMode = MaterialData::ShadingMode::LIT;
			} else if (shading == "Unlit") {
				loaded.data.shadingMode = MaterialData::ShadingMode::UNLIT;
			} else {
				throw std::runtime_error("Unsupported material shading mode.");
			}
		}

		loaded.data.baseColor = {color[0], color[1], color[2], color[3]};
		loaded.data.uvScale = {scale[0], scale[1]};
		loaded.data.uvOffset = {offset[0], offset[1]};
		loaded.baseColorTexture =
		    PathUtils::FromUtf8(json.at("baseColorTexture").get<std::string>());

		if (!loaded.IsValid()) {
			throw std::runtime_error(
			    "Invalid material parameters or texture path.");
		}

		asset = std::move(loaded);
		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

bool MaterialSerializer::Save(const std::filesystem::path &path,
                              const MaterialAsset &asset, std::string &error,
                              bool overwrite) {
	std::string text;

	if (!Serialize(asset, text, error)) {
		return false;
	}

	return FileUtils::WriteTextAtomically(path, text, error, overwrite);
}
