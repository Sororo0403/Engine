#include "dialogs/SceneRecoveryDialog.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include <format>
#include <imgui.h>
#include <utility>

void SceneRecoveryDialog::Show(std::vector<SceneRecovery::Entry> entries,
                               std::string error) {
	entries_ = std::move(entries);
	error_ = std::move(error);
	selectedPath_.clear();
	selected_ = 0;
	confirmingDelete_ = false;
	openRequested_ = true;
}

SceneRecoveryDialog::Result
SceneRecoveryDialog::Draw(const ImGuiSystem &ui, SceneRecovery &recovery,
                          const std::filesystem::path &protectedFile) {
	constexpr const char *kTitle = "Scene Recovery###SceneRecoveryDialog";

	if (openRequested_) {
		ui.OpenPopup(kTitle);
		openRequested_ = false;
	}

	ImGui::SetNextWindowSize({720, 400}, ImGuiCond_FirstUseEver);

	if (!ui.BeginPopupModal(kTitle)) {
		return Result::NONE;
	}

	if (!confirmingDelete_ && ui.Button("Refresh")) {
		std::vector<SceneRecovery::Entry> refreshed;

		if (recovery.Enumerate(refreshed, error_)) {
			entries_.swap(refreshed);
			selected_ = 0;
		}
	}

	const bool selected = selected_ < entries_.size();
	bool protectedEntry = false;

	if (selected && !protectedFile.empty()) {
		protectedEntry =
		    PathUtils::IsSameFile(entries_[selected_].path, protectedFile);
	}

	if (confirmingDelete_) {
		ui.Text("Delete this recovery permanently?");

		if (selected) {
			ui.Text(PathUtils::ToUtf8(entries_[selected_].path));
		}

		ui.BeginDisabled(!selected || protectedEntry);

		if (ui.Button("Delete Recovery")) {
			if (recovery.Remove(entries_[selected_].path, error_)) {
				entries_.erase(entries_.begin() + selected_);
				selected_ = 0;
				confirmingDelete_ = false;
			}
		}

		ui.EndDisabled();
		ui.SameLine();

		if (ui.Button("Back") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			confirmingDelete_ = false;
		}
	} else {
		if (entries_.empty()) {
			ui.Text("No available recoveries.");
		}

		if (ui.BeginChild("Recoveries", 0, 200)) {
			for (std::size_t i = 0; i < entries_.size(); ++i) {
				const auto &entry = entries_[i];
				const auto name =
				    entry.sourceFile.empty()
				        ? std::string("Untitled")
				        : PathUtils::ToUtf8(entry.sourceFile.filename());
				const auto label =
				    std::format("{} | {}###Recovery{}", name,
				                entry.capturedUtc.empty() ? "Unreadable"
				                                          : entry.capturedUtc,
				                i);
				if (ui.Selectable(label.c_str(), selected_ == i)) {
					selected_ = i;
					error_.clear();
				}

				ImGui::SetItemTooltip("%s",
				                      PathUtils::ToUtf8(entry.sourceFile.empty()
				                                            ? entry.path
				                                            : entry.sourceFile)
				                          .c_str());
			}
		}

		ui.EndChild();

		if (selected && !entries_[selected_].error.empty()) {
			ImGui::TextWrapped("%s", entries_[selected_].error.c_str());
		}

		ui.BeginDisabled(!selected ||
		                 (selected && !entries_[selected_].error.empty()));
		const bool restore = ui.Button("Restore");
		ui.EndDisabled();

		if (restore) {
			selectedPath_ = entries_[selected_].path;
			ui.CloseCurrentPopup();
			ui.EndPopup();
			return Result::RESTORE;
		}

		ui.SameLine();
		ui.BeginDisabled(!selected || protectedEntry);

		if (ui.Button("Delete...")) {
			confirmingDelete_ = true;
		}

		ui.EndDisabled();
		ui.SameLine();

		if (ui.Button("Keep for Later") ||
		    ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			ui.CloseCurrentPopup();
			ui.EndPopup();
			return Result::CLOSED;
		}
	}

	if (!error_.empty()) {
		ImGui::TextWrapped("%s", error_.c_str());
	}

	ui.EndPopup();
	return Result::NONE;
}

const std::filesystem::path &SceneRecoveryDialog::GetSelectedPath() const {
	return selectedPath_;
}
