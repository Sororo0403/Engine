#pragma once

#include "gameplay/StageFlowProject.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

class ImGuiSystem;
class SceneDocument;

/// <summary>
/// フローグラフを編集し、選んだシードで試遊シーンへ保存する
/// </summary>
class StageFlowDialog {
  public:
	enum class Result { NONE, CLOSED, OPEN_SAVED_SCENE };

	/// <summary>
	/// プロジェクトの区間カタログとグラフを読み込んで表示する
	/// </summary>
	void Show(const std::filesystem::path &projectRoot);

	/// <summary>
	/// 区間と接続の編集・保存・シーン生成を受け付ける
	/// </summary>
	Result Draw(const ImGuiSystem &ui, SceneDocument &document,
	            const std::filesystem::path &scenesDirectory);

	/// <summary>
	/// 最後に生成したシーンのパスを取得する
	/// </summary>
	const std::filesystem::path &GetSavedPath() const { return savedPath_; }

  private:
	void DrawGraph();
	void DrawNodes(const ImGuiSystem &ui);
	void DrawConnections(const ImGuiSystem &ui);
	void Generate(SceneDocument &document, const std::filesystem::path &scenesDirectory);

	StageFlow::Project project_;
	std::filesystem::path root_;
	std::filesystem::path savedPath_;
	std::size_t selectedNode_ = 0;
	std::size_t selectedConnection_ = 0;
	std::uint32_t seed_ = 0;
	std::string newNodeName_;
	std::string sceneName_;
	std::string message_;
	std::string error_;
	std::vector<std::string> placements_;
	bool opening_ = false;
	bool loaded_ = false;
	bool dirty_ = false;
};
