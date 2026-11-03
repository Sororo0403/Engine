#include "LegacyMaterial.h"
#include "platform/windows/PathUtils.h"
#include <fstream>
#include <json.hpp>
#include <stdexcept>
#include <vector>

bool LegacyMaterial::IsValid() const {
	if (!data.IsValid() || baseColorTexture.has_root_path()) {
		return false;
	}

	const auto &text = baseColorTexture.native();
	return text.find(L'\0') == std::wstring::npos &&
	       text.find(L':') == std::wstring::npos;
}

bool LegacyMaterialReader::Load(const std::filesystem::path &path,
                                LegacyMaterial &asset, std::string &error) {
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
		LegacyMaterial loaded;

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
