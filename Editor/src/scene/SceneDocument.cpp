#include "scene/SceneDocument.h"

#include "core/Logger.h"
#include "platform/windows/PathUtils.h"
#include "scene/SceneLighting.h"
#include "scene/SceneSerializer.h"

#include <exception>
#include <format>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace {
/// <summary>
/// 保存時の外部変更確認と、読み込みに使用するファイル内容を取得する
/// </summary>
/// <param name="path">読み込む絶対パス</param>
/// <returns>ファイルの内容。開けない場合や読み取り失敗は例外</returns>
std::string ReadSceneText(const std::filesystem::path &path) {
	std::ifstream input(path, std::ios::binary);

	if (!input) {
		throw std::runtime_error(
		    "Could not read the scene file. It may be missing or locked. "
		    "Use Save As or Save Copy to a different file.");
	}

	input.exceptions(std::ios::badbit);
	return {std::istreambuf_iterator<char>(input),
	        std::istreambuf_iterator<char>()};
}
} // namespace

Scene &SceneDocument::GetScene() {
	return scene_;
}

const Scene &SceneDocument::GetScene() const {
	return scene_;
}

const std::filesystem::path &SceneDocument::GetFilePath() const {
	return filePath_;
}

const std::string &SceneDocument::GetError() const {
	return error_;
}

EditorHistory &SceneDocument::GetHistory() {
	return history_;
}

const EditorHistory &SceneDocument::GetHistory() const {
	return history_;
}

bool SceneDocument::IsModified() const {
	return history_.IsModified();
}

void SceneDocument::MarkModified() {
	history_.Invalidate();
}

void SceneDocument::SetProjectRoot(std::filesystem::path root) {
	projectRoot_.swap(root);
}

bool SceneDocument::MatchesSavedText(std::string_view text) const {
	return savedText_ == text;
}

void SceneDocument::ApplyAssetRename(Scene &scene, std::filesystem::path path,
                                    std::string *savedText,
                                    EditorHistory &preparedHistory) {
	scene_.Swap(scene);
	filePath_.swap(path);

	if (savedText) {
		savedText_.swap(*savedText);
	}

	history_.ApplyAssetRewrite(preparedHistory);
}

void SceneDocument::New() {
	scene_.Clear();
	filePath_.clear();
	fileProjectRoot_.clear();
	savedText_.clear();
	error_.clear();
	history_.Reset();
}

void SceneDocument::SetUntitled() {
	filePath_.clear();
	fileProjectRoot_.clear();
	savedText_.clear();
	error_.clear();
	history_.Reset();
	history_.Invalidate();
}

bool SceneDocument::Open(const std::filesystem::path &filePath,
                         const LightingSettings *lighting,
                         const std::function<bool(const Scene &)> &validate) {
	try {
		auto resolvedPath = PathUtils::ResolveFromExecutable(filePath);

		if (resolvedPath.native().find(L'\0') != std::wstring::npos) {
			error_ = "Choose a valid scene path.";
			return false;
		}

		auto sourceText = ReadSceneText(resolvedPath);
		std::filesystem::path projectRoot;

		// 読み込みと事前確認は一時シーンで実行し、両方成功した場合だけ置き換える
		const bool loaded = scene_.ApplyTransaction([&](Scene &candidate) {
			if (!SceneSerializer::Deserialize(candidate, sourceText, resolvedPath,
			                                  &projectRoot)) {
				return false;
			}

			if (lighting) {
				SceneLighting::MigrateLegacyDirectionalLight(candidate, *lighting);
			}

			if (validate && !validate(candidate)) {
				Logger::Error(
				    "SceneDocument::Open: シーンの事前確認に失敗しました。");
				return false;
			}

			return true;
		});

		if (!loaded) {
			error_ = "Could not load or validate the scene. See the log for "
			         "details.";
			return false;
		}

		// 読み込み成功後に、保存先と変更状態を更新する
		filePath_.swap(resolvedPath);
		fileProjectRoot_.swap(projectRoot);
		savedText_.swap(sourceText);
		history_.Reset();
		error_.clear();
		return true;
	} catch (const std::exception &error) {
		error_ = error.what();
		Logger::Error(std::format("SceneDocument::Open: {}", error_));
		return false;
	}
}

bool SceneDocument::Save() {
	saveConflict_ = false;
	if (filePath_.empty()) {
		error_ = "Choose a scene destination with Save As first.";
		Logger::Error("SceneDocument::Save: 保存先が未指定です。");
		return false;
	}

	return SaveAs(filePath_);
}

bool SceneDocument::SaveCopy(const std::filesystem::path &filePath, bool overwrite) {
	return SaveCopy(filePath, scene_, overwrite);
}

bool SceneDocument::SaveCopy(const std::filesystem::path &filePath,
                             const Scene &snapshot, bool overwrite) {
	try {
		const auto resolvedPath = PathUtils::ResolveFromExecutable(filePath);
		// 書き込み用の一時ファイルも、現在の保存先と別のファイルであることを確認する
		auto temporaryPath = resolvedPath;
		temporaryPath += ".tmp";

		if (resolvedPath.native().find(L'\0') != std::wstring::npos ||
		    (!filePath_.empty() &&
		     (PathUtils::IsSameFile(resolvedPath, filePath_) ||
		      PathUtils::IsSameFile(temporaryPath, filePath_)))) {
			error_ = "Choose a copy destination that does not conflict with "
			         "the active scene.";
			Logger::Error("SceneDocument::SaveCopy: "
			              "現在の保存先とは別の有効なパスを指定してください。");
			return false;
		}

		history_.CommitEdit();

		const auto &root =
		    projectRoot_.empty() ? fileProjectRoot_ : projectRoot_;

		if (!SceneSerializer::Save(snapshot, resolvedPath, root, nullptr, overwrite)) {
			error_ = "Could not save a scene copy. See the log for details.";
			return false;
		}

		error_.clear();
		return true;
	} catch (const std::exception &error) {
		error_ = error.what();
		Logger::Error(std::format("SceneDocument::SaveCopy: {}", error_));
		return false;
	}
}

bool SceneDocument::SaveAs(const std::filesystem::path &filePath, bool overwrite) {
	saveConflict_ = false;
	try {
		auto resolvedPath = PathUtils::ResolveFromExecutable(filePath);

		if (resolvedPath.native().find(L'\0') != std::wstring::npos) {
			error_ = "Choose a valid scene path.";
			return false;
		}

		auto temporaryPath = resolvedPath;
		temporaryPath += ".tmp";

		if (!filePath_.empty() &&
		    PathUtils::IsSameFile(temporaryPath, filePath_)) {
			error_ = "Choose another destination. This name conflicts with the "
			         "active scene.";
			return false;
		}

		if (!filePath_.empty() &&
		    PathUtils::IsSameFile(resolvedPath, filePath_)) {
			saveConflict_ = true;
			if (ReadSceneText(resolvedPath) != savedText_) {
				error_ = "The scene changed outside the editor. Save a copy of "
				         "your edits or reload the scene from disk.";
				return false;
			}
			saveConflict_ = false;
		}

		// 名前の空欄処理などを、保存するデータへ先に反映する
		history_.CommitEdit();

		std::string savedText;
		const auto &root =
		    projectRoot_.empty() ? fileProjectRoot_ : projectRoot_;

		if (!SceneSerializer::Save(scene_, resolvedPath, root, &savedText, overwrite)) {
			error_ = "Could not save the scene. See the log for details.";
			return false;
		}

		// 保存成功後に、保存先と保存地点を更新する
		filePath_.swap(resolvedPath);
		savedText_.swap(savedText);
		history_.MarkSaved();
		error_.clear();

		return true;
	} catch (const std::exception &error) {
		error_ = error.what();
		Logger::Error(std::format("SceneDocument::SaveAs: {}", error_));
		return false;
	}
}
