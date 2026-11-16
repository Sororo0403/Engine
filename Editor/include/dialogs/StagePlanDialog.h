#pragma once

#include "gameplay/StagePlan.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class ImGuiSystem;
class SceneDocument;

/// <summary>
/// ステージの進行を編集し、ChatGPTとの受け渡しと別シーンへの保存を行う
/// </summary>
class StagePlanDialog {
  public:
	enum class Result { NONE, CLOSED, OPEN_SAVED_SCENE };

	/// <summary>
	/// プロジェクトに対応するステージの下書きを開く
	/// </summary>
	void Show(const std::filesystem::path &projectRoot);

	/// <summary>
	/// 区間の編集・案の取り込み・保存を受け付ける
	/// </summary>
	Result Draw(const ImGuiSystem &ui, SceneDocument &document,
	            const std::filesystem::path &scenesDirectory);

	/// <summary>
	/// 最後に保存したシーンのパスを取得する
	/// </summary>
	const std::filesystem::path &GetSavedPath() const { return savedPath_; }

  private:
	void DrawSections(const ImGuiSystem &ui);
	void CopyPrompt();
	void ImportClipboard();
	void Save(SceneDocument &document, const std::filesystem::path &scenesDirectory);
	bool HasLayout() const;

	std::filesystem::path projectRoot_;
	std::filesystem::path savedPath_;
	std::vector<StagePlan::SectionBrief> brief_;
	StagePlan::Layout layout_;
	std::size_t selected_ = 0;
	unsigned nextSectionId_ = 3;
	std::string sceneName_;
	std::string message_;
	std::string error_;
	bool opening_ = false;
	bool reviseSelected_ = false;
};
