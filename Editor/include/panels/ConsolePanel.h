#pragma once

#include "core/LogEntry.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class ImGuiSystem;

/// <summary>
/// 直近のログを重要度と文字列で絞り込み、詳細を表示する
/// </summary>
class ConsolePanel {
  public:
	struct Request {
		std::filesystem::path path;
		std::string diagnostic;
		std::uint64_t logId = 0;
		bool retryPlay = false;
		std::filesystem::path diagnosticPath;
		bool editInput = false;
		bool openFailedScene = false;
	};

	/// <summary>
	/// Consoleを開き、次の描画時にフォーカスを移す
	/// </summary>
	void Show();

	/// <summary>
	/// Consoleを開き、指定した試遊エラーの選択を予約する
	/// </summary>
	void ShowPlayError(std::uint64_t logId);

	/// <summary>
	/// 試遊の開始または再開より前のログを区別するため、現在位置を記録する
	/// </summary>
	void MarkPlayStart();

	/// <summary>
	/// Consoleが開いている場合にログの一覧と操作用UIを描画する
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="canEditScript">ログに対応するLuaファイルを開けるか</param>
	/// <param name="stopPlayToEdit">編集に移るときに試遊を停止するか</param>
	/// <param name="retryPlayLogId">再試遊を許可するエラーログのID。0なら無効</param>
	/// <param name="retryScene">再試遊の開始先。空なら編集中のシーン</param>
	/// <param name="retryScript">移動後のLuaパス。空ならログのパスを使う</param>
	/// <param name="retryInput">直近の試遊失敗で修正が必要な名前付き入力。空なら編集操作を表示しない</param>
	/// <param name="failedScene">エラーが起きた編集元シーン。空ならシーンを開く操作を表示しない</param>
	/// <returns>Luaや入力の編集、シーンを開く操作、または再試遊の要求</returns>
	Request Draw(const ImGuiSystem &ui, bool canEditScript = false,
	             bool stopPlayToEdit = false, std::uint64_t retryPlayLogId = 0,
	             const std::filesystem::path &retryScene = {},
	             const std::filesystem::path &retryScript = {},
	             std::string_view retryInput = {},
	             const std::filesystem::path &failedScene = {});

  private:
	/// <summary>
	/// メッセージの重要度と本文が現在の絞り込みに一致するか確認する
	/// </summary>
	/// <param name="entry">確認するログ</param>
	/// <param name="searchPath">区切りと引用符を揃えた検索パス</param>
	/// <returns>true:表示する false:非表示</returns>
	bool MatchesFilter(const LogEntry &entry,
	                   const std::filesystem::path &searchPath) const;

	/// <summary>
	/// 現在の絞り込みに一致するログを時刻・重要度付きでまとめる
	/// </summary>
	/// <returns>コピー用のUTF-8文字列。メッセージのNUL文字は\0へ変換する</returns>
	std::string BuildFilteredText(const std::filesystem::path &searchPath) const;

	bool open_ = false;
	bool focusRequested_ = false;
	std::uint64_t requestedPlayErrorId_ = 0;
	bool autoScroll_ = true;
	bool scrollRequested_ = true;
	bool showInfo_ = true;
	bool showWarnings_ = true;
	bool showErrors_ = true;
	bool latestPlayOnly_ = false;
	bool collapseRepeated_ = true;
	std::string filter_;
	std::string sourceActionError_;
	std::uint64_t revision_ = 0;
	std::uint64_t selectedId_ = 0;
	std::uint64_t sourceActionErrorId_ = 0;
	std::uint64_t lastVisibleId_ = 0;
	std::uint64_t playStartBoundary_ = 0;
	std::vector<LogEntry> entries_;
};
