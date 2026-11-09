#include "assets/AssetDependencies.h"
#include "graphics/model/ModelLoader.h"
#include "platform/windows/PathUtils.h"
#include <array>
#include <fstream>
#include <iomanip>
#include <json.hpp>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
namespace fs = std::filesystem;

/// <summary>
/// glTFのURI参照をローカル相対パスへ変換する
/// </summary>
/// <param name="uri">外部ファイルのURI</param>
/// <returns>復号した相対パス。ネットワークや絶対参照は例外</returns>
fs::path DecodeUri(const std::string &uri) {
	std::string text;
	auto hex = [](char c) -> int {
		if (c >= '0' && c <= '9') {
			return c - '0';
		}

		if (c >= 'a' && c <= 'f') {
			return c - 'a' + 10;
		}

		if (c >= 'A' && c <= 'F') {
			return c - 'A' + 10;
		}

		return -1;
	};

	for (size_t i = 0; i < uri.size(); ++i) {
		if (uri[i] == '%') {
			if (i + 2 >= uri.size() || hex(uri[i + 1]) < 0 ||
			    hex(uri[i + 2]) < 0) {
				throw std::runtime_error("Invalid resource URI.");
			}

			text += char(hex(uri[i + 1]) * 16 + hex(uri[i + 2]));
			i += 2;
		} else {
			text += uri[i];
		}
	}

	const auto path = PathUtils::FromUtf8(text);

	if (path.empty() || path.has_root_path() ||
	    text.find(':') != std::string::npos ||
	    text.find('\0') != std::string::npos) {
		throw std::runtime_error(
		    "Use relative local asset references before exporting.");
	}

	return path;
}

/// <summary>
/// 外部APIへ渡す参照先を検証し、実行ファイル基準の絶対パスへ揃える
/// </summary>
/// <param name="path">空でなくNULを含まないアセット参照</param>
/// <returns>正規化した絶対パス。入力が無効なら例外</returns>
fs::path ResolveAsset(const fs::path &path) {
	if (path.native().find(L'\0') != std::wstring::npos) {
		throw std::invalid_argument(
		    "Asset paths cannot contain null characters.");
	}

	return PathUtils::ResolveFromExecutable(path);
}
} // namespace

std::vector<std::filesystem::path>
AssetDependencies::ModelFiles(const std::filesystem::path &input,
                              std::vector<fs::path> *absoluteReferences) {
	const auto model = ResolveAsset(input);
	std::vector<fs::path> files{model};

	ModelData data;

	if (!ModelLoader::Load(model, data)) {
		throw std::runtime_error("Could not load model: " +
		                         PathUtils::ToUtf8(model));
	}

	for (const auto &material : data.materials) {
		if (material.baseColorTexture.source == TextureSource::FILE) {
			files.push_back(material.baseColorTexture.filePath);

			if (absoluteReferences && material.baseColorTextureUsesAbsolutePath) {
				absoluteReferences->push_back(material.baseColorTexture.filePath);
			}
		}
	}

	if (PathUtils::HasExtension(model, ".gltf") ||
	    PathUtils::HasExtension(model, ".glb")) {
		std::ifstream file(model, std::ios::binary);
		nlohmann::json document;

		if (PathUtils::HasExtension(model, ".glb")) {
			std::array<std::uint32_t, 5> header{};
			file.read(reinterpret_cast<char *>(header.data()), sizeof(header));

			if (!file || header[0] != 0x46546c67 || header[1] != 2 ||
			    header[4] != 0x4e4f534a ||
			    fs::file_size(model) < sizeof(header) ||
			    header[3] > fs::file_size(model) - sizeof(header)) {
				throw std::runtime_error("Invalid GLB JSON chunk.");
			}

			std::string json(header[3], '\0');
			file.read(json.data(), static_cast<std::streamsize>(json.size()));

			if (!file) {
				throw std::runtime_error("Could not read GLB JSON.");
			}

			document = nlohmann::json::parse(json);
		} else {
			document = nlohmann::json::parse(file);
		}

		for (const char *key : {"buffers", "images"}) {
			if (document.contains(key)) {
				for (const auto &item : document.at(key)) {
					if (item.contains("uri")) {
						const auto uri = item.at("uri").get<std::string>();

						if (uri.rfind("data:", 0) != 0) {
							files.push_back(model.parent_path() /
							                DecodeUri(uri));
						}
					}
				}
			}
		}
	} else if (PathUtils::HasExtension(model, ".obj")) {
		std::ifstream file(model);

		if (!file) {
			throw std::runtime_error("Could not read OBJ material references.");
		}

		std::string line;

		while (std::getline(file, line)) {
			std::istringstream fields(line);
			std::string command;
			fields >> command;

			if (command != "mtllib") {
				continue;
			}

			std::string value;
			std::getline(fields, value);
			const auto first = value.find_first_not_of(" \t\r");

			if (first == std::string::npos) {
				continue;
			}

			value.erase(0, first);
			value.erase(value.find_last_not_of(" \t\r") + 1);
			const auto entire = PathUtils::FromUtf8(value);

			if (!entire.has_root_path() &&
			    fs::is_regular_file(model.parent_path() / entire)) {
				files.push_back(model.parent_path() / entire);
			} else {
				std::istringstream names(value);
				std::string name;

				while (names >> std::quoted(name)) {
					if (name.starts_with('#')) {
						break;
					}

					const auto path = PathUtils::FromUtf8(name);

					if (path.has_root_path() ||
					    name.find(':') != std::string::npos) {
						throw std::runtime_error(
						    "Use relative material paths in OBJ files.");
					}

					files.push_back(model.parent_path() / path);
				}
			}
		}
	} else {
		throw std::runtime_error("Export supports GLB, glTF and OBJ models.");
	}

	return files;
}

std::vector<std::filesystem::path>
AssetDependencies::TextureFiles(const std::filesystem::path &path) {
	if (!PathUtils::IsImageFile(path)) {
		throw std::invalid_argument("Unsupported image texture extension.");
	}

	return {ResolveAsset(path)};
}

std::vector<std::filesystem::path>
AssetDependencies::AudioFiles(const std::filesystem::path &path) {
	if (!PathUtils::HasExtension(path, ".wav"))
		throw std::invalid_argument("Unsupported audio extension. Use WAV.");
	return {ResolveAsset(path)};
}
