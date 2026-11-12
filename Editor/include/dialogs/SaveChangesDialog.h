#pragma once

#include <string>

class ImGuiSystem;

/// <summary>
/// 未保存の変更に対する選択結果
/// </summary>
enum class SaveChangesResult {
	NONE,
	SAVE,
	DISCARD,
	CANCEL,
};

/// <summary>
/// 未保存の変更を保存するか確認する
/// </summary>
class SaveChangesDialog {
  public:
	/// <summary>
	/// 保存確認の表示を予約する
	/// </summary>
	/// <param name="name">編集中のファイルの表示名</param>
	/// <param name="beforeExport">出力前の確認として表示するか</param>
	void Show(std::string name, bool beforeExport = false);

	/// <summary>
	/// 保存確認を描画する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <returns>選択結果。未選択の場合はNONE</returns>
	SaveChangesResult Draw(const ImGuiSystem &ui);

  private:
	std::string name_;
	bool openRequested_ = false;
	bool beforeExport_ = false;
};
