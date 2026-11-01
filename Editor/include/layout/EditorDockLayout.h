#pragma once

#include <string>

class ImGuiSystem;

/// <summary>
/// Editorのパネルの初期配置とドッキング領域を管理する
/// </summary>
class EditorDockLayout {
  public:
	/// <summary>
	/// 必要に応じて初期配置を構築し、ドッキング領域を表示する
	/// </summary>
	/// <param name="ui">ドッキングが有効で、フレーム開始済みのUI</param>
	/// <param name="resetLayout">保存済みの配置を破棄して初期配置へ戻すか</param>
	void Draw(const ImGuiSystem &ui, bool resetLayout = false) const;

  private:
	// 識別名は保存済みのImGui設定と対応する
	std::string dockspaceName_ = "EditorDockSpace";
	std::string hierarchyWindow_ = "Hierarchy";
	std::string sceneWindow_ = "Scene";
	std::string inspectorWindow_ = "Inspector";
	std::string assetsWindow_ = "Assets";
	std::string historyWindow_ = "History";
	std::string consoleWindow_ = "Console";
	std::string performanceWindow_ = "Performance";

	// 全体の幅に対する割合。左右の合計は1未満にする
	float leftRatio_ = 0.20f;
	float rightRatio_ = 0.25f;

	// 全体の高さに対するAssets領域の割合
	float bottomRatio_ = 0.25f;
};
