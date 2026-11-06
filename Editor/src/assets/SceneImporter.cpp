#include "assets/SceneImporter.h"
#include "assets/SceneDependencyGraph.h"

#include "assets/AssetDependencies.h"
#include "assets/AssetFolders.h"
#include "assets/ImageValidation.h"
#include "audio/WaveReader.h"
#include "files/OutputDirectory.h"
#include "gameplay/GameConfig.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"
#include "scripting/LuaScript.h"
#include <Windows.h>
#include <format>
#include <fstream>
#include <json.hpp>
#include <map>
#include <new>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;

/// <summary>
/// Windows上の同じパスを、大文字・小文字を区別せず照合する
/// </summary>
struct PathLess {
	bool operator()(const fs::path &a, const fs::path &b) const {
		return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) ==
		       CSTR_LESS_THAN;
	}
};

/// <summary>
/// パッケージ内の通常ファイルを確認し、リンクと不正なローカル名を拒否する
/// </summary>
/// <param name="root">正規化済みのパッケージルート</param>
/// <param name="input">絶対パス、またはパッケージ基準の相対パス</param>
/// <returns>ルート内の正規化した絶対パス。無効なら例外</returns>
fs::path PackageFile(const fs::path &root, const fs::path &input) {
	if (input.empty() || input.native().find(L'\0') != std::wstring::npos ||
	    (input.has_root_path() && !input.is_absolute())) {
		throw std::invalid_argument("A scene asset has an invalid path.");
	}
	const auto normal =
	    (input.is_absolute() ? input : root / input).lexically_normal();
	const auto relative = PathUtils::RelativeInside(root, normal);
	if (relative.empty()) {
		throw std::runtime_error(
		    "A scene asset must be a file inside the game package.");
	}
	for (const auto &part : relative) {
		if (!PathUtils::IsValidFileName(part)) {
			throw std::runtime_error(
			    "A scene asset has an invalid local file name.");
		}
	}
	for (auto part = normal; part.has_filename(); part = part.parent_path()) {
		const auto attributes = GetFileAttributesW(part.c_str());
		if (attributes != INVALID_FILE_ATTRIBUTES &&
		    (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
			throw std::runtime_error("Copy linked scene assets into the game "
			                         "package before importing.");
		}
	}
	const auto file = fs::canonical(normal);
	PathUtils::RelativeInside(root, file);
	if (!fs::is_regular_file(file)) {
		throw std::runtime_error("A scene asset is not a regular file.");
	}
	return file;
}

/// <summary>
/// 明示割り当て画像を復号検証し、失敗時は参照先を含む理由を報告する
/// </summary>
/// <param name="path">パッケージ内で確認済みの通常ファイル</param>
/// <param name="maximumDimension">画像の各辺の上限。UIは4096、それ以外は16384</param>
void ValidateTexture(const fs::path &path, unsigned maximumDimension = 16384) {
	try {
		ImageValidation::Read(path, maximumDimension);
	} catch (const std::bad_alloc &) {
		throw;
	} catch (const std::exception &error) {
		throw std::runtime_error(
		    std::format("Could not import image \"{}\": {}",
		                PathUtils::ToUtf8(path), error.what()));
	}
}

/// <summary>
/// WAVを復号検証し、空間音声にはモノラルだけを受け付ける
/// </summary>
/// <param name="path">パッケージ内で確認済みの通常ファイル</param>
/// <param name="spatial">同じファイルを空間音声として使う物体があるか</param>
void ValidateAudio(const fs::path &path, bool spatial) {
	WaveData wave;
	std::string error;

	if (!WaveReader::Load(path, wave, error)) {
		throw std::runtime_error(
		    std::format("Could not import audio \"{}\": {}",
		                PathUtils::ToUtf8(path), error));
	}

	if (spatial && wave.channels != 1) {
		throw std::runtime_error(
		    "Spatial Audio Source needs a mono WAV clip: " +
		    PathUtils::ToUtf8(path));
	}
}

/// <summary>
/// Luaの構文を実行せず確認し、取り込めない理由を報告する
/// </summary>
/// <param name="path">パッケージ内で確認済みの通常ファイル</param>
void ValidateScript(const fs::path &path) {
	std::string source, error;
	if (!LuaScript::Read(path, source, error) ||
	    !LuaScript::Validate(source, error))
		throw std::runtime_error(error);
}

/// <summary>
/// 制限内のシーンファイルを読み、途中の切り詰めや追加を検出する
/// </summary>
/// <param name="file">読み込む通常ファイル</param>
/// <returns>読み取ったバイト列。読み込み失敗や制限超過は例外</returns>
std::string ReadScene(const fs::path &file) {
	const auto size = fs::file_size(file);
	if (size > SceneImporter::kMaximumSceneBytes) {
		throw std::runtime_error("The scene exceeds the import size limit.");
	}
	std::ifstream input(file, std::ios::binary);
	std::string text(static_cast<std::size_t>(size), '\0');
	if (!input ||
	    !input.read(text.data(), static_cast<std::streamsize>(text.size())) ||
	    input.peek() != std::char_traits<char>::eof()) {
		throw std::runtime_error("Could not read a complete scene file.");
	}
	return text;
}

/// <summary>
/// 各形式のモデル・画像・音声・旧材質JSON参照を、読み込み前にパッケージ内の絶対パスへ揃える
/// </summary>
/// <param name="document">入力シーンのJSON。形式の詳細検証はSceneSerializerで行う</param>
/// <param name="root">パッケージの基準フォルダ</param>
void ResolveReferences(Json &document, const fs::path &root) {
	const int version = document.at("version").get<int>();
	const auto resolve = [&](Json &value) {
		const auto path = PathUtils::FromUtf8(value.get<std::string>());
		if (!path.empty()) {
			value = PathUtils::ToUtf8(PackageFile(root, path));
		}
	};
	for (auto &object : document.at("objects")) {
		if (version >= 30 && object.contains("image"))
			resolve(object.at("image").at("texture"));
		if (version >= 29 && object.contains("script"))
			resolve(object.at("script").at("asset"));
		if (version >= 28 && object.contains("triggerActions")) {
			for (auto &action : object.at("triggerActions").at("actions")) {
				if (action.at("operation") == "loadScene")
					resolve(action.at("scene"));
			}
		}
		if (version >= 20 && object.contains("audioSource")) {
			resolve(object.at("audioSource").at("clip"));
		}

		if (version >= 9) {
			if (object.contains("mesh")) {
				resolve(object.at("mesh").at("model"));
			}
			if (object.contains("meshRenderer")) {
				for (auto &binding :
				     object.at("meshRenderer")
				         .at(version >= 11 ? "textures" : "materials")) {
					resolve(binding.at("path"));
				}
			}
		} else {
			resolve(object.at("model"));
			if (version >= 8 && object.contains("materials")) {
				for (auto &binding : object.at("materials")) {
					resolve(binding.at("path"));
				}
			}
		}
	}
}
} // namespace

bool SceneImporter::Import(const SceneImportOptions &options, Scene &imported,
                           fs::path &createdFolder, std::string &error,
                           GameConfig *configuration) {
	static_assert(std::is_nothrow_move_assignable_v<GameConfig>);
	OutputDirectory output;
	try {
		if (options.source.empty() ||
		    options.source.native().find(L'\0') != std::wstring::npos ||
		    !PathUtils::HasExtension(options.source, ".json") ||
		    !PathUtils::IsValidFileName(options.folderName)) {
			throw std::invalid_argument("Choose an exported game.json and a "
			                            "valid new Assets folder name.");
		}
		const auto source =
		    fs::canonical(PathUtils::ResolveFromExecutable(options.source));
		const auto sourceRoot = source.parent_path();
		PackageFile(sourceRoot, source);
		if (fs::file_size(source) > kMaximumSceneBytes) {
			throw std::runtime_error(
			    "The game configuration exceeds the import size limit.");
		}
		GameConfig config;
		std::string problem;
		if (!config.Load(source, problem)) {
			throw std::runtime_error(problem);
		}
		const auto sceneFile = PackageFile(sourceRoot, config.scenePath);
		auto document = Json::parse(ReadScene(sceneFile));
		ResolveReferences(document, sourceRoot);
		Scene candidate;
		if (!SceneSerializer::Deserialize(candidate, document.dump())) {
			throw std::runtime_error(
			    "Could not read the exported scene. See Console for details.");
		}
		const auto root = AssetFolders::Resolve(options.assetRoot, {});
		const auto destination =
		    AssetFolders::Resolve(options.assetRoot, options.folder) /
		    options.folderName;
		if (fs::exists(destination)) {
			throw std::runtime_error(
			    "The Assets folder already exists. Choose another name.");
		}
		bool inside = false;
		try {
			PathUtils::RelativeInside(sourceRoot, destination);
			inside = true;
		} catch (const std::runtime_error &) {
		}
		if (inside) {
			throw std::runtime_error("Import into an Assets folder outside the "
			                         "source game package.");
		}
		auto folderReference = PathUtils::RelativeInside(root, destination);
		std::map<fs::path, fs::path, PathLess> files;
		std::set<fs::path, PathLess> textures, scripts, uiTextures;
		std::map<fs::path, bool, PathLess> audio;
		const auto addFile = [&](const fs::path &path) {
			const auto file = PackageFile(sourceRoot, path);
			const auto relative = PathUtils::RelativeInside(sourceRoot, file);
			const auto [found, inserted] = files.try_emplace(relative, file);
			if (!inserted && !fs::equivalent(found->second, file)) {
				throw std::runtime_error(
				    "Imported scene assets would use the same output path.");
			}
			return relative;
		};
		auto graph = SceneDependencyGraph::Read(
		    candidate, sceneFile,
		    [&](const auto &path) { return PackageFile(sourceRoot, path); },
		    [&](const auto &path, Scene &loaded) {
			    auto document = Json::parse(ReadScene(path));
			    ResolveReferences(document, sourceRoot);
			    if (!SceneSerializer::Deserialize(loaded, document.dump()))
				    throw std::runtime_error(
				        "Could not read an imported destination scene.");
		    });
		const auto reference = [&](const fs::path &path) {
			const auto target = destination / addFile(path);
			return AssetFolders::MakeReference(options.assetRoot, target);
		};
		for (auto &entry : graph) {
			for (auto &[id, object] : entry.objects) {
				if (object.triggerActions)
					for (auto &action : object.triggerActions->actions) {
						if (action.operation ==
						        TriggerAction::Operation::LOAD_SCENE &&
						    !action.scenePath.empty())
							action.scenePath = AssetFolders::MakeReference(
							    options.assetRoot,
							    destination /
							        PathUtils::RelativeInside(
							            sourceRoot, action.scenePath));
					}
				if (object.image && !object.image->texture.empty()) {
					const auto relative = addFile(object.image->texture);
					if (uiTextures.insert(relative).second)
						ValidateTexture(files.at(relative), 4096);
					object.image->texture = AssetFolders::MakeReference(
					    options.assetRoot, destination / relative);
				}
				if (object.script && !object.script->asset.empty()) {
					const auto relative = addFile(object.script->asset);
					if (scripts.insert(relative).second)
						ValidateScript(files.at(relative));
					object.script->asset = AssetFolders::MakeReference(
					    options.assetRoot, destination / relative);
				}
				if (object.audioSource &&
				    !object.audioSource->clipPath.empty()) {
					auto &emitter = *object.audioSource;
					AssetDependencies::AudioFiles(emitter.clipPath);
					const auto relative = addFile(emitter.clipPath);
					audio[relative] |= emitter.spatial;
					emitter.clipPath = AssetFolders::MakeReference(
					    options.assetRoot, destination / relative);
				}

				if (object.HasMesh()) {
					for (const auto &file : AssetDependencies::ModelFiles(
					         object.mesh->modelPath)) {
						addFile(file);
					}
					object.mesh->modelPath = reference(object.mesh->modelPath);
				}
				if (object.meshRenderer) {
					for (auto &[slot, path] :
					     object.meshRenderer->textures.slots) {
						AssetDependencies::TextureFiles(path);
						const auto relative = addFile(path);

						if (textures.insert(relative).second) {
							ValidateTexture(files.at(relative));
						}

						path = AssetFolders::MakeReference(
						    options.assetRoot, destination / relative);
					}
				}
			}
		}
		for (const auto &[relative, spatial] : audio) {
			ValidateAudio(files.at(relative), spatial);
		}

		std::vector<std::pair<fs::path, std::string>> sceneTexts;
		for (const auto &entry : graph) {
			const auto relative =
			    PathUtils::RelativeInside(sourceRoot, entry.source);
			if (files.contains(relative))
				throw std::runtime_error(
				    "An imported scene path conflicts with an asset.");
			Scene loaded;
			loaded.ReplaceObjects(entry.objects);
			std::string text;
			if (!SceneSerializer::Serialize(loaded, text))
				throw std::runtime_error("Could not encode an imported scene.");
			sceneTexts.emplace_back(relative, std::move(text));
		}
		output.Create(destination);
		for (const auto &[relative, sourceFile] : files) {
			output.Copy(sourceFile, relative);
		}
		for (const auto &relative : uiTextures)
			ValidateTexture(PackageFile(destination, destination / relative),
			                4096);
		for (const auto &relative : textures) {
			ValidateTexture(PackageFile(destination, destination / relative));
		}
		for (const auto &[relative, spatial] : audio) {
			ValidateAudio(PackageFile(destination, destination / relative),
			              spatial);
		}

		for (const auto &relative : scripts)
			ValidateScript(PackageFile(destination, destination / relative));

		// コピー後も、モデルの相対参照が取り込み先の内側で完結することを確認する。
		for (const auto &entry : graph)
			for (const auto &[id, object] : entry.objects) {
				if (object.HasMesh()) {
					for (const auto &file : AssetDependencies::ModelFiles(
					         object.mesh->modelPath)) {
						PackageFile(destination, file);
					}
				}
			}
		for (const auto &[path, text] : sceneTexts)
			output.Text(path, text);
		imported.ReplaceObjects(std::move(graph.front().objects));
		createdFolder.swap(folderReference);
		if (configuration) {
			*configuration = std::move(config);
		}
		output.Complete();
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		error += output.Cleanup();
		return false;
	} catch (...) {
		error = "An unknown error interrupted scene import.";
		error += output.Cleanup();
		return false;
	}
}
