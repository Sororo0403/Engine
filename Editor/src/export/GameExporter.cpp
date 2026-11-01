#include "export/GameExporter.h"
#include "gameplay/SceneSimulation.h"
#include "graphics/model/ModelLoader.h"
#include "platform/windows/PathUtils.h"
#include "scene/SceneSerializer.h"
#include <Windows.h>
#include <array>
#include <fstream>
#include <iomanip>
#include <json.hpp>
#include <map>
#include <objbase.h>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {
namespace fs = std::filesystem;
/// <summary>
/// Windowsのパス文字列を大文字・小文字を区別せず順序付けする
/// </summary>
struct PathLess {
	bool operator()(const fs::path &a, const fs::path &b) const {
		return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) ==
		       CSTR_LESS_THAN;
	}
};
/// <summary>
/// 正規化したパスが基準フォルダ内なら、表記の大小に依存しない相対パスを返す
/// </summary>
/// <param name="root">正規化済みの基準フォルダ</param>
/// <param name="path">正規化済みの対象</param>
/// <returns>基準からの相対パス。外部の場合は例外</returns>
fs::path RelativeInside(const fs::path &root, const fs::path &path) {
	auto a = root.begin(), b = path.begin();
	for (; a != root.end(); ++a, ++b) {
		if (b == path.end() || CompareStringOrdinal(a->c_str(), -1, b->c_str(),
		                                            -1, TRUE) != CSTR_EQUAL) {
			throw std::runtime_error(
			    "A referenced file is outside resources: " +
			    PathUtils::ToUtf8(path));
		}
	}
	fs::path relative;
	for (; b != path.end(); ++b) {
		relative /= *b;
	}
	return relative;
}
/// <summary>
/// リソース参照の別名やリンクによる外部依存を拒否し、通常ファイルを確認する
/// </summary>
/// <param name="root">リソースの正規化済みルート</param>
/// <param name="input">検証する絶対パス</param>
/// <returns>正規化した通常ファイル</returns>
fs::path ResourceFile(const fs::path &root, const fs::path &input) {
	if (input.native().find(L'\0') != std::wstring::npos) {
		throw std::runtime_error(
		    "Resource paths cannot contain null characters.");
	}
	for (auto part = input; part.has_filename(); part = part.parent_path()) {
		const auto attributes = GetFileAttributesW(part.c_str());
		if (attributes != INVALID_FILE_ATTRIBUTES &&
		    (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
			throw std::runtime_error("Copy linked resources into the resources "
			                         "folder before exporting.");
		}
	}
	const auto path = fs::canonical(input);
	const auto relative = RelativeInside(root, path);
	if (relative.empty() || !fs::is_regular_file(path)) {
		throw std::runtime_error("A resource is not a regular file.");
	}
	for (const auto &part : relative) {
		if (part.native().find(L':') != std::wstring::npos) {
			throw std::runtime_error("Resource paths cannot contain streams.");
		}
	}
	return path;
}
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
/// 参照するモデル・バッファ・画像・材質ファイルを収集する
/// </summary>
class Assets {
  public:
	explicit Assets(fs::path root) : root_(std::move(root)) {}
	fs::path AddModel(const fs::path &input) {
		const auto model = AddFile(input);
		if (!models_.insert(model).second) {
			return model;
		}
		ModelData data;
		if (!ModelLoader::Load(model, data)) {
			throw std::runtime_error("Could not load model: " +
			                         PathUtils::ToUtf8(model));
		}
		for (const auto &material : data.materials) {
			if (material.baseColorTexture.source == ModelTextureSource::FILE) {
				AddFile(material.baseColorTexture.filePath);
			}
		}
		if (PathUtils::HasExtension(model, ".gltf") ||
		    PathUtils::HasExtension(model, ".glb")) {
			std::ifstream file(model, std::ios::binary);
			nlohmann::json document;
			if (PathUtils::HasExtension(model, ".glb")) {
				std::array<std::uint32_t, 5> header{};
				file.read(reinterpret_cast<char *>(header.data()),
				          sizeof(header));
				if (!file || header[0] != 0x46546c67 || header[1] != 2 ||
				    header[4] != 0x4e4f534a ||
				    fs::file_size(model) < sizeof(header) ||
				    header[3] > fs::file_size(model) - sizeof(header)) {
					throw std::runtime_error("Invalid GLB JSON chunk.");
				}
				std::string json(header[3], '\0');
				file.read(json.data(),
				          static_cast<std::streamsize>(json.size()));
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
								AddFile(model.parent_path() / DecodeUri(uri));
							}
						}
					}
				}
			}
		} else if (PathUtils::HasExtension(model, ".obj")) {
			std::ifstream file(model);
			if (!file) {
				throw std::runtime_error(
				    "Could not read OBJ material references.");
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
					AddFile(model.parent_path() / entire);
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
						AddFile(model.parent_path() / path);
					}
				}
			}
		} else {
			throw std::runtime_error(
			    "Export supports GLB, glTF and OBJ models.");
		}
		return model;
	}
	const auto &Files() const { return files_; }

  private:
	fs::path AddFile(const fs::path &path) {
		const auto file = ResourceFile(root_, path);
		files_.insert(file);
		return file;
	}
	fs::path root_;
	std::set<fs::path, PathLess> files_, models_;
};
/// <summary>
/// 新規ステージングフォルダの所有権を保持し、失敗した出力を片付ける
/// </summary>
class Staging {
  public:
	explicit Staging(const fs::path &parent) {
		GUID id{};
		if (FAILED(CoCreateGuid(&id))) {
			throw std::runtime_error("Could not allocate an export folder.");
		}
		wchar_t text[40]{};
		StringFromGUID2(id, text, 40);
		path = parent / (L".game-export-" + std::wstring(text));
		if (!fs::create_directory(path)) {
			throw std::runtime_error("Could not create the export folder.");
		}
	}
	~Staging() {
		if (!path.empty()) {
			std::error_code ignored;
			fs::remove_all(path, ignored);
		}
	}
	fs::path path;
};
/// <summary>
/// 通常ファイルだけを指定相対パスへコピーする
/// </summary>
/// <param name="source">コピー元</param>
/// <param name="stage">新規出力のルート</param>
/// <param name="relative">コピー先の相対パス</param>
void CopyRequiredFile(const fs::path &source, const fs::path &stage,
                      const fs::path &relative) {
	if (!fs::is_regular_file(source) ||
	    (GetFileAttributesW(source.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)) {
		throw std::runtime_error(
		    "A required runtime file is missing or linked: " +
		    PathUtils::ToUtf8(source));
	}
	const auto target = stage / relative;
	fs::create_directories(target.parent_path());
	fs::copy_file(source, target);
}
/// <summary>
/// Engineのシェーダーとライセンスを、リンクを辿らずにコピーする
/// </summary>
/// <param name="source">コピー元の通常フォルダ</param>
/// <param name="stage">新規出力のルート</param>
/// <param name="relative">コピー先の相対フォルダ</param>
void CopyTree(const fs::path &source, const fs::path &stage,
              const fs::path &relative) {
	if (!fs::is_directory(source) ||
	    (GetFileAttributesW(source.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)) {
		throw std::runtime_error("Required runtime resources are missing: " +
		                         PathUtils::ToUtf8(source));
	}
	for (const auto &entry : fs::recursive_directory_iterator(source)) {
		if (GetFileAttributesW(entry.path().c_str()) &
		    FILE_ATTRIBUTE_REPARSE_POINT) {
			throw std::runtime_error("Runtime resources cannot contain links.");
		}
		if (entry.is_regular_file()) {
			CopyRequiredFile(entry.path(), stage,
			                 relative /
			                     entry.path().lexically_relative(source));
		}
	}
}
} // namespace

bool GameExporter::Export(const Scene &scene, const GameExportOptions &options,
                          std::string &error) {
	try {
		error.clear();
		RelativeInside(fs::path(L"resources"),
		               options.config.scenePath.lexically_normal());
		if (!options.config.IsValid()) {
			throw std::runtime_error(
			    "Game title, size or fallback camera is invalid.");
		}
		if (options.destination.empty() ||
		    options.destination.native().find(L'\0') != std::wstring::npos) {
			throw std::runtime_error("Choose a new output folder.");
		}
		const auto destination =
		    fs::weakly_canonical(fs::absolute(options.destination));
		if (fs::exists(destination)) {
			throw std::runtime_error(
			    "Output folder already exists. Choose a new folder.");
		}
		const auto resourceRoot = fs::canonical(options.resourceDirectory);
		const auto runtimeRoot = fs::canonical(options.runtimeDirectory);
		// 出力先が入力リソースの中に入らないことを確認する。
		bool inside = false;
		try {
			RelativeInside(resourceRoot, destination);
			inside = true;
		} catch (const std::runtime_error &) {
		}
		if (inside) {
			throw std::runtime_error(
			    "Output must be outside the resources folder.");
		}
		nlohmann::json build;
		{
			std::ifstream file(runtimeRoot / L"Runtime.build.json");
			if (!file) {
				throw std::runtime_error(
				    "Build the Release configuration before exporting.");
			}
			file >> build;
		}
		if (build.at("configuration") != "Release" ||
		    build.at("architecture") != "x64") {
			throw std::runtime_error(
			    "A Release x64 Runtime build is required.");
		}
		SceneSimulation check;
		std::string problem;
		if (!check.Start(scene, problem, options.config.fallbackCamera)) {
			throw std::runtime_error(problem);
		}
		Assets assets(resourceRoot);
		auto objects = scene.GetObjects();
		for (auto &[id, object] : objects) {
			if (!object.modelPath.empty()) {
				const auto path =
				    object.modelPath.is_absolute()
				        ? object.modelPath
				        : resourceRoot /
				              RelativeInside(
				                  fs::path(L"resources"),
				                  object.modelPath.lexically_normal());
				const auto model = assets.AddModel(path);
				object.modelPath = fs::path(L"resources") /
				                   RelativeInside(resourceRoot, model);
			}
		}
		Scene snapshot;
		snapshot.ReplaceObjects(std::move(objects));
		fs::create_directories(destination.parent_path());
		Staging stage(destination.parent_path());
		CopyRequiredFile(runtimeRoot / L"Runtime.exe", stage.path, L"Game.exe");
		for (const auto *name :
		     {L"dxcompiler.dll", L"dxil.dll", L"GameInputRedist.dll",
		      L"GameInputRawInputProxy.exe"}) {
			CopyRequiredFile(runtimeRoot / name, stage.path, name);
		}
		for (const auto *name :
		     {L"msvcp140.dll", L"vcruntime140.dll", L"vcruntime140_1.dll"}) {
			if (!fs::is_regular_file(runtimeRoot / name)) {
				throw std::runtime_error("Runtime C++ libraries are missing. "
				                         "Rebuild Release Runtime.");
			}
		}
		for (const auto &entry : fs::directory_iterator(runtimeRoot)) {
			const auto name = entry.path().filename().native();
			if (PathUtils::HasExtension(entry.path(), ".dll") &&
			    (name.starts_with(L"msvcp140") ||
			     name.starts_with(L"vcruntime140") ||
			     name == L"concrt140.dll" || name == L"vccorlib140.dll")) {
				CopyRequiredFile(entry.path(), stage.path,
				                 entry.path().filename());
			}
		}
		for (const auto *shader :
		     {L"shaders/mesh/Mesh.VS.hlsl", L"shaders/mesh/Mesh.PS.hlsl",
		      L"shaders/debug/DebugLine.VS.hlsl",
		      L"shaders/debug/DebugLine.PS.hlsl"}) {
			if (!fs::is_regular_file(runtimeRoot / L"resources/engine" /
			                         shader)) {
				throw std::runtime_error(
				    "Runtime shaders are missing. Rebuild Engine Release.");
			}
		}
		CopyTree(runtimeRoot / L"resources" / L"engine", stage.path,
		         L"resources/engine");
		CopyTree(runtimeRoot / L"licenses", stage.path, L"licenses");
		for (const auto &file : assets.Files()) {
			CopyRequiredFile(file, stage.path,
			                 fs::path(L"resources") /
			                     RelativeInside(resourceRoot, file));
		}
		const auto assetLicenses = resourceRoot / L"licenses";
		if (fs::is_directory(assetLicenses)) {
			CopyTree(assetLicenses, stage.path, L"licenses/assets");
		}
		Assets stagedAssets(fs::canonical(stage.path / L"resources"));
		for (const auto &[id, object] : snapshot.GetObjects()) {
			if (!object.modelPath.empty()) {
				stagedAssets.AddModel(stage.path / object.modelPath);
			}
		}
		if (fs::exists(stage.path / options.config.scenePath)) {
			throw std::runtime_error(
			    "Startup scene path conflicts with a resource.");
		}
		if (!SceneSerializer::Save(snapshot,
		                           stage.path / options.config.scenePath)) {
			throw std::runtime_error("Could not write the exported scene.");
		}
		if (!options.config.Save(stage.path / L"game.json", problem)) {
			throw std::runtime_error(problem);
		}
		if (!MoveFileExW(stage.path.c_str(), destination.c_str(),
		                 MOVEFILE_WRITE_THROUGH)) {
			throw std::runtime_error(
			    "Could not publish output. Choose a new writable folder.");
		}
		stage.path.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}
