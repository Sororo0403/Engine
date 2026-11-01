#pragma once

#include "core/LogEntry.h"

#include <cstdint>
#include <string>
#include <vector>

class ImGuiSystem;

/// <summary>
/// 直近のログを重要度と文字列で絞り込み、詳細を表示する
/// </summary>
class ConsolePanel {
  public:
	/// <summary>
	/// Consoleを開き、次の描画時にフォーカスを移す
	/// </summary>
	void Show();

	/// <summary>
	/// Consoleが開いている場合にログの一覧と操作用UIを描画する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	void Draw(const ImGuiSystem &ui);

  private:
	/// <summary>
	/// メッセージの重要度と本文が現在の絞り込みに一致するか確認する
	/// </summary>
	/// <param name="entry">確認するログ</param>
	/// <returns>true:表示する false:非表示</returns>
	bool MatchesFilter(const LogEntry &entry) const;

	bool open_ = false;
	bool focusRequested_ = false;
	bool autoScroll_ = true;
	bool scrollRequested_ = true;
	bool showInfo_ = true;
	bool showWarnings_ = true;
	bool showErrors_ = true;
	std::string filter_;
	std::uint64_t revision_ = 0;
	std::uint64_t selectedId_ = 0;
	std::uint64_t lastVisibleId_ = 0;
	std::vector<LogEntry> entries_;
};
