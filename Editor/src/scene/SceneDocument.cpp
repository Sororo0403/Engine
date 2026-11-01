#include "scene/SceneDocument.h"

#include "core/Logger.h"
#include "platform/windows/PathUtils.h"
#include "scene/SceneSerializer.h"

#include <exception>
#include <format>

Scene &SceneDocument::GetScene() { return scene_; }

const Scene &SceneDocument::GetScene() const { return scene_; }

const std::filesystem::path &SceneDocument::GetFilePath() const {
	return filePath_;
}

EditorHistory &SceneDocument::GetHistory() { return history_; }

const EditorHistory &SceneDocument::GetHistory() const { return history_; }

bool SceneDocument::IsModified() const { return history_.IsModified(); }

void SceneDocument::MarkModified() { history_.Invalidate(); }

void SceneDocument::New() {
	scene_.Clear();
	filePath_.clear();
	history_.Reset();
}

bool SceneDocument::Open(const std::filesystem::path &filePath,
                         const std::function<bool(const Scene &)> &validate) {
	try {
		auto resolvedPath = PathUtils::ResolveFromExecutable(filePath);

		// 読み込みと事前確認は一時シーンで実行し、両方成功した場合だけ置き換える
		const bool loaded = scene_.ApplyTransaction([&](Scene &candidate) {
			if (!SceneSerializer::Load(candidate, resolvedPath)) {
				return false;
			}
			if (validate && !validate(candidate)) {
				Logger::Error("SceneDocument::Open: シーンの事前確認に失敗しました。");
				return false;
			}
			return true;
		});
		if (!loaded) {
			return false;
		}

		// 読み込み成功後に、保存先と変更状態を更新する
		filePath_.swap(resolvedPath);
		history_.Reset();
		return true;
	} catch (const std::exception &error) {
		Logger::Error(std::format("SceneDocument::Open: {}", error.what()));
		return false;
	}
}

bool SceneDocument::Save() {
	if (filePath_.empty()) {
		Logger::Error("SceneDocument::Save: 保存先が未指定です。");
		return false;
	}

	return SaveAs(filePath_);
}

bool SceneDocument::SaveCopy(const std::filesystem::path &filePath) {
	try {
		const auto resolvedPath = PathUtils::ResolveFromExecutable(filePath);
		// 書き込み用の一時ファイルも、現在の保存先と別のファイルであることを確認する
		auto temporaryPath = resolvedPath;
		temporaryPath += ".tmp";
		if (resolvedPath.native().find(L'\0') != std::wstring::npos ||
		    (!filePath_.empty() &&
		     (PathUtils::IsSameFile(resolvedPath, filePath_) ||
		      PathUtils::IsSameFile(temporaryPath, filePath_)))) {
			Logger::Error("SceneDocument::SaveCopy: 現在の保存先とは別の有効なパスを指定してください。");
			return false;
		}

		history_.CommitEdit();
		return SceneSerializer::Save(scene_, resolvedPath);
	} catch (const std::exception &error) {
		Logger::Error(std::format("SceneDocument::SaveCopy: {}", error.what()));
		return false;
	}
}

bool SceneDocument::SaveAs(const std::filesystem::path &filePath) {
	try {
		auto resolvedPath = PathUtils::ResolveFromExecutable(filePath);

		// 名前の空欄処理などを、保存するデータへ先に反映する
		history_.CommitEdit();

		if (!SceneSerializer::Save(scene_, resolvedPath)) {
			return false;
		}

		// 保存成功後に、保存先と保存地点を更新する
		filePath_.swap(resolvedPath);
		history_.MarkSaved();

		return true;
	} catch (const std::exception &error) {
		Logger::Error(std::format("SceneDocument::SaveAs: {}", error.what()));
		return false;
	}
}
