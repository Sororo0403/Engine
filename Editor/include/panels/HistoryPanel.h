#pragma once

#include <cstddef>
#include <optional>

class EditorHistory;
class ImGuiSystem;

/// <summary>
/// 編集履歴の現在地点・保存地点を表示し、移動先の要求を管理する
/// </summary>
class HistoryPanel {
  public:
	/// <summary>
	/// 履歴パネルを開き、次の描画時にフォーカスを移す
	/// </summary>
	void Show();

	/// <summary>
	/// 履歴の一覧を描画する。履歴とシーンは変更せず、選択した移動先を返す
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="history">表示する編集履歴</param>
	/// <param name="enabled">履歴の移動を受け付けるか。編集中は常に無効</param>
	/// <returns>選択された状態番号。要求がない場合はnullopt</returns>
	std::optional<std::size_t> Draw(const ImGuiSystem &ui, const EditorHistory &history, bool enabled = true);

  private:
	bool open_ = false;
	bool focusRequested_ = false;
	bool followCurrent_ = true;
	std::optional<std::size_t> lastCurrent_;
};
