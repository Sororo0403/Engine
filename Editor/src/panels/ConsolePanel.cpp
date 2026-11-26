#include "panels/ConsolePanel.h"

#include "core/Logger.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/ShellUtils.h"
#include "scripting/LuaScript.h"

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

void ConsolePanel::ShowPlayError(std::uint64_t logId) {
	Show();
	requestedPlayErrorId_ = logId;
}

void ConsolePanel::MarkPlayStart() {
	playStartBoundary_ = Logger::BeginPlayLog();
	requestedPlayErrorId_ = 0;
	if (latestPlayOnly_) {
		selectedId_ = 0;
		scrollRequested_ = true;
	}
}

bool ConsolePanel::MatchesFilter(const LogEntry &entry,
                                 const std::filesystem::path &searchPath) const {
	if ((latestPlayOnly_ && entry.id <= playStartBoundary_) ||
	    (entry.level == LogLevel::LOG_INFO && !showInfo_) ||
	    (entry.level == LogLevel::LOG_WARNING && !showWarnings_) ||
	    (entry.level == LogLevel::LOG_ERROR && !showErrors_)) {
		return false;
	}

	return filter_.empty() || entry.message.find(filter_) != std::string::npos ||
	       PathUtils::MatchesPathSearch(entry.sourcePath, searchPath);
}

std::string ConsolePanel::BuildFilteredText(const std::filesystem::path &searchPath) const {
	std::string text;

	for (const auto &entry : entries_) {
		if (MatchesFilter(entry, searchPath)) {
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

ConsolePanel::Request ConsolePanel::Draw(const ImGuiSystem &ui,
                                         bool canEditScript,
                                         bool stopPlayToEdit,
                                         std::uint64_t retryPlayLogId,
                                         const std::filesystem::path &retryScene,
                                         const std::filesystem::path &retryScript,
                                         std::string_view retryInput,
                                         const std::optional<std::filesystem::path> &failedScene,
                                         bool failedTestSave) {
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
	ImGui::SetItemTooltip("Follow new messages while the list is at the bottom.");
	ui.SameLine();
	ImGui::Checkbox("Collapse Repeats", &collapseRepeated_);
	ImGui::SetItemTooltip("Group consecutive messages with the same level, text and source.");
	ui.SameLine();
	const bool jumpToLatest = ui.Button("Jump to Latest");

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
	ImGui::SetItemTooltip("Search message text or paste a source file path.");
	ui.SameLine();
	ui.BeginDisabled(filter_.empty());
	const bool clearedSearch = ui.Button("Clear Search");
	ui.EndDisabled();
	if (clearedSearch) {
		filter_.clear();
	}
	auto searchPath = PathUtils::FromPathSearch(filter_);
	Logger::ReadEntries(revision_, entries_);
	const auto firstPlayError = Logger::GetFirstPlayError();
	const auto latestPlayError = Logger::GetLatestPlayError();
	const auto playFailure = Logger::GetPlayFailure();
	ui.BeginDisabled(!firstPlayError);
	const bool clickedFirstPlayError = ui.Button("First Play Error");
	ui.EndDisabled();
	ui.SameLine();
	ImGui::BeginDisabled(!latestPlayError);
	const bool clickedLatestPlayError = ui.Button("Latest Play Error");
	ImGui::EndDisabled();
	bool clickedPlayFailure = false;
	if (playFailure && (!latestPlayError || playFailure->id != latestPlayError->id)) {
		ui.SameLine();
		clickedPlayFailure = ui.Button("Play Failure");
		ImGui::SetItemTooltip("Show the error reported by the Play failure dialog.");
	}
	const LogEntry *jumpError = nullptr;
	const auto requested = std::exchange(requestedPlayErrorId_, 0);
	if (requested != 0) {
		const auto found = std::ranges::find(entries_, requested, &LogEntry::id);
		if (found != entries_.end()) {
			jumpError = &*found;
		} else if (firstPlayError && firstPlayError->id == requested) {
			jumpError = &*firstPlayError;
		} else if (latestPlayError && latestPlayError->id == requested) {
			jumpError = &*latestPlayError;
		} else if (playFailure && playFailure->id == requested) {
			jumpError = &*playFailure;
		}
		if (!jumpError) {
			selectedId_ = 0;
			ui.Text("That play error is no longer in Console.");
		}
	} else if (clickedLatestPlayError && latestPlayError) {
		jumpError = &*latestPlayError;
	} else if (clickedFirstPlayError && firstPlayError) {
		jumpError = &*firstPlayError;
	} else if (clickedPlayFailure) {
		jumpError = &*playFailure;
	}
	if (jumpError) {
		latestPlayOnly_ = jumpError->id > playStartBoundary_;
		showErrors_ = true;
		if (!MatchesFilter(*jumpError, searchPath)) {
			filter_.clear();
			searchPath.clear();
		}
		selectedId_ = jumpError->id;
	}

	const bool canShowContext =
	    (!filter_.empty() || !showInfo_ || !showWarnings_ || !showErrors_) &&
	    std::ranges::any_of(entries_, [&](const auto &entry) {
		    return entry.id == selectedId_ && MatchesFilter(entry, searchPath);
	    });
	ui.SameLine();
	ui.BeginDisabled(!canShowContext);
	const bool showContext = ui.Button("Show Surrounding Logs");
	ui.EndDisabled();
	ImGui::SetItemTooltip("Clear text and level filters and return to the selected message.");
	if (showContext) {
		filter_.clear();
		searchPath.clear();
		showInfo_ = showWarnings_ = showErrors_ = true;
	}
	const bool jumpToSelection = jumpError || showContext ||
	                             (clearedSearch && selectedId_ != 0);

	ImGui::SameLine();
	const auto matchingCount = std::count_if(
	    entries_.begin(), entries_.end(),
	    [&](const auto &entry) { return MatchesFilter(entry, searchPath); });
	ImGui::BeginDisabled(matchingCount == 0);

	if (ui.Button("Copy Filtered Logs")) {
		const auto text = BuildFilteredText(searchPath);
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
		const bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
		struct MessageRow {
			const LogEntry *first;
			const LogEntry *last;
		};
		std::vector<MessageRow> rows;
		rows.reserve(static_cast<std::size_t>(matchingCount));

		for (auto first = entries_.cbegin(); first != entries_.cend();) {
			const auto &head = *first;
			if (!MatchesFilter(head, searchPath)) {
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
			rows.push_back({&head, &*last});
			first = last + 1;
		}

		const auto lastVisible = rows.empty() ? 0 : rows.back().last->id;
		const bool followLatest = autoScroll_ &&
		                          (scrollRequested_ ||
		                           (wasAtBottom && lastVisible != lastVisibleId_));
		const bool scrollToLatest = !jumpToSelection && (jumpToLatest || followLatest);
		if (rows.empty()) {
			ui.Text("No matching messages.");
		}

		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(rows.size()), ImGui::GetTextLineHeightWithSpacing());
		if (jumpToSelection) {
			const auto row = std::ranges::lower_bound(
			    rows, selectedId_, {}, [](const auto &value) { return value.last->id; });
			if (row != rows.end() && row->first->id <= selectedId_) {
				clipper.IncludeItemByIndex(static_cast<int>(row - rows.begin()));
			}
		} else if (scrollToLatest && !rows.empty()) {
			clipper.IncludeItemByIndex(static_cast<int>(rows.size()) - 1);
		}

		while (clipper.Step()) {
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
				const auto &head = *rows[row].first;
				const auto &entry = *rows[row].last;
				const auto count = rows[row].last - rows[row].first + 1;
				const bool selected = head.id <= selectedId_ && selectedId_ <= entry.id;
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
					// 同じログをまとめても、再試遊に対応する記録を選べるようにする。
					selectedId_ = head.id <= retryPlayLogId && retryPlayLogId <= entry.id
					                  ? retryPlayLogId
					                  : entry.id;
				}
				if (jumpToSelection && selected) {
					ImGui::SetScrollHereY(0.5f);
				}

				ImGui::SameLine(messageOffset);
				ui.Text(std::string_view{entry.message}.substr(
				    0, entry.message.find('\n')));
				if (scrollToLatest && entry.id == lastVisible) {
					ImGui::SetScrollHereY(1.0f);
				}

				if (colored) {
					ImGui::PopStyleColor();
				}
			}
		}

		lastVisibleId_ = lastVisible;
		if (jumpToSelection || scrollToLatest) {
			scrollRequested_ = false;
		}
	}

	ui.EndChild();

	const auto found =
	    std::find_if(entries_.begin(), entries_.end(), [&](const auto &entry) {
		    return entry.id == selectedId_;
	    });

	const LogEntry *selected = found != entries_.end() ? &*found : nullptr;
	if (!selected && firstPlayError && firstPlayError->id == selectedId_) {
		selected = &*firstPlayError;
	} else if (!selected && latestPlayError && latestPlayError->id == selectedId_) {
		selected = &*latestPlayError;
	} else if (!selected && playFailure && playFailure->id == selectedId_) {
		selected = &*playFailure;
	}
	if (!selected || !MatchesFilter(*selected, searchPath)) {
		selectedId_ = 0;
		selected = nullptr;
	}
	if (sourceActionErrorId_ != selectedId_) {
		sourceActionError_.clear();
	}

	const bool drawDetails = ImGui::BeginChild(
	    "ConsoleDetails", {0.0f, 0.0f}, ImGuiChildFlags_Borders,
	    ImGuiWindowFlags_HorizontalScrollbar);

	Request request;
	if (drawDetails) {
		if (selected) {
			const auto &sourcePath = selected->id == retryPlayLogId && !retryScript.empty()
			                             ? retryScript : selected->sourcePath;
			const bool isLua = PathUtils::HasExtension(selected->sourcePath, ".lua");
			const bool hasLuaDiagnostic = isLua && selected->level == LogLevel::LOG_ERROR;
			if (isLua) {
				ui.BeginDisabled(!canEditScript);
				if (ui.Button(stopPlayToEdit ? "Stop and Edit Script" : "Edit Script")) {
					request.path = sourcePath;
					request.logId = selected->id;
					if (hasLuaDiagnostic) {
						request.diagnostic = selected->message;
						request.diagnosticPath = selected->sourcePath;
					}
				}
				ui.EndDisabled();
				ImGui::SetItemTooltip("%s",
				                      PathUtils::ToUtf8(sourcePath).c_str());
			}

			if (!stopPlayToEdit && selected->id == retryPlayLogId) {
				if (failedScene) {
					if (ui.Button(failedScene->empty() ? "Select Failed Object" : "Open Failed Scene")) {
						request.logId = selected->id;
						request.openFailedScene = true;
					}
					if (!failedScene->empty()) {
						ImGui::SetItemTooltip("%s", PathUtils::ToUtf8(*failedScene).c_str());
					}
				}
				if (!retryInput.empty()) {
					if (ui.Button("Edit Input Binding")) {
						request.logId = selected->id;
						request.editInput = true;
					}
					ImGui::SetItemTooltip("Edit the input binding for %.*s.",
					                      static_cast<int>(retryInput.size()), retryInput.data());
				}
				if (ui.Button("Play Scene Again")) {
					request.logId = selected->id;
					request.retryPlay = true;
				}
				ImGui::SetItemTooltip("Start Play in %s.",
				                      retryScene.empty() ? "the editing scene"
				                                         : PathUtils::ToUtf8(retryScene).c_str());
				if (failedTestSave) {
					ui.SameLine();
					if (ui.Button("Play Without Test Save")) {
						request.logId = selected->id;
						request.retryPlay = true;
						request.playWithoutTestSave = true;
					}
					ImGui::SetItemTooltip("Retry the same scene without loading the test save for this run.");
				}
			}

			ui.Text(std::format("[{}] {}", selected->timestamp,
			                    LevelName(selected->level)));
			if (found == entries_.end()) {
				ui.Text("Play error (outside recent messages)");
			}
			if (!selected->sourcePath.empty()) {
				auto location = PathUtils::ToUtf8(sourcePath);
				if (hasLuaDiagnostic) {
					auto absolute = PathUtils::ResolveFromExecutable(selected->sourcePath);
					absolute.make_preferred();
					const auto line = LuaScript::FindErrorLine(
					    PathUtils::ToUtf8(absolute), selected->message);
					if (line > 0) {
						location += std::format(":{}", line);
					}
				}
				if (ui.Button("Search This Source")) {
					// 移動前のログも検索できるよう、ログ自身が保持するパスを使う。
					filter_ = PathUtils::ToUtf8(selected->sourcePath);
				}
				ImGui::SetItemTooltip("Search for this source path in the current log filters.");
				ui.SameLine();
				if (ui.Button("Show Source in Explorer")) {
					sourceActionErrorId_ = selectedId_;
					sourceActionError_.clear();
					ShellUtils::RevealLocationInExplorer(sourcePath,
					                                    sourceActionError_);
				}
				ui.Text(location);
				if (!sourceActionError_.empty()) {
					ui.Text(sourceActionError_);
				}
			}
			ui.Text(selected->message);
		} else {
			ui.Text("Select a message to read its full text.");
		}
	}

	ImGui::EndChild();
	ImGui::End();
	return request;
}
