#include "dialogs/GameExportResultDialog.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/ShellUtils.h"
#include <algorithm>
#include <format>
#include <fstream>
#include <imgui.h>
#include <string_view>
#include <system_error>
#include <utility>

namespace {
constexpr auto kGameLogPath = L"logs/Engine.log";
constexpr auto kLogRefreshInterval = std::chrono::milliseconds{500};

/// <summary>
/// 表示中のログから、Loggerが出力した最後のエラー行の先頭を探す
/// </summary>
std::size_t FindLatestError(std::string_view log) {
	std::size_t latest = std::string_view::npos;
	for (std::size_t start = 0; start < log.size();) {
		const auto end = log.find('\n', start);
		const auto line = log.substr(start, end == std::string_view::npos
		                                       ? end : end - start);
		const auto timestampEnd = line.find(']');
		if (line.starts_with('[') && timestampEnd != std::string_view::npos &&
		    line.substr(timestampEnd + 1).starts_with(" [ERROR] ")) {
			latest = start;
		}
		if (end == std::string_view::npos) {
			break;
		}
		start = end + 1;
	}
	return latest;
}
}

void GameExportResultDialog::Show(std::filesystem::path directory,
                                  bool reopening) {
	directory_ = std::move(directory);
	runs_.try_emplace(directory_);
	reopening_ = reopening;
	error_.clear();
	log_.clear();
	logError_.clear();
	logTruncated_ = false;
	nextLogPage_.reset();
	if (runs_.at(directory_).showLog) {
		RefreshLog();
	}
	opening_ = true;
}

void GameExportResultDialog::RefreshLog() {
	auto &run = runs_.at(directory_);
	nextLogRefresh_ = std::chrono::steady_clock::now() + kLogRefreshInterval;
	constexpr std::streamoff maximumBytes = 64 * 1024;
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
	                       : end - (std::min)(available, maximumBytes);
	const auto size = (std::min)(end - start, maximumBytes);
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

bool GameExportResultDialog::StartGame() {
	auto &run = runs_.at(directory_);
	// 起動後すぐにログが追記されても、今回の行を落とさないよう先に記録する。
	std::error_code status;
	const auto size = std::filesystem::file_size(directory_ / kGameLogPath, status);
	std::string launchError;
	error_.clear();
	run.launchFailed = !run.process.Start(directory_ / L"Game.exe", launchError,
	                                      ShellUtils::WindowVisibility::NORMAL,
	                                      L"--editor-launch");
	if (run.launchFailed) {
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
	if (run.showLog)
		RefreshLog();
	return run.showLog;
}

bool GameExportResultDialog::UpdateGameProcess(bool &jumpToLatest) {
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

	auto &run = runs_.at(directory_);
	bool jumpToLatest = false;
	bool refreshedLog = UpdateGameProcess(jumpToLatest);
	if (!run.status.empty())
		ui.Text(run.status);

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
	if (ui.Button(run.showLog ? "Hide Game Log" : "View Game Log")) {
		run.showLog = !run.showLog;
		if (run.showLog) {
			RefreshLog();
			jumpToLatest = true;
		}
	}

	ui.SameLine();
	GameExportResultAction action = GameExportResultAction::NONE;

	if (ui.Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		if (std::exchange(run.restartRequested, false)) {
			run.status = "Requested game exit. Waiting for the process to finish.";
		}
		action = GameExportResultAction::CLOSE;
		ui.CloseCurrentPopup();
	}

	if (!run.status.empty() && ui.Button("Copy Run Report")) {
		auto report = "Output: " + PathUtils::ToUtf8(directory_) + "\n" +
		              run.status + "\n";
		if (!run.launchFailed) {
			RefreshLog();
			refreshedLog = true;
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
		bool jumpToBeginning = false;
		if (run.logStart &&
		    ImGui::Checkbox("Since Run Game", &run.sinceLaunchOnly)) {
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
		ui.SameLine();
		const auto latestError = FindLatestError(log_);
		ui.BeginDisabled(latestError == std::string_view::npos || jumpToLatest);
		const bool jumpToError = ui.Button("Latest Log Error") &&
		                         latestError != std::string_view::npos &&
		                         !jumpToLatest;
		ui.EndDisabled();
		ImGui::SetItemTooltip("Jump to the latest error in the displayed log.");
		if (jumpToError) {
			run.followLog = false;
		}

		if (!logError_.empty()) {
			ui.Text(logError_);
		} else if (log_.empty()) {
			ui.Text(run.sinceLaunchOnly ? "No log messages since Run Game yet."
			                         : "Game log is empty.");
		} else {
			if (run.showLogBeginning) {
				ui.Text(std::format("Log page {}", run.earlierLogPages.size() + 1));
			} else if (logTruncated_) {
				ui.Text("Showing the end of the game log.");
			}
			if (ui.BeginChild("GameLog", 570.0f, 210.0f)) {
				const bool wasAtBottom =
				    ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
				if (jumpToError) {
					const auto prefixHeight = latestError == 0 ? 0.0f
					    : ImGui::CalcTextSize(log_.data(), log_.data() + latestError,
					                          false, ImGui::GetContentRegionAvail().x).y;
					ImGui::SetScrollY((std::max)(
					    0.0f, ImGui::GetCursorPosY() + prefixHeight -
					              ImGui::GetWindowHeight() * 0.5f));
				}
				ImGui::PushTextWrapPos();
				ui.Text(log_);
				ImGui::PopTextWrapPos();
				if (jumpToBeginning && !jumpToError) {
					ImGui::SetScrollY(0.0f);
				} else if (!jumpToError && !run.showLogBeginning &&
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
