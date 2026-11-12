#include "dialogs/GameExportResultDialog.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/ShellUtils.h"
#include <algorithm>
#include <fstream>
#include <imgui.h>
#include <system_error>
#include <utility>

void GameExportResultDialog::Show(std::filesystem::path directory,
                                  bool reopening) {
	directory_ = std::move(directory);
	reopening_ = reopening;
	error_.clear();
	log_.clear();
	logError_.clear();
	showLog_ = logTruncated_ = false;
	opening_ = true;
}

void GameExportResultDialog::RefreshLog() {
	constexpr std::streamoff maximumBytes = 64 * 1024;
	log_.clear();
	logError_.clear();
	logTruncated_ = false;
	const auto path = directory_ / L"logs" / L"Engine.log";
	std::error_code status;
	if (!std::filesystem::exists(path, status) && !status) {
		logError_ = "Game log hasn't been created yet.";
		return;
	}

	std::ifstream input(path, std::ios::binary | std::ios::ate);
	const auto end = static_cast<std::streamoff>(input.tellg());
	if (end < 0) {
		logError_ = "Could not read the game log.";
		return;
	}
	const auto size = (std::min)(end, maximumBytes);
	const auto start = end - size;
	bool partialLine = false;
	if (start > 0) {
		input.seekg(start - 1);
		partialLine = input.get() != '\n';
	} else {
		input.seekg(0);
	}
	std::string text(static_cast<std::size_t>(size), '\0');
	if (size > 0 && !input.read(text.data(), size)) {
		logError_ = "Could not read the game log. Refresh to try again.";
		return;
	}

	logTruncated_ = start > 0;
	if (partialLine) {
		// 先頭が行の途中なら取り除く。長い1行ではUTF-8の継続バイトだけ除く。
		const auto newline = text.find('\n');
		if (newline != std::string::npos && newline + 1 < text.size()) {
			text.erase(0, newline + 1);
		} else {
			const auto first = std::ranges::find_if(text, [](unsigned char byte) {
				return (byte & 0xc0) != 0x80;
			});
			text.erase(text.begin(), first);
		}
	}
	for (const char byte : text) {
		if (byte == '\0')
			log_ += "\\0";
		else
			log_ += byte;
	}
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

	if (!error_.empty()) {
		ImGui::TextWrapped("%s", error_.c_str());
	}

	if (ui.Button("Open Output Folder")) {
		ShellUtils::OpenDirectory(directory_, error_);
	}

	ui.SameLine();

	if (ui.Button("Run Game")) {
		ShellUtils::RunExecutable(directory_ / L"Game.exe", error_);
	}

	bool refreshedLog = false;
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
		if (ui.Button("Refresh Log")) {
			RefreshLog();
			refreshedLog = true;
		}
		ui.SameLine();
		ui.BeginDisabled(log_.empty());
		if (ui.Button("Copy Log"))
			ImGui::SetClipboardText(log_.c_str());
		ui.EndDisabled();

		if (!logError_.empty()) {
			ui.Text(logError_);
		} else if (log_.empty()) {
			ui.Text("Game log is empty.");
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
