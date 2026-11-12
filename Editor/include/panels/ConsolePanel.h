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
	struct ScriptEditRequest {
		std::filesystem::path path;
		std::string diagnostic;
	};

	/// <summary>
	/// Consoleを開き、次の描画時にフォーカスを移す
	/// </summary>
	void Show();

	/// <summary>
	/// 試遊の開始または再開より前のログを区別するため、現在位置を記録する
	/// </summary>
	void MarkPlayStart();

	/// <summary>
	/// Consoleが開いている場合にログの一覧と操作用UIを描画する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="canEditScript">ログに対応するLuaファイルを開けるか</param>
	/// <returns>編集するLuaファイルと診断。操作がなければ空</returns>
	ScriptEditRequest Draw(const ImGuiSystem &ui, bool canEditScript = false);

  private:
	/// <summary>
	/// メッセージの重要度と本文が現在の絞り込みに一致するか確認する
	/// </summary>
	/// <param name="entry">確認するログ</param>
	/// <returns>true:表示する false:非表示</returns>
	bool MatchesFilter(const LogEntry &entry) const;

	/// <summary>
	/// 現在の絞り込みに一致するログを時刻・重要度付きでまとめる
	/// </summary>
	/// <returns>コピー用のUTF-8文字列。メッセージのNUL文字は\0へ変換する</returns>
	std::string BuildFilteredText() const;

	bool open_ = false;
	bool focusRequested_ = false;
	bool autoScroll_ = true;
	bool scrollRequested_ = true;
	bool showInfo_ = true;
	bool showWarnings_ = true;
	bool showErrors_ = true;
	bool latestPlayOnly_ = false;
	std::string filter_;
	std::uint64_t revision_ = 0;
	std::uint64_t selectedId_ = 0;
	std::uint64_t lastVisibleId_ = 0;
	std::uint64_t playStartBoundary_ = 0;
	std::vector<LogEntry> entries_;
};
