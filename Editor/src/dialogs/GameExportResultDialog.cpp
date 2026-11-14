#include "dialogs/GameExportResultDialog.h"
#include "gameplay/GameConfig.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/ShellUtils.h"
#include "platform/windows/StringConversion.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <imgui.h>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

namespace {
constexpr auto kGameLogPath = L"logs/Engine.log";
constexpr auto kLogRefreshInterval = std::chrono::milliseconds{500};
constexpr std::streamoff kLogPageBytes = 64 * 1024;

/// <summary>
/// Loggerのヘッダーを持つ行からログレベルを取得する
/// </summary>
std::string_view LogLevelPrefix(std::string_view line) {
	const auto timestampEnd = line.find(']');
	return line.starts_with('[') && timestampEnd != std::string_view::npos
	           ? line.substr(timestampEnd + 1)
	           : std::string_view{};
}

/// <summary>
/// ログ項目の開始を示すレベル表記か確認する
/// </summary>
bool IsLogHeader(std::string_view level) {
	return level.starts_with(" [INFO] ") || level.starts_with(" [WARNING] ") ||
	       level.starts_with(" [ERROR] ");
}

/// <summary>
/// 表示中のログを行単位で走査する。行末の改行も範囲に含める
/// </summary>
void VisitLogLines(std::string_view log, const auto &visit) {
	for (std::size_t start = 0; start < log.size();) {
		const auto newline = log.find('\n', start);
		const auto end = newline == std::string_view::npos ? log.size() : newline + 1;
		visit(start, log.substr(start, end - start));
		start = end;
	}
}

/// <summary>
/// エラーの見出しと、それに続く詳細行を取り出す
/// </summary>
std::string CollectLogErrors(std::string_view log) {
	std::string errors;
	bool includeLine = false;
	VisitLogLines(log, [&](std::size_t, std::string_view line) {
		const auto level = LogLevelPrefix(line);
		if (level.starts_with(" [ERROR] ")) {
			includeLine = true;
		} else if (IsLogHeader(level)) {
			includeLine = false;
		}
		if (includeLine) {
			errors.append(line);
		}
	});
	return errors;
}

/// <summary>
/// 本文はそのまま照合し、貼り付けた絶対パスは区切りや大文字小文字を揃えて照合する
/// </summary>
std::size_t FindLogText(std::string_view text, std::string_view query,
                        const std::filesystem::path &searchPath) {
	const auto literal = text.find(query);
	if (literal != std::string_view::npos || !searchPath.is_absolute()) {
		return literal;
	}

	auto wide = StringConversion::Utf8ToWide(text);
	std::ranges::replace(wide, L'/', L'\\');
	const auto offset = FindStringOrdinal(FIND_FROMSTART, wide.c_str(), -1,
	                                      searchPath.c_str(), -1, TRUE);
	if (offset < 0) {
		return std::string_view::npos;
	}

	// ページ移動には、UTF-16の文字位置ではなく元のUTF-8のバイト位置を使う。
	return StringConversion::WideToUtf8(
	           std::wstring_view(wide).substr(0, static_cast<std::size_t>(offset)))
	    .size();
}

/// <summary>
/// 検索語を含むログ項目を、複数行の詳細を保ったまま取り出す
/// </summary>
std::string FilterLogEntries(std::string_view log, std::string_view filter) {
	std::string result;
	const auto searchPath = PathUtils::FromPathSearch(filter);
	std::size_t start = 0;
	const auto appendMatch = [&](std::size_t end) {
		const auto entry = log.substr(start, end - start);
		if (FindLogText(entry, filter, searchPath) != std::string_view::npos) {
			result.append(entry);
		}
	};
	VisitLogLines(log, [&](std::size_t offset, std::string_view line) {
		if (offset != 0 && IsLogHeader(LogLevelPrefix(line))) {
			appendMatch(offset);
			start = offset;
		}
	});
	appendMatch(log.size());
	return result;
}

/// <summary>
/// エラーから始まるページで、次のログ見出しまでの診断を返す
/// </summary>
std::string_view LeadingLogError(std::string_view log) {
	if (!LogLevelPrefix(log.substr(0, log.find('\n'))).starts_with(" [ERROR] ")) {
		return {};
	}

	auto end = log.size();
	VisitLogLines(log, [&](std::size_t offset, std::string_view line) {
		if (offset == 0 || end != log.size()) {
			return;
		}
		if (IsLogHeader(LogLevelPrefix(line))) {
			end = offset;
		}
	});
	return log.substr(0, end);
}
}

void GameExportResultDialog::Show(std::filesystem::path directory,
                                  bool reopening) {
	directory_ = std::move(directory);
	runs_.try_emplace(directory_);
	reopening_ = reopening;
	error_.clear();
	GameConfig config;
	ReloadGameSettings(config);
	log_.clear();
	logError_.clear();
	logTruncated_ = false;
	nextLogPage_.reset();
	if (runs_.at(directory_).showLog) {
		RefreshLog();
	}
	opening_ = true;
}

void GameExportResultDialog::ReloadGameSettings(GameConfig &config) {
	const auto path = directory_ / L"game.json";
	if (!config.Load(path, packageSettingsError_)) {
		packageSettingsError_ = std::format("Could not load {}: {}",
		                                    PathUtils::ToUtf8(path), packageSettingsError_);
		packageSettings_.clear();
		return;
	}

	packageSettings_ = std::format("Game window: {} ({} × {})\nStartup scene: {}",
	                               config.title, config.width, config.height,
	                               PathUtils::ToUtf8(config.scenePath));
}

void GameExportResultDialog::RefreshLog() {
	auto &run = runs_.at(directory_);
	nextLogRefresh_ = std::chrono::steady_clock::now() + kLogRefreshInterval;
	log_.clear();
	logError_.clear();
	logTruncated_ = false;
	nextLogPage_.reset();
	const auto path = directory_ / kGameLogPath;
	std::error_code status;
	if (!std::filesystem::exists(path, status) && !status) {
		if (run.logStart)
			*run.logStart = 0;
		run.earlierLogPages.clear();
		run.logPageOffset = 0;
		run.beforeErrorJump.reset();
		run.lastMatch.reset();
		logError_ = "Game log hasn't been created yet.";
		return;
	}

	std::ifstream input(path, std::ios::binary | std::ios::ate);
	const auto end = static_cast<std::streamoff>(input.tellg());
	if (end < 0) {
		logError_ = "Could not read the game log.";
		return;
	}
	if (run.logStart && *run.logStart > static_cast<std::uintmax_t>(end)) {
		// ログが切り詰められた場合は、新しいファイルの先頭から表示する。
		*run.logStart = 0;
		run.earlierLogPages.clear();
		run.logPageOffset = 0;
		run.beforeErrorJump.reset();
		run.lastMatch.reset();
	}
	const auto firstIncluded = run.sinceLaunchOnly && run.logStart
	                               ? static_cast<std::streamoff>(*run.logStart)
	                               : 0;
	const auto available = end - firstIncluded;
	if (run.logPageOffset >= static_cast<std::uintmax_t>(available)) {
		run.earlierLogPages.clear();
		run.logPageOffset = 0;
	}
	const auto start = run.showLogBeginning
	                       ? firstIncluded + static_cast<std::streamoff>(run.logPageOffset)
	                       : end - (std::min)(available, kLogPageBytes);
	const auto size = (std::min)(end - start, kLogPageBytes);
	bool partialLine = false;
	if (!run.showLogBeginning && start > firstIncluded) {
		input.seekg(start - 1);
		partialLine = input.get() != '\n';
	} else {
		input.seekg(start);
	}
	std::string text(static_cast<std::size_t>(size), '\0');
	if (size > 0 && !input.read(text.data(), size)) {
		logError_ = "Could not read the game log. Refresh to try again.";
		return;
	}

	logTruncated_ = size < end - firstIncluded;
	if (run.showLogBeginning && size < end - start && text.back() != '\n') {
		// 表示上限が行の途中なら、不完全な行を取り除く。
		if (const auto newline = text.rfind('\n'); newline != std::string::npos) {
			text.resize(newline + 1);
		} else if ((input.peek() & 0xc0) == 0x80) {
			// 1行が上限を超える場合は、末尾の不完全なUTF-8文字だけを除く。
			auto last = text.size() - 1;
			while (last > 0 && (text[last] & 0xc0) == 0x80) {
				--last;
			}
			text.resize(last);
		}
	}
	if (run.showLogBeginning && start + static_cast<std::streamoff>(text.size()) < end) {
		// 次のページが詳細行から始まる場合は、見出しごと次へ送る。
		input.seekg(start + static_cast<std::streamoff>(text.size()));
		std::array<char, 128> prefix{};
		input.getline(prefix.data(), prefix.size());
		if (!IsLogHeader(LogLevelPrefix(prefix.data()))) {
			std::size_t lastHeader = 0;
			VisitLogLines(text, [&](std::size_t offset, std::string_view line) {
				if (IsLogHeader(LogLevelPrefix(line))) {
					lastHeader = offset;
				}
			});
			if (lastHeader != 0) {
				text.resize(lastHeader);
			}
		}
		nextLogPage_ = run.logPageOffset + text.size();
	}
	if (partialLine) {
		// 先頭が行の途中なら取り除く。長い1行ではUTF-8の継続バイトだけ除く。
		const auto newline = text.find('\n');
		if (newline != std::string::npos && newline + 1 < text.size()) {
			text.erase(0, newline + 1);
		} else {
			const auto firstByte = std::ranges::find_if(text, [](unsigned char byte) {
				return (byte & 0xc0) != 0x80;
			});
			text.erase(text.begin(), firstByte);
		}
	}
	for (const char byte : text) {
		if (byte == '\0')
			log_ += "\\0";
		else
			log_ += byte;
	}
}

bool GameExportResultDialog::OpenLogAtError(ErrorSearch search, bool reportMissing) {
	auto &run = runs_.at(directory_);
	std::ifstream input(directory_ / kGameLogPath, std::ios::binary | std::ios::ate);
	const auto end = static_cast<std::streamoff>(input.tellg());
	if (end < 0) {
		if (reportMissing)
			error_ = "Could not read the game log.";
		return false;
	}
	const auto start = run.sinceLaunchOnly && run.logStart &&
	                           *run.logStart <= static_cast<std::uintmax_t>(end)
	                       ? static_cast<std::streamoff>(*run.logStart) : 0;
	const bool previous = search == ErrorSearch::PREVIOUS;
	const bool nextError = search == ErrorSearch::NEXT;
	const auto anchor = start + static_cast<std::streamoff>(
	                                (std::min)(run.logPageOffset,
	                                           static_cast<std::uintmax_t>(end - start)));
	input.seekg(nextError ? anchor : start);
	std::optional<std::uintmax_t> match;
	// 見出しだけを読み取り、長い詳細行でもメモリ使用量を抑える。
	std::array<char, 128> prefix{};
	for (auto offset = nextError ? anchor : start;
	     offset < (previous ? anchor : end);) {
		input.getline(prefix.data(), prefix.size());
		if (input.bad())
			break;
		if (input.fail() && !input.eof()) {
			input.clear();
			input.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
		}
		if ((!nextError || offset > anchor) &&
		    LogLevelPrefix(prefix.data()).starts_with(" [ERROR] ")) {
			match = static_cast<std::uintmax_t>(offset - start);
			if (search == ErrorSearch::FIRST || nextError)
				break;
		}
		const auto next = static_cast<std::streamoff>(input.tellg());
		if (next <= offset)
			break;
		offset = next;
	}
	if (input.bad()) {
		if (reportMissing)
			error_ = "Could not search the game log.";
		return false;
	}
	if (!match) {
		if (reportMissing) {
			error_ = previous ? "No earlier errors in the selected log range."
			         : nextError ? "No later errors in the selected log range."
			                     : "No errors in the selected log range.";
		}
		return false;
	}

	OpenLogAtOffset(*match);
	run.lastMatch.reset();
	return true;
}

bool GameExportResultDialog::FindLogMatch(MatchSearch direction) {
	const bool next = direction == MatchSearch::NEXT;
	const bool previous = direction == MatchSearch::PREVIOUS;
	const bool relativeSearch = next || previous;
	auto &run = runs_.at(directory_);
	if (relativeSearch && !run.lastMatch) {
		return false;
	}
	auto query = relativeSearch ? run.lastMatch->query : run.logFilter;
	const auto searchPath = PathUtils::FromPathSearch(query);
	const bool errorsOnly = relativeSearch ? run.lastMatch->errorsOnly : run.showErrorsOnly;
	std::ifstream input(directory_ / kGameLogPath, std::ios::binary | std::ios::ate);
	const auto end = static_cast<std::streamoff>(input.tellg());
	if (end < 0) {
		error_ = "Could not read the game log.";
		return false;
	}

	const auto start = run.sinceLaunchOnly && run.logStart &&
	                           *run.logStart <= static_cast<std::uintmax_t>(end)
	                       ? static_cast<std::streamoff>(*run.logStart) : 0;
	const auto anchor = relativeSearch ? run.lastMatch->offset : 0;
	if (relativeSearch && anchor >= static_cast<std::uintmax_t>(end - start)) {
		run.lastMatch.reset();
		error_ = "The game log changed. Start the search again.";
		return false;
	}
	const auto scanStart = start + static_cast<std::streamoff>(next ? anchor : 0);
	input.seekg(scanStart);
	bool isError = next && run.lastMatch->inError;
	auto entryOffset = next ? run.lastMatch->entryOffset : std::uintmax_t{0};
	std::optional<GameRun::LogMatch> found;
	std::uintmax_t pageOffset = 0;
	bool showErrorsOnly = false;
	const auto scanEnd = previous ? start + static_cast<std::streamoff>(anchor) : end;
	std::string line;
	for (auto offset = scanStart; offset < scanEnd && std::getline(input, line);
	     offset = static_cast<std::streamoff>(input.tellg())) {
		const auto level = LogLevelPrefix(line);
		const auto relative = static_cast<std::uintmax_t>(offset - start);
		if (IsLogHeader(level)) {
			isError = level.starts_with(" [ERROR] ");
			entryOffset = relative;
		}
				const auto match = (!next || relative > anchor) && (!errorsOnly || isError)
		                       ? FindLogText(line, query, searchPath) : std::string_view::npos;
		if (match != std::string_view::npos) {
			// 見出しから開くと一致行がページ外になる場合は、一致行を優先する。
			const auto lineEnd = input.eof() ? end : static_cast<std::streamoff>(input.tellg());
			const bool fits = lineEnd - start - static_cast<std::streamoff>(entryOffset) <=
			                  kLogPageBytes;
			pageOffset = fits ? entryOffset : relative;
			if (!fits) {
				// 長い1行では、一致した文字列を含む位置から開く。
				const auto context = static_cast<std::size_t>(kLogPageBytes / 4);
				if (match >= context) {
					auto first = match - context;
					while (first > 0 && (line[first] & 0xc0) == 0x80) {
						--first;
					}
					pageOffset += first;
				}
			}
			showErrorsOnly = errorsOnly && fits;
			found = GameRun::LogMatch{query, relative, entryOffset, errorsOnly, isError};
			if (!previous) {
				break;
			}
		}
		if (input.eof()) {
			break;
		}
	}
	if (found && !input.bad()) {
		OpenLogAtOffset(pageOffset);
		run.showErrorsOnly = showErrorsOnly;
		run.lastMatch = std::move(found);
		return true;
	}
	error_ = input.bad() ? "Could not search the game log."
	         : previous  ? "No earlier matching lines in the selected log range."
	         : next      ? "No later matching lines in the selected log range."
	                     : "No matching lines in the selected log range.";
	return false;
}

void GameExportResultDialog::OpenLogAtOffset(std::uintmax_t offset) {
	auto &run = runs_.at(directory_);
	error_.clear();
	if (!run.beforeErrorJump) {
		run.beforeErrorJump = LogPosition{run.earlierLogPages, run.logPageOffset,
		                                 run.showLogBeginning, run.followLog,
		                                 run.showErrorsOnly, run.sinceLaunchOnly,
		                                 run.logScrollY, run.logFilter};
	}
	if (!run.showLogBeginning || run.logPageOffset != offset) {
		run.earlierLogPages.clear();
		if (offset != 0)
			run.earlierLogPages.push_back(0);
		run.logPageOffset = offset;
	}
	run.followLog = false;
	run.showLogBeginning = true;
	run.logFilter.clear();
	RefreshLog();
}

bool GameExportResultDialog::StartGame() {
	auto &run = runs_.at(directory_);
	GameConfig config;
	ReloadGameSettings(config);
	auto launchError = packageSettingsError_;
	run.launchSettings = packageSettings_;
	run.crashError.clear();
	run.launchFailurePath.clear();
	error_.clear();
	if (!launchError.empty()) {
		run.launchFailurePath = directory_ / L"game.json";
	} else {
		const auto scenePath = directory_ / config.scenePath;
		std::error_code status;
		if (!std::filesystem::is_regular_file(scenePath, status)) {
			run.launchFailurePath = scenePath;
			launchError = std::format(
			    "The startup scene is missing or unavailable: {}",
			    PathUtils::ToUtf8(scenePath));
		}
	}
	if (!launchError.empty()) {
		run.launchFailed = true;
		run.status = std::move(launchError);
		return false;
	}
	// 起動後すぐにログが追記されても、今回の行を落とさないよう先に記録する。
	std::error_code status;
	const auto size = std::filesystem::file_size(directory_ / kGameLogPath, status);
	run.launchFailed = !run.process.Start(directory_ / L"Game.exe", launchError,
	                                      ShellUtils::WindowVisibility::NORMAL,
	                                      L"--editor-launch");
	if (run.launchFailed) {
		run.launchFailurePath = directory_ / L"Game.exe";
		run.status = "Could not launch Game.exe: " + launchError;
		return false;
	}

	run.status = "Game process running.";
	run.logStart.reset();
	if (!status)
		run.logStart = size;
	else if (status == std::errc::no_such_file_or_directory)
		run.logStart = 0;
	run.sinceLaunchOnly = run.logStart.has_value();
	run.showLogBeginning = false;
	run.earlierLogPages.clear();
	run.logPageOffset = 0;
	run.beforeErrorJump.reset();
	run.lastMatch.reset();
	if (run.showLog)
		RefreshLog();
	return run.showLog;
}

bool GameExportResultDialog::UpdateGameProcess(bool &jumpToBeginning,
                                               bool &jumpToLatest) {
	auto &run = runs_.at(directory_);
	if (!run.process.IsTracking())
		return false;

	std::optional<std::uint32_t> exitCode;
	std::string error;
	if (!run.process.Poll(exitCode, error)) {
		run.status = std::move(error);
		run.restartRequested = false;
		run.process.Reset();
		return false;
	}
	if (!exitCode)
		return false;

	run.process.Reset();
	const bool restart = std::exchange(run.restartRequested, false);
	if (*exitCode == 0) {
		if (restart) {
			jumpToLatest = StartGame();
			return jumpToLatest;
		}
		run.status = "Game exited normally.";
		if (run.showLog)
			RefreshLog();
		return run.showLog;
	}
	run.status = std::format("Game exited with code 0x{:08X}.", *exitCode);
	run.showLog = true;
	// 表示範囲に関わらず、異常終了の原因は今回の実行から探す。
	const bool previousRange = run.sinceLaunchOnly;
	const bool hadPreviousPosition = run.beforeErrorJump.has_value();
	run.sinceLaunchOnly = run.logStart.has_value();
	if (run.sinceLaunchOnly && OpenLogAtError(ErrorSearch::LATEST, false)) {
		if (!hadPreviousPosition && run.beforeErrorJump) {
			run.beforeErrorJump->sinceLaunchOnly = previousRange;
		}
		run.crashError = LeadingLogError(log_);
		jumpToBeginning = true;
		return true;
	}
	run.sinceLaunchOnly = previousRange;
	run.showLogBeginning = false;
	RefreshLog();
	jumpToLatest = true;
	return true;
}

GameExportResultAction GameExportResultDialog::Draw(const ImGuiSystem &ui) {
	const char *title = reopening_ ? "Game Export###GameExportResultDialog"
	                              : "Game Exported###GameExportResultDialog";

	if (opening_) {
		ui.OpenPopup(title);
		opening_ = false;
	}

	ImGui::SetNextWindowSize({600, 0}, ImGuiCond_Appearing);

	if (!ui.BeginPopupModal(title)) {
		return GameExportResultAction::NONE;
	}

	ImGui::TextWrapped("%s", PathUtils::ToUtf8(directory_).c_str());
	if (!packageSettings_.empty()) {
		ImGui::TextWrapped("%s", packageSettings_.c_str());
	}

	auto &run = runs_.at(directory_);
	bool jumpToBeginning = false;
	bool jumpToLatest = false;
	bool refreshedLog = UpdateGameProcess(jumpToBeginning, jumpToLatest);
	if (!run.status.empty())
		ImGui::TextWrapped("%s", run.status.c_str());

	if (!packageSettingsError_.empty()) {
		if (packageSettingsError_ != run.status) {
			ImGui::TextWrapped("%s", packageSettingsError_.c_str());
		}
		if (ui.Button("Reload Game Settings")) {
			GameConfig config;
			ReloadGameSettings(config);
		}
	}

	if (!run.launchFailurePath.empty()) {
		const auto path = PathUtils::ToUtf8(run.launchFailurePath);
		if (ui.Button("Show Startup Error Location")) {
			ShellUtils::RevealLocationInExplorer(run.launchFailurePath, error_);
		}
		ImGui::SetItemTooltip("%s", path.c_str());
		ui.SameLine();
		if (ui.Button("Copy Startup Error Path")) {
			ImGui::SetClipboardText(path.c_str());
		}
	}

	if (!error_.empty()) {
		ImGui::TextWrapped("%s", error_.c_str());
	}

	if (ui.Button("Open Output Folder")) {
		ShellUtils::OpenDirectory(directory_, error_);
	}

	ui.SameLine();

	ui.BeginDisabled(run.process.IsTracking());
	if (ui.Button("Run Game")) {
		jumpToLatest |= StartGame();
	}
	ui.EndDisabled();

	if (run.process.IsTracking()) {
		ui.SameLine();
		if (ui.Button("Show Running Game")) {
			run.process.BringToForeground(error_);
		}
		ui.SameLine();
		if (ui.Button("Close Running Game") && run.process.RequestClose(error_)) {
			run.restartRequested = false;
			run.status = "Requested game exit. Waiting for the process to finish.";
		}
		ui.BeginDisabled(run.restartRequested);
		if (ui.Button("Restart Game") && run.process.RequestClose(error_)) {
			run.restartRequested = true;
			run.status = "Waiting for the game to exit before restarting.";
		}
		ui.EndDisabled();
		ImGui::SetItemTooltip("Start the game again after it exits normally.");
	}

	ui.SameLine();
	const auto *logButton = run.showLog ? "Hide Game Log"
	                       : run.launchFailed ? "View Previous Game Log"
	                                          : "View Game Log";
	if (ui.Button(logButton)) {
		run.showLog = !run.showLog;
		if (run.showLog) {
			RefreshLog();
			jumpToLatest = true;
		}
	}

	ui.SameLine();
	const bool newExport = ui.Button("New Export...");
	ui.SameLine();
	const bool close = ui.Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
	GameExportResultAction action = GameExportResultAction::NONE;
	if (close || newExport) {
		if (std::exchange(run.restartRequested, false)) {
			run.status = "Requested game exit. Waiting for the process to finish.";
		}
		action = close ? GameExportResultAction::CLOSE : GameExportResultAction::NEW_EXPORT;
		ui.CloseCurrentPopup();
	}

	if (!run.status.empty() && ui.Button("Copy Run Report")) {
		auto report = "Output: " + PathUtils::ToUtf8(directory_) + "\n";
		if (!run.launchSettings.empty()) {
			report += run.launchSettings + "\n";
		}
		report += run.status + "\n";
		if (!run.launchFailurePath.empty()) {
			report += "Startup error path: " + PathUtils::ToUtf8(run.launchFailurePath) + "\n";
		}
		if (!run.launchFailed) {
			RefreshLog();
			refreshedLog = true;
			if (!run.crashError.empty() && log_.find(run.crashError) == std::string::npos) {
				report += "\nLast error before abnormal exit:\n";
				report += run.crashError + "\n";
			}
			if (!logError_.empty()) {
				report += logError_ + "\n";
			} else if (!log_.empty()) {
				report += run.sinceLaunchOnly ? "\nGame log since Run Game"
				                              : "\nGame log";
				if (logTruncated_) {
					report += !run.showLogBeginning ? " (tail)"
					          : run.logPageOffset == 0 ? " (beginning)" : " (page)";
				}
				report += ":\n";
				report += log_;
			}
		}
		ImGui::SetClipboardText(report.c_str());
	}

	if (run.showLog) {
		if (run.launchFailed) {
			ui.Text("Previous game log. The latest attempt did not start.");
		}
		std::optional<float> restoreScroll;
		if (run.logStart &&
		    ImGui::Checkbox("Since Run Game", &run.sinceLaunchOnly)) {
			run.beforeErrorJump.reset();
			run.lastMatch.reset();
			run.earlierLogPages.clear();
			run.logPageOffset = 0;
			RefreshLog();
			jumpToBeginning = run.showLogBeginning;
			jumpToLatest = !run.showLogBeginning;
		}
		if (ui.Checkbox("Show Log Beginning", run.showLogBeginning)) {
			run.earlierLogPages.clear();
			run.logPageOffset = 0;
			RefreshLog();
			jumpToBeginning = run.showLogBeginning;
			jumpToLatest = !run.showLogBeginning;
		}
		ui.BeginDisabled(run.showLogBeginning);
		if (ui.Checkbox("Follow Log", run.followLog) && run.followLog) {
			jumpToLatest = true;
		}
		ui.EndDisabled();
		ImGui::SetItemTooltip("Follow new messages while the log is at the bottom.");
		if (ui.Button("Refresh Log")) {
			RefreshLog();
			refreshedLog = true;
		}
		if (run.process.IsTracking() &&
		    std::chrono::steady_clock::now() >= nextLogRefresh_) {
			const auto previous = std::move(log_);
			RefreshLog();
			refreshedLog |= log_ != previous;
		}
		ui.SameLine();
		ui.BeginDisabled(log_.empty());
		if (ui.Button("Copy Log"))
			ImGui::SetClipboardText(log_.c_str());
		ui.EndDisabled();
		ImGui::SetItemTooltip("Copy the current page or tail before filtering.");

		ui.SameLine();
		const auto logPath = directory_ / kGameLogPath;
		std::error_code status;
		ui.BeginDisabled(!std::filesystem::is_regular_file(logPath, status));
		if (ui.Button("Show Log in Explorer"))
			ShellUtils::RevealInExplorer(logPath, error_);
		ui.EndDisabled();

		ui.BeginDisabled(log_.empty());
		if (ui.Button("Jump to Latest")) {
			if (run.showLogBeginning) {
				run.showLogBeginning = false;
				RefreshLog();
			}
			jumpToLatest = true;
			jumpToBeginning = false;
		}
		ui.EndDisabled();
		if (run.showLogBeginning) {
			ui.BeginDisabled(run.earlierLogPages.empty());
			if (ui.Button("Previous Page") && !run.earlierLogPages.empty()) {
				run.logPageOffset = run.earlierLogPages.back();
				run.earlierLogPages.pop_back();
				RefreshLog();
				jumpToBeginning = true;
			}
			ui.EndDisabled();
			ui.SameLine();
			ui.BeginDisabled(!nextLogPage_);
			if (ui.Button("Next Page") && nextLogPage_) {
				run.earlierLogPages.push_back(run.logPageOffset);
				run.logPageOffset = *nextLogPage_;
				RefreshLog();
				jumpToBeginning = true;
			}
			ui.EndDisabled();
		}
		bool changedFilter = ui.Checkbox("Errors Only", run.showErrorsOnly);
		ImGui::SetItemTooltip("Show errors and their details on the current log page.");
		changedFilter |= ui.InputText("Search Log Page", run.logFilter);
		ImGui::SetItemTooltip("Filter the current page by text or paste an absolute file path.");
		ui.SameLine();
		const bool canClearSearch = !run.logFilter.empty() || run.lastMatch.has_value();
		ui.BeginDisabled(!canClearSearch);
		if (ui.Button("Clear Search") && canClearSearch) {
			run.logFilter.clear();
			run.lastMatch.reset();
			error_.clear();
		}
		ui.EndDisabled();
		if (changedFilter) {
			run.lastMatch.reset();
		}
		ui.SameLine();
		ui.BeginDisabled(run.logFilter.empty());
		if (ui.Button("Find First in Log") && !run.logFilter.empty()) {
			jumpToBeginning |= FindLogMatch(MatchSearch::FIRST);
		}
		ui.EndDisabled();
		ImGui::SetItemTooltip("Find the first matching line across the selected log range.");
		if (run.lastMatch) {
			ui.SameLine();
			if (ui.Button("Find Previous in Log")) {
				jumpToBeginning |= FindLogMatch(MatchSearch::PREVIOUS);
			}
			ImGui::SetItemTooltip("Find the previous line matching the same search.");
			ui.SameLine();
			if (ui.Button("Find Next in Log")) {
				jumpToBeginning |= FindLogMatch(MatchSearch::NEXT);
			}
			ImGui::SetItemTooltip("Find the next line matching the same search.");
		}
		ui.BeginDisabled(log_.empty() || jumpToLatest);
		if (ui.Button("First Log Error")) {
			jumpToBeginning |= OpenLogAtError(ErrorSearch::FIRST);
		}
		ImGui::SetItemTooltip("Find the first error across the selected log range.");
		ui.SameLine();
		if (ui.Button("Latest Log Error")) {
			jumpToBeginning |= OpenLogAtError(ErrorSearch::LATEST);
		}
		ImGui::SetItemTooltip("Find the latest error across the selected log range.");
		ui.EndDisabled();
		if (run.showLogBeginning && !log_.empty()) {
			if (ui.Button("Previous Log Error")) {
				jumpToBeginning |= OpenLogAtError(ErrorSearch::PREVIOUS);
			}
			ui.SameLine();
			if (ui.Button("Next Log Error")) {
				jumpToBeginning |= OpenLogAtError(ErrorSearch::NEXT);
			}
		}
		if (run.beforeErrorJump) {
			ui.SameLine();
			if (ui.Button("Back to Log Position")) {
				auto previous = std::move(*run.beforeErrorJump);
				run.beforeErrorJump.reset();
				run.lastMatch.reset();
				run.earlierLogPages = std::move(previous.earlierPages);
				run.logPageOffset = previous.offset;
				run.showLogBeginning = previous.paged;
				run.followLog = previous.follow;
				run.showErrorsOnly = previous.errorsOnly;
				run.sinceLaunchOnly = previous.sinceLaunchOnly;
				run.logFilter = std::move(previous.filter);
				RefreshLog();
				restoreScroll = previous.scrollY;
				jumpToBeginning = false;
				jumpToLatest = false;
			}
			ImGui::SetItemTooltip("Return to the log position and filters from before the jump.");
		}
		const auto filtered = run.logFilter.empty() ? std::string{}
		                                           : FilterLogEntries(log_, run.logFilter);
		const auto &matching = run.logFilter.empty() ? log_ : filtered;
		const auto errors = CollectLogErrors(matching);
		const auto &displayedLog = run.showErrorsOnly ? errors : matching;
		ui.BeginDisabled(errors.empty());
		if (ui.Button("Copy Displayed Errors") && !errors.empty()) {
			ImGui::SetClipboardText(errors.c_str());
		}
		ui.EndDisabled();
		ImGui::SetItemTooltip("Copy errors and their details from the displayed log.");
		if (!run.logFilter.empty() || run.showErrorsOnly) {
			ui.SameLine();
			ui.BeginDisabled(displayedLog.empty());
			if (ui.Button("Copy Filtered Log") && !displayedLog.empty()) {
				ImGui::SetClipboardText(displayedLog.c_str());
			}
			ui.EndDisabled();
			ImGui::SetItemTooltip("Copy entries on this page that match the current filters.");
		}
		if (run.showLogBeginning) {
			const auto firstError = LeadingLogError(log_);
			if (!firstError.empty() &&
			    FindLogText(firstError, run.logFilter, PathUtils::FromPathSearch(run.logFilter)) !=
			        std::string_view::npos) {
				ui.SameLine();
				if (ui.Button("Copy First Error on Page")) {
					ImGui::SetClipboardText(std::string(firstError).c_str());
				}
				ImGui::SetItemTooltip("Copy the error at the top of the page and its details.");
			}
		}

		if (!logError_.empty()) {
			ui.Text(logError_);
		} else if (log_.empty()) {
			ui.Text(run.sinceLaunchOnly ? "No log messages since Run Game yet."
			                         : "Game log is empty.");
		} else {
			if (run.showLogBeginning) {
				ui.Text(std::format("Log offset: {} bytes", run.logPageOffset));
			} else if (logTruncated_) {
				ui.Text("Showing the end of the game log.");
			}
			if (ui.BeginChild("GameLog", 570.0f, 210.0f)) {
				run.logScrollY = ImGui::GetScrollY();
				const bool wasAtBottom =
				    ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
				ImGui::PushTextWrapPos();
				ui.Text(displayedLog.empty()
				            ? (run.logFilter.empty() ? "No errors on this log page."
				                                      : "No matching entries on this log page.")
				            : std::string_view(displayedLog));
				ImGui::PopTextWrapPos();
				if (restoreScroll) {
					ImGui::SetScrollY(*restoreScroll);
				} else if (jumpToBeginning || (changedFilter && !jumpToLatest)) {
					ImGui::SetScrollY(0.0f);
				} else if (!run.showLogBeginning &&
				    (jumpToLatest || (refreshedLog && run.followLog && wasAtBottom))) {
					ImGui::SetScrollHereY(1.0f);
				}
			}
			ui.EndChild();
		}
	}

	ui.EndPopup();
	return action;
}
