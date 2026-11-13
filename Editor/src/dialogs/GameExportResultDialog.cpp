#include "dialogs/GameExportResultDialog.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/ShellUtils.h"
#include <algorithm>
#include <format>
#include <fstream>
#include <imgui.h>
#include <system_error>
#include <utility>

namespace {
constexpr auto kGameLogPath = L"logs/Engine.log";
constexpr auto kLogRefreshInterval = std::chrono::milliseconds{500};
}

void GameExportResultDialog::Show(std::filesystem::path directory,
                                  bool reopening) {
	if (directory_ != directory) {
		logLaunchStart_.reset();
		sinceLaunchOnly_ = false;
		process_.Reset();
		processStatus_.clear();
	}
	directory_ = std::move(directory);
	reopening_ = reopening;
	error_.clear();
	log_.clear();
	logError_.clear();
	showLog_ = logTruncated_ = false;
	opening_ = true;
}

void GameExportResultDialog::RefreshLog() {
	nextLogRefresh_ = std::chrono::steady_clock::now() + kLogRefreshInterval;
	constexpr std::streamoff maximumBytes = 64 * 1024;
	log_.clear();
	logError_.clear();
	logTruncated_ = false;
	const auto path = directory_ / kGameLogPath;
	std::error_code status;
	if (!std::filesystem::exists(path, status) && !status) {
		if (logLaunchStart_)
			*logLaunchStart_ = 0;
		logError_ = "Game log hasn't been created yet.";
		return;
	}

	std::ifstream input(path, std::ios::binary | std::ios::ate);
	const auto end = static_cast<std::streamoff>(input.tellg());
	if (end < 0) {
		logError_ = "Could not read the game log.";
		return;
	}
	if (logLaunchStart_ && *logLaunchStart_ > static_cast<std::uintmax_t>(end)) {
		// ログが切り詰められた場合は、新しいファイルの先頭から表示する。
		*logLaunchStart_ = 0;
	}
	const auto firstIncluded = sinceLaunchOnly_ && logLaunchStart_
	                               ? static_cast<std::streamoff>(*logLaunchStart_)
	                               : 0;
	const auto size = (std::min)(end - firstIncluded, maximumBytes);
	const auto start = end - size;
	bool partialLine = false;
	if (start > firstIncluded) {
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

	logTruncated_ = start > firstIncluded;
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

bool GameExportResultDialog::UpdateGameProcess() {
	if (!process_.IsTracking())
		return false;

	std::optional<std::uint32_t> exitCode;
	std::string error;
	if (!process_.Poll(exitCode, error)) {
		processStatus_ = std::move(error);
		process_.Reset();
		return false;
	}
	if (!exitCode)
		return false;

	process_.Reset();
	if (*exitCode == 0) {
		processStatus_ = "Game exited normally.";
		if (showLog_)
			RefreshLog();
		return showLog_;
	}
	processStatus_ = std::format("Game exited with code 0x{:08X}.", *exitCode);
	showLog_ = true;
	RefreshLog();
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

	bool refreshedLog = UpdateGameProcess();
	if (!processStatus_.empty())
		ui.Text(processStatus_);

	if (!error_.empty()) {
		ImGui::TextWrapped("%s", error_.c_str());
	}

	if (ui.Button("Open Output Folder")) {
		ShellUtils::OpenDirectory(directory_, error_);
	}

	ui.SameLine();

	ui.BeginDisabled(process_.IsTracking());
	if (ui.Button("Run Game")) {
		// 起動後すぐにログが追記されても、今回の行を落とさないよう先に記録する。
		std::error_code status;
		const auto size = std::filesystem::file_size(
		    directory_ / kGameLogPath, status);
		if (process_.Start(directory_ / L"Game.exe", error_,
		                   ShellUtils::WindowVisibility::NORMAL, L"--editor-launch")) {
			processStatus_ = "Game process running.";
			logLaunchStart_.reset();
			if (!status)
				logLaunchStart_ = size;
			else if (status == std::errc::no_such_file_or_directory)
				logLaunchStart_ = 0;
			sinceLaunchOnly_ = logLaunchStart_.has_value();
			if (showLog_) {
				RefreshLog();
				refreshedLog = true;
			}
		}
	}
	ui.EndDisabled();

	ui.SameLine();
	if (ui.Button(showLog_ ? "Hide Game Log" : "View Game Log")) {
		showLog_ = !showLog_;
		if (showLog_) {
			RefreshLog();
			refreshedLog = true;
		}
	}

	ui.SameLine();
	GameExportResultAction action = GameExportResultAction::NONE;

	if (ui.Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		action = GameExportResultAction::CLOSE;
		ui.CloseCurrentPopup();
	}

	if (showLog_) {
		if (logLaunchStart_ &&
		    ImGui::Checkbox("Since Run Game", &sinceLaunchOnly_)) {
			RefreshLog();
			refreshedLog = true;
		}
		if (ui.Button("Refresh Log")) {
			RefreshLog();
			refreshedLog = true;
		}
		if (process_.IsTracking() &&
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

		if (!processStatus_.empty()) {
			ui.SameLine();
			if (ui.Button("Copy Run Report")) {
				auto report = "Output: " + PathUtils::ToUtf8(directory_) + "\n";
				report += processStatus_ + "\n";
				if (!logError_.empty()) {
					report += logError_ + "\n";
				} else if (!log_.empty()) {
					report += sinceLaunchOnly_ ? "\nGame log since Run Game"
					                           : "\nGame log";
					report += logTruncated_ ? " (tail):\n" : ":\n";
					report += log_;
				}
				ImGui::SetClipboardText(report.c_str());
			}
		}

		if (!logError_.empty()) {
			ui.Text(logError_);
		} else if (log_.empty()) {
			ui.Text(sinceLaunchOnly_ ? "No log messages since Run Game yet."
			                         : "Game log is empty.");
		} else {
			if (logTruncated_)
				ui.Text("Showing the end of the game log.");
			if (ui.BeginChild("GameLog", 570.0f, 210.0f)) {
				ImGui::PushTextWrapPos();
				ui.Text(log_);
				ImGui::PopTextWrapPos();
				if (refreshedLog)
					ImGui::SetScrollHereY(1.0f);
			}
			ui.EndChild();
		}
	}

	ui.EndPopup();
	return action;
}
