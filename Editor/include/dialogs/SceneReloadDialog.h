#pragma once

#include <string>

class ImGuiSystem;

/// <summary>
/// シーンの再読み込みの確認結果
/// </summary>
enum class SceneReloadResult {
	NONE,
	RELOAD,
	SAVE_COPY_AND_RELOAD,
	CANCELLED,
};

/// <summary>
/// 編集内容を破棄して保存先のシーンを読み直すか確認する
/// </summary>
class SceneReloadDialog {
  public:
	/// <summary>
	/// 再読み込みの確認の表示を予約する
	/// </summary>
	/// <param name="name">現在のシーンファイルの表示名</param>
	/// <param name="modified">未保存の編集があるか</param>
	void Show(std::string name, bool modified);

	/// <summary>
	/// 再読み込みの確認を描画する。Escはキャンセルとして扱う
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <returns>選択結果。未選択の場合はNONE</returns>
	SceneReloadResult Draw(const ImGuiSystem &ui);

  private:
	std::string name_;
	bool modified_ = false;
	bool openRequested_ = false;
};
