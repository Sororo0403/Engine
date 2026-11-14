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
	/// <param name="returnToOpenScene">取消でシーンの選択に戻るか</param>
	/// <param name="destination">次に開く場所の表示名。表示しない場合は空</param>
	/// <param name="recovered">復旧したシーンの保存確認か</param>
	void Show(std::string name, bool beforeExport, bool returnToOpenScene,
	          std::string destination = {}, bool recovered = false);

	/// <summary>
	/// 保存確認を描画する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <returns>選択結果。未選択の場合はNONE</returns>
	SaveChangesResult Draw(const ImGuiSystem &ui);

  private:
	std::string name_;
	std::string destination_;
	bool openRequested_ = false;
	bool beforeExport_ = false;
	bool returnToOpenScene_ = false;
	bool recovered_ = false;
};
