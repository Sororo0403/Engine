#include "dialogs/GameExportResultDialog.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include "platform/windows/ShellUtils.h"
#include <imgui.h>
#include <utility>

void GameExportResultDialog::Show(std::filesystem::path directory,
                                  bool reopening) {
	directory_ = std::move(directory);
	reopening_ = reopening;
	error_.clear();
	opening_ = true;
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

	ui.SameLine();
	GameExportResultAction action = GameExportResultAction::NONE;

	if (ui.Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		action = GameExportResultAction::CLOSE;
		ui.CloseCurrentPopup();
	}

	ui.EndPopup();
	return action;
}
