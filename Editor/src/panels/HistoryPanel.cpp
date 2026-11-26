#include "panels/HistoryPanel.h"

#include "graphics/ui/ImGuiSystem.h"
#include "history/EditorHistory.h"

#include <format>
#include <imgui.h>

void HistoryPanel::Show() {
	open_ = true;
	focusRequested_ = true;
	followCurrent_ = true;
}

std::optional<std::size_t> HistoryPanel::Draw(const ImGuiSystem &ui,
                                              const EditorHistory &history,
                                              bool enabled) {
	if (!open_) {
		return std::nullopt;
	}

	ImGui::SetNextWindowSize({540.0f, 320.0f}, ImGuiCond_FirstUseEver);

	if (focusRequested_) {
		ImGui::SetNextWindowCollapsed(false);
		ImGui::SetNextWindowFocus();
		focusRequested_ = false;
	}

	if (!ImGui::Begin("History", &open_)) {
		ImGui::End();
		return std::nullopt;
	}

	ImGui::TextUnformatted("Click a state to undo or redo up to that point.");

	if (history.IsEditing()) {
		ImGui::TextDisabled("Finish the current edit to navigate history.");
	}

	std::optional<std::size_t> requested;
	bool afterCurrent = false;
	ImGui::BeginDisabled(!enabled || history.IsEditing());
	const bool drawStates = ImGui::BeginChild(
	    "HistoryStates", {0.0f, 0.0f}, ImGuiChildFlags_Borders);

	if (drawStates) {
		for (std::size_t i = 0; i < history.GetStateCount(); ++i) {
			const auto state = history.GetState(i);

			if (!state) {
				continue;
			}

			if (afterCurrent) {
				ImGui::PushStyleColor(
				    ImGuiCol_Text,
				    ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			}

			const auto label =
			    std::format("{}: {}{}###HistoryState{}", i, state->description,
			                state->saved ? " [Saved]" : "", i);

			if (ui.Selectable(label.c_str(), state->current) &&
			    !state->current) {
				requested = i;
			}

			if (state->current) {
				if (lastCurrent_ != i) {
					followCurrent_ = true;
				}

				lastCurrent_ = i;

				if (followCurrent_ && !ImGui::IsAnyItemActive() &&
				    !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
				    !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
				    !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
					ImGui::SetScrollHereY(0.5f);
					followCurrent_ = false;
				}
			}

			if (afterCurrent) {
				ImGui::PopStyleColor();
			}

			afterCurrent |= state->current;
		}
	}

	ImGui::EndChild();
	ImGui::EndDisabled();

	ImGui::End();
	return requested;
}
