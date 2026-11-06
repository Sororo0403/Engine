#include "export/GameExporter.h"
#include "assets/AssetDependencies.h"
#include "assets/ImageValidation.h"
#include "assets/SceneDependencyGraph.h"
#include "audio/WaveReader.h"
#include "files/OutputDirectory.h"
#include "gameplay/SceneSimulation.h"
#include "platform/windows/PathUtils.h"
#include "scene/SceneSerializer.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <json.hpp>
#include <map>
#include <new>
#include <set>
#include <stdexcept>
#include <system_error>
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
/// 正規化した参照先が、指定したフォルダの内側にあるか判定する
/// </summary>
/// <param name="root">基準フォルダの絶対パス</param>
/// <param name="path">判定する絶対パス</param>
/// <returns>true:同じ場所または配下 false:別のフォルダ</returns>
bool IsInside(const fs::path &root, const fs::path &path) {
	try {
		PathUtils::RelativeInside(root, path);
		return true;
	} catch (const std::runtime_error &) {
		return false;
	}
}

/// <summary>
/// Runtimeの確認情報を厳密に読み、構成と対応形式を照合する。不正な場合は再ビルドの案内を含む例外
/// </summary>
/// <param name="directory">正規化済みのRuntime取得元</param>
void ValidateRuntimeBuild(const fs::path &directory) {
	std::ifstream file(directory / L"Runtime.build.json", std::ios::binary);

	if (!file) {
		throw std::runtime_error(
		    "Runtime build information could not be read. Rebuild Release "
		    "Runtime before exporting.");
	}

	try {
		const auto build = nlohmann::json::parse(file);
		const auto configuration = build.at("configuration").get<std::string>();
		const auto architecture = build.at("architecture").get<std::string>();

		if (configuration != "Release" || architecture != "x64") {
			throw std::runtime_error(
			    "A Release x64 Runtime build is required. Rebuild Release "
			    "Runtime before exporting.");
		}

		if (!build.contains("gameConfigVersion") ||
		    !build.at("gameConfigVersion").is_number_integer() ||
		    build.at("gameConfigVersion") != GameConfig::kCurrentVersion ||
		    !build.contains("sceneVersion") ||
		    !build.at("sceneVersion").is_number_integer() ||
		    build.at("sceneVersion") != SceneSerializer::kCurrentVersion) {
			throw std::runtime_error(
			    "Runtime format versions do not match the Editor. Rebuild "
			    "Release Runtime before exporting.");
		}
	} catch (const nlohmann::json::exception &) {
		throw std::runtime_error(
		    "Runtime build information is invalid. Rebuild Release Runtime "
		    "before exporting.");
	}
}

/// <summary>
/// 取得元と、それに対応するパッケージ内の名前空間を保持する
/// </summary>
struct AssetSource {
	fs::path directory;
	fs::path output;
	bool used = false;
};

/// <summary>
/// 明示した取得元からモデル・バッファ・画像・材質を収集し、相対構成を保って出力先へ対応付ける
/// </summary>
class Assets {
  public:
	explicit Assets(fs::path root,
	                const std::vector<fs::path> &additional = {}) {
		if (!fs::is_directory(root)) {
			throw std::invalid_argument("Choose an existing resource folder.");
		}
		sources_.push_back({std::move(root), L"resources"});
		for (std::size_t i = 0; i < additional.size(); ++i) {
			const auto &input = additional[i];
			if (input.empty() ||
			    input.native().find(L'\0') != std::wstring::npos) {
				throw std::invalid_argument(
				    "Choose valid external asset folders.");
			}

			const auto directory =
			    fs::canonical(PathUtils::ResolveFromExecutable(input));
			if (!fs::is_directory(directory)) {
				throw std::invalid_argument(
				    "An external asset source is not a folder.");
			}
			if (std::ranges::any_of(sources_, [&](const auto &source) {
				    return IsInside(source.directory, directory);
			    })) {
				continue;
			}

			// 追加元どうしが重なる場合は、依存構成を保てる広いフォルダへまとめる。
			// 組み込み側の取得元は優先し、従来の出力パスを保つ。
			std::erase_if(sources_, [&](const auto &source) {
				return source.output != L"resources" &&
				       IsInside(directory, source.directory);
			});
			sources_.push_back({directory, fs::path(L"resources/external") /
			                                   std::to_wstring(i)});
		}
	}

	fs::path AddModel(const fs::path &input) {
		const auto index = FindSource(input);
		const auto model = ResourceFile(sources_[index].directory, input);
		if (const auto found = models_.find(model); found != models_.end()) {
			return found->second;
		}

		const auto reference = AddFile(index, model);
		for (const auto &file : AssetDependencies::ModelFiles(model)) {
			AddFile(index, file);
		}
		models_.emplace(model, reference);
		return reference;
	}

	fs::path AddTexture(const fs::path &input) {
		AssetDependencies::TextureFiles(input);
		const auto reference = AddFile(FindSource(input), input);
		const auto &file = files_.at(reference);

		if (textures_.insert(file).second) {
			try {
				ImageValidation::Read(file);
			} catch (const std::bad_alloc &) {
				throw;
			} catch (const std::exception &error) {
				throw std::runtime_error(
				    std::format("Could not export image \"{}\": {}",
				                PathUtils::ToUtf8(file), error.what()));
			}
		}

		return reference;
	}

	fs::path AddAudio(const fs::path &input, bool spatial) {
		AssetDependencies::AudioFiles(input);
		const auto reference = AddFile(FindSource(input), input);
		const auto &file = files_.at(reference);
		auto found = audioChannels_.find(file);
		if (found == audioChannels_.end()) {
			WaveData wave;
			std::string error;
			if (!WaveReader::Load(file, wave, error))
				throw std::runtime_error(
				    std::format("Could not export audio \"{}\": {}",
				                PathUtils::ToUtf8(file), error));
			found = audioChannels_.emplace(file, wave.channels).first;
		}

		if (spatial && found->second != 1)
			throw std::runtime_error(
			    "Spatial Audio Source needs a mono WAV clip: " +
			    PathUtils::ToUtf8(file));
		return reference;
	}

	void ValidateScene(const fs::path &path) {
		const auto index = FindSource(path);
		ResourceFile(sources_[index].directory, path);
		sources_[index].used = true;
	}

	const auto &Files() const {
		return files_;
	}

	const auto &Sources() const {
		return sources_;
	}

  private:
	/// <summary>
	/// 参照先を許可された取得元へ対応付ける。リンクや実体の範囲はResourceFileで検証する
	/// </summary>
	/// <param name="path">素材の絶対パス</param>
	/// <returns>対応する取得元の添字。取得元外や無効な指定は例外</returns>
	std::size_t FindSource(const fs::path &path) const {
		if (!path.is_absolute() ||
		    path.native().find(L'\0') != std::wstring::npos) {
			throw std::invalid_argument("Use valid absolute asset references.");
		}
		for (std::size_t i = 0; i < sources_.size(); ++i) {
			if (IsInside(sources_[i].directory, path.lexically_normal())) {
				return i;
			}
		}
		throw std::runtime_error(
		    "An asset is outside the configured resource folders: " +
		    PathUtils::ToUtf8(path));
	}

	/// <summary>
	/// 同じ取得元の構成を保持して通常ファイルを登録する。出力先の衝突は拒否する
	/// </summary>
	/// <param name="index">モデル本体と共通の取得元</param>
	/// <param name="path">依存ファイルの絶対パス</param>
	/// <returns>パッケージ基準の参照</returns>
	fs::path AddFile(std::size_t index, const fs::path &path) {
		auto &source = sources_[index];
		const auto file = ResourceFile(source.directory, path);
		const auto relative =
		    source.output / PathUtils::RelativeInside(source.directory, file);
		const auto [found, inserted] = files_.try_emplace(relative, file);
		if (!inserted && !fs::equivalent(found->second, file)) {
			throw std::runtime_error("Asset output paths conflict: " +
			                         PathUtils::ToUtf8(relative));
		}
		source.used = true;
		return relative;
	}

	std::vector<AssetSource> sources_;
	std::map<fs::path, fs::path, PathLess> files_, models_;
	std::set<fs::path, PathLess> textures_;
	std::map<fs::path, std::uint16_t, PathLess> audioChannels_;
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

		for (const auto &path :
		     {options.resourceDirectory, options.runtimeDirectory}) {
			if (path.empty() ||
			    path.native().find(L'\0') != std::wstring::npos ||
			    (path.has_root_path() && !path.is_absolute())) {
				throw std::invalid_argument(
				    "Choose valid resource and Runtime folders.");
			}
		}
		const auto resourceRoot = fs::canonical(options.resourceDirectory);
		std::error_code runtimeError;
		const auto runtimeRoot =
		    fs::canonical(options.runtimeDirectory, runtimeError);

		if (runtimeError || !fs::is_directory(runtimeRoot, runtimeError)) {
			throw std::runtime_error(
			    "Release Runtime output folder is unavailable. Build Release "
			    "Runtime before exporting.");
		}

		auto graph = SceneDependencyGraph::Read(
		    scene, {},
		    [&](const auto &path) {
			    const auto absolute =
			        path.is_absolute()
			            ? path
			            : resourceRoot / PathUtils::RelativeInside(
			                                 fs::path(L"resources"),
			                                 path.lexically_normal());
			    return ResourceFile(fs::canonical(absolute.parent_path()),
			                        absolute);
		    },
		    [&](const auto &path, Scene &loaded) {
			    if (!SceneSerializer::Load(loaded, path))
				    throw std::runtime_error(
				        "Could not read destination scene: " +
				        PathUtils::ToUtf8(path));
		    });
		auto sourceDirectories = options.assetDirectories;
		for (const auto &entry : graph)
			if (!entry.source.empty())
				sourceDirectories.push_back(entry.source.parent_path());
		Assets assets(resourceRoot, sourceDirectories);
		std::vector<fs::path> scenePaths{options.config.scenePath};
		for (std::size_t i = 1; i < graph.size(); ++i) {
			assets.ValidateScene(graph[i].source);
			scenePaths.push_back(fs::path(L"resources/scenes/transitions") /
			                     std::format("Scene{}.scene.json", i));
		}
		for (const auto &source : assets.Sources()) {
			if (IsInside(source.directory, destination)) {
				throw std::runtime_error(
				    "Output must be outside all resource and asset folders.");
			}
		}

		ValidateRuntimeBuild(runtimeRoot);

		for (auto &entry : graph) {
			Scene candidate;
			candidate.ReplaceObjects(entry.objects);
			SceneSimulation check;
			std::string issue;
			if (!check.Start(candidate, issue, options.config.fallbackCamera))
				throw std::runtime_error(issue);
			auto &objects = entry.objects;

			for (auto &[id, object] : objects) {
				object.prefabLink.reset();
				if (object.triggerActions)
					for (auto &action : object.triggerActions->actions) {
						if (action.operation !=
						        TriggerAction::Operation::LOAD_SCENE ||
						    action.scenePath.empty())
							continue;
						const auto found = std::ranges::find_if(
						    graph, [&](const auto &target) {
							    return !target.source.empty() &&
							           PathUtils::IsSameFile(target.source,
							                                 action.scenePath);
						    });
						if (found == graph.end())
							throw std::runtime_error("Destination scene is not "
							                         "in the export graph.");
						action.scenePath = scenePaths[static_cast<std::size_t>(
						    found - graph.begin())];
					}
				if (object.audioSource &&
				    !object.audioSource->clipPath.empty()) {
					auto &path = object.audioSource->clipPath;
					const auto input =
					    path.is_absolute()
					        ? path
					        : resourceRoot / PathUtils::RelativeInside(
					                             fs::path(L"resources"),
					                             path.lexically_normal());
					path = assets.AddAudio(input, object.audioSource->spatial);
				}

				if (object.meshRenderer) {
					for (auto &[slot, path] :
					     object.meshRenderer->textures.slots) {
						const auto input =
						    path.is_absolute()
						        ? path
						        : resourceRoot / PathUtils::RelativeInside(
						                             fs::path(L"resources"),
						                             path.lexically_normal());
						path = assets.AddTexture(input);
					}
				}

				if (object.HasMesh()) {
					const auto path =
					    object.mesh->modelPath.is_absolute()
					        ? object.mesh->modelPath
					        : resourceRoot / PathUtils::RelativeInside(
					                             fs::path(L"resources"),
					                             object.mesh->modelPath
					                                 .lexically_normal());
					object.mesh->modelPath = assets.AddModel(path);
				}
			}
		}
		std::string problem;
		CopyPlan plan(destination);
		for (const auto &path : scenePaths)
			plan.Reserve(path);
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

		for (const auto &[relative, file] : assets.Files()) {
			plan.File(file, relative);
		}

		for (const auto &source : assets.Sources()) {
			// 従来のライセンス配置を保ち、未使用の追加フォルダからはコピーしない。
			if (!source.used && source.output != L"resources") {
				continue;
			}
			const auto licenses = source.directory / L"licenses";
			if (fs::is_directory(licenses)) {
				plan.Tree(licenses,
				          fs::path(L"licenses/assets") /
				              source.output.lexically_relative(L"resources"));
			}
		}

		std::string configText;
		std::vector<std::string> sceneTexts;
		for (const auto &entry : graph) {
			Scene snapshot;
			snapshot.ReplaceObjects(entry.objects);
			std::string text;
			if (!SceneSerializer::Serialize(snapshot, text))
				throw std::runtime_error(
				    "Could not serialize an exported scene.");
			sceneTexts.push_back(std::move(text));
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

		for (const auto &entry : graph)
			for (const auto &[id, object] : entry.objects) {
				if (object.audioSource && !object.audioSource->clipPath.empty())
					exportedAssets.AddAudio(destination /
					                            object.audioSource->clipPath,
					                        object.audioSource->spatial);
				if (object.meshRenderer) {
					for (const auto &[slot, path] :
					     object.meshRenderer->textures.slots) {
						exportedAssets.AddTexture(destination / path);
					}
				}

				if (object.HasMesh()) {
					exportedAssets.AddModel(destination /
					                        object.mesh->modelPath);
				}
			}

		for (std::size_t i = 0; i < scenePaths.size(); ++i)
			output.Text(scenePaths[i], sceneTexts[i]);
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
