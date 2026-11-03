#include "assets/AssetDatabase.h"

#include "core/Logger.h"
#include "platform/windows/PathUtils.h"

#include <algorithm>
#include <array>
#include <exception>
#include <format>
#include <stdexcept>
#include <system_error>
#include <utility>

AssetDatabase::AssetDatabase(std::filesystem::path rootDirectory)
    : rootDirectory_(std::move(rootDirectory).lexically_normal()) {
}

bool AssetDatabase::Refresh() {
	try {
		if (rootDirectory_.empty() ||
		    rootDirectory_.native().find(L'\0') != std::wstring::npos ||
		    (rootDirectory_.has_root_path() && !rootDirectory_.is_absolute())) {
			throw std::invalid_argument("検索先には絶対パスか実行ファイル基準の"
			                            "相対パスを指定してください。");
		}

		const auto root =
		    rootDirectory_.is_absolute()
		        ? std::filesystem::canonical(rootDirectory_)
		        : PathUtils::ResolveFromExecutable(rootDirectory_);
		auto selectedRoot =
		    rootDirectory_.is_absolute() ? root : rootDirectory_;

		const auto executableDirectory = PathUtils::GetExecutableDirectory();

		if (executableDirectory.empty()) {
			throw std::runtime_error(
			    "実行ファイルのフォルダを取得できませんでした。");
		}

		std::error_code error;

		if (!std::filesystem::is_directory(root, error)) {
			if (error) {
				throw std::system_error(
				    error, "アセットフォルダを確認できませんでした。");
			}

			throw std::runtime_error("アセットフォルダが存在しません。");
		}

		std::vector<std::filesystem::path> models;
		std::vector<std::filesystem::path> textures;
		std::vector<std::filesystem::path> directories;
		const auto reference = [&](const std::filesystem::path &path) {
			return selectedRoot.is_absolute()
			           ? path
			           : path.lexically_relative(executableDirectory);
		};

		std::filesystem::recursive_directory_iterator iterator(root, error);
		const std::filesystem::recursive_directory_iterator end;

		while (!error && iterator != end) {
			const auto &entry = *iterator;

			std::error_code typeError;
			const bool regularFile = entry.is_regular_file(typeError);

			if (typeError) {
				throw std::system_error(
				    typeError, "アセットの種類を確認できませんでした。");
			}

			const bool directory = entry.is_directory(typeError);

			if (typeError) {
				throw std::system_error(
				    typeError, "アセットのフォルダを確認できませんでした。");
			}

			if (directory) {
				directories.push_back(reference(entry.path()));
			}

			if (regularFile) {
				const auto &path = entry.path();

				if (PathUtils::IsImageFile(path)) {
					textures.push_back(reference(path));
				}

				const bool supported = PathUtils::IsModelFile(path);

				if (supported) {
					// 検索先の指定方法を保ち、外部フォルダは実行ファイルの移動に依存させない。
					auto assetPath = reference(path);

					if (assetPath.empty()) {
						throw std::runtime_error(
						    "モデルの参照パスを作成できませんでした。");
					}

					models.push_back(std::move(assetPath));
				}
			}

			iterator.increment(error);
		}

		if (error) {
			throw std::system_error(error, "アセットの検索に失敗しました。");
		}

		std::sort(models.begin(), models.end());
		std::sort(textures.begin(), textures.end());
		std::sort(directories.begin(), directories.end());

		rootDirectory_.swap(selectedRoot);
		models_.swap(models);
		textures_.swap(textures);
		directories_.swap(directories);
		error_.clear();
		return true;
	} catch (const std::exception &error) {
		models_.clear();
		textures_.clear();
		directories_.clear();
		error_ = error.what();

		Logger::Error(std::format("AssetDatabase::Refresh: {}", error_));
		return false;
	}
}

bool AssetDatabase::SetRootDirectory(std::filesystem::path rootDirectory) {
	try {
		AssetDatabase candidate(std::move(rootDirectory));
		if (!candidate.Refresh()) {
			error_ = candidate.GetError();
			return false;
		}

		rootDirectory_.swap(candidate.rootDirectory_);
		models_.swap(candidate.models_);
		textures_.swap(candidate.textures_);
		directories_.swap(candidate.directories_);
		error_.clear();
		return true;
	} catch (const std::exception &error) {
		error_ = error.what();
		return false;
	}
}

const std::filesystem::path &AssetDatabase::GetRootDirectory() const {
	return rootDirectory_;
}

const std::vector<std::filesystem::path> &AssetDatabase::GetModels() const {
	return models_;
}

const std::string &AssetDatabase::GetError() const {
	return error_;
}

const std::vector<std::filesystem::path> &
AssetDatabase::GetDirectories() const {
	return directories_;
}

void AssetDatabase::Clear() {
	models_.clear();
	textures_.clear();
	directories_.clear();
	error_.clear();
}

const std::vector<std::filesystem::path> &AssetDatabase::GetTextures() const {
	return textures_;
}
