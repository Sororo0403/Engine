#include "dialogs/SceneRecoveryDialog.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/ShellUtils.h"
#include <algorithm>
#include <format>
#include <imgui.h>
#include <utility>

void SceneRecoveryDialog::Show(std::vector<SceneRecovery::Entry> entries,
                               std::filesystem::path projectAssets,
                               std::string error) {
	projectAssets_ = std::move(projectAssets);
	entries_.clear();
	SetEntries(std::move(entries));
	error_ = std::move(error);
	selectedPath_.clear();
	confirmingDelete_ = false;
	openRequested_ = true;
}

void SceneRecoveryDialog::Reopen(std::string error) {
	error_ = std::move(error);
	selectedPath_.clear();
	openRequested_ = true;
}

void SceneRecoveryDialog::SetEntries(std::vector<SceneRecovery::Entry> entries) {
	const auto previous = selected_ < entries_.size() ? entries_[selected_].path
	                                                  : std::filesystem::path{};
	std::stable_partition(entries.begin(), entries.end(), [&](const auto &entry) {
		return entry.CanRestore(projectAssets_);
	});
	const auto found = std::ranges::find(entries, previous, &SceneRecovery::Entry::path);
	selected_ = found == entries.end() ? 0 : static_cast<std::size_t>(found - entries.begin());
	entries_ = std::move(entries);
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
			SetEntries(std::move(refreshed));
		}
	}

	const auto isProtected = [&](const auto &entry) {
		return !protectedFile.empty() && PathUtils::IsSameFile(entry.path, protectedFile);
	};

	if (confirmingDelete_) {
		const bool selected = selected_ < entries_.size();
		ui.Text("Delete this recovery permanently?");

		if (selected) {
			ui.Text(PathUtils::ToUtf8(entries_[selected_].path));
		}

		ui.BeginDisabled(!selected || isProtected(entries_[selected_]));

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

		const bool selected = selected_ < entries_.size();
		const bool canRestore =
		    selected && entries_[selected_].CanRestore(projectAssets_);
		if (selected) {
			const auto &entry = entries_[selected_];
			if (!entry.assetDirectory.empty()) {
				ImGui::TextWrapped("Assets: %s",
				                   PathUtils::ToUtf8(entry.assetDirectory).c_str());
				if (ui.Button("Show Assets Location")) {
					ShellUtils::RevealLocationInExplorer(entry.assetDirectory, error_);
				}
				ImGui::SetItemTooltip("Show the Assets folder or its nearest existing parent.");
				std::error_code statusError;
				if (!std::filesystem::is_directory(
				        PathUtils::ResolveFromExecutable(entry.assetDirectory), statusError)) {
					ImGui::TextWrapped("The Assets folder is unavailable. "
					                   "Some models and images may be missing after restoration.");
				}
			}
			if (!entry.sourceFile.empty()) {
				if (ui.Button("Show Original Location")) {
					ShellUtils::RevealLocationInExplorer(entry.sourceFile, error_);
				}
				ImGui::SetItemTooltip("%s", PathUtils::ToUtf8(entry.sourceFile).c_str());
				ui.SameLine();
			}
			if (ui.Button("Show Recovery File")) {
				ShellUtils::RevealLocationInExplorer(entry.path, error_);
			}
			ImGui::SetItemTooltip("%s", PathUtils::ToUtf8(entry.path).c_str());
			if (!entry.error.empty()) {
				ImGui::TextWrapped("%s", entry.error.c_str());
			} else if (!canRestore) {
				ImGui::TextWrapped(
				    "Open the project for this Assets folder to restore this scene.");
			}
		}

		ui.BeginDisabled(!canRestore);
		const bool restore = ui.Button("Restore");
		ui.EndDisabled();

		if (restore) {
			selectedPath_ = entries_[selected_].path;
			ui.CloseCurrentPopup();
			ui.EndPopup();
			return Result::RESTORE;
		}

		ui.SameLine();
		ui.BeginDisabled(!selected || isProtected(entries_[selected_]));

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
