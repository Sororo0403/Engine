#include "panels/ConsolePanel.h"

#include "core/Logger.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"

#include <algorithm>
#include <format>
#include <imgui.h>
#include <utility>

namespace {
/// <summary>
/// クリップボードの文字列終端になるNULを表記へ変換し、メッセージを追加する
/// </summary>
/// <param name="text">追加先の文字列</param>
/// <param name="message">追加するUTF-8メッセージ</param>
void AppendClipboardMessage(std::string &text, std::string_view message) {
	std::size_t start = 0;

	for (auto end = message.find('\0'); end != std::string_view::npos;
	     end = message.find('\0', start)) {
		text.append(message.substr(start, end - start));
		text += "\\0";
		start = end + 1;
	}

	text.append(message.substr(start));
}

/// <summary>
/// ログの重要度を表示名へ変換する
/// </summary>
/// <param name="level">重要度</param>
/// <returns>表示用の固定文字列</returns>
const char *LevelName(LogLevel level) {
	switch (level) {
	case LogLevel::LOG_INFO:
		return "Info";
	case LogLevel::LOG_WARNING:
		return "Warning";
	case LogLevel::LOG_ERROR:
		return "Error";
	}

	return "Unknown";
}
} // namespace

void ConsolePanel::Show() {
	open_ = true;
	focusRequested_ = true;
	scrollRequested_ = true;
}

void ConsolePanel::ShowLatestPlayError() {
	Show();
	latestPlayErrorRequested_ = true;
}

void ConsolePanel::MarkPlayStart() {
	playStartBoundary_ = Logger::BeginPlayLog();
	latestPlayErrorRequested_ = false;
	if (latestPlayOnly_) {
		selectedId_ = 0;
		scrollRequested_ = true;
	}
}

bool ConsolePanel::MatchesFilter(const LogEntry &entry) const {
	if ((latestPlayOnly_ && entry.id <= playStartBoundary_) ||
	    (entry.level == LogLevel::LOG_INFO && !showInfo_) ||
	    (entry.level == LogLevel::LOG_WARNING && !showWarnings_) ||
	    (entry.level == LogLevel::LOG_ERROR && !showErrors_)) {
		return false;
	}

	return filter_.empty() || entry.message.find(filter_) != std::string::npos ||
	       (!entry.sourcePath.empty() &&
	        PathUtils::ToUtf8(entry.sourcePath).find(filter_) != std::string::npos);
}

std::string ConsolePanel::BuildFilteredText() const {
	std::string text;

	for (const auto &entry : entries_) {
		if (MatchesFilter(entry)) {
			text += std::format("[{}] {}: ", entry.timestamp,
			                    LevelName(entry.level));
			AppendClipboardMessage(text, entry.message);
			if (!entry.sourcePath.empty()) {
				text += "\nSource: ";
				AppendClipboardMessage(text, PathUtils::ToUtf8(entry.sourcePath));
			}
			text += '\n';
		}
	}

	return text;
}

ConsolePanel::ScriptEditRequest ConsolePanel::Draw(const ImGuiSystem &ui,
                                                   bool canEditScript,
                                                   bool stopPlayToEdit) {
	if (!open_) {
		return {};
	}

	ImGui::SetNextWindowSize({720.0f, 420.0f}, ImGuiCond_FirstUseEver);

	if (focusRequested_) {
		ImGui::SetNextWindowCollapsed(false);
		ImGui::SetNextWindowFocus();
		focusRequested_ = false;
	}

	if (!ImGui::Begin("Console", &open_)) {
		ImGui::End();
		return {};
	}

	if (ui.Button("Clear Console")) {
		Logger::ClearEntries();
		selectedId_ = 0;
	}

	ImGui::SetItemTooltip("Clear recent messages. File output is kept.");
	ui.SameLine();

	if (ImGui::Checkbox("Auto-scroll", &autoScroll_) && autoScroll_) {
		scrollRequested_ = true;
	}
	ui.SameLine();
	ImGui::Checkbox("Collapse Repeats", &collapseRepeated_);
	ImGui::SetItemTooltip("Group consecutive messages with the same level, text and source.");

	ImGui::Checkbox("Info", &showInfo_);
	ui.SameLine();
	ImGui::Checkbox("Warnings", &showWarnings_);
	ui.SameLine();
	ImGui::Checkbox("Errors", &showErrors_);
	ui.SameLine();
	ui.BeginDisabled(playStartBoundary_ == 0);
	if (ImGui::Checkbox("Latest Play Run", &latestPlayOnly_)) {
		scrollRequested_ = true;
	}
	ImGui::SetItemTooltip("Show messages since the most recent Play or Restart attempt.");
	ui.EndDisabled();
	ui.InputText("Search", filter_);
	ImGui::SetItemTooltip("Search message text and source file paths.");
	Logger::ReadEntries(revision_, entries_);
	const auto firstPlayError = Logger::GetFirstPlayError();
	const auto latestPlayError =
	    std::find_if(entries_.rbegin(), entries_.rend(), [this](const auto &entry) {
		    return playStartBoundary_ != 0 && entry.id > playStartBoundary_ &&
		           entry.level == LogLevel::LOG_ERROR;
	    });
	ui.BeginDisabled(!firstPlayError);
	const bool clickedFirstPlayError = ui.Button("First Play Error");
	ui.EndDisabled();
	ui.SameLine();
	ImGui::BeginDisabled(latestPlayError == entries_.rend());
	const bool clickedLatestPlayError = ui.Button("Latest Play Error");
	ImGui::EndDisabled();
	const LogEntry *jumpError = nullptr;
	if (std::exchange(latestPlayErrorRequested_, false) || clickedLatestPlayError) {
		if (latestPlayError != entries_.rend()) {
			jumpError = &*latestPlayError;
		}
	} else if (clickedFirstPlayError && firstPlayError) {
		jumpError = &*firstPlayError;
	}
	const bool jumpToError = jumpError != nullptr;
	if (jumpToError) {
		latestPlayOnly_ = true;
		showErrors_ = true;
		if (!MatchesFilter(*jumpError)) {
			filter_.clear();
		}
		selectedId_ = jumpError->id;
	}
	ImGui::SameLine();
	const auto matchingCount = std::count_if(
	    entries_.begin(), entries_.end(),
	    [this](const auto &entry) { return MatchesFilter(entry); });
	ImGui::BeginDisabled(matchingCount == 0);

	if (ui.Button("Copy Filtered Logs")) {
		const auto text = BuildFilteredText();
		ImGui::SetClipboardText(text.c_str());
	}

	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Copy all matching recent messages, including rows "
	                      "outside the scroll area.");
	ui.Text(std::format("Matching: {} / {} recent messages", matchingCount,
	                    entries_.size()));
	const auto content = ui.GetContentSize();
	const bool drawMessages = ui.BeginChild("ConsoleMessages", 0.0f,
	                                        (std::max)(1.0f, content.y * 0.6f));

	if (drawMessages) {
		bool anyVisible = false;
		std::uint64_t lastVisible = 0;

		for (auto first = entries_.cbegin(); first != entries_.cend();) {
			const auto &head = *first;
			if (!MatchesFilter(head)) {
				++first;
				continue;
			}

			auto last = first;
			while (collapseRepeated_ && last + 1 != entries_.cend()) {
				const auto &next = *(last + 1);
				if (next.level != head.level || next.message != head.message ||
				    next.sourcePath != head.sourcePath ||
				    (head.id <= playStartBoundary_ && next.id > playStartBoundary_)) {
					break;
				}
				++last;
			}
			const auto count = last - first + 1;
			first = last + 1;
			const auto &entry = *last;
			const bool selected = head.id <= selectedId_ && selectedId_ <= entry.id;
			anyVisible = true;
			lastVisible = entry.id;
			const bool colored = entry.level != LogLevel::LOG_INFO;

			if (colored) {
				ImGui::PushStyleColor(ImGuiCol_Text,
				                      entry.level == LogLevel::LOG_ERROR
				                          ? ImVec4{1.0f, 0.4f, 0.4f, 1.0f}
				                          : ImVec4{1.0f, 0.8f, 0.3f, 1.0f});
			}

			auto title =
			    std::format("[{}] {}", entry.timestamp, LevelName(entry.level));
			if (count > 1) {
				title += std::format(" (x{})", count);
			}
			const auto label =
			    std::format("{}###ConsoleLog{}", title, head.id);
			const float messageOffset = ImGui::GetCursorPosX() +
			                            ImGui::CalcTextSize(title.c_str()).x +
			                            ImGui::GetStyle().ItemSpacing.x * 2.0f;

			if (ui.Selectable(label.c_str(), selected)) {
				selectedId_ = entry.id;
			}
			if (jumpToError && selected) {
				ImGui::SetScrollHereY(0.5f);
			}

			ImGui::SameLine(messageOffset);
			ui.Text(std::string_view{entry.message}.substr(
			    0, entry.message.find('\n')));

			if (colored) {
				ImGui::PopStyleColor();
			}
		}

		if (!anyVisible) {
			ui.Text("No matching messages.");
		}

		if (!jumpToError && autoScroll_ &&
		    (scrollRequested_ || lastVisible != lastVisibleId_)) {
			ImGui::SetScrollHereY(1.0f);
			scrollRequested_ = false;
		}

		lastVisibleId_ = lastVisible;
		if (jumpToError) {
			scrollRequested_ = false;
		}
	}

	ui.EndChild();

	const auto found =
	    std::find_if(entries_.begin(), entries_.end(), [&](const auto &entry) {
		    return entry.id == selectedId_;
	    });

	const LogEntry *selected = found != entries_.end() ? &*found : nullptr;
	const bool retained = !selected && firstPlayError && firstPlayError->id == selectedId_;
	if (retained) {
		selected = &*firstPlayError;
	}
	if (!selected || !MatchesFilter(*selected)) {
		selectedId_ = 0;
		selected = nullptr;
	}

	const bool drawDetails = ImGui::BeginChild(
	    "ConsoleDetails", {0.0f, 0.0f}, ImGuiChildFlags_Borders,
	    ImGuiWindowFlags_HorizontalScrollbar);

	ScriptEditRequest script;
	if (drawDetails) {
		if (selected) {
			if (ui.Button("Copy Message")) {
				std::string text;
				AppendClipboardMessage(text, selected->message);
				ImGui::SetClipboardText(text.c_str());
			}

			if (PathUtils::HasExtension(selected->sourcePath, ".lua")) {
				ui.SameLine();
				ui.BeginDisabled(!canEditScript);
				if (ui.Button(stopPlayToEdit ? "Stop and Edit Script" : "Edit Script")) {
					script = {selected->sourcePath, selected->message};
				}
				ui.EndDisabled();
				ImGui::SetItemTooltip("%s",
				                      PathUtils::ToUtf8(selected->sourcePath).c_str());
			}

			ui.SameLine();
			ui.Text(std::format("[{}] {}", selected->timestamp,
			                    LevelName(selected->level)));
			if (retained) {
				ui.Text("First Play Error (outside recent messages)");
			}
			if (!selected->sourcePath.empty()) {
				ui.Text("Source: " + PathUtils::ToUtf8(selected->sourcePath));
			}
			ui.Text(selected->message);
		} else {
			ui.Text("Select a message to read its full text.");
		}
	}

	ImGui::EndChild();
	ImGui::End();
	return script;
}
