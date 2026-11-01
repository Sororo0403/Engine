#include "panels/ConsolePanel.h"

#include "core/Logger.h"
#include "graphics/ui/ImGuiSystem.h"

#include <algorithm>
#include <format>
#include <imgui.h>

namespace {
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

bool ConsolePanel::MatchesFilter(const LogEntry &entry) const {
	if ((entry.level == LogLevel::LOG_INFO && !showInfo_) ||
	    (entry.level == LogLevel::LOG_WARNING && !showWarnings_) ||
	    (entry.level == LogLevel::LOG_ERROR && !showErrors_)) {
		return false;
	}
	return filter_.empty() || entry.message.find(filter_) != std::string::npos;
}

void ConsolePanel::Draw(const ImGuiSystem &ui) {
	if (!open_) {
		return;
	}
	ImGui::SetNextWindowSize({720.0f, 420.0f}, ImGuiCond_FirstUseEver);
	if (focusRequested_) {
		ImGui::SetNextWindowCollapsed(false);
		ImGui::SetNextWindowFocus();
		focusRequested_ = false;
	}
	if (!ImGui::Begin("Console", &open_)) {
		ImGui::End();
		return;
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
	ImGui::Checkbox("Info", &showInfo_);
	ui.SameLine();
	ImGui::Checkbox("Warnings", &showWarnings_);
	ui.SameLine();
	ImGui::Checkbox("Errors", &showErrors_);
	ui.InputText("Search", filter_);
	Logger::ReadEntries(revision_, entries_);
	const auto content = ui.GetContentSize();
	const bool drawMessages = ui.BeginChild("ConsoleMessages", 0.0f, (std::max)(1.0f, content.y * 0.6f));
	if (drawMessages) {
		bool anyVisible = false;
		std::uint64_t lastVisible = 0;
		for (const auto &entry : entries_) {
			if (!MatchesFilter(entry)) {
				continue;
			}
			anyVisible = true;
			lastVisible = entry.id;
			const bool colored = entry.level != LogLevel::LOG_INFO;
			if (colored) {
				ImGui::PushStyleColor(ImGuiCol_Text, entry.level == LogLevel::LOG_ERROR
				    ? ImVec4{1.0f, 0.4f, 0.4f, 1.0f} : ImVec4{1.0f, 0.8f, 0.3f, 1.0f});
			}
			const auto title = std::format("[{}] {}", entry.timestamp, LevelName(entry.level));
			const auto label = std::format("{}###ConsoleLog{}", title, entry.id);
			const float messageOffset = ImGui::GetCursorPosX() + ImGui::CalcTextSize(title.c_str()).x +
			                            ImGui::GetStyle().ItemSpacing.x * 2.0f;
			if (ui.Selectable(label.c_str(), selectedId_ == entry.id)) {
				selectedId_ = entry.id;
			}
			ImGui::SameLine(messageOffset);
			ui.Text(std::string_view{entry.message}.substr(0, entry.message.find('\n')));
			if (colored) {
				ImGui::PopStyleColor();
			}
		}
		if (!anyVisible) {
			ui.Text("No matching messages.");
		}
		if (autoScroll_ && (scrollRequested_ || lastVisible != lastVisibleId_)) {
			ImGui::SetScrollHereY(1.0f);
			scrollRequested_ = false;
		}
		lastVisibleId_ = lastVisible;
	}
	ui.EndChild();

	auto selected = std::find_if(entries_.begin(), entries_.end(), [&](const auto &entry) { return entry.id == selectedId_; });
	if (selected == entries_.end() || !MatchesFilter(*selected)) {
		selectedId_ = 0;
		selected = entries_.end();
	}
	const bool drawDetails = ImGui::BeginChild("ConsoleDetails", {0.0f, 0.0f}, ImGuiChildFlags_Borders,
	                                          ImGuiWindowFlags_HorizontalScrollbar);
	if (drawDetails) {
		if (selected != entries_.end()) {
			if (ui.Button("Copy Message")) {
				ImGui::SetClipboardText(selected->message.c_str());
			}
			ui.SameLine();
			ui.Text(std::format("[{}] {}", selected->timestamp, LevelName(selected->level)));
			ui.Text(selected->message);
		} else {
			ui.Text("Select a message to read its full text.");
		}
	}
	ImGui::EndChild();
	ImGui::End();
}
