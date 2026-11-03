#include "assets/ModelImporter.h"
#include "assets/AssetDependencies.h"
#include "assets/AssetFolders.h"
#include "files/OutputDirectory.h"
#include "graphics/model/ModelManager.h"
#include "platform/windows/PathUtils.h"
#include <Windows.h>
#include <map>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;

/// <summary>
/// Windows上で同じ保存先になる名前を、大文字・小文字を区別せず照合する
/// </summary>
struct PathLess {
	bool operator()(const fs::path &left, const fs::path &right) const {
		return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1,
		                            TRUE) == CSTR_LESS_THAN;
	}
};

/// <summary>
/// 入力元の内側にある通常ファイルと、構成を保持する相対パスを検証する
/// </summary>
/// <param name="root">正規化済みの基準フォルダ</param>
/// <param name="input">モデルが参照するファイル</param>
/// <returns>入力元内で使用する相対パス。実体の外部参照や無効な名前は例外</returns>
fs::path RelativeFile(const fs::path &root, const fs::path &input) {
	if (input.native().find(L'\0') != std::wstring::npos) {
		throw std::invalid_argument(
		    "A model reference contains a null character.");
	}

	const auto normal =
	    PathUtils::ResolveFromExecutable(input).lexically_normal();
	const auto relative = PathUtils::RelativeInside(root, normal);
	PathUtils::RelativeInside(root, fs::canonical(normal));

	if (relative.empty() || !fs::is_regular_file(normal)) {
		throw std::runtime_error("A model dependency is not a regular file.");
	}

	for (const auto &part : relative) {
		if (!PathUtils::IsValidFileName(part)) {
			throw std::invalid_argument(
			    "A model dependency has an invalid local file name.");
		}
	}

	return relative;
}
} // namespace

bool ModelImporter::Import(const ModelImportOptions &options,
                           ModelManager &models,
                           std::filesystem::path &imported,
                           std::string &error) {
	OutputDirectory output;
	try {
		if (!PathUtils::IsModelFile(options.source) ||
		    options.source.native().find(L'\0') != std::wstring::npos ||
		    !PathUtils::IsValidFileName(options.folderName)) {
			throw std::invalid_argument(
			    "Choose a GLB, glTF or OBJ model and a valid new folder name.");
		}

		if (options.sourceRoot.native().find(L'\0') != std::wstring::npos) {
			throw std::invalid_argument(
			    "The source folder contains a null character.");
		}

		const auto source = std::filesystem::canonical(
		    PathUtils::ResolveFromExecutable(options.source));
		const auto sourceRoot =
		    options.sourceRoot.empty()
		        ? source.parent_path()
		        : std::filesystem::canonical(
		              PathUtils::ResolveFromExecutable(options.sourceRoot));
		if (!std::filesystem::is_directory(sourceRoot)) {
			throw std::invalid_argument(
			    "Choose an existing source folder containing the model and its "
			    "dependencies.");
		}

		const auto modelRelative = RelativeFile(sourceRoot, source);
		const auto destination =
		    AssetFolders::Resolve(options.assetRoot, options.folder) /
		    options.folderName;
		if (std::filesystem::exists(destination)) {
			throw std::runtime_error(
			    "The model folder already exists. Choose another name.");
		}

		std::map<std::filesystem::path, std::filesystem::path, PathLess> plan;
		for (const auto &file : AssetDependencies::ModelFiles(source)) {
			const auto relative = RelativeFile(sourceRoot, file);
			const auto canonical = std::filesystem::canonical(file);
			const auto [found, inserted] =
			    plan.try_emplace(relative, canonical);
			if (!inserted &&
			    !std::filesystem::equivalent(found->second, canonical)) {
				throw std::runtime_error(
				    "Model dependencies would use the same output path.");
			}
		}

		auto selected = AssetFolders::MakeReference(
		    options.assetRoot, destination / modelRelative);

		output.Create(destination);
		for (const auto &[relative, file] : plan) {
			output.Copy(file, relative);
		}

		// コピーしたモデルから参照を収集し直し、元データへの依存が残っていないことを確認する。
		for (const auto &file :
		     AssetDependencies::ModelFiles(destination / modelRelative)) {
			RelativeFile(destination, file);
		}

		if (!models.Load(destination / modelRelative)) {
			throw std::runtime_error(
			    "The imported model could not create its GPU resources.");
		}

		output.Complete();
		imported.swap(selected);
		error.clear();
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		error += output.Cleanup();
		return false;
	} catch (...) {
		error = "An unknown error interrupted model import.";
		error += output.Cleanup();
		return false;
	}
}
