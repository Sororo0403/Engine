#include "export/GameExporter.h"
#include "assets/AssetDependencies.h"
#include "files/OutputDirectory.h"
#include "gameplay/SceneSimulation.h"
#include "platform/windows/PathUtils.h"
#include "scene/SceneSerializer.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <json.hpp>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

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
	const auto relative = PathUtils::RelativeInside(root, path);

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
/// 参照するモデル・バッファ・画像・材質ファイルを収集する
/// </summary>
class Assets {
  public:
	explicit Assets(fs::path root) : root_(std::move(root)) {
	}

	fs::path AddModel(const fs::path &input) {
		const auto model = AddFile(input);

		if (!models_.insert(model).second) {
			return model;
		}

		for (const auto &file : AssetDependencies::ModelFiles(model)) {
			AddFile(file);
		}

		return model;
	}

	fs::path AddTexture(const fs::path &input) {
		AssetDependencies::TextureFiles(input);
		return AddFile(input);
	}

	const auto &Files() const {
		return files_;
	}

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
/// 出力を作成する前に必要なファイルと相対パスの衝突を確認する
/// </summary>
class CopyPlan {
  public:
	explicit CopyPlan(fs::path destination)
	    : destination_(std::move(destination)) {
	}

	void Reserve(fs::path relative, const fs::path &source = {}) {
		relative = relative.lexically_normal().make_preferred();

		const auto found = files_.find(relative);

		if (found != files_.end()) {
			if (!source.empty() && !found->second.empty() &&
			    !PathLess{}(source, found->second) &&
			    !PathLess{}(found->second, source)) {
				return;
			}

			throw std::runtime_error("Output paths conflict: " +
			                         PathUtils::ToUtf8(relative));
		}

		if (directories_.contains(relative)) {
			throw std::runtime_error(
			    "An output file conflicts with a folder: " +
			    PathUtils::ToUtf8(relative));
		}

		for (auto parent = relative.parent_path(); !parent.empty();
		     parent = parent.parent_path()) {
			if (files_.contains(parent)) {
				throw std::runtime_error(
				    "An output folder conflicts with a file: " +
				    PathUtils::ToUtf8(parent));
			}

			directories_.insert(parent);
		}

		files_.emplace(relative, source);
	}

	void File(const fs::path &source, const fs::path &relative) {
		if (!fs::is_regular_file(source) ||
		    (GetFileAttributesW(source.c_str()) &
		     FILE_ATTRIBUTE_REPARSE_POINT)) {
			throw std::runtime_error(
			    "A required runtime file is missing or linked: " +
			    PathUtils::ToUtf8(source));
		}

		Reserve(relative, source);
	}

	void Tree(const fs::path &source, const fs::path &relative) {
		if (!fs::is_directory(source) || (GetFileAttributesW(source.c_str()) &
		                                  FILE_ATTRIBUTE_REPARSE_POINT)) {
			throw std::runtime_error(
			    "Required runtime resources are missing: " +
			    PathUtils::ToUtf8(source));
		}

		bool inside = false;
		try {
			PathUtils::RelativeInside(fs::canonical(source), destination_);
			inside = true;
		} catch (const std::runtime_error &) {
		}

		if (inside) {
			throw std::runtime_error(
			    "Output must be outside runtime resource and license folders.");
		}

		for (const auto &entry : fs::recursive_directory_iterator(source)) {
			if (GetFileAttributesW(entry.path().c_str()) &
			    FILE_ATTRIBUTE_REPARSE_POINT) {
				throw std::runtime_error(
				    "Runtime resources cannot contain links.");
			}

			if (entry.is_regular_file()) {
				File(entry.path(),
				     relative / entry.path().lexically_relative(source));
			}
		}
	}

	const auto &Files() const {
		return files_;
	}

  private:
	fs::path destination_;
	std::map<fs::path, fs::path, PathLess> files_;
	std::set<fs::path, PathLess> directories_;
};
} // namespace

bool GameExporter::Export(const Scene &scene, const GameExportOptions &options,
                          std::string &error) {
	OutputDirectory output;
	try {
		error.clear();
		PathUtils::RelativeInside(fs::path(L"resources"),
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
			PathUtils::RelativeInside(resourceRoot, destination);
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
			if (object.meshRenderer) {
				for (auto &[slot, path] : object.meshRenderer->textures.slots) {
					const auto input =
					    path.is_absolute()
					        ? path
					        : resourceRoot / PathUtils::RelativeInside(
					                             fs::path(L"resources"),
					                             path.lexically_normal());
					path = fs::path(L"resources") /
					       PathUtils::RelativeInside(resourceRoot,
					                                 assets.AddTexture(input));
				}
			}

			if (object.HasMesh()) {
				const auto path =
				    object.mesh->modelPath.is_absolute()
				        ? object.mesh->modelPath
				        : resourceRoot /
				              PathUtils::RelativeInside(
				                  fs::path(L"resources"),
				                  object.mesh->modelPath.lexically_normal());
				const auto model = assets.AddModel(path);
				object.mesh->modelPath =
				    fs::path(L"resources") /
				    PathUtils::RelativeInside(resourceRoot, model);
			}
		}

		Scene snapshot;
		snapshot.ReplaceObjects(std::move(objects));
		CopyPlan plan(destination);
		plan.Reserve(options.config.scenePath);
		plan.Reserve(L"game.json");
		plan.File(runtimeRoot / L"Runtime.exe", L"Game.exe");

		for (const auto *name :
		     {L"dxcompiler.dll", L"dxil.dll", L"GameInputRedist.dll",
		      L"GameInputRawInputProxy.exe"}) {
			plan.File(runtimeRoot / name, name);
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
				plan.File(entry.path(), entry.path().filename());
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

		plan.Tree(runtimeRoot / L"resources" / L"engine", L"resources/engine");
		plan.Tree(runtimeRoot / L"licenses", L"licenses");

		for (const auto &file : assets.Files()) {
			plan.File(file, fs::path(L"resources") /
			                    PathUtils::RelativeInside(resourceRoot, file));
		}

		const auto assetLicenses = resourceRoot / L"licenses";

		if (fs::is_directory(assetLicenses)) {
			plan.Tree(assetLicenses, L"licenses/assets");
		}

		std::string sceneText, configText;

		if (!SceneSerializer::Serialize(snapshot, sceneText)) {
			throw std::runtime_error("Could not serialize the exported scene.");
		}

		if (!options.config.Serialize(configText, problem)) {
			throw std::runtime_error(problem);
		}

		output.Create(destination);

		for (const auto &[relative, source] : plan.Files()) {
			if (!source.empty() && relative != L"Game.exe") {
				output.Copy(source, relative);
			}
		}

		Assets exportedAssets(fs::canonical(destination / L"resources"));

		for (const auto &[id, object] : snapshot.GetObjects()) {
			if (object.meshRenderer) {
				for (const auto &[slot, path] :
				     object.meshRenderer->textures.slots) {
					exportedAssets.AddTexture(destination / path);
				}
			}

			if (object.HasMesh()) {
				exportedAssets.AddModel(destination / object.mesh->modelPath);
			}
		}

		output.Text(options.config.scenePath, sceneText);
		output.Text(L"game.json", configText);
		output.Copy(runtimeRoot / L"Runtime.exe", L"Game.exe");
		output.Complete();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		error += output.Cleanup();
		return false;
	} catch (...) {
		error = "An unknown error interrupted game export.";
		error += output.Cleanup();
		return false;
	}
}
