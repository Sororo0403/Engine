#include "export/GameExporter.h"
#include "animation/AnimationClipAsset.h"
#include "assets/AssetDependencies.h"
#include "assets/ImageValidation.h"
#include "assets/SceneDependencyGraph.h"
#include "audio/WaveReader.h"
#include "files/OutputDirectory.h"
#include "gameplay/SceneScripts.h"
#include "gameplay/SceneSimulation.h"
#include "gameplay/SceneValidation.h"
#include "platform/windows/PathUtils.h"
#include "scene/PrefabSerializer.h"
#include "scene/SceneAssetReferences.h"
#include "scene/SceneLighting.h"
#include "scene/SceneSerializer.h"
#include "scripting/LuaScript.h"
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

constexpr std::array kRuntimeBinaries{
    L"dxcompiler.dll", L"dxil.dll", L"GameInputRedist.dll",
    L"GameInputRawInputProxy.exe"};
constexpr std::array kRuntimeLibraries{
    L"msvcp140.dll", L"vcruntime140.dll", L"vcruntime140_1.dll"};
constexpr std::array kRuntimeShaders{
    L"shaders/mesh/Mesh.VS.hlsl", L"shaders/mesh/Mesh.PS.hlsl",
    L"shaders/mesh/Shadow.VS.hlsl", L"shaders/mesh/SkinnedMesh.VS.hlsl",
    L"shaders/mesh/SkinnedShadow.VS.hlsl", L"shaders/mesh/Skinning.hlsli",
    L"shaders/particles/Particle.VS.hlsl", L"shaders/particles/Particle.PS.hlsl",
    L"shaders/debug/DebugLine.VS.hlsl", L"shaders/debug/DebugLine.PS.hlsl"};

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
/// シミュレーションを検証できないときも、外部クリップの割り当てを確認する
/// </summary>
void CheckAnimationBindings(AnimationClipComponent player, const fs::path &path,
                            const Scene &scene) {
	player.asset = path;
	AnimationClipComponent clip;
	std::string error;
	if (!AnimationClipAsset::Resolve(player, clip, error))
		throw std::runtime_error(error);

	for (const auto &track : clip.tracks) {
		const auto *target = scene.FindObject(track.target);
		if (!target || !target->CanAnimate(track))
			throw std::runtime_error(
			    "Animation Clip target is missing a required component "
			    "or has another transform controller.");
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
		if (!build.contains("scriptApiVersion") ||
		    !build.at("scriptApiVersion").is_number_integer() ||
		    build.at("scriptApiVersion") != LuaScript::kApiVersion)
			throw std::runtime_error(
			    "Runtime script API version does not match the Editor. Rebuild "
			    "Release Runtime before exporting.");
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
/// 配布先に含められないモデルの依存ファイルと理由を保持する
/// </summary>
class ModelDependencyExportError : public std::runtime_error {
  public:
	using Failure = std::pair<fs::path, std::string>;

	explicit ModelDependencyExportError(std::vector<Failure> items)
	    : std::runtime_error(items.front().second),
	      failures(std::move(items)) {
	}

	std::vector<Failure> failures;
};

/// <summary>
/// 取得元ごとに素材と依存ファイルを登録する
/// </summary>
class Assets {
  public:
	explicit Assets(fs::path root) {
		if (!fs::is_directory(root)) {
			throw std::invalid_argument("Choose an existing resource folder.");
		}

		sources_.push_back({std::move(root), L"resources"});
	}

	void AddSource(const fs::path &input, std::size_t index) {
		if (input.empty() || input.native().find(L'\0') != std::wstring::npos) {
			throw std::invalid_argument("Choose valid external asset folders.");
		}

		const auto directory = fs::canonical(PathUtils::ResolveFromExecutable(input));
		if (!fs::is_directory(directory)) {
			throw std::invalid_argument("An external asset source is not a folder.");
		}

		if (std::ranges::any_of(sources_, [&](const auto &source) {
			    return IsInside(source.directory, directory);
		    })) {
			return;
		}

		// 追加元どうしが重なる場合は、依存構成を保てる広いフォルダへまとめる。
		// 組み込み側の取得元は優先し、従来の出力パスを保つ。
		std::erase_if(sources_, [&](const auto &source) {
			return source.output != L"resources" && IsInside(directory, source.directory);
		});
		sources_.push_back({directory, fs::path(L"resources/external") /
		                                   std::to_wstring(index)});
	}

	fs::path AddModel(const fs::path &input) {
		const auto index = FindSource(input);
		const auto reference = AddFile(index, input);
		const auto &model = files_.at(reference);

		if (const auto found = models_.find(model); found != models_.end()) {
			return found->second;
		}

		std::vector<ModelDependencyExportError::Failure> failures;
		std::set<fs::path> failedPaths;
		for (const auto &file : AssetDependencies::ModelFiles(model)) {
			try {
				AddFile(index, file);
			} catch (const std::bad_alloc &) {
				throw;
			} catch (const std::exception &error) {
				auto normalized = file.lexically_normal();
				if (failedPaths.insert(normalized).second) {
					auto reason = std::format("Could not include model dependency {}: {}",
					                          PathUtils::ToUtf8(normalized), error.what());
					failures.emplace_back(std::move(normalized), std::move(reason));
				}
			}
		}
		if (!failures.empty())
			throw ModelDependencyExportError(std::move(failures));

		models_.emplace(model, reference);
		return reference;
	}

	fs::path AddTexture(const fs::path &input,
	                    unsigned maximumDimension = 16384) {
		AssetDependencies::TextureFiles(input);
		const auto reference = AddFile(FindSource(input), input);
		const auto &file = files_.at(reference);

		const auto validated = textures_.find(file);

		if (validated == textures_.end() ||
		    maximumDimension < validated->second) {
			try {
				ImageValidation::Read(file, maximumDimension);
				textures_.insert_or_assign(file, maximumDimension);
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

	/// <summary>
	/// Luaを構文確認し、元のフォルダ構成を保って出力一覧へ登録する
	/// </summary>
	/// <param name="input">許可された取得元の絶対パス</param>
	/// <param name="invalidScript">構文検査に失敗した場合の編集先。不要ならnullptr</param>
	/// <returns>出力先基準のLuaアセット参照</returns>
	fs::path AddScript(const fs::path &input, fs::path *invalidScript = nullptr) {
		const auto reference = AddFile(FindSource(input), input);
		const auto &file = files_.at(reference);

		if (!scripts_.contains(file)) {
			std::string source, error;
			if (!LuaScript::Read(file, source, error))
				throw std::runtime_error(error);
			auto scriptPath = file;
			scriptPath.make_preferred();
			LuaScriptProgram program;
			if (!LuaScript::Compile(source, PathUtils::ToUtf8(scriptPath), program,
			                        error)) {
				if (invalidScript)
					*invalidScript = std::move(scriptPath);
				throw std::runtime_error(error);
			}
			scripts_.insert(file);
		}

		return reference;
	}

	fs::path AddAnimationClip(const fs::path &input) {
		const auto reference = AddFile(FindSource(input), input);
		const auto &file = files_.at(reference);
		if (!animationClips_.contains(file)) {
			AnimationClipComponent clip;
			std::string error;
			if (!AnimationClipAsset::Load(file, clip, error))
				throw std::runtime_error(error);
			animationClips_.insert(file);
		}
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
		// リンク検査を省略できるのは、同じ取得元と検査済みの生パスの組だけ。
		const auto key = std::pair{index, path.native()};
		if (const auto found = registeredFiles_.find(key);
		    found != registeredFiles_.end()) {
			return found->second;
		}

		auto &source = sources_[index];
		const auto file = ResourceFile(source.directory, path);
		const auto relative =
		    source.output / PathUtils::RelativeInside(source.directory, file);
		const auto [found, inserted] = files_.try_emplace(relative, file);

		if (!inserted && found->second.native() != file.native() &&
		    !fs::equivalent(found->second, file)) {
			throw std::runtime_error("Asset output paths conflict: " +
			                         PathUtils::ToUtf8(relative));
		}

		source.used = true;
		registeredFiles_.emplace(key, relative);
		return relative;
	}

	std::vector<AssetSource> sources_;
	std::map<fs::path, fs::path, PathLess> files_, models_;
	std::map<std::pair<std::size_t, fs::path::string_type>, fs::path> registeredFiles_;
	std::map<fs::path, unsigned, PathLess> textures_;
	std::set<fs::path, PathLess> scripts_;
	std::set<fs::path, PathLess> animationClips_;
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

/// <summary>
/// 既存の項目と重ならない出力先を取得する
/// </summary>
fs::path ResolveDestination(const fs::path &path) {
	if (path.empty() || path.native().find(L'\0') != std::wstring::npos ||
	    (path.has_root_path() && !path.is_absolute())) {
		throw std::runtime_error("Choose a new output folder.");
	}

	// Windowsの絶対パス変換で末尾のドットなどが取り除かれる前に検査する。
	for (const auto &part : path.relative_path()) {
		if (!part.empty() && part != L"." && part != L".." &&
		    !PathUtils::IsValidFileName(part)) {
			throw std::runtime_error("Invalid output folder name: " +
			                         PathUtils::ToUtf8(part));
		}
	}
	auto absolute = fs::absolute(path);
	// フォルダのパスを貼り付けたときに末尾の区切りがあっても同じ出力先として扱う。
	if (!absolute.has_filename() && absolute.has_relative_path()) {
		absolute = absolute.parent_path();
	}
	auto destination = fs::weakly_canonical(absolute);
	if (!destination.has_filename() && destination.has_relative_path()) {
		destination = destination.parent_path();
	}
	if (fs::exists(fs::symlink_status(absolute)) || fs::exists(destination)) {
		throw std::runtime_error(
		    "Output folder already exists. Choose a new folder.");
	}

	for (auto parent = destination.parent_path(); !parent.empty();) {
		if (fs::exists(parent)) {
			if (fs::is_directory(parent)) {
				return destination;
			}
			throw std::runtime_error("An output parent is a file: " +
			                         PathUtils::ToUtf8(parent));
		}
		auto next = parent.parent_path();
		if (next == parent) {
			break;
		}
		parent = std::move(next);
	}
	throw std::runtime_error("The output folder has no accessible parent folder.");
}

bool ProcessExport(const Scene &scene, const GameExportOptions &options,
                   std::string &error, bool writeOutput,
                   std::vector<GameExportIssue> *issues) {
	OutputDirectory output;
	const auto appendIssue = [&](std::string issue, const fs::path &scenePath,
	                             SceneObjectId objectId,
	                             const fs::path &scriptPath,
	                             const fs::path &assetPath,
	                             const fs::path &locationPath = {}) {
		if (!error.empty()) {
			error += "\n\n";
		}
		error += issue;
		if (issues) {
			issues->push_back(
			    {std::move(issue), scenePath, objectId, scriptPath, assetPath,
			     locationPath});
		}
	};
	// 出力前に、同じ検査で独立して直せる問題をまとめる。
	const auto reportIssue = [&](std::string issue, const fs::path &scenePath = {},
	                             SceneObjectId objectId = 0,
	                             const fs::path &scriptPath = {},
	                             const fs::path &assetPath = {}) {
		appendIssue(std::move(issue), scenePath, objectId, scriptPath, assetPath);
	};

	try {
		error.clear();
		if (issues) {
			issues->clear();
		}
		if (!options.config.HasValidWindowSettings()) {
			reportIssue("Enter a title and a valid window size (1-8192).");
		}
		const bool startupSettingsValid = options.config.HasValidStartupSettings();
		if (!startupSettingsValid) {
			reportIssue("Game startup settings are invalid. Check Game Settings.");
		} else {
			try {
				PathUtils::RelativeInside(fs::path(L"resources"),
				                          options.config.scenePath.lexically_normal());
			} catch (const std::runtime_error &issue) {
				reportIssue(issue.what());
			}
		}

		fs::path destination;
		try {
			destination = ResolveDestination(options.destination);
		} catch (const std::bad_alloc &) {
			throw;
		} catch (const std::exception &issue) {
			const auto &path = options.destination;
			const auto location =
			    path.is_absolute() && path.native().find(L'\0') == std::wstring::npos
			        ? path : fs::path{};
			appendIssue(issue.what(), {}, 0, {}, {}, location);
		}

		const auto validDirectoryPath = [](const fs::path &path) {
			return !path.empty() && path.native().find(L'\0') == std::wstring::npos &&
			       (!path.has_root_path() || path.is_absolute());
		};
		fs::path runtimeRoot;
		fs::path runtimeLocation;
		try {
			if (!validDirectoryPath(options.runtimeDirectory)) {
				throw std::invalid_argument("Choose a valid Runtime folder.");
			}
			std::error_code runtimeError;
			runtimeLocation = fs::absolute(options.runtimeDirectory, runtimeError);
			runtimeRoot = fs::canonical(options.runtimeDirectory, runtimeError);
			if (runtimeError || !fs::is_directory(runtimeRoot, runtimeError)) {
				throw std::runtime_error(
				    "Release Runtime output folder is unavailable. Build Release "
				    "Runtime before exporting.");
			}
			ValidateRuntimeBuild(runtimeRoot);
			const auto checkRuntimeFile = [&](const fs::path &path) {
				std::error_code status;
				if (!fs::is_regular_file(path, status) ||
				    (GetFileAttributesW(path.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)) {
					appendIssue("A required Runtime file is missing, inaccessible, or linked: " +
					                PathUtils::ToUtf8(path) + "\nRebuild Release Runtime.",
					            {}, 0, {}, {}, path);
				}
			};
			checkRuntimeFile(runtimeRoot / L"Runtime.exe");
			for (const auto *name : kRuntimeBinaries)
				checkRuntimeFile(runtimeRoot / name);
			for (const auto *name : kRuntimeLibraries)
				checkRuntimeFile(runtimeRoot / name);
			for (const auto *shader : kRuntimeShaders)
				checkRuntimeFile(runtimeRoot / L"resources/engine" / shader);
		} catch (const std::bad_alloc &) {
			throw;
		} catch (const std::exception &issue) {
			appendIssue(issue.what(), {}, 0, {}, {}, runtimeLocation);
		}

		if (!validDirectoryPath(options.resourceDirectory)) {
			throw std::invalid_argument("Choose a valid resource folder.");
		}
		const auto resourceRoot = fs::canonical(options.resourceDirectory);

		const auto reportObjectIssue = [&](std::string issue, const auto &entry,
		                                   SceneObjectId id,
		                                   const fs::path &scriptPath = {},
		                                   const fs::path &assetPath = {}) {
			appendIssue(std::move(issue), entry.prefab ? fs::path{} : entry.source,
			            entry.prefab || !entry.objects.contains(id) ? 0 : id,
			            scriptPath, assetPath, entry.prefab ? entry.source : fs::path{});
		};

		bool startupIsTarget = false;
		bool incompleteGraph = false;
		const auto resolveReference = [&](const fs::path &path) {
			return path.is_absolute()
			           ? path
			           : resourceRoot / PathUtils::RelativeInside(
			                                fs::path(L"resources"),
			                                path.lexically_normal());
		};
		auto graph = SceneDependencyGraph::Read(
		    scene, options.startupScenePath,
		    [&](const auto &path) {
			    const auto absolute = resolveReference(path);
			    auto resolved = ResourceFile(fs::canonical(absolute.parent_path()),
			                                 absolute);
			    if (!startupIsTarget && !options.startupScenePath.empty()) {
				    startupIsTarget =
				        PathUtils::IsSameFile(resolved, options.startupScenePath);
			    }
			    return resolved;
		    },
		    [&](const auto &path, Scene &loaded) {
			    if (!SceneSerializer::Load(loaded, path))
				    throw std::runtime_error(
				        "Could not read destination scene: " +
				        PathUtils::ToUtf8(path));
		    },
		    [&](const auto &path, Scene &loaded, SceneObjectId &counter) {
			    std::string issue;
			    if (!PrefabSerializer::Load(path, loaded, issue, &counter))
				    throw std::runtime_error(issue);
		    },
		    [&](std::string issue, const auto &entry, SceneObjectId id,
		        const fs::path &path) {
			    incompleteGraph = true;
			    fs::path target;
			    try {
				    target = resolveReference(path);
			    } catch (const std::bad_alloc &) {
				    throw;
			    } catch (const std::exception &) {
				    // 解決できない参照は、参照元への移動だけを提供する。
			    }
			    reportObjectIssue(std::move(issue), entry, id, {}, target);
		    });
		auto sourceDirectories = options.assetDirectories;
		for (std::size_t i = 1; i < graph.size(); ++i)
			sourceDirectories.push_back(graph[i].source.parent_path());
		Assets assets(resourceRoot);
		for (std::size_t i = 0; i < sourceDirectories.size(); ++i) {
			const auto &path = sourceDirectories[i];
			try {
				assets.AddSource(path, i);
			} catch (const std::bad_alloc &) {
				throw;
			} catch (const std::exception &issue) {
				const auto location = validDirectoryPath(path) ? path : fs::path{};
				appendIssue("Could not use external asset folder: " + PathUtils::ToUtf8(location) +
				                "\n" + issue.what(), {}, 0, {}, {}, location);
			}
		}
		auto config = options.config;
		// Luaの遷移先として参照できる名前で、起動シーンも出力する。
		if (startupIsTarget &&
		    !PathUtils::HasExtension(config.scenePath.stem(), ".scene")) {
			config.scenePath.replace_extension(L".scene.json");
		}
		std::vector<fs::path> scenePaths{config.scenePath};

		for (std::size_t i = 1; i < graph.size(); ++i) {
			const auto &entry = graph[i];
			try {
				assets.ValidateScene(entry.source);
			} catch (const std::bad_alloc &) {
				throw;
			} catch (const std::exception &issue) {
				reportObjectIssue("Could not use scene dependency: " + PathUtils::ToUtf8(entry.source) +
				                      "\n" + issue.what(), entry, 0);
			}
			if (graph[i].prefab)
				scenePaths.push_back(fs::path(L"resources/prefabs") /
				                     std::format("Prefab{}.prefab.json", i));
			else
				scenePaths.push_back(fs::path(L"resources/scenes/transitions") /
				                     std::format("Scene{}.scene.json", i));
		}

		for (const auto &source : assets.Sources()) {
			if (!destination.empty() && IsInside(source.directory, destination)) {
				appendIssue(
				    "Output must be outside all resource and asset folders: " +
				        PathUtils::ToUtf8(source.directory),
				    {}, 0, {}, {}, destination);
				break;
			}
		}

		// アセット参照を出力先へ書き換える前に、遷移先の到着地点を検証する。
		std::set<std::pair<std::size_t, std::string>> checkedArrivals;

		for (const auto &entry : graph) {
			for (const auto &[id, object] : entry.objects) {
				if (!startupSettingsValid || !object.triggerActions)
					continue;
				for (const auto &action : object.triggerActions->actions) {
					if (action.operation !=
					        TriggerAction::Operation::LOAD_SCENE ||
					    action.scenePath.empty() || action.arrivalTag.empty())
						continue;
					const auto found =
					    std::ranges::find_if(graph, [&](const auto &target) {
						    return !target.prefab && !target.source.empty() &&
						           PathUtils::IsSameFile(target.source,
						                                 action.scenePath);
					    });
					if (found == graph.end()) {
						// 読み込めなかった遷移先は依存関係の検査で報告済み。
						if (incompleteGraph)
							continue;
						throw std::runtime_error(
						    "Tagged arrival requires a destination scene in "
						    "the export graph.");
					}
					const auto index =
					    static_cast<std::size_t>(found - graph.begin());
					if (!checkedArrivals.emplace(index, action.arrivalTag)
					         .second)
						continue;
					Scene candidate;
					candidate.ReplaceObjects(found->objects);
					std::string issue;
					SceneObjectId invalidObject = 0;
					if (!SceneSimulation::ValidateArrival(
					        candidate, action.arrivalTag, issue,
					        options.config.fallbackCamera, &invalidObject))
						reportIssue(
						    std::format("Load Scene on object ID {} to '{}' "
						                "(Arrival Tag '{}'): {}",
						                id, PathUtils::ToUtf8(found->source),
							                action.arrivalTag, issue),
						    found->source, invalidObject);
				}
			}
		}

		for (auto &entry : graph) {
			const auto sceneLabel = entry.source.empty()
			                            ? "Startup scene"
			                            : PathUtils::ToUtf8(entry.source);
			const auto describe = [&](SceneObjectId id) {
				const auto found = entry.objects.find(id);
				return found == entry.objects.end()
				           ? sceneLabel
				           : std::format("{} / {} (ID {})", sceneLabel,
				                         found->second.name, id);
			};
			Scene candidate;
			candidate.ReplaceObjects(entry.objects);
			const auto inputIssues = SceneValidation::InspectInputs(
			    candidate, options.config.actions);
			for (const auto &inputIssue : inputIssues) {
				reportObjectIssue(
				    describe(inputIssue.objectId) + "\n" + inputIssue.message,
				    entry, inputIssue.objectId);
			}
			SceneSimulation check;
			std::string issue;
			SceneObjectId invalidObject = 0;
			// 参照先が欠けている場合も、読めたシーンの入力設定と素材を検査する。
			const bool canCheckSimulation =
			    startupSettingsValid && !incompleteGraph && !entry.prefab;
			if (canCheckSimulation &&
			    !(&entry == &graph.front()
			          ? check.Start(candidate, issue,
			                        options.config.fallbackCamera,
			                        &invalidObject)
			          : SceneSimulation::Validate(
			                candidate, issue, options.config.fallbackCamera,
			                &invalidObject))) {
				auto scriptPath = SceneScripts::FindFailedScript(candidate, invalidObject, issue);
				std::error_code status;
				if (!scriptPath.empty()) {
					scriptPath = PathUtils::ResolveFromExecutable(scriptPath);
					if (!fs::is_regular_file(scriptPath, status)) {
						scriptPath.clear();
					}
				}
				reportObjectIssue(describe(invalidObject) + "\n" + issue, entry,
				                  invalidObject, scriptPath);
			}
			auto &objects = entry.objects;

			for (auto &[id, object] : objects) {
				object.prefabLink.reset();
				if (!incompleteGraph && object.triggerActions)
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
				if (!incompleteGraph && object.script)
					for (auto *bindings :
					     {&object.script->prefabs, &object.script->scenes})
						for (auto &[name, path] : *bindings) {
							(void)name;

							if (path.empty())
								continue;
							const auto found = std::ranges::find_if(
							    graph, [&](const auto &target) {
								    return target.prefab ==
								               PrefabSerializer::IsPrefabPath(
								                   path) &&
								           !target.source.empty() &&
								           PathUtils::IsSameFile(target.source,
								                                 path);
							    });
							if (found == graph.end())
								throw std::runtime_error(
								    "Script Scene or Prefab is not in the "
								    "export graph.");
							path = scenePaths[static_cast<std::size_t>(
							    found - graph.begin())];
						}
				const auto includeAsset = [&](fs::path &path, const char *kind,
				                              const auto &add,
				                              const fs::path &failedScript = {}) {
					fs::path input;
					const auto report = [&](std::string_view reason, const fs::path &location) {
						reportObjectIssue(std::format(
						    "{} / {}: {}\n{}", describe(id), kind, PathUtils::ToUtf8(path),
						    reason), entry, id, failedScript, location);
					};
					try {
						input = resolveReference(path);
						path = add(input);
					} catch (const std::bad_alloc &) {
						throw;
					} catch (const AssetDependencies::MissingModelDependencies &error) {
						for (const auto &file : error.paths)
							report(AssetDependencies::DescribeMissingModelDependency(file), file);
					} catch (const ModelDependencyExportError &error) {
						for (const auto &[file, reason] : error.failures)
							report(reason, file);
					} catch (const std::exception &error) {
						report(error.what(), input);
					}
				};
				const auto addUITexture = [&](const fs::path &path) {
					return assets.AddTexture(path, 4096);
				};

				if (object.image && !object.image->texture.empty())
					includeAsset(object.image->texture, "Image", addUITexture);
				if (object.particleEmitter &&
				    !object.particleEmitter->texture.empty())
					includeAsset(object.particleEmitter->texture, "Particle texture",
					             addUITexture);
				if (object.animationClip && object.animationClip->usesAsset)
					includeAsset(object.animationClip->asset, "Animation Clip",
					             [&](const auto &path) {
						             auto reference = assets.AddAnimationClip(path);
						             if (!canCheckSimulation)
							             CheckAnimationBindings(*object.animationClip, path, candidate);
						             return reference;
					             });
				if (object.script && !object.script->asset.empty()) {
					fs::path failedScript;
					includeAsset(object.script->asset, "Lua Script",
					             [&](const auto &path) {
						             return assets.AddScript(path, &failedScript);
					             },
					             failedScript);
				}
				if (object.audioSource && !object.audioSource->clipPath.empty())
					includeAsset(object.audioSource->clipPath, "Audio Source",
					             [&](const auto &path) {
						             return assets.AddAudio(
						                 path, object.audioSource->spatial);
					             });

				if (object.meshRenderer)
					for (auto &[slot, path] : object.meshRenderer->textures.slots)
						includeAsset(path, "Material texture",
						             [&](const auto &input) {
							             return assets.AddTexture(input);
						             });

				if (object.HasMesh())
					includeAsset(object.mesh->modelPath, "Mesh",
					             [&](const auto &path) { return assets.AddModel(path); });
			}
		}

		if (!error.empty()) {
			return false;
		}

		std::string problem;
		CopyPlan plan(destination);
		for (const auto &path : scenePaths)
			plan.Reserve(path);
		plan.Reserve(L"game.json");
		plan.File(runtimeRoot / L"Runtime.exe", L"Game.exe");

		for (const auto *name : kRuntimeBinaries) {
			plan.File(runtimeRoot / name, name);
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

		for (std::size_t i = 0; i < graph.size(); ++i) {
			const auto &entry = graph[i];
			Scene snapshot;
			snapshot.ReplaceObjects(entry.objects);
			snapshot.SetLighting(entry.lighting);
			snapshot.SetFog(entry.fog);
			snapshot.SetUseLegacyDirectionalLight(entry.legacyDirectionalLight);
			std::string text;

			if (entry.prefab) {
				SceneAssetReferences::Rewrite(snapshot, [&](const auto &path) {
					return path.is_absolute() ? path : destination / path;
				});
				SceneObjectId root = 0;
				if (!PrefabSerializer::Validate(snapshot, root, problem) ||
				    !PrefabSerializer::Serialize(
				        snapshot, root, destination / scenePaths[i], text,
				        problem, entry.nextSourceId))
					throw std::runtime_error(problem);
			} else {
				SceneLighting::MigrateLegacyDirectionalLight(snapshot,
				                                             options.config.lighting);
				if (!SceneSerializer::Serialize(snapshot, text))
					throw std::runtime_error("Could not serialize an exported scene.");
			}
			sceneTexts.push_back(std::move(text));
		}

		if (!config.Serialize(configText, problem)) {
			throw std::runtime_error(problem);
		}

		if (!writeOutput) {
			return true;
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
				if (object.image && !object.image->texture.empty())
					exportedAssets.AddTexture(
					    destination / object.image->texture, 4096);
				if (object.particleEmitter &&
				    !object.particleEmitter->texture.empty())
					exportedAssets.AddTexture(
					    destination / object.particleEmitter->texture, 4096);
				if (object.animationClip && object.animationClip->usesAsset)
					exportedAssets.AddAnimationClip(destination / object.animationClip->asset);
				if (object.script && !object.script->asset.empty())
					exportedAssets.AddScript(destination /
					                         object.script->asset);
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
		appendIssue(std::string(e.what()) + output.Cleanup(), {}, 0, {}, {});
		return false;
	} catch (...) {
		appendIssue("An unknown error interrupted game export preparation or writing." +
		                output.Cleanup(), {}, 0, {}, {});
		return false;
	}
}
} // namespace

bool GameExporter::Check(const Scene &scene, const GameExportOptions &options,
                         std::string &error, std::vector<GameExportIssue> *issues) {
	return ProcessExport(scene, options, error, false, issues);
}

bool GameExporter::Export(const Scene &scene, const GameExportOptions &options,
                          std::string &error, std::vector<GameExportIssue> *issues) {
	return ProcessExport(scene, options, error, true, issues);
}
