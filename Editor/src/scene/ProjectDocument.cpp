#include "scene/ProjectDocument.h"

#include "platform/windows/FileUtils.h"
#include "platform/windows/PathUtils.h"

#include <json.hpp>

#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace {
/// <summary>
/// プロジェクト内の指定フォルダを確認する
/// </summary>
void ValidateDirectory(const std::filesystem::path &root,
                       const std::filesystem::path &name) {
	const auto path = root / name;

	if (!std::filesystem::is_directory(path) ||
	    PathUtils::RelativeInside(root, std::filesystem::canonical(path)) !=
	        name) {
		throw std::runtime_error("Missing or redirected project directory: " +
		                         PathUtils::ToUtf8(path));
	}
}

/// <summary>
/// 起動シーンを確認し、プロジェクト基準の保存用パスを取得する
/// </summary>
std::filesystem::path StartupReference(const std::filesystem::path &root,
                                       const std::filesystem::path &path,
                                       bool requireFile = true) {
	if (path.empty()) {
		return {};
	}

	const auto scene = std::filesystem::weakly_canonical(path);

	if (((requireFile || std::filesystem::exists(scene)) &&
	     !std::filesystem::is_regular_file(scene)) ||
	    !PathUtils::HasExtension(scene, ".json")) {
		throw std::runtime_error(
		    "Choose a saved scene in the project's scenes folder.");
	}

	PathUtils::RelativeInside(root / L"scenes", scene);
	return PathUtils::RelativeInside(root, scene);
}

/// <summary>
/// プロジェクトの設定をJSONへ保存する
/// </summary>
bool WriteProject(const std::filesystem::path &root,
                  const std::filesystem::path &startup, const GameConfig &game,
                  bool overwrite, std::string &error) {
	std::string gameText;

	if (!game.Serialize(gameText, error)) {
		return false;
	}

	const nlohmann::json data{{"format", "EditorProject"},
	                          {"version", 1},
	                          {"startupScene", PathUtils::ToUtf8(startup)},
	                          {"game", nlohmann::json::parse(gameText)}};
	return FileUtils::WriteTextAtomically(root / L"project.json", data.dump(2),
	                                      error, overwrite);
}
} // namespace

bool ProjectDocument::Create(const std::filesystem::path &folder,
                             std::string &error) {
	std::filesystem::path root;
	bool createdAssets = false, createdScenes = false;

	try {
		root = std::filesystem::canonical(folder);
		const auto manifest = root / L"project.json";

		if (std::filesystem::exists(manifest)) {
			throw std::runtime_error("This folder already contains a project.");
		}

		createdAssets = std::filesystem::create_directory(root / L"assets");
		createdScenes = std::filesystem::create_directory(root / L"scenes");
		ValidateDirectory(root, L"assets");
		ValidateDirectory(root, L"scenes");
		GameConfig game;

		if (root.has_filename()) {
			game.title = PathUtils::ToUtf8(root.filename());
		}

		if (WriteProject(root, {}, game, false, error)) {
			root_ = std::move(root);
			startupScene_.clear();
			game_ = std::move(game);
			return true;
		}
	} catch (const std::exception &failure) {
		error = failure.what();
	}

	std::error_code cleanupError;

	if (createdScenes) {
		std::filesystem::remove(root / L"scenes", cleanupError);
	}

	if (createdAssets) {
		std::filesystem::remove(root / L"assets", cleanupError);
	}

	return false;
}

bool ProjectDocument::Open(const std::filesystem::path &folder,
                           std::string &error) {
	try {
		auto root = std::filesystem::canonical(folder);
		const auto manifest = root / L"project.json";

		std::error_code status;
		const auto resolved = std::filesystem::canonical(manifest, status);
		if (status == std::errc::no_such_file_or_directory) {
			throw std::runtime_error("This folder does not contain project.json.");
		}
		if (status) {
			throw std::filesystem::filesystem_error("Could not locate project.json",
			                                        manifest, status);
		}
		if (resolved != manifest) {
			throw std::runtime_error(
			    "The project manifest must be inside its project folder.");
		}

		std::ifstream input(manifest, std::ios::binary);
		const auto data = nlohmann::json::parse(input);

		if (data.at("format") != "EditorProject" ||
		    !data.at("version").is_number_integer() ||
		    data.at("version") != 1) {
			throw std::runtime_error("Unsupported project format.");
		}

		ValidateDirectory(root, L"assets");
		ValidateDirectory(root, L"scenes");
		GameConfig game;

		if (!game.Deserialize(data.at("game").dump(), error)) {
			return false;
		}

		auto startup =
		    PathUtils::FromUtf8(data.at("startupScene").get<std::string>());

		if (!startup.empty() &&
		    (startup.has_root_path() ||
		     StartupReference(root, root / startup, false) != startup)) {
			throw std::runtime_error("Invalid project startup scene path.");
		}

		root_ = std::move(root);
		startupScene_ = std::move(startup);
		game_ = std::move(game);
		error.clear();
		return true;
	} catch (const std::exception &failure) {
		error = failure.what();
		return false;
	}
}

bool ProjectDocument::Save(const GameConfig &game,
                           const std::filesystem::path &startupScene,
                           std::string &error) {
	try {
		if (root_.empty()) {
			throw std::runtime_error(
			    "Open a project before saving its settings.");
		}

		auto settings = game;
		auto startup = StartupReference(root_, startupScene);

		if (!WriteProject(root_, startup, settings, true, error)) {
			return false;
		}

		startupScene_ = std::move(startup);
		game_ = std::move(settings);
		return true;
	} catch (const std::exception &failure) {
		error = failure.what();
		return false;
	}
}

const std::filesystem::path &ProjectDocument::GetRoot() const {
	return root_;
}

std::filesystem::path ProjectDocument::GetAssetsDirectory() const {
	return root_ / L"assets";
}

std::filesystem::path ProjectDocument::GetScenesDirectory() const {
	return root_ / L"scenes";
}

std::filesystem::path ProjectDocument::GetStartupScene() const {
	return startupScene_.empty() ? std::filesystem::path{}
	                             : root_ / startupScene_;
}

const GameConfig &ProjectDocument::GetGameConfig() const {
	return game_;
}
